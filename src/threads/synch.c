/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  
  while (sema->value == 0) //사용 가능한 자원이 없을 때
    {
      //ready queue에 thread를 추가했으니 preemption해야되는지 체크하자.
      list_insert_ordered(&sema->waiters, &thread_current ()->elem, priority_greater, NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters))
  {
    //block된 상태에서도 우선순위가 변경될 수 있으므로 확인차 sort해줘야 안전함.
      list_sort(&sema->waiters, priority_greater, NULL);
      thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                struct thread, elem));
  }
  sema->value++;
  //ready queue에 thread를 추가했으니 preemption해야되는지 체크하자.
  thread_preemption_check();
  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  if (lock->holder != NULL)//락이 이미 다른 스레드한테 있으면
  {
    thread_current()->desire_lock = lock;
    if(thread_current()->priority > lock->holder->priority)
    {
      list_insert_ordered(&lock->holder->donation_list, &thread_current()->donation_elem,
                          priority_greater_in_donation_list, NULL);
      priority_donation();
    }
  }
  
  sema_down (&lock->semaphore); //여기서 락이 풀릴 때까지 기다린다.
  //sema_down이후로 진행이 되었다는 소리는 요청하던 락을 가졌다는 뜻이니
  thread_current()->desire_lock = NULL; 
  lock->holder = thread_current (); //락이 풀리면 락을 자신이 가진다.
  
}
/*
priority_greater과 같은 기능의 함수인데, donation_list의 순서를 정하기 위해 조금 수정한 버전이다.
*/
bool
priority_greater_in_donation_list(const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED)
{
  const struct thread *a = list_entry (a_, struct thread, donation_elem);
  const struct thread *b = list_entry (b_, struct thread, donation_elem);
  
  return a->priority > b->priority;
}
/*
제일 높은 우선순위를 연쇄적으로 기부한다.
*/
void
priority_donation(void)
{
  const int MAX_DEPTH = 8; //depth 최대는 8이라고 써져 있음.
  int depth;
  struct thread *cur = thread_current();

  for(depth = 0; depth < MAX_DEPTH; depth++)//그러니 8번까지만 이렇게 들어갈 수 있다.
  {
    if(cur->desire_lock == NULL)//요청하는 락이 없을 때까지
      break;
    
    struct thread *holder = cur->desire_lock->holder;
    holder->priority = cur->priority;//모두 제일 높은 priority를 연쇄적으로 받는다.
    cur = holder;
  }
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  //락을 반환하기 전에
  //그 락을 위해서 priority를 donation했던 thread를 donation리스트에서 제거하고
  remove_from_donator(lock);
  //priority를 재설정해야한다.
  set_priority_again();
  
  lock->holder = NULL;
  sema_up (&lock->semaphore);//락을 반환한다.
}
/*
priority를 새로고침해준다.
*/
void
set_priority_again()
{
  struct thread *cur = thread_current();
  
  // - 더이상 내게 donation을 한 스레드가 없다면 (= donation list가 비어있다면) 원래 priority로 돌리면 되고.
  if (list_empty(&cur->donation_list))
  {
    cur->priority = cur->origin_priority;
  }
  // - donation list가 차있다면 이제 지금 donation list에 가지고 있는 priority중에 제일 높은 우선순위로 바꿔줘야한다.
  else
  {
    list_sort(&cur->donation_list, priority_greater_in_donation_list, NULL);
    struct thread *highest_thread = list_entry(list_front(&cur->donation_list), struct thread, donation_elem);
    //원래 나의 우선순위보다 우선순위가 높은 친구가 donation 해준다면 받아야
    if(cur->origin_priority < highest_thread)
    {
      cur->priority = highest_thread->priority;
    }
    //원래 나의 우선순위보다 우선순위가 낮은 친구가 donation 해준다그러면 받지 말고
    else
    {
      cur->priority = cur->origin_priority;
    }
  }
}
/*
lock을 요청한 스레드를 찾아 기부자 목록에서 삭제한다.
*/
void
remove_from_donator(struct lock *lock)
{
  struct list_elem *e;
  struct thread *cur = thread_current();
  for(e = list_begin(&cur->donation_list); e!= list_end(&cur->donation_list); )
  {
    struct thread *t = list_entry(e, struct thread, donation_elem);
    if(t->desire_lock == lock)
    {
      e = list_remove(&t->donation_elem);
    }
    else
    {
      e = list_next(e);
    }
  }
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  list_insert_ordered(&cond->waiters, &waiter.elem, sema_priority_greater, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* Returns true if value A's greatest priority is greater than value B's greatest priority, 
false otherwise. */
bool
sema_priority_greater (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  struct semaphore_elem *a_sema = list_entry (a_, struct semaphore_elem, elem);
  struct semaphore_elem *b_sema = list_entry (b_, struct semaphore_elem, elem);

  struct list * a_wait_list = &(a_sema->semaphore.waiters);
  struct list * b_wait_list = &(b_sema->semaphore.waiters);

  int a_priority = list_entry(list_begin(a_wait_list), struct thread, elem)->priority;
  int b_priority = list_entry(list_begin(b_wait_list), struct thread, elem)->priority;
  
  return a_priority > b_priority;
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters))
  {
    //리스트에서 팝해주기 전에 리스트가 우선순위 큰 순서로 정렬되었는지 확인차 정렬
    list_sort(&cond->waiters, sema_priority_greater, NULL);
    sema_up (&list_entry (list_pop_front (&cond->waiters),
              struct semaphore_elem, elem)->semaphore);
  }
    
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
