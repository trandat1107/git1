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
#include <stdarg.h>
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
sema_down (struct semaphore *sema) // lấy lại khóa mà thread hiện tại có trong list waiter 
{
  enum intr_level old_level;
  
  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
  {
    list_insert_ordered (&sema->waiters, &thread_current ()->elem, compare_priority, 0); // chèn và sắp xếp
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
sema_up (struct semaphore *sema)  // Thêm thread hiện tại được cho phép truy cập source và sắp xếp chúng trong list waiters của sema
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) {
    list_sort(&sema->waiters, compare_priority, 0); // Sắp xếp các thread như dạng các list element trong list waiters theo compare_priority
    thread_unblock (list_entry (list_pop_front (&sema->waiters),	
              struct thread, elem));  // Mở khóa cho thread đó truy cập source
  
  }
  
  sema->value++;		// tăng biến value lên 1
  thread_yield();		// chạy thread hiện tại
  
  
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
  lock->is_donated=false;	// Khóa đang không thuộc về thread nào
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.
   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)	// cấp phát lock cho thread
{
  
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  
  if(lock->holder != NULL)
  {
    thread_current()->waiting_for= lock;	// lock là thời gian khóa mà thread hiện tại phải chờ 
    if(lock->holder->priority < thread_current()->priority) // So sánh khóa có priority nhỏ hơn priority của khóa hiện tại hay không
    {  struct thread *temp=thread_current();	// temp = thread hiện tại
       while(temp->waiting_for!=NULL)		// trong khi thread temp có khóa nào muốn chiếm giữ
       {
         struct lock *cur_lock=temp->waiting_for;
         cur_lock->holder->priorities[cur_lock->holder->size] = temp->priority;	 //  add priority của thread hiện tại vào list donated
         cur_lock->holder->size+=1;	// tăng size donated lên 1
         cur_lock->holder->priority = temp->priority;	//mức độ ưu tiên của thread cầm khóa sẽ bằng mức độ ưu tiên của thread hiện tại
         if(cur_lock->holder->status == THREAD_READY)	// Nếu thread cầm khóa có trạng thái là sẵn sàng 
           break;	
         temp=cur_lock->holder;		// tiếp tục quy trình với temp = thread đang cầm lock
       }
       if(!lock->is_donated)	// Nếu lock được thread nào đó nắm giữ
         lock->holder->donation_no +=1;	// Số lượng cầm khóa + 1
       lock->is_donated = true; // khóa lock đã có thread cầm
       sort_ready_list();	// sắp xếp list_thread sẵn sàng 
    }
  }
  sema_down (&lock->semaphore);
  lock->holder = thread_current ();			// thread hiện tại sẽ giữ lock
  lock->holder->waiting_for=NULL;			// thread cầm lock không có khóa nào để chờ ( thread hoạt động )
} // Mục đích của hàm này là sắp xếp các thread thay quyền nhau nắm giữ các khóa theo mức độ ưu tiên 

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
  ASSERT (lock != NULL);		// Nếu lock == Null thì kết thúc và báo lỗi
  ASSERT (lock_held_by_current_thread (lock));	// Nêu thread hiện tại không nắm giữ lock thì kết thúc và báo lỗi

  struct semaphore *lock_sema=&lock->semaphore;	
  list_sort(&lock_sema->waiters, compare_priority, 0);	// sắp xếp list waiters theo hàm sort compare_priority

  if (lock->is_donated)	// kiểm tra có thread nào đang giữ lock hay không
  {
    thread_current()->donation_no -=1;	// Số lock mà thread hiện tại nắm giữ giảm 1
    int elem=list_entry (list_front (&lock_sema->waiters),struct thread, elem)->priority;	 // mức độ ưu tiên của thread ở đầu list priority  = elem
    search_array(thread_current(),elem);	// tìm mức độ ưu tiên của thread trong đầu list có trong mảng lưu các giá trị ưu tiên trước đó của thread không 
    thread_current()->priority = thread_current()->priorities[(thread_current()->size)-1]; 
    lock->is_donated = false;		// không có thread nào nắm giữ lock
  }
  if(thread_current()->donation_no ==0)
  {
    thread_current()-> size=1;
    thread_current()-> priority = thread_current()->priorities[0];
  }
  lock->holder = NULL;
  sema_up (&lock->semaphore);
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
  list_insert_ordered (&cond->waiters, &waiter.elem, compare_priority, 0);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
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
  ASSERT (cond != NULL);	// Nếu cond == NULL thoát và báo lỗi
  ASSERT (lock != NULL);	// Nếu lock == NULL thoát và báo lỗi	
  ASSERT (!intr_context ());	// Nếu có ngắt ngoài thì thoát và báo lỗi
  ASSERT (lock_held_by_current_thread (lock)); // Kiểm tra thread hiện tại nắm giữ lock hay không ?
  list_sort(&cond->waiters, compare_sema, 0);	// Săp xếp các waiters theo hàm compare_sema
  if (!list_empty (&cond->waiters))		// list waiters trong cond khác rỗng thì
    sema_up (&list_entry (list_pop_front (&cond->waiters),	
                          struct semaphore_elem, elem)->semaphore); // tăng value lên 1 thông báo là thread có thể truy cập source
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

/*sorts the corresponding semaphores on the basis of the priority
of the first thread in waiting list of each semaphore.*/
bool compare_sema(struct list_elem *l1, struct list_elem *l2,void *aux)		// So sánh 2 list elem hay là thuật toán để sắp xếp các list elem
{
  struct semaphore_elem *t1 = list_entry(l1,struct semaphore_elem,elem);	// 
  struct semaphore_elem *t2 = list_entry(l2,struct semaphore_elem,elem);
  struct semaphore *s1=&t1->semaphore;
  struct semaphore *s2=&t2->semaphore;
  if( list_entry (list_front(&s1->waiters), struct thread, elem)->priority > list_entry (list_front(&s2->waiters),struct thread, elem)->priority)
    return true;
  return false;
}
