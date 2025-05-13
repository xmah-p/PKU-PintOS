#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include <hash.h>

#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/file.h"

#define MAX_FD 128 /**< Maximum number of file descriptors. */

typedef int pid_t; /**< Process ID type. */
#define PID_ERROR ((pid_t) -1)          /**< Error value for pid_t. */

struct proc_info
  { 
    char **argv;                   /**< Command line arguments. */
    pid_t pid;                     /**< Thread ID. */
    int exit_status;               /**< Exit status. */
    struct file *executable;       /**< Executable file. */
    bool waited;                   /**< True if process has been waited on. */
    bool loaded;                   /**< True if process has been loaded. */
    struct thread *parent;         /**< Parent thread. */
    struct list child_list;        /**< List of child processes. */
    struct list_elem child_elem;   /**< List element for child list. */
    struct file *fd_table[MAX_FD]; /**< File descriptor table. */
    struct semaphore wait_sema;    /**< Semaphore for syscall wait */
    struct lock lock;              /**< Lock for proc_info. */
    int ref_count;                 /**< Reference count for process. */

    /* Owned by vm/page.c. */
    struct hash sup_page_table;    /**< Supplemental page table. */
    struct lock spt_lock;          /**< Lock for the sup_page_table. */
  };

/* pid-tid mapping */
static inline 
pid_t tid_to_pid (tid_t tid)
{
  return (pid_t) tid;
}

static inline 
tid_t pid_to_tid (pid_t pid)
{
  return (tid_t) pid;
}

void run_first_process (const char *commandline);

pid_t process_execute (const char *commandline);
int process_wait (pid_t);
void free_pd (void);
void process_exit (int status);
void process_activate (void);
void free_proc_info_refcnt (struct proc_info *proc_info);

#endif /**< userprog/process.h */
