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
#include "filesys/file.h"
#include "devices/input.h"
#include "lib/kernel/stdio.h"

#define	PHYS_BASE 0xc0000000

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  lock_init(&file_lock);
  lock_init(&load_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  void* arg_buf[3];// 시스템콜 인수 최대 3개
  uintptr_t* esp = f->esp;//user stack pointer

  IsValidAddr(esp);

  //1. 시스템 콜 넘버 받아오기
  int syscall_num = *esp;
  //printf ("system call!: %d\n", syscall_num);
  //hex_dump((uintptr_t)esp , esp , PHYS_BASE - (int)esp , true);

  //2. 해당하는 시스템 콜 넘버에 맞는 핸들러 불러주기
  switch (syscall_num)
  {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      get_args_addr(esp, arg_buf, 1);
      exit(*(int*)arg_buf[0]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_EXEC:
      get_args_addr(esp, arg_buf, 1);
      f->eax = (uint32_t)exec((const char*)*(int*)arg_buf[0]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_WAIT:
      get_args_addr(esp, arg_buf, 1);
      f->eax = (uint32_t)wait(*(pid_t*)arg_buf[0]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_CREATE:
      get_args_addr(esp, arg_buf, 2);
      f->eax = (uint32_t)create((const char*)*(int*)arg_buf[0], *(unsigned*)arg_buf[1]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_REMOVE:
      get_args_addr(esp, arg_buf, 1);
      f->eax = (uint32_t)remove((const char*)*(int*)arg_buf[0]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_OPEN:
      get_args_addr(esp, arg_buf, 1);
      f->eax = (uint32_t)open((const char*)*(int*)arg_buf[0]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_FILESIZE:
      get_args_addr(esp, arg_buf, 1);
      f->eax = (uint32_t)filesize(*(int*)arg_buf[0]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_READ:
      get_args_addr(esp, arg_buf, 3);
      f->eax = (uint32_t)read(*(int*)arg_buf[0], (char*)*(int*)arg_buf[1], *(unsigned*)arg_buf[2]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_WRITE:
      get_args_addr(esp, arg_buf, 3);
      f->eax = (uint32_t)write(*(int*)arg_buf[0], (const char*)*(int*)arg_buf[1], *(unsigned*)arg_buf[2]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_SEEK:
      get_args_addr(esp, arg_buf, 2);
      seek(*(int*)arg_buf[0], *(unsigned*)arg_buf[1]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_TELL:
      get_args_addr(esp, arg_buf, 1);
      f->eax = (uint32_t)tell(*(int*)arg_buf[0]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    case SYS_CLOSE:
      get_args_addr(esp, arg_buf, 1);
      close(*(int*)arg_buf[0]);
      memset(arg_buf, 0, sizeof(arg_buf));
      break;
    default:
      printf("Undefined System Call Number.");
      exit(-1);
      break;
  }

  //thread_exit ();
}

void get_args_addr(void* esp, void** argument_arr, int cnt)
{
  int i;
  for(i = 0; i < cnt; i++)
  {
    void* addr = esp + 4 * (i+1);
    IsValidAddr(addr);
    argument_arr[i] = addr;
  }
}

void IsValidAddr(const void* addr)
{
  //1. null pointer거나
  if(addr == NULL)
  {
    //printf("ERROR]] Address is null pointer.");
    exit(-1);
  }
  //2.유저 가상 메모리 주소 범위에서 벗어났을 때
  if((unsigned)addr >= PHYS_BASE || (unsigned)addr < 0x08048000)
  {
    //printf("ERROR]] Address is not in the user stack");
    exit(-1);
  }
  //3. 아직 매핑되지 않은 주소에 접근했는지
  if(pagedir_get_page(thread_current()->pagedir, addr) == NULL)
  {
    //printf("ERROR]] Address is not mapped now");
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
  pid_t child_pid = process_execute(cmd_line);
  
  return child_pid;
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

//file을 연다. 열리지 않으면 -1 반환하고 열리면 파일 디스크립터를 반환한다.
int open (const char *file)
{
  IsValidAddr(file);

  //파일 여는 중에 실행파일이 수정 될 수도 있으니까 열기전에 락 걸기
  //printf("Thread %d, %s acquire lock\n",thread_current()->tid, thread_current()->name);
  //lock_acquire(&file_lock);
  //파일을 연다.
  struct file* f = filesys_open(file);

  //만약 파일이 열리지 않으면 -1을 반환한다.
  if(f == NULL)
  {
    //printf ("open ERROR]] CANNOT OPEN FILE");
    return -1;
  }

  //파일 디스크립터를 추가하고 추가한 fd를 반환한다.
  int fd = add_file_desc(f);

  //fd를 추가로 할당할 공간이 없으면(fd = -1) 연 파일을 닫는다.
  if(fd == -1)
  {
    file_close(f);
  }
  //파일 열기 관련 작업 다 끝났으니 락 풀기
  //printf("Thread %d, %s release lock\n",thread_current()->tid, thread_current()->name);
  //lock_release(&file_lock);
  return fd;
}

/*
fd로 열린 파일을 닫는다.
*/
void close (int fd)
{
  struct file* f = get_file_by_fd(fd);
  if(f == NULL)
    return;
  
  //파일 닫기
  file_close(f);
  
  //파일 디스크립터 배열에서 fd에 해당하는 부분 null로 초기화
  remove_fd(fd);
}

//fd로 찾은 파일의 크기를 바이트단위로 반환한다.
int filesize (int fd)
{
  struct file* f = get_file_by_fd(fd);
  
  if(f == NULL)
  {
    //printf ("filesize ERROR]] CANNOT FIND FILE");
    return -1;
  }

  return file_length(f);
}

/*
fd로 열린 파일을 size 만큼 읽어와서 buffer에 담는다. 
그리고 실제로 읽은 바이트수를 반환한다.
반환값이 0이면 이미 fd가 파일의 마지막에 도달했다는 뜻
반환값이 -1이면 파일의 마지막까지 도달한 것은 아닌데 에러가 나서 파일을 읽어오지 못했다는 뜻
Fd 0를 넣으면 input_getc() 함수로 키보드 인풋을 가져온다.
*/
int read (int fd, void *buffer, unsigned size)
{
  IsValidAddr(buffer);
  int read_bytes = 0;

  //표준 입력을 가져와야 하는 경우
  if(fd == 0)
  {
    char input;
    for(read_bytes = 0; (unsigned)read_bytes < size; read_bytes++)
    {
      input = input_getc();
      *((uint8_t*)buffer + read_bytes) = input;
      if(input == '\0')
      {
        break;
      }
    }
  }
  //이건 표준 출력일 때 -> 읽어올 수 없다.
  else if(fd == 1)
  {
    //printf ("read ERROR]] CANNOT READ FD 1");
    return -1;
  }
  //일반적인 파일 read
  else
  {
    struct file* f = get_file_by_fd(fd);
    //fd에 해당하는 파일을 못 찾았을 때
    if (f == NULL)
    {
      //printf ("read ERROR]] CANNOT find file");
      return -1;
    }
    //printf("Thread %d, %s acquire lock\n",thread_current()->tid, thread_current()->name);
    lock_acquire(&file_lock);
    read_bytes = file_read(f, buffer, size);
    //printf("Thread %d, %s release lock\n",thread_current()->tid, thread_current()->name);
    lock_release(&file_lock);
  }

  return read_bytes;
}
/*
buffer에서 size 바이트만큼 가져와서 fd로 열린 파일에 적는다. 
실제로 적힌 문자의 바이트 수를 반환한다. 
Fd 1은 콘솔에다가 적는다. 콘솔에 쓸 때는 버퍼에 있는 모든 내용을 써야 한다.
*/
int write (int fd, const void *buffer, unsigned size)
{
  IsValidAddr(buffer);
  int write_bytes = 0;

  //표준 입력인 경우
  if(fd == 0)
  {
    //printf ("write ERROR]] CANNOT write FD 0");
    exit(-1);
  }
  //표준 출력인 경우 버퍼에 있는 모든 내용을 써야 한다.
  else if(fd == 1)
  {
    putbuf((const char*)buffer, size);
    write_bytes = size;
  }
  //일반적인 파일에 적는 경우
  else
  {
    struct file* f = get_file_by_fd(fd);
    //fd에 해당하는 파일을 못 찾았을 때
    if (f == NULL)
    {
      //printf ("write ERROR]] CANNOT find file");
      exit(-1);
    }
    //printf("Thread %d, %s acquire lock\n",thread_current()->tid, thread_current()->name);
    lock_acquire(&file_lock);
    write_bytes = file_write(f, buffer, size);
    //printf("Thread %d, %s release lock\n",thread_current()->tid, thread_current()->name);
    lock_release(&file_lock);
  }
  
  return write_bytes;
}
/*
fd로 열린 파일이 다음에 접근할 위치를 position으로 고친다
*/
void seek (int fd, unsigned position)
{
  struct file* f = get_file_by_fd(fd);

  f->pos = position;
}
/*
fd로 열린 파일의 next byte를 알려준다. 
*/
unsigned tell (int fd)
{
  struct file* f = get_file_by_fd(fd);

  return f->pos;
}

