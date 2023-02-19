#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_waitx(void)
{
  uint64 addr, addr1, addr2;
  uint wtime, rtime;
  argaddr(0, &addr);
  argaddr(1, &addr1); // user virtual memory
  argaddr(2, &addr2);
  int ret = waitx(addr, &wtime, &rtime);
  struct proc* p = myproc();
  if (copyout(p->pagetable, addr1,(char*)&wtime, sizeof(int)) < 0)
    return -1;
  if (copyout(p->pagetable, addr2,(char*)&rtime, sizeof(int)) < 0)
    return -1;
  return ret;
}

uint64
sys_set_priority(void)
{
  int proc_pri, proc_pid;
  argint(0, &proc_pri);
  argint(1, &proc_pid);
  int ret = set_priority(proc_pri, proc_pid);
  return ret;
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_sigalarm(void)
{
  uint64 addr;
  int ticks;
  argint(0, &ticks);
  argaddr(1, &addr);
  if(ticks < 0 || addr < 0) return -1;
  struct proc *p = myproc();
  p->alarm_ticks = ticks;
  p->handler = addr;
  p->currTicks = 0;
  p->flag_alarm = 0;
  // printf("In alarm!\n");
  return 0;
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  p->tf_copy->kernel_satp = p->trapframe->kernel_satp;
  p->tf_copy->kernel_sp = p->trapframe->kernel_sp;
  p->tf_copy->kernel_trap = p->trapframe->kernel_trap;
  p->tf_copy->kernel_hartid = p->trapframe->kernel_hartid;
  *(p->trapframe) = *(p->tf_copy);
  p->flag_alarm = 0;
  return 0;
}

uint64
sys_trace(void)
{
  //if 0th argument is empty, i.e. if myproc()->trapframe->a0 is empty then return -1- HOW??
  int mask;
  argint(0, &mask);

  myproc()->mask = mask;
  return 0;
}

uint64
sys_settickets(void)
{
  struct proc *p = myproc();
  int number;
  argint(0, &number);
  //increase number of tickets of myproc by number.
  int curr_size = p->num_of_tickets;

  p->num_of_tickets = curr_size + number;
  for(int i = curr_size;i<p->num_of_tickets;i++)
  {
    p->tickets[i]=TOT_TICKETS;
    TOT_TICKETS++;
  }
  return 0;

}