#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

/* Executable file's lock. 프로세스가 실행파일을 로드하는 동안 실행파일이 변경되지 않게 한다. */
struct lock load_lock;

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void remove_child(struct thread* child);
int add_file_desc(struct file* f);
struct file* get_file_by_fd(int fd);
void remove_fd(int fd);

#endif /* userprog/process.h */
