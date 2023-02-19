Team members: Ananya Amancherla (2019101041), Pranjali Pathre (2019112002)

## Specification 1: `strace`
- Added $U/_strace to UPROGS in Makefile
- Added `sys_trace()` in kernel/sysproc.c
- Modified `fork()` to copy `mask` from parent to child
- Modified `syscall()` to print syscall info if the syscall is traced
- Created user program `strace` in user/strace.c that calls the trace system call to set the mask of the process and run the function passed.

## Specification 1: `sigalarm and sigreturn`
- Added alarmtest.c to Makefile add two syscall `sigalarm` and `sigreturn`.
- Add new fields to proc structure
- If the process has a timer outstanding then expire the handler function.
- We do not directly restore all variables to trapframe because all kernel stack and something other are used for public (becuase if we restore kernel stack we may encounter error).
## Specification 2: Scheduler Overview

### ROUND ROBIN - DEFAULT

This is the default scheduling algorithm. The processes are preemted from the CPU they were assigned to once their time slice expires. This ensures that all processes in the ready queue are given CPU attention.

```c
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
```

### FCFS

As the name suggests, the process that arrives first is assigned CPU time first. Only once it is done is another process assigned that CPU. This is a non preemptive policy. Hence if a process with a longer CPU burst time arrives first and one with a shorter CPU burst time arrives next, the second process will have to wait till the longer process is done (in case of 1 CPU). Processes are picked based on least creation time.

1. Loop through the processses to find the one with the lowest creationTime.
2. Switch to chosen process.
3. Update the process parameters (STATE, running_number)
4. Only enable premption for the required schedulers.
5. Release the process's lock and then reacquire it before jumping context switching again.

- Edited `struct proc` to store the necessary variables
- Edited `allocproc()` to initialise the new variable created above
- Edited `scheduler()` to run the process with the lowest time created
- Edited `kerneltrap()` and `usertrap()` in trap.c to disable premption with timer interrupts

```c
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
```

### LOTTERY BASED SCHEDULER
It assigns a time slice to the process randomly in proportion to the number of tickets it owns. That is the probability that the process runs in a given time slice is proportional to the number of tickets owned by it.
Implemented a system call int `settickets(int number)` , which sets the number of tickets of the calling process. By default, each process is assigned one ticket; calling this routine makes it such that a process raise the number of tickets it receives, and thus receive a higher proportion of CPU cycles.

- Edited `struct proc` to store the  number of tickets
- Edited `allocproc()` to initialise the new variables created above
- Edited `scheduler()` to choose the process randomly based on ticket chosen
- Added syscall `settickets`
```c
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
```
### PRIORITY BASED SCHEDULING

Each process is assigned a default priority of 60. In case two or more processes have the same priority, we
use the number of times the process has been scheduled to break the tie. If the tie remains, use the start-time of the process to break the tie. The processes with higher priority are chosen and given CPU attention before the ones with lower priority. (A lower value of priority indicates a higher priority).
The `set_priority` syscall can be used to change the priority of the processes. Processes are picked based on highest priority.


1. Loop through the processses to find the one with the highest priority (lowest value). If priority equals for two or more processes then consider running_number and creationTime to break the tie.
2. Switch to chosen process.
3. Update the process parameters (STATE, running_number, wait_time (set to 0), run_time (set to 0))
5. Release the process's lock and then reacquire it before jumping context switching again.

- Edited `struct proc` to store the dynamic and static priority, wait time, runtime after every CPU burst.
- Edited `allocproc()` to initialise the new variables created above
- Edited `scheduler()` to run the process with the highest priority
- Edited `clockintr()` to track runtime and wait time
- Added a new sycall `set_priority` to change the priority of a process

```c
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
```

### MULTI-LEVEL FEEDBACK QUEUE

* There are five priority queues, with the highest priority being number as 0 and bottom queue with the lowest priority as 4. The time slices of the 5 queues are {1,2,4,8,16}. If the number of ticks that a process receives in that queue exceeds the permissible number of ticks it is downgraded to a lower queue.
* A new process always starts in the Q0, after which it is moved down as time proceeds.
* If the process used the complete time slice assigned for its current priority queue, it is pre-empted and ​ inserted at the end of the next lower level queue else at the end of the same queue.
* A round-robin scheduler should be used for processes at the lowest priority queue.
* To prevent starvation ageing is implemented initially wherein if a process has been waiting for CPU attention for more than its wait time, it is moved one queue up.

1. Initialize five queues to the required time slices and other parameters.
2. Check if any procs have to be moved up the queue to prevent aging.
3. Pick the process (if exists) from the lowest numbered priority queue to execute.
4. Check if the process was terminated or self-exited and accordingly move the down the queue or append at the end of the queue.

- Edited `struct proc` to store the time elapsed in queue x, current queue, added a new struct to store queue data.
- Edited `allocproc()` to initialise the new variables created above
- Edited `scheduler()` to run the process with the highest priority
- Edited `clockintr()` to track runtime in a particular queue
- Edited `kerneltrap()` and `usertrap()` to yield when process has exhausted its time slice

```c
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
```

## PERFORMANCE COMPARISON REPORT

The five scheduling policies that have been implemented in this modified version of the xv6 operating system are
FCFS
Multilevel-Feedback Queue
Round Robin
Priority Based Scheduling
Lottery Based Scheduling

## Comparing Performances:

Choosing Processes for CPU attention :
FCFS: The process with the earliest ctime (creation time) is chosen and given CPU time.
Round Robin: a process with state RUNNABLE is chosen to execute. The process is preempted (yielded) after a particular number of clock ticks.
Priority based scheduling: The process with the highest priority is selected for execution.
MLFQ: The processes are chosen from the top most queue. Processes in lower queues are chosen only if the higher queues are empty.
LBS: Assigns a time slice to the process randomly in proportion to the number of tickets it owns.

Context Switching:
FCFS: There is no context switching involved during the total CPU burst time of the process and hence low overheads.
Round Robin: Context switching will arise since it is premptive and needs to keep going over the entire queue.
Priority based scheduling: Context switching will arise since it is premptive and needs to keep finding the lowest priority process. If priority changes, rescheduling done.
MLFQ: Context switching since the processes change their priority queue and also due to pushing and popping in the same queue.
LBS: Context switching will arise since it is premptive.

Waiting Time:
FCFS: It has the highest waiting time (excluding LBS)since it is non premptive. Convoy effect might occur.
Round Robin: Average waiting time is often long. However there is bounded waiting. In general, each process must wait no longer than (n − 1) × q time units until its next time quantum
Priority based scheduling: Wait time can be adjusted by setting appropriate priority. Low priority will take much more wait time. As aging is not implemented, starvation may occur.
LBS: Processes with high number of tickets have a high probability of getting low waiting time.
- **Benchmarking**

  |         Scheduler         | <rtime> | <wtime> |
  | :-----------------------: | :-----: | :-----: |
  |        Round robin        |   36    |   246   |
  |  First come first serve   |   82    |   377   |
  | Priority based scheduler  |   40    |   283   |
  | Multilevel Feedback queue |   39    |   248   |
  |  Lottery Based Scheduling |   42    |   1029  |

  The above results are obtained by running `schedulertest` on a single CPU.

  *Note: MLFQ scheduling analysis timeline plot added.

## COPY-ON-WRITE FORK

### Overview

The current implementation of the fork system call in xv6 makes a complete copy of the parent’s memory
image for the child.A copy-on-write fork will let both parent and child use the same physical
pages initially, and make a copy only when either of them wants to modify any page of the
memory image.

### Implementation

- *Forking a process*

* In `fork()`, to copy parent's memory for the child process, `uvmcopy()` from `vm.c` is called.
* Modified `uvmcopy()` to map every virtual address of the child process pagetable `new`, to the physical address of the corresponding parent pagetable (`old`) entries. Each page table entry is also marked READ-ONLY and COPY-ON-WRITE for the child and parent pagetables. The parent pagetable is remapped, and TLB is flushed.

- *Handling page faults*
- Currently, the xv6 kills the process when a pagefault occurs.
- Modified `usertrap()` in `trap.c`:
  - [`scause` value 15][1] corresponds to STORE pagefault.
  - If the page table entry corresponding to the faulting address (stored in `stval`) is marked COPY-ON-WRITE and is READ-ONLY and has a reference count of 2, i.e. 2 virtual addresses currently point to the same page, copy the physical page of the page table entry and map the faulting address to it.
  - The new page table entry is allowed write permission and not marked COPY-ON-WRITE.
  - If the pagetable entry has a reference count of 1, but is marked COPY-ON-WRITE and READ-ONLY, simply give it write permissions. This happens if the page was mapped to 2 virtual addresses before but on a pagefault, had already been copied.
  - In other cases of page fault (not when trying to write to a read-only cow page), kill the process.

  [1] <https://riscv.org/wp-content/uploads/2017/05/riscv-privileged-v1.10.pdf> : 4.1.10 Supervisor Cause Register (scause)
- *Freeing pages*
- Maintain a data structure `page_ref` in `kalloc.c` to keep track of how many times a physical address is referenced.
- Added functions to increment and decrement this count, used when necessary.
- Added checks to make sure that a page can be freed only if the reference count is 0.