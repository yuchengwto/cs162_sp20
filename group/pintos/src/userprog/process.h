#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "lib/user/stdlib.h"
#include "lib/user/syscall.h"
#include "threads/synch.h"



struct pnode {
    pid_t pid;
    struct list_elem elem;
    struct semaphore sema;
    int exit_status;
};


struct fnode {
    int fd;                   /* File descriptor */
    char *file_name;            /* File name */
    struct file *file_ptr;      /* File pointer */
    struct list_elem elem;   /* File node elem in list */
    struct lock *file_lock;
};


tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void free_child_list(void);
void free_file_list(void);



#endif /* userprog/process.h */
