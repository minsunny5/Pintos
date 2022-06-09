#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include <debug.h>

/* Process identifier. */
typedef int pid_t;

/* file's lock. 한번에 하나만 파일을 건드릴 수 있게 한다.*/
struct lock file_lock;
/* 실행파일의 락이다. 한번에 하나만 실행파일을 로드할 수 있게 한다.*/
struct lock load_lock;

void syscall_init (void);

void get_args_addr(void* esp, void** argument_arr, int cnt);
void IsValidAddr(const void* addr);

void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
pid_t exec (const char *file);
int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

#endif /* userprog/syscall.h */
