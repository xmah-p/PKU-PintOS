#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "devices/input.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

static void syscall_handler (struct intr_frame *);

/* Checks if addr is a valid user address. */
static bool 
is_valid_addr (const void *addr) 
{
  return addr != NULL 
      && is_user_vaddr (addr)
      && pagedir_get_page (thread_current ()->pagedir, addr) != NULL;
      
}

/* Checks if address [ptr, ptr + n - 1] is valid. */
static bool
is_valid_nbyte_ptr (const void *ptr, size_t n) 
{
  return is_valid_addr (ptr) && is_valid_addr (ptr + n - 1);
}

static struct file *get_file (int fd);

/* System call implementations. */

static void 
syscall_halt (void) 
{
  shutdown_power_off ();
}

static void 
syscall_exit (int status) 
{
  struct thread *cur = thread_current ();
  struct proc_info *proc_info = cur->proc_info;
  char *prog_name;
  struct file *executable;

  lock_acquire (&proc_info->lock);
  prog_name = proc_info->argv[0];
  printf("%s: exit(%d)\n", prog_name, status);

  /* Allow writes to the executable file. */
  lock_acquire (&filesys_lock);
  executable = filesys_open (prog_name);
  if (executable != NULL)
    {
      file_allow_write (executable);
      file_close (executable);
    }
  lock_release (&filesys_lock);

  proc_info->exit_status = status;
  proc_info->exited = true;
  lock_release (&proc_info->lock);

  sema_up (&proc_info->wait_sema);    /* Notify parent thread */

  free_proc_info_refcnt (proc_info);    /* Free proc_info */
  thread_exit ();               /* Exit the thread */
}


static tid_t 
syscall_exec (const char *cmd_line) 
{
  if (!is_valid_nbyte_ptr (cmd_line, 2))
      syscall_exit (-1);
  return process_execute (cmd_line);
}

static int 
syscall_wait (tid_t tid) 
{
  return process_wait (tid);
}


static bool 
syscall_create (const char *file, unsigned initial_size) 
{
  if (!is_valid_nbyte_ptr (file, 2))
      syscall_exit (-1);

  lock_acquire (&filesys_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return success;
}

static bool 
syscall_remove (const char *file) 
{
  if (!is_valid_nbyte_ptr (file, 2))
      syscall_exit (-1);

  lock_acquire (&filesys_lock);
  bool success = filesys_remove (file);
  lock_release (&filesys_lock);
  return success;
}

static int 
syscall_open (const char *file) 
{
  if (!is_valid_nbyte_ptr (file, 2))
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
  if (!is_valid_nbyte_ptr(buffer, size))
      syscall_exit (-1);

  if (fd == STDIN_FILENO)
    {
      unsigned i;
      for (i = 0; i < size; i++)
        {
          char c = input_getc ();
          ((char *) buffer)[i] = c;
        }
      return i;
    }

  struct file *file = get_file (fd);
  lock_acquire (&filesys_lock);
  int bytes_read = file_read (file, buffer, size);
  lock_release (&filesys_lock);
  return bytes_read;

}

static int 
syscall_write (int fd, const void *buffer, unsigned size) 
{
  if (!is_valid_nbyte_ptr(buffer, size))
      syscall_exit (-1);
  
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }
    
  struct file *file = get_file (fd);

  lock_acquire (&filesys_lock);
  int bytes_written = file_write (file, buffer, size);
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
  [SYS_CLOSE] = 1
};

static void
syscall_handler (struct intr_frame *f) 
{
  int *esp = (int *) f->esp;

  if (!is_valid_nbyte_ptr(esp, sizeof (int)))
      syscall_exit (-1);

  int syscall_num = esp[0];
  if (syscall_num < SYS_MIN || syscall_num > SYS_MAX)
      syscall_exit (-1);

  int argc = syscall_argc[syscall_num];
  for (int i = 1; i <= argc; i++)
    if (!is_valid_nbyte_ptr(esp + i, sizeof (int)))  
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
        f->eax = syscall_write (esp[1], (const void *) esp[2], (unsigned) esp[3]);
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