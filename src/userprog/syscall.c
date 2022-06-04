#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <string.h>
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "filesys/filesys.h"

#define	PHYS_BASE 0xc0000000

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int* arg_buf[3];// 시스템콜 인수 최대 3개
  int* esp = f->esp;//user stack pointer

  IsValidAddr(esp);

  //1. 시스템 콜 넘버 받아오기
  int syscall_num = *esp;
  printf ("system call!: %d\n", syscall_num);

  //2. 해당하는 시스템 콜 넘버에 맞는 핸들러 불러주기
  switch (syscall_num)
  {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      get_args_addr(esp, arg_buf, 1);
      exit(*arg_buf[0]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_EXEC:
      break;
    case SYS_WAIT:
      get_args_addr(esp, arg_buf, 1);
      f->eax = wait(*(pid_t*)arg_buf[0]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_CREATE:
      get_args_addr(esp, arg_buf, 2);
      f->eax = (uint32_t)create((const char*)arg_buf[0], *(unsigned*)arg_buf[1]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_REMOVE:
      get_args_addr(esp, arg_buf, 1);
      f->eax = (uint32_t)remove((const char*)arg_buf[0]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_OPEN:
      break;
    case SYS_FILESIZE:
      break;
    case SYS_READ:
      break;
    case SYS_WRITE:
      break;
    case SYS_SEEK:
      break;
    case SYS_TELL:
      break;
    case SYS_CLOSE:
      break;
    default:
      break;
  }

  //thread_exit ();
}

void get_args_addr(void* esp, int** argument_arr, int cnt)
{
  int i;
  for(i = 0; i < cnt; i++)
  {
    int* addr = esp + 4 * (i+1);
    IsValidAddr(addr);
    argument_arr[i] = addr;
  }
}

void IsValidAddr(const void* addr)
{
  //1. null pointer거나
  if(addr == NULL)
  {
    exit(-1);
  }
  //2.유저 가상 메모리 주소 범위에서 벗어났을 때
  if((unsigned)addr >= PHYS_BASE || (unsigned)addr < 0x08048000)
  {
    exit(-1);
  }
  //3. 아직 매핑되지 않은 주소에 접근했는지
  if(pagedir_get_page(thread_current()->pagedir, addr) == NULL)
  {
    exit(-1);
  }
}

void halt (void)
{
  shutdown_power_off();
}

void exit (int status)
{
  struct thread* cur = thread_current();
  cur->exit_status = status;
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit();
  
}

/*User Process가 자식 프로세스를 만드려면 사용하는 함수*/
pid_t exec (const char *cmd_line)
{
  //process_execute로 자식 프로세스 생성
  //
}

int wait (pid_t pid)
{
  return process_wait(pid);
}

bool create (const char *file, unsigned initial_size)
{
  //포인터가 있다면 일단 valid한지 체크
  IsValidAddr(file);
  return filesys_create(file, initial_size);
}

bool remove (const char *file)
{
  //만약 파일이 열려있으면 파일디스크립터가 다 사라질 때까지는 삭제 안됨.

  ////
  IsValidAddr(file);
  return filesys_remove(file);
}
/*
int open (const char *file)
{

}
int filesize (int fd)
{

}
int read (int fd, void *buffer, unsigned size)
{

}
int write (int fd, const void *buffer, unsigned size)
{

}
void seek (int fd, unsigned position)
{

}
unsigned tell (int fd)
{

}
void close (int fd)
{

}
*/