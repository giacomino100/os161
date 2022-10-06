/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <synch.h>
#include <current.h>

/*
 * simple proc management system calls
 */
void sys__exit(int status){
  
  struct proc *p = curproc;
  p->p_status = status & 0xff; /* just lower 8 bits returned */

  proc_remthread(curthread);

  #if USE_SEMAPHORE_FOR_WAITPID
    V(p->p_sem);
  #else
    lock_acquire(p->p_lock);
    cv_signal(p->p_cv);
    lock_release(p->p_lock);
  #endif

  /*  VERSIONE PRECEDENTE AL LAB4
  /* get address space of current process and destroy 
  struct addrspace *as = proc_getas();
  as_destroy(as);
  */
  
  /* thread exits. proc data structure will be lost */
  thread_exit();

  panic("thread_exit returned (should not happen)\n");
  (void) status; // TODO: status handling
}


int sys_waitpid(pid_t pid, userptr_t statusp, int options) {
  struct proc *p = proc_search_pid(pid); 
  int s;
  (void)options; /* not handled */
  
  if (p==NULL) return -1;
  
  s = proc_wait(p);
  
  if (statusp!=NULL) 
    *(int*)statusp = s;
  
  return pid;

}

pid_t sys_getpid(void){
  KASSERT(curproc != NULL);
  return curproc->p_pid;
}