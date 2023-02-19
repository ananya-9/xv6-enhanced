#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#include <stddef.h>

struct queue queue_mlfq[5];
struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

uint TOT_TICKETS;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
  for(int j = 0; j < 5; j++){
    queue_mlfq[j].q_num = 0;
    queue_mlfq[j].age_time = 500;
    if(j == 0) queue_mlfq[j].q_timeslice = 1;
    else queue_mlfq[j].q_timeslice = (queue_mlfq[j-1].q_timeslice)*2;
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  // printf("In alloc proc!\n");
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->creationTime = ticks;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  if((p->tf_copy = (struct trapframe *)kalloc()) == 0){
    release(&p->lock);
    return 0;
  }
  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

// #ifdef MLFQ
    // On the initiation of a process, push it to the end of the highest priority queue.

// #endif
  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  p->rtime = 0;
  p->etime = 0;
  p->creationTime = ticks;
  p->static_priority = 60;
  p->priority = 60;
  p->waitTime = 0;
  p->run_time = 0;
  p->currTicks = 0;
  p->handler = 0;
  p->flag_alarm = 0;
  p->alarm_ticks = 0;

  queue_mlfq[0].proc[queue_mlfq[0].q_num] = p;
  p->queue_curr = 0;
  p->entryTime = ticks;
  queue_mlfq[0].q_num++;
  for (int i = 0; i < 5; i++)
    p->time_inq[i] = 0;

  #ifdef LBS
  p->tickets[0] = TOT_TICKETS;
  TOT_TICKETS++;
  p->num_of_tickets = 1;
  #endif

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  if(p->tf_copy)
    kfree((void*)p->tf_copy);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->rtime = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->creationTime = 0;
  p->etime = 0;
  p->priority = 60;
  p->running_number = 0;
  p->run_time = 0;
  p->waitTime = 0;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;
  //enabling tracing for forked child
  np->mask = p->mask;
  #ifdef LBS
  for(int i=0;i<p->num_of_tickets; i++)
  {
    np->tickets[i] = TOT_TICKETS;
    TOT_TICKETS++;
  }
  #endif

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->etime = ticks;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

int waitx(uint64 addr, uint* wtime, uint* rtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          for(int h = 0; h < 5; h++){
            // printf("[q%d -> %d] ", h, np->time_inq[h]);
          }
          // printf("\n");
          pid = np->pid;
          *rtime = np->rtime;
          *wtime = np->etime - np->creationTime - np->rtime;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

void
update_time()
{
  struct proc* p;
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    p->time_inq[p->queue_curr]++;
    if (p->state == RUNNING) {
      p->rtime++;
      p->run_time++;
      p->ticks_inq++;
    }
    else if(p->state == SLEEPING){
      p->waitTime++;
    }
    release(&p->lock);
  }
}

int get_dp(int static_pri, struct proc *p){
  int sleep_time = p->waitTime;
  int run_time = p->run_time;
  int niceness;
  if(sleep_time + run_time > 0){
    niceness = sleep_time*10/(sleep_time + run_time);
  }
  else{
    niceness = 5;
  }

  int dp = 0;
  if((static_pri - niceness + 5) < 100) dp = static_pri - niceness + 5;
  else dp = 100;

  if(dp < 0) dp = 0;
  p->priority = dp;

  return dp;
}

int
set_priority(int new_priority, int pid){
  if(new_priority > 100 || new_priority < 0){
    // (2, "set_priority error\n");
    // printf("Error in set_priority!\n");
    return -2;
  }

  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid) {
      int old_pri = p->static_priority;
      p->static_priority = new_priority;

      int niceness = 5;
      int dp = 0;
      if((new_priority - niceness + 5) < 100) dp = new_priority - niceness + 5;
      else dp = 100;

      if(dp < 0) dp = 0;

      int dp_old = p->priority;
      p->priority = dp;
      p->run_time = 0;
      release(&p->lock);

      if(dp < dp_old) yield();
      return old_pri;
    }
    release(&p->lock);
  }
  return -1;
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
#ifdef DEFAULT
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
#endif
#ifdef FCFS
  struct proc *p;
  struct proc *p_fcfs = 0;
  struct cpu *c = mycpu();
  c->proc = 0;
  for(;;){
    intr_on();
    for (p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if (p->state == RUNNABLE){
        if (p_fcfs == 0){
          p_fcfs = p;
          continue;
        }
        else{
          if(p_fcfs->creationTime > p->creationTime)
          {
            release(&p_fcfs->lock);
            p_fcfs = p;
            continue;
          }
        }
      }
      release(&p->lock);
    }

    if(p_fcfs != 0){
      p_fcfs->state = RUNNING;
      p_fcfs->running_number++;
      c->proc = p_fcfs;
      // printf("Proc to run: %s\n", p_fcfs->name);
      swtch(&c->context, &p_fcfs->context);
      c->proc = 0;
      release(&p_fcfs->lock);
      p_fcfs = 0;
    }

  }
#endif

#ifdef PBS
  struct proc *p;
  struct proc *p_pbs = 0;
  struct cpu *c = mycpu();
  c->proc = 0;
  for(;;){
    intr_on();
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE){
        if (p_pbs == 0){
          p_pbs = p;
          continue;
        }
        else{
          get_dp(p->static_priority, p);
          get_dp(p_pbs->static_priority, p_pbs);
          if(p_pbs->priority > p->priority){
            release(&p_pbs->lock);
            p_pbs = p;
            continue;
          }
          else if(p_pbs->priority == p->priority){
            if(p_pbs->running_number > p->running_number){
              release(&p_pbs->lock);
              p_pbs = p;
              continue;
            }
            else if(p_pbs->running_number == p->running_number && p_pbs->creationTime < p->creationTime){
              release(&p_pbs->lock);
              p_pbs = p;
              continue;
            }
          }
        }
      }
      release(&p->lock);
    }
    if(p_pbs != 0){
      p_pbs->state = RUNNING;
      p_pbs->running_number++;
      c->proc = p_pbs;
      p_pbs->waitTime = 0;
      p_pbs->run_time = 0;
      swtch(&c->context, &p_pbs->context);
      c->proc = 0;
      release(&p_pbs->lock);
      p_pbs = 0;
    }
  }
#endif
#ifdef LBS
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    //random function
    int new = (ticks * 1103515245) + 12345;
	  new = ((new>> 16) & 0x7fff);

    int r = new%TOT_TICKETS;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        //int len = sizeof(p->tickets)/4;
        for(int j=0;j<p->num_of_tickets;j++)
        {
          if(p->tickets[j] == r)
          {
            p->state = RUNNING;
            c->proc = p;
            swtch(&c->context, &p->context);
            c->proc = 0;
          }

        }
      }
      release(&p->lock);
    }
  }
#endif
#ifdef MLFQ
  // struct proc *p;
  // printf("IN mlfq!\n");
  struct proc *p_mlfq = 0, *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  // printf("OUT\n");
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    // Prevent starvation
    for(int qnum = 1; qnum < 5; qnum++){
      // printf("Found proc qnum = %d!\n", qnum);
      for(int pnum = 0; pnum < queue_mlfq[qnum].q_num; pnum++){
        // printf("****************\n");
        if(queue_mlfq[qnum].proc[pnum] != 0){
          if(queue_mlfq[qnum].proc[pnum]->state != RUNNABLE) continue;
          else if(ticks - queue_mlfq[qnum].proc[pnum]->entryTime > queue_mlfq[qnum].age_time){
            // Adding to the new queue
            // printf("Aging: Moving proc to: %d\n", qnum - 1);
            struct proc *p_temp;
            p_temp = queue_mlfq[qnum].proc[pnum];
            queue_mlfq[qnum - 1].proc[queue_mlfq[qnum - 1].q_num] = queue_mlfq[qnum].proc[pnum];
            queue_mlfq[qnum - 1].proc[queue_mlfq[qnum - 1].q_num]->entryTime = ticks;
            p_temp->entryTime = ticks;
            p_temp->ticks_inq = 0;
            p_temp->queue_curr--;
            // printf("%d - %d", p_temp->queue_curr,  qnum - 1);
            queue_mlfq[qnum - 1].proc[queue_mlfq[qnum - 1].q_num]->ticks_inq = 0;
            queue_mlfq[qnum - 1].proc[queue_mlfq[qnum - 1].q_num]->queue_curr = qnum - 1;
            queue_mlfq[qnum - 1].q_num++;

            // Removing from previous queue
            for(int t = pnum + 1; t < queue_mlfq[qnum].q_num; t++){
              queue_mlfq[qnum].proc[t - 1] = queue_mlfq[qnum].proc[t];
            }
            queue_mlfq[qnum].q_num--;
            queue_mlfq[qnum].proc[queue_mlfq[qnum].q_num] = 0;
          }
        }
      }
      // printf("unFound proc qnum = %d!\n", qnum);
    }
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // printf("Found a runnable process!\n");
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        c->proc = 0;
      }
      release(&p->lock);
    }

    int curr_q = 0, prev_q = 0, pnum_stop = 0;
    for(int qnum = 0; qnum < 5; qnum++){
      if(p_mlfq == 0){
        // printf("In\n");
        for(int pnum = 0; pnum < queue_mlfq[qnum].q_num; pnum++){
          if(queue_mlfq[qnum].proc[pnum] != 0){
            acquire(&queue_mlfq[qnum].proc[pnum]->lock);
            if(queue_mlfq[qnum].proc[pnum]->state == RUNNABLE){
              p_mlfq = queue_mlfq[qnum].proc[pnum];
              curr_q = qnum;
              prev_q = qnum;
              pnum_stop = pnum;
              break;
            }
            release(&queue_mlfq[qnum].proc[pnum]->lock);
          }
        }
      }
    }
    if(p_mlfq != 0){
      // printf("In-In\n");
      p_mlfq->state = RUNNING;
      p_mlfq->running_number++;
      c->proc = p_mlfq;
      swtch(&c->context, &p_mlfq->context);
      c->proc = 0;
      release(&p_mlfq->lock);

      // Check if the process is running
      if(p_mlfq != 0){
        if(p_mlfq->state == RUNNABLE || p_mlfq->state == SLEEPING){
          if(p_mlfq->queue_curr <= 3){
            if(p_mlfq->ticks_inq >= queue_mlfq[p_mlfq->queue_curr].q_timeslice){
              p_mlfq->queue_curr++;
              curr_q = p_mlfq->queue_curr+=1;
              // printf("Changing queue to %d\n", p_mlfq->queue_curr+=1);

              queue_mlfq[curr_q].proc[queue_mlfq[curr_q].q_num] = p_mlfq;
              queue_mlfq[curr_q].proc[queue_mlfq[curr_q].q_num]->queue_curr = curr_q;
              queue_mlfq[curr_q].proc[queue_mlfq[curr_q].q_num]->entryTime = ticks;
              queue_mlfq[curr_q].proc[queue_mlfq[curr_q].q_num]->ticks_inq = 0;
              p_mlfq->queue_curr = curr_q;
              p_mlfq->ticks_inq = 0;
              p_mlfq->entryTime = ticks;
              queue_mlfq[curr_q].q_num++;
            }
          }
          else{
            curr_q = p_mlfq->queue_curr;
            queue_mlfq[curr_q].proc[queue_mlfq[curr_q].q_num] = p_mlfq;
            queue_mlfq[curr_q].proc[queue_mlfq[curr_q].q_num]->queue_curr = curr_q;
            queue_mlfq[curr_q].proc[queue_mlfq[curr_q].q_num]->entryTime = ticks;
            queue_mlfq[curr_q].proc[queue_mlfq[curr_q].q_num]->ticks_inq = 0;
            p_mlfq->queue_curr = curr_q;
            p_mlfq->ticks_inq = 0;
            p_mlfq->entryTime = ticks;
            queue_mlfq[curr_q].q_num++;
          }
          for(int t = pnum_stop + 1; t < queue_mlfq[prev_q].q_num; t++){
            queue_mlfq[prev_q].proc[t - 1] = queue_mlfq[prev_q].proc[t];
          }
          queue_mlfq[prev_q].q_num--;
          queue_mlfq[prev_q].proc[queue_mlfq[prev_q].q_num] = 0;
        }
      }
    }
    p_mlfq = 0;
  }
#endif
}
// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");

#ifdef DEFAULT
  printf("PID State Name\n");
#endif
#ifdef FCFS
  printf("PID State Name ctime\n");
#endif
#ifdef PBS
  printf("PID Priority State Name rtime wtime nrun\n");
#endif
// #ifdef MLFQ
//   printf("PID Priority State rtime wtime nrun q0 q1 q2 q3 q4\n");
// #endif

  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
#ifdef DEFAULT
    printf("%d %s %s", p->pid, state, p->name);
#endif
#ifdef FCFS
    printf("%d %s %s %d", p->pid, state, p->name, p->creationTime);
#endif
#ifdef LBS
    printf("%d %s %s %d", p->pid, state, p->name, p->creationTime);
#endif
#ifdef PBS
    int wtime = ticks - p->creationTime - p->rtime;
    get_dp(p->static_priority, p);
    printf("%d %d %s %s %d %d %d", p->pid, p->priority, state, p->name, p->rtime, wtime, p->running_number);
    // printf("ticks: %d, ctime: %d, rtime: %d\n", ticks, p->creationTime, p->rtime);
#endif
#ifdef MLFQ
    int wtime = ticks - p->ticks_inq;
    printf("%d %d %s %d %d %d %d", p->pid, p->priority, state, p->rtime, wtime, p->running_number, p->ticks_inq);
#endif
    printf("\n");
  }
}
