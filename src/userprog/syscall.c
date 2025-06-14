#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <round.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "lib/string.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/mmap.h"
#include "vm/vm_region.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

#define MMAP_FAILED ((mapid_t) -1)

typedef unsigned char byte_t;

static void syscall_handler (struct intr_frame *);

/* Helper functions. */

static int get_user (const byte_t *uaddr);
static bool put_user (byte_t *udst, byte_t byte);
static bool is_valid_addr (byte_t *addr, bool writable);
static bool is_valid_nbyte_ptr (byte_t *ptr, size_t n, bool writable);
static bool is_valid_string (const char *str);

static struct file *get_file (int fd);

static void page_set_pinned (const void *buffer, unsigned size, bool pinned);

/* System call implementations. */

static void 
syscall_halt (void) 
{
  shutdown_power_off ();
}

static void 
syscall_exit (int status) 
{
  process_exit (status);
}


static pid_t 
syscall_exec (const char *cmd_line) 
{
  if (!is_valid_string (cmd_line))
      syscall_exit (-1);
  return process_execute (cmd_line);
}

static int 
syscall_wait (pid_t pid) 
{
  return process_wait (pid);
}


static bool 
syscall_create (const char *file, unsigned initial_size) 
{
  if (!is_valid_string (file))
      syscall_exit (-1);

  lock_acquire (&filesys_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return success;
}

static bool 
syscall_remove (const char *file) 
{
  if (!is_valid_string (file))
      syscall_exit (-1);

  lock_acquire (&filesys_lock);
  bool success = filesys_remove (file);
  lock_release (&filesys_lock);
  return success;
}


static int
syscall_open (const char *file)
{
  if (!is_valid_string (file))
    syscall_exit (-1);

  struct thread *cur = thread_current ();
  struct proc_info *proc_info = cur->proc_info;

  lock_acquire (&proc_info->lock);
  lock_acquire (&filesys_lock);

  struct file *file_ptr = filesys_open (file);
  if (file_ptr == NULL)
  {
    lock_release (&filesys_lock);
    lock_release (&proc_info->lock);
    return -1;
  }

  int fd = -1;
  for (int i = STDOUT_FILENO + 1; i < MAX_FD; i++)
      if (proc_info->fd_table[i] == NULL)
      {
        fd = i;
        break;
      }

  if (fd == -1)
  {
    file_close (file_ptr);
    lock_release (&filesys_lock);
    lock_release (&proc_info->lock);
    return -1;
  }

  proc_info->fd_table[fd] = file_ptr;

  lock_release (&filesys_lock);
  lock_release (&proc_info->lock);
  return fd;
}

static int 
syscall_filesize (int fd) 
{
  struct file *file = get_file (fd);

  lock_acquire (&filesys_lock);
  int len = file_length (file);
  lock_release (&filesys_lock);
    
  return len;
}

static int 
syscall_read (int fd, void *buffer, unsigned size) 
{
  if (!is_valid_nbyte_ptr((byte_t *) buffer, size, true))
    {  
      syscall_exit (-1);
    }

  if (fd == STDIN_FILENO)
    {
      unsigned i;
      page_set_pinned (buffer, size, true);
      for (i = 0; i < size; i++)
        {
          char c = input_getc ();
          ((char *) buffer)[i] = c;
        }
      page_set_pinned (buffer, size, false);
      return i;
    }

  struct file *file = get_file (fd);
  lock_acquire (&filesys_lock);
  page_set_pinned (buffer, size, true);
  int bytes_read = file_read (file, buffer, size);
  page_set_pinned (buffer, size, false);
  lock_release (&filesys_lock);
  return bytes_read;
}

static int 
syscall_write (int fd, const void *buffer, unsigned size) 
{
  if (!is_valid_nbyte_ptr ((byte_t *) buffer, size, false))
      syscall_exit (-1);
  
  if (fd == STDOUT_FILENO)
    {
      page_set_pinned (buffer, size, true);
      putbuf (buffer, size);
      page_set_pinned (buffer, size, false);
      return size;
    }
    
  struct file *file = get_file (fd);

  lock_acquire (&filesys_lock);
  page_set_pinned (buffer, size, true);
  int bytes_written = file_write (file, buffer, size);
  page_set_pinned (buffer, size, false);
  lock_release (&filesys_lock);
  return bytes_written;
}

static void 
syscall_seek (int fd, unsigned position) 
{
  struct file *file = get_file (fd);

  lock_acquire (&filesys_lock);
  file_seek (file, position);
  lock_release (&filesys_lock);
  return;
}

static unsigned 
syscall_tell (int fd) 
{
  struct file *file = get_file (fd);
    
  lock_acquire (&filesys_lock);
  unsigned position = file_tell (file);
  lock_release (&filesys_lock);
  return position;
}

static void 
syscall_close (int fd) 
{
  struct file *file = get_file (fd);

  struct thread *cur = thread_current ();
  struct proc_info *proc_info = cur->proc_info;
  lock_acquire (&proc_info->lock);
  lock_acquire (&filesys_lock);

  proc_info->fd_table[fd] = NULL;
  file_close (file);
    
  lock_release (&filesys_lock);
  lock_release (&proc_info->lock);
  return;
}

static mapid_t
syscall_mmap (int fd, void *addr) 
{  
  struct thread *cur = thread_current ();
  struct proc_info *proc_info = cur->proc_info;

  /* Check if arguments are valid */
  if (fd <= STDOUT_FILENO || 
      fd >= MAX_FD ||
      addr == NULL || 
      pg_ofs (addr) != 0)
      return MMAP_FAILED;

  lock_acquire (&filesys_lock);
  struct file *file = file_reopen (get_file (fd));
  size_t length = file_length (file);
  lock_release (&filesys_lock);

  /* Check if the memory region to be mapped is valid */
  if (length == 0 || 
      !vm_region_available (&proc_info->vm_region_list, addr, length))
      return MMAP_FAILED;
  
  /* Create a mmap entry */
  mapid_t mapid = proc_info->mmap_next_mapid++;
  if (!mmap_create (&proc_info->mmap_list, mapid, file, length, addr))
    return MMAP_FAILED;
  
  /* Install pages in the supplemental page table */
  size_t read_bytes = length;
  size_t zero_bytes = (ROUND_UP (length, PGSIZE) - length);
  if (!spt_install_file_pages (file, 0, addr, read_bytes, zero_bytes, 
                               true, REGION_MMAP))
    return MMAP_FAILED;

  return mapid;
}

static void
syscall_munmap (mapid_t mapid) 
{
  struct list *mmap_list = &thread_current ()->proc_info->mmap_list;
  mmap_write_back_and_delete (mmap_list, mapid);
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static int syscall_argc[] = {
  [SYS_HALT] = 0,
  [SYS_EXIT] = 1,
  [SYS_EXEC] = 1,
  [SYS_WAIT] = 1,
  [SYS_CREATE] = 2,
  [SYS_REMOVE] = 1,
  [SYS_OPEN] = 1,
  [SYS_FILESIZE] = 1,
  [SYS_READ] = 3,
  [SYS_WRITE] = 3,
  [SYS_SEEK] = 2,
  [SYS_TELL] = 1,
  [SYS_CLOSE] = 1,
  [SYS_MMAP] = 2,
  [SYS_MUNMAP] = 1
};

static void
syscall_handler (struct intr_frame *f) 
{
  int *esp = (int *) f->esp;
  thread_current ()->esp = (uaddr_t) esp;

  if (!is_valid_nbyte_ptr((byte_t *) esp, sizeof (int), false))
      syscall_exit (-1);

  int syscall_num = esp[0];
  if (syscall_num < SYS_MIN || syscall_num > SYS_MAX)
      syscall_exit (-1);

  int argc = syscall_argc[syscall_num];
  for (int i = 1; i <= argc; i++)
    if (!is_valid_nbyte_ptr((byte_t *) (esp + i), sizeof (int), false))  
      syscall_exit (-1);
  
  switch (syscall_num)
    {
      case SYS_HALT:
        syscall_halt ();
        break;
      case SYS_EXIT:
        syscall_exit (esp[1]);
        break;
      case SYS_EXEC:
        f->eax = syscall_exec ((const char *) esp[1]);
        break;
      case SYS_WAIT:
        f->eax = syscall_wait (esp[1]);
        break;
      case SYS_CREATE:
        f->eax = syscall_create ((const char *) esp[1], (unsigned) esp[2]);
        break;
      case SYS_REMOVE:
        f->eax = syscall_remove ((const char *) esp[1]);
        break;
      case SYS_OPEN:
        f->eax = syscall_open ((const char *) esp[1]);
        break;
      case SYS_FILESIZE:
        f->eax = syscall_filesize (esp[1]);
        break;
      case SYS_READ:
        f->eax = syscall_read (esp[1], (void *) esp[2], (unsigned) esp[3]);
        break;
      case SYS_WRITE:
        f->eax = syscall_write 
                 (esp[1], (const void *) esp[2], (unsigned) esp[3]);
        break;
      case SYS_SEEK:
        syscall_seek (esp[1], (unsigned) esp[2]);
        break;
      case SYS_TELL:
        f->eax = syscall_tell (esp[1]);
        break;
      case SYS_CLOSE: 
        syscall_close (esp[1]);
        break;
      case SYS_MMAP:
        f->eax = syscall_mmap (esp[1], (void *) esp[2]);
        break;
      case SYS_MUNMAP:
        syscall_munmap ((mapid_t) esp[1]);
        break;
      default:
        syscall_exit (-1);
        break;
    }

}

/* Gets file * from fd. Also checks if fd and file * are valid. */
static struct file *
get_file (int fd) 
{
  struct thread *cur = thread_current ();
  struct proc_info *proc_info = cur->proc_info;
  if (fd < STDIN_FILENO || fd >= MAX_FD)
      syscall_exit (-1);
  
  struct file *file = proc_info->fd_table[fd];
  if (file == NULL)  
      syscall_exit (-1);
  
  return file;
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const byte_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (byte_t *udst, byte_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}


/* Checks if addr is a valid user address. Helper function for 
   is_valid_nbyte_ptr and is_valid_string. */
static bool 
is_valid_addr (byte_t *addr, bool writable) 
{
  return addr != NULL && is_user_vaddr (addr)
         && (writable ? put_user (addr, 'x') : get_user (addr) != -1);
}

/* Checks if address [ptr, ptr + n - 1] is valid. */
static bool
is_valid_nbyte_ptr (byte_t *ptr, size_t n, bool writable) 
{
  for (size_t i = 0; i < n; i += PGSIZE)
    {
      if (!is_valid_addr (ptr + i, writable))
        return false;
    }
  return is_valid_addr (ptr + n - 1, writable);
}

/* Checks if a string is valid. */
static bool
is_valid_string (const char *str) 
{
  byte_t *addr = (byte_t *) str;
  if (!is_valid_addr (addr, false))
    return false;
  
  while (true) 
    {
      if (!is_valid_addr (addr, false))
        return false;
      if (*addr == '\0')
        break;
      addr++;
    }
  return true;
}

/* Called in syscall_read () and syscall_write () to pin the 
   frames in user buffer. */
static void 
page_set_pinned (const void *buffer, unsigned size, bool pinned)
{
  for (unsigned i = 0; i < size; i += PGSIZE)
    {
      void *upage = pg_round_down (buffer + i);
      void *kpage = pagedir_get_page (thread_current ()->pagedir, upage);
      frame_set_pinned (kpage, pinned);
    }
  void *upage = pg_round_down (buffer + size - 1);
  void *kpage = pagedir_get_page (thread_current ()->pagedir, upage);
  frame_set_pinned (kpage, pinned);
  return;
}

