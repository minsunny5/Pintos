#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/syscall.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);


/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE); //strlcpy (dst, src, dst size);

  /* Tokenize fn_copy to get arguments(argv) from file_name. 
  스레드 만들기 전에 argument 파싱을 해야 argument 제외하고 딱 실행 파일명으로 스레드를 만들 수 있다.*/
  char *exe_name, *save_ptr;
  char fn_copy2[128];//핀토스는 128바이트 제한이랬으니 그것보단 적게 들어오지않을까
  strlcpy (fn_copy2, file_name, sizeof(fn_copy2));
  exe_name = strtok_r (fn_copy2, " ", &save_ptr);//strtok_r함수는 fn_copy2를 수정해버리니까
  
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (exe_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
  {
    palloc_free_page (fn_copy);
  } 
  
  //생성된 자식 프로세스를 가져오기
  struct thread* child = get_child_process(tid);

  //sema_down()으로 자식 프로세스가 로드를 마칠때까지 대기하기
  struct semaphore sema_load;
  sema_init(&sema_load, 0);
  child->sema_load = &sema_load;
  printf("process execute: Thread %d, %s load sema down\n",thread_current()->tid, thread_current()->name);
  sema_down(&sema_load);

  //대기를 마치고
  //프로그램 load 실패시, -1 반환
  if(child->is_load == false)
  {
   //printf ("ERROR]] LOAD FAIL");
    return -1;
  }

  //프로그램 load 성공시, 자식 프로세스의 tid(=pid) 반환
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  
  //printf("Loading..Thread %d, %s acquire file lock\n",thread_current()->tid, thread_current()->name);
  lock_acquire(&load_lock);
  /* load executable. */
  success = load (file_name, &if_.eip, &if_.esp);
  
  /* If load failed, quit. */
  palloc_free_page (file_name);
  //printf("Loading..Thread %d, %s release file lock\n",thread_current()->tid, thread_current()->name);
  lock_release(&load_lock);
  if (!success)
  {
    //initial thread같은 경우 sema_load를 안가지고 있으니
    if(thread_current()->sema_load != NULL)
    {
      printf("start process: Thread %d, %s load sema up\n",thread_current()->tid, thread_current()->name);
      sema_up(thread_current()->sema_load);
    }
    thread_current()->is_load = false;
    thread_exit ();
  }

  //argument도 로드가 잘되었는지 디버깅
  //hex_dump((uintptr_t)if_.esp , if_.esp , PHYS_BASE - if_.esp , true);

  /* If load successed */
  thread_current()->is_load = true;
  //initial thread같은 경우 sema_load를 안가지고 있으니 그 경우에도 sema_up이 되게 해준다.
  if(thread_current()->sema_load != NULL)
  {
    printf("start process: Thread %d, %s load sema up\n",thread_current()->tid, thread_current()->name);
    sema_up(thread_current()->sema_load);
  }
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}



/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct semaphore sema_exit; //= thread_current()->sema_exit;
  struct thread* child = get_child_process(child_tid);
  int exit_status;

  if (child == NULL)
  {
    //printf ("WAIT ERROR]] CHILD NULL");
    return -1;
  }  
  //만약 기다리려고 했던 child_tid가 이미 exit된 상황이라면
  //이미 커널에 의해 종료되었다면(ex.예외상황에 걸려서 kill당했다던가) 
  if (child->is_exit)
  {
    //printf ("WAIT ERROR]] EXCEPTION BY KERNEL");
    return -1;
  }
    
  //child_tid를 누군가가 이미 기다리고 있는 상황이라면 똑같은 child를 또 기다릴 수 없다.
  if (child->sema_exit != NULL)
  {
    //printf ("WAIT ERROR]] Already Waiting");
    return -1;
  }
    
  /*waiting*/ 
  //tid로 프로세스를 찾아서 그 프로세스에 sema를 넘겨줘야됨.
  sema_init(&sema_exit, 0);//부모의 세마포어 초기화
  child->sema_exit = &sema_exit;//자식에게 부모의 세마포어 넘겨주기
  printf("process wait: Thread %d, %s exit sema down\n",thread_current()->tid, thread_current()->name);
  sema_down(&sema_exit);//부모가 블록되고 아까 process_execute에서 만든 유저프로세스 실행
  
  //자식 프로세스 실행을 마치면
  exit_status = child->exit_status;//자식의 exit status를 받아오고
  //유저 프로세스의 자식이든 initial thread의 자식인 유저프로세스이든
  //process_wait가 불리기 때문에 여기서 메모리 해제를 하면 된다.
  //근데 child가 exit해서 메모리 안남아있을텐데?
  remove_child(child);
  return exit_status;//자식의 exit status 반환
}

void
remove_child(struct thread* child)
{
  ASSERT (!list_empty(&child->parent->child_list));
  //나의 부모 프로세스가 존재한다면 
  if(child->parent != NULL)
  {
    //부모프로세스의 child list에서 (현재 exit하고 있는)본인을 제거한다.
    list_remove(&child->child_elem);
  }
  palloc_free_page(child);//자식의 프로세스 디스크립터 메모리를 해제한다.
}


/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  int fd;
  //open된 파일 디스크립터 모두 close해주기
  for(fd = 2; fd <= cur->fd_idx; fd++)
  {
    close(fd);
  }
  //파일 디스크립터 배열 free하기
  palloc_free_page(cur->fd_arr);

  //실행파일을 file_close하면 실행파일 쓰기권한 원래대로 가능하게 돌리기도 됨.
  file_close(cur->exefile);
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */


#define MAX_FDCOUNT 128 /* count of maximum limit of file descriptor */

static bool setup_stack (void **esp, char** argv, int argc);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Tokenize fn_copy to get arguments(argv) from file_name. 
  파일 열기 전에 argument 파싱을 해야 딱 실행 파일명만 입력할 수 있다.*/
  char file_name_copy[128];
  strlcpy (file_name_copy, file_name, strlen(file_name)+1);
  char *token, *save_ptr;
  int argc = 0; //arguments count
  char *argv[128];//argument list

  for (token = strtok_r (file_name_copy, " ", &save_ptr); token != NULL;
        token = strtok_r (NULL, " ", &save_ptr))
  {
    argv[argc] = token;
    argc++;
  }

  char* exe_name = argv[0];

  //파일 여는 중에 실행파일이 수정 될 수도 있으니까 열기전에 락 걸기
  //printf("Opening..Thread %d, %s acquire lock\n",thread_current()->tid, thread_current()->name);
  //lock_acquire(&file_lock);
  /* Open executable file. */
  file = filesys_open (exe_name);
  if (file == NULL) 
    {
      //lock_release(&file_lock);
      printf ("load: %s: open failed\n", exe_name);
      goto done; 
    }
  //현재 이 스레드에서 실행되고 있는 실행파일을 넣어준다.
  t->exefile = file;
  //실행파일이 프로세스 실행중에는 편집되지 않도록 편집권한을 막는다.
  file_deny_write(file);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }
    
  //파일 열기 끝났으니 락 풀기
  //printf("Opening..Thread %d, %s release lock\n",thread_current()->tid, thread_current()->name);
  //lock_release(&file_lock);

  /* Set up stack. */
  if (!setup_stack (esp, argv, argc))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  //file_close (file);
  return success;
  
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. and Initialize stack with arguments. */
static bool
setup_stack (void **esp, char** argv, int argc) 
{
  /* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory.*/
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
  {
    success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
    if (success)
    {
      /*esp를 유저가상메모리의 최상단으로 초기화*/
      *esp = PHYS_BASE;

      /* put arguments to stack */
      char* arg_address_in_stack[128];
      int i;
      //뭐 하나를 스택에 넣기 전에 스택포인터(아무것도 안넣으면 PHYS_BASE라서)를 그 크기만큼 아래로 내려줘야한다.
      for (i = argc-1; i >= 0; i--)
      {
        int arg_size = strlen(*(argv+i)) + 1;// 널문자 넣을 공간도 있어야돼서 +1
        *esp -= arg_size; // 그 문자개수(각각 1바이트짜리라)만큼 스택포인터 esp를 내려준다.
        memcpy(*esp, *(argv+i), arg_size); //esp 내려서 생긴 공간에 argv[i]를 argv_size크기만큼 복사해서 넣어준다.
        arg_address_in_stack[i] = *esp; //주소는 4바이트로 맞춰주기
      }

      while ((uint32_t)*esp % 4 != 0)//arg data 다넣고 나서 스택포인터의 위치가 4의 배수가 아닐 때 word allign
      {
        (*esp)--;
        memset(*esp, 0, sizeof(char)); //스택포인터의 위치가 4의 배수가 될 때까지 dummy 값을 넣어준다.(padding)
      }

      //이제 arg_address_in_stack을 스택에 넣어주자.
      for (i = argc; i >= 0; i--)
      {
        //일단 주소(무조건 4바이트짜리)를 넣을 공간부터 만들어야되니까 esp를 내려준다.
        *esp -= 4;
        //생긴 공간에 주소를 차례로 넣어준다.
        if(i == argc)
        {
          memset(*esp, 0, sizeof(int*));
        }
        else
        {
          memcpy(*esp, &arg_address_in_stack[i], sizeof(uint32_t**));
        }
      }
      
      //이제 argv 시작주소값(스택의 어디에 들어있는지)을 넣고 argc 넣으면 된다.
      //먼저 자리 만들고
      *esp -= 4;
      *(uintptr_t**)*esp = *esp + 4;//그림보면 아까 argv[0]를 저장한 바로 그 주소(if_->esp + 4)를 넣는다.

      //또 argc넣을 자리 만들고 넣어준다.
      *esp -= 4;
      *(int*)*esp = argc;//원래 이것도 memset으로 했는데 0으로 초기화할 때 쓰는거아니면 문제생길 수 있대서 바꿈.

      //마지막에는 return address자리에 fake address를 넣어줘야한다.
      *esp -= 4;
      memset(*esp, 0, sizeof(int*));

    }
      
    else
      palloc_free_page (kpage);
  }

  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

//파일 f에 대한 파일 디스크립터 생성+추가한 뒤 반환해주는 함수
int
add_file_desc(struct file* f)
{
  struct thread* cur = thread_current();
  
  //파일디스크립터를 추가할 위치를 찾는 방법: 비어있는 곳을 찾고
  //그 위치의 index가 fd 최고 개수를 넘지 않을 때까지 돌아간다.
  while(cur->fd_arr[cur->fd_idx] != 0 && cur->fd_idx < MAX_FDCOUNT)
  {
    cur->fd_idx++;
  }

  //그러다 만약 비어있는 위치가 없어서 최고개수를 넘었을 때, -1 반환
  if(cur->fd_idx >= MAX_FDCOUNT)
  {
    //printf ("add fd ERROR]] Full FD");
    return -1;
  }
    

  //비어있는 위치를 찾아서 거기에 파일을 넣는다.
  cur->fd_arr[cur->fd_idx] = f;

  //파일을 넣은 위치(=파일 디스크립터)를 리턴한다.
  return cur->fd_idx;
}

//파일 디스크립터 fd에 해당하는 파일을 반환해주는 함수
struct file* 
get_file_by_fd(int fd)
{
  struct thread* cur = thread_current();

  //invalid fd
  if (fd < 0 || fd >= MAX_FDCOUNT)
  {
    return NULL;
  }
  //valid fd
  else
  {
    return cur->fd_arr[fd];
  }
}

//파일 디스크립터 fd에 해당하는 배열 내용(파일 포인터)을 지워주는 함수
void remove_fd(int fd)
{
  struct thread* cur = thread_current();

  //invalid fd
  if (fd < 0 || fd >= MAX_FDCOUNT)
  {
    return;
  }

  cur->fd_arr[fd] = NULL;
}