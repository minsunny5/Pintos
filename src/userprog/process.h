#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void remove_child(struct thread* child);
int add_file_desc(struct file* f);
struct file* get_file_by_fd(int fd);
void remove_fd(int fd);

#endif /* userprog/process.h */
