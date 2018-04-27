#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;
  int signum;
  if(argint(0, &pid) < 0 || argint(1, &signum) < 0 || signum < 0 || signum > 31)
    return -1;
  return kill(pid,signum);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }

    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
uint
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
//=================NEW SYSTEM CALLS SECTION========================================================
int
sys_sigprocmask(void)
{
  uint sigmask;
  if(argint(0, (int *)&sigmask) < 0)
    return -1;
  return sigprocmask(sigmask);
}

int
sys_signal(void)
{
    int signum;
    sighandler_t handler;
    if(argint(0, (int *)&signum) < 0 || argptr(1,(char**)&handler, sizeof(handler)) < 0 || signum < 0 || signum > 31)
        return -2;
    return (int)signal(signum, handler);

}

int
sys_sigret(void)
{
    sigret();
    return 0;

}
//=================================================================================================