#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"

/** 
  @brief Create a new thread in the current process. 
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  PTCB* new_process_thread = acquire_PTCB();                                                    // we allocate space for PTCB
  initialize_PTCB(new_process_thread);                                                          // initialization of PTCB

  if(task != NULL) {
    TCB *new_thread = spawn_thread(CURPROC, new_process_thread, start_process_thread);  
    new_process_thread->tcb = new_thread;
    new_thread->ptcb = new_process_thread;
    new_process_thread->task = task;
    new_process_thread->argl = argl;
    new_process_thread->args = args;
    rlnode_init(&new_process_thread->ptcb_list_node, new_process_thread);
    rlist_push_back(&CURPROC->ptcb_list, &new_process_thread->ptcb_list_node);                  // filling the list with PTCBs 
    wakeup(new_process_thread->tcb);
  }
  
  CURPROC->thread_count++;                                                                      // the number of threads has encreased by one

  return (Tid_t)new_process_thread;
}


/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
  return (Tid_t) cur_thread()->ptcb;
}


/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  PCB* curproc = CURPROC;                                 
  PTCB* ptcb = (PTCB*)tid;

  if (rlist_find(&curproc->ptcb_list, ptcb, NULL) == NULL)
    return -1;

  if (ptcb == (PTCB*)sys_ThreadSelf())
    return -1;

  /* Legality checks */

    ptcb->refcount++;
    
    while (ptcb->exited != 1 && ptcb->detached != 1) {                
        kernel_wait(& ptcb->exit_cv, SCHED_USER);
    }

    ptcb->refcount--;                                 

    if (ptcb->detached == 1) 
      return -1;

    if (exitval != NULL) {
      *exitval = ptcb->exitval;     
     }  

    if (ptcb->refcount == 0) {
      rlist_remove(& ptcb->ptcb_list_node);
      free(ptcb);
    }

  return 0;
}


/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  PCB* curproc = CURPROC;
  PTCB* ptcb = (PTCB*)tid;

  if (rlist_find(&curproc->ptcb_list, ptcb, NULL) == NULL)
    return -1;

  if((ptcb->exited == 1))                                 // Once the thread exits, it won't be detached 
    return -1;
  
  ptcb->detached = 1;                                     // Flag = 1, this thread can not be joined
  ptcb->refcount = 0;
  kernel_broadcast(& ptcb->exit_cv);
  
  return 0;
}


/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
  PCB *curproc = CURPROC;  /* cache for efficiency */
  PTCB* ptcb = (PTCB*)sys_ThreadSelf();

  ptcb->exited = 1;
  ptcb->exitval = exitval;
  kernel_broadcast(& cur_thread()->ptcb->exit_cv);

  /* First, store the exit status */
  curproc->thread_count--;

  if (curproc->thread_count == 0) {

    /* free ptcbs */
    while(!is_rlist_empty(& curproc->ptcb_list)) {

      PTCB* ptcb_temp = rlist_pop_front(& curproc->ptcb_list)->ptcb;
      free(ptcb_temp);
    }


    if(get_pid(curproc)!=1) {
    /* Reparent any children of the exiting process to the
       initial task */
      PCB* initpcb = get_pcb(1);
      while(!is_rlist_empty(& curproc->children_list)) {
        rlnode* child = rlist_pop_front(& curproc->children_list);
        child->pcb->parent = initpcb;
        rlist_push_front(& initpcb->children_list, child);
      }

      /* Add exited children to the initial task's exited list
         and signal the initial task */
      if(!is_rlist_empty(& curproc->exited_list)) {
        rlist_append(& initpcb->exited_list, &curproc->exited_list);
        kernel_broadcast(& initpcb->child_exit);
      }

      /* Put me into my parent's exited list */
      rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
      kernel_broadcast(& curproc->parent->child_exit);

    }

    assert(is_rlist_empty(& curproc->children_list));
    assert(is_rlist_empty(& curproc->exited_list));

    /*
      Do all the other cleanup we want here, close files etc.
     */

    /* Release the args data */
    if(curproc->args) {
      free(curproc->args);
      curproc->args = NULL;
    }

    /* Clean up FIDT */
    for(int i=0;i<MAX_FILEID;i++) {
      if(curproc->FIDT[i] != NULL) {
        FCB_decref(curproc->FIDT[i]);
        curproc->FIDT[i] = NULL;
      }
    }

    /* Disconnect my main_thread */
    curproc->main_thread = NULL;

    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;
  }

  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
}
