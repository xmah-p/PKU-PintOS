#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/file.h"

#define MAX_FD 128 /**< Maximum number of file descriptors. */

struct proc_info
  { 
    char **argv;                   /**< Command line arguments. */
    tid_t tid;                     /**< Thread ID. */
    int exit_status;               /**< Exit status. */
    bool exited;                   /**< True if process has exited. */
    bool waited;                   /**< True if process has been waited on. */
    bool loaded;                   /**< True if process has been loaded. */
    struct thread *parent;         /**< Parent thread. */
    struct list child_list;        /**< List of child processes. */
    struct list_elem child_elem;   /**< List element for child list. */
    struct file *fd_table[MAX_FD]; /**< File descriptor table. */
    struct semaphore wait_sema;    /**< Semaphore for syscall wait */
    struct lock lock;              /**< Lock for proc_info. */
    int ref_count;                 /**< Reference count for process. */
  };

void run_first_process (const char *commandline);
tid_t process_execute (const char *commandline);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void free_proc_info_refcnt (struct proc_info *proc_info);

#endif /**< userprog/process.h */
