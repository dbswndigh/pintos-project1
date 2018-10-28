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
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      list_push_back (&sema->waiters, &thread_current ()->elem);
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
  struct thread * t1;
  struct thread * t2;
  struct thread * highest;
  bool flag = false;
  ASSERT (sema != NULL);
  
  old_level = intr_disable ();
  
  ///
  if (!list_empty (&sema->waiters)){ 
    t1 = list_entry (list_front (&sema->waiters), struct thread, elem);
    highest = t1;
    while(&t1->elem != list_back(&sema->waiters)){
      t2 = list_entry ((&t1->elem)->next, struct thread, elem);
      if(highest->priority < t2->priority)
        highest = t2;
      t1 = t2;
    }
    list_remove(&highest->elem);
    sema->value++;
    flag = true;
    thread_unblock (highest);
  }
  ///
  if(!flag)
    sema->value++;
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
  ///
  struct thread * owner;///donee_candidate
  struct thread * acquirer;///doner_candidate
  struct donated_elem * d1;
  struct donated_elem * d2;
  struct donated_elem this_lock;
  struct lock * temp_lock;
  bool flag;///true if overall donation needed and temp_lock is already in.
  ///
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  
  owner = lock->holder;///donee_candidate = lock->holder;
  acquirer = thread_current ();///doner_candidate = thread_current();
  temp_lock = lock;///ÇØ´ç lock

  if(owner != NULL)
    if(owner->own_priority < acquirer->priority)
    /* priority donation bet owner and acquirer newly occurs */
    {
      /* change donee info of acquirer */
      acquirer->donee = owner;
      acquirer->donees_lock = temp_lock;
    }

  while(owner != NULL){
    if(owner->own_priority < acquirer->priority)
    /* donation or donated priority change needed */
    {
      /* owner->priority change needed */
      if(owner->priority < acquirer->priority)
        owner->priority = acquirer->priority;
      
      /* change donated_list of owner */
      flag = false;
      if(!list_empty(&owner->donated_list)){///temp_lock is already in
        d1 = list_entry(list_front(&owner->donated_list), struct donated_elem, elem);
        while((&d1->elem)->next != NULL){///while not tail.
          d2 = list_entry((&d1->elem)->next, struct donated_elem, elem);
          if(d1->lock == temp_lock){
            d1->priority = acquirer->priority;
            flag = true;
            break;
          }
          d1 = d2;
        }
      }
      if(!flag){///no exist of temp_lock, so addition needed 
        this_lock.lock = temp_lock;
        this_lock.priority = acquirer->priority;
        list_push_back(&owner->donated_list, &this_lock.elem);
      }
    }
    temp_lock = owner->donees_lock;//can be NULL
    acquirer = owner;//can't be NULL
    owner = acquirer->donee;//can be NULL
  }
  sema_down (&lock->semaphore);
  if(lock->holder != NULL)//Is the condition exact? but inclusive.
  {
    thread_current()->donee = NULL;
    thread_current()->donees_lock = NULL;
  }
  lock->holder = thread_current();
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
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock)/// 
{
  struct thread * curr;
  struct donated_elem * d1;
  struct donated_elem * d2;
  int max_priority;
  bool flag = false;//becomes true if donation exists.
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  lock->holder = NULL;
  sema_up (&lock->semaphore);
  
  curr = thread_current();
  if(!list_empty(&curr->donated_list)){
    d1 = list_entry(list_front(&curr->donated_list), struct donated_elem, elem);
    while((&d1->elem)->next != NULL){///while not tail
      d2 = list_entry((&d1->elem)->next, struct donated_elem, elem);
      if(d1->lock == lock){
        list_remove(&d1->elem);
        flag = true;
        break;
      }
      d1 = d2;
    }

    ///recover
    //if donation doesn't exist, no need to recover.
    if(flag){
      max_priority = curr->own_priority;
      if(!list_empty(&curr->donated_list)){//should use this condition again.
        d1 = list_entry(list_front(&curr->donated_list), struct donated_elem, elem);
        while((&d1->elem)->next != NULL){///while not tail
          d2 = list_entry((&d1->elem)->next, struct donated_elem, elem);
          if(d1->priority > max_priority)
            max_priority = d1->priority;
          d1 = d2;
        }
      }
    curr->priority = max_priority;///no using set_priority!
    }
  }
  thread_yield();
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
  list_push_back (&cond->waiters, &waiter.elem);
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
  struct semaphore_elem * s1;
  struct semaphore_elem * s2;
  struct semaphore_elem * highest_s;
  struct semaphore * sema;
  struct thread * t1;
  struct thread * t2;
  struct thread * highest = NULL;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)){
    s1 = list_entry (list_front (&cond->waiters), struct semaphore_elem, elem);
    while((&s1->elem)->next != NULL){//while not tail
      s2 = list_entry((&s1->elem)->next, struct semaphore_elem, elem);
      sema = &s1->semaphore;
      if(!list_empty (&sema->waiters)){
        t1 = list_entry (list_front(&sema->waiters), struct thread, elem);
        if(highest == NULL){
          highest = t1;
          highest_s = s1;
        }
        else if(highest->priority < t1->priority){
          highest = t1;
          highest_s = s1;
        }
        while(&t1->elem != list_back(&sema->waiters)){
          t2 = list_entry ((&t1->elem)->next, struct thread, elem);
          if(highest->priority < t2->priority){
            highest = t2;
            highest_s = s1;
          }
          t1 = t2;
        }
      }
      s1 = s2;
    }
    list_remove(&highest_s->elem);
    sema_up (&highest_s->semaphore);
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
