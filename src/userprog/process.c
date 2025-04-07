#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "lib/string.h"
#include "devices/timer.h"   /* DEBUGGING */
#include "threads/synch.h"   

static thread_func start_process NO_RETURN;
static bool load (struct proc_info *proc_info, 
                  void (**eip) (void), void **esp);

/* Free the proc_info structure. This is a helper function for
   free_proc_info_refcnt and should not be called directly. */
static void 
free_proc_info (struct proc_info *proc_info)
{
  if (proc_info->argv != NULL) {
    palloc_free_page (proc_info->argv[0]);
    palloc_free_page (proc_info->argv);
  }

  free (proc_info);
}

/* Free the proc_info structure if its reference count is 0.
   Otherwise, decrement the reference count. This function is
   thread-safe. */
void 
free_proc_info_refcnt (struct proc_info *proc_info)
{
  lock_acquire (&proc_info->lock);
  proc_info->ref_count--;
  if (proc_info->ref_count == 0)
    {
      lock_release (&proc_info->lock);
      free_proc_info (proc_info);
    }
  else if (proc_info->ref_count > 0)
    {
      lock_release (&proc_info->lock);
    }
  else
    {
      lock_release (&proc_info->lock);
      printf ("free_proc_info_refcnt: ref_count < 0\n");
    }
}

static void
init_proc_info (struct proc_info *proc_info, char **argv)
{
  proc_info->argv = argv;
  proc_info->exit_status = -1;
  proc_info->exited = false;
  proc_info->waited = false;
  proc_info->loaded = false;
  proc_info->parent = thread_current ();
  list_init (&proc_info->child_list);
  lock_init (&proc_info->lock);
  sema_init (&proc_info->wait_sema, 0);
  for (int i = 0; i < MAX_FD; i++)
    proc_info->fd_table[i] = NULL;
  proc_info->ref_count = 1;
}

/* Called in init.c to run the first user process. */
void 
run_first_process (const char *commandline)
{
  struct proc_info *proc_info;
  struct thread *cur = thread_current ();

  /* Initialize proc_info structure. */
  proc_info = malloc (sizeof (struct proc_info));
  if (proc_info == NULL)
    PANIC ("run_first_process: malloc failed");
  init_proc_info (proc_info, NULL);

  cur->proc_info = proc_info;
  process_wait (process_execute (commandline));

  free_proc_info_refcnt (proc_info);    /* Free proc_info */
}


/** Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *commandline) 
{
  char **argv;
  char *cmdline_copy;
  int argc = 0;
  char *save_ptr;
  char *token;
  tid_t tid;
  struct proc_info *child_proc_info;
  struct thread *cur = thread_current ();
  struct proc_info *proc_info = cur->proc_info;

  /* Parse cmdline into argv. */ 
  argv = palloc_get_page (0);
  if (argv == NULL)
    {
      return TID_ERROR;
    }

  cmdline_copy = palloc_get_page (0);
  if (cmdline_copy == NULL)
    {
      palloc_free_page (argv);
      return TID_ERROR;
    }
  strlcpy (cmdline_copy, commandline, PGSIZE);

  for (token = strtok_r (cmdline_copy, " ", &save_ptr); token != NULL;
       token = strtok_r (NULL, " ", &save_ptr))
    { 
      if ((size_t) argc >= PGSIZE / sizeof (char *))
        {
          palloc_free_page (argv);
          palloc_free_page (cmdline_copy);
          return TID_ERROR;
        }
      argv[argc++] = token;
    }
  argv[argc] = NULL;

  /* Initialize proc_info structure. */
  child_proc_info = malloc (sizeof (struct proc_info));
  if (child_proc_info == NULL)
    {
      palloc_free_page (argv);
      palloc_free_page (cmdline_copy);
      return TID_ERROR;
    }
  init_proc_info (child_proc_info, argv);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (argv[0], PRI_DEFAULT, start_process, child_proc_info);
  if (tid == TID_ERROR) 
    {
      free_proc_info_refcnt (child_proc_info);
      return TID_ERROR;
    }
  child_proc_info->tid = tid;
  /* Add child_proc_info to the parent's child list. */
  lock_acquire (&proc_info->lock);
  list_push_back (&proc_info->child_list, &child_proc_info->child_elem);
  lock_release (&proc_info->lock);

  /* Wait for the child to finish loading. */
  sema_down (&child_proc_info->wait_sema);

  /* If load failed, free child_proc_info and return TID_ERROR. */
  if (!child_proc_info->loaded)
    {
      lock_acquire (&proc_info->lock);
      list_remove (&child_proc_info->child_elem);
      lock_release (&proc_info->lock);

      free_proc_info_refcnt (child_proc_info);
      return TID_ERROR;
    }

  return tid;
}

/** A thread function that loads a user process and starts it
   running. */
static void
start_process (void *proc_info_)
{
  struct proc_info *proc_info = proc_info_;
  struct intr_frame if_;
  bool success;

  proc_info->ref_count++;    /* Increment reference count. */

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (proc_info, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  if (!success) 
    {
      proc_info->loaded = false;
      free_proc_info_refcnt (proc_info);
      sema_up (&proc_info->wait_sema);    /* Notify parent thread */
      thread_exit ();
    }
  proc_info->loaded = true;
  sema_up (&proc_info->wait_sema);    /* Notify parent thread */

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/** Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread *cur = thread_current ();
  struct proc_info *proc_info = cur->proc_info;
  struct list_elem *e;
  struct proc_info *child_proc_info = NULL;
  int exit_status = -1;
  bool found = false;

  /* Check if child_tid is a valid child. */
  lock_acquire (&proc_info->lock);
  for (e = list_begin (&proc_info->child_list); e != list_end (&proc_info->child_list);
       e = list_next (e))
    {
      child_proc_info = list_entry (e, struct proc_info, child_elem);
      if (child_proc_info->tid == child_tid)
        {
          found = true;
          break;
        }
    }
  lock_release (&proc_info->lock);

  if (!found) return -1;    /* Not a child of the current process. */

  lock_acquire (&child_proc_info->lock);
  if (child_proc_info->waited) return -1;    /* Already waited on. */
  child_proc_info->waited = true;
  lock_release (&child_proc_info->lock);

  /* Wait for child to exit. */
  sema_down (&child_proc_info->wait_sema);

  lock_acquire (&child_proc_info->lock);
  /* If child has exited, its proc_info's exit_status will be set.
   * Otherwise, it is terminated by the kernel, then its proc_info's
   * exit_status should be -1. */
  exit_status = child_proc_info->exit_status;
  /* Free child_proc_info. */
  list_remove (&child_proc_info->child_elem);
  lock_release (&child_proc_info->lock);
  free_proc_info_refcnt (child_proc_info);
  return exit_status;
}

/** Free the current process's resources. */
void
free_pd (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd == NULL) return;

  /* Correct ordering here is crucial.  We must set
      cur->pagedir to NULL before switching page directories,
      so that a timer interrupt can't switch back to the
      process page directory.  We must activate the base page
      directory before destroying the process's page
      directory, or our active page directory will be one
      that's been freed (and cleared). */
  cur->pagedir = NULL;
  pagedir_activate (NULL);
  pagedir_destroy (pd);   
}

void
process_exit (int status) 
{
  struct thread *cur = thread_current ();
  struct proc_info *proc_info = cur->proc_info;
  char *prog_name;
  struct file *executable;

  lock_acquire (&proc_info->lock);
  prog_name = proc_info->argv[0];
  printf("%s: exit(%d)\n", prog_name, status);

  /* Allow writes to the executable file. */
  executable = proc_info->executable;
  lock_acquire (&filesys_lock);
  file_close (executable);
  for (int i = 2; i < MAX_FD; i++)
    {
      // if (proc_info->fd_table[i] == NULL) continue;    
      file_close (proc_info->fd_table[i]);
      proc_info->fd_table[i] = NULL;
    }
  lock_release (&filesys_lock);

  proc_info->exit_status = status;
  proc_info->exited = true;
  lock_release (&proc_info->lock);

  sema_up (&proc_info->wait_sema);    /* Notify parent thread */

  free_proc_info_refcnt (proc_info);    /* Free proc_info */
  thread_exit ();               /* Exit the thread */
}

/** Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/** We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/** ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/** For use with ELF types in printf(). */
#define PE32Wx PRIx32   /**< Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /**< Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /**< Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /**< Print Elf32_Half in hexadecimal. */

/** Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/** Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/** Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /**< Ignore. */
#define PT_LOAD    1            /**< Loadable segment. */
#define PT_DYNAMIC 2            /**< Dynamic linking info. */
#define PT_INTERP  3            /**< Name of dynamic loader. */
#define PT_NOTE    4            /**< Auxiliary info. */
#define PT_SHLIB   5            /**< Reserved. */
#define PT_PHDR    6            /**< Program header table. */
#define PT_STACK   0x6474e551   /**< Stack segment. */

/** Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /**< Executable. */
#define PF_W 2          /**< Writable. */
#define PF_R 4          /**< Readable. */

static bool setup_stack (void **esp, char **argv);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/** Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (struct proc_info *proc_info, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  int i;
  char **argv = proc_info->argv;
  char *prog_name = argv[0];

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    return false;
  process_activate ();

  /* Open executable file. */
  lock_acquire (&filesys_lock);
  file = filesys_open (prog_name);
  if (file == NULL) 
    {
      lock_release (&filesys_lock);
      return false;
    }

  /* Deny writes to the executable file. */
  file_deny_write (file);
  proc_info->executable = file;

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", prog_name);
      goto fail; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto fail;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto fail;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto fail;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto fail;
            }
          else
            goto fail;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, argv))
    goto fail;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  lock_release (&filesys_lock);
  return true;

 fail:
  /* We will only reach here if load failed. */
  file_close (file);
  lock_release (&filesys_lock);
  return false;
}

/** load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/** Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/** Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL) {
        return false;
      }

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/** Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, char **argv)
{
  uint8_t *kpage;
  bool success = false;
  int argc = 0;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage == NULL) 
  {
    return false; 
  }
    
  success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
  if (!success) 
    {
      palloc_free_page (kpage);
      return false; 
    }
  *esp = PHYS_BASE;

  for (; argv[argc] != NULL; argc++);

  /* argv[0:argc][...] */
  char *argv_copy[argc + 1];
  for (int i = argc - 1; i >= 0; i--)
    {
      *esp -= strlen (argv[i]) + 1;
      strlcpy (*esp, argv[i], strlen (argv[i]) + 1);
      argv_copy[i] = *esp;
    }
  argv_copy[argc] = NULL;

  /* word-align */
  *esp = (void *) ((uintptr_t) *esp & ~0x3);

  /* argv[0:argc]: 4 * (argc + 1) bytes */
  *esp -= (argc + 1) * sizeof (char *);
  memcpy (*esp, argv_copy, (argc + 1) * sizeof (char *));
  
  /* argv and argc: 12 bytes */
  char **argv_ptr = *esp;
  *esp -= sizeof (char **);
  *(char ***) *esp = argv_ptr;
  *esp -= sizeof (int);
  *(int *) *esp = argc;

  /* fake return address */
  *esp -= sizeof (void *);
  *(void **) *esp = 0;
  
  return true;
}

/** Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
