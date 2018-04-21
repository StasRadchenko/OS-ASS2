#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"


// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;
//#################Helper functions################################################################
int 
hasSIGFunc(uint pendings, int signal)
{
  int pow = 1;
  uint tempPending = pendings;
  pow = pow << signal;
  tempPending = tempPending & pow;
  if (tempPending > 0)
    return 1;
  return 0;
}

//#################Helper functions END############################################################

//#################SIGNAL HANDLER##################################################################
void
handle_signal(struct trapframe *tf)
{
  uint mask_backup;
  uint signals;
  uint papow = 1;
  int i;
  sighandler_t cur_handler;
  if(myproc() == 0 || ((tf->cs & 3) != DPL_USER) || myproc()->pendig_signals == 0)
    return;
  mask_backup = myproc()->signal_mask;
  signals = myproc()->pendig_signals;
  for (i = 0; i < 32; i++){
      int is_sig = hasSIGFunc(signals,papow);
      int is_masked = hasSIGFunc(myproc()->signal_mask,papow);

      if (is_sig & (~is_masked)){
          myproc()->isHandlingSig = 1;
          myproc()->signal_mask |= papow;
          cur_handler = myproc()->signal_handlers[i];
          if (cur_handler == (void*)SIG_DFL){
              dfl_handler(i);
              uint temp = ~papow;
              myproc()->pendig_signals &= temp;
              myproc()->signal_mask = mask_backup;
              return;
          }
          else if(cur_handler == (void*)SIG_IGN){
              uint temp = ~papow;
              myproc()->pendig_signals &= temp;
              myproc()->signal_mask = mask_backup;
              return;
          }
          else{
              user_hadnler(i,myproc());
              return;

          }

      }


      papow = papow << 1;  //keep moving on signals bits

  }

}
//#################SIGNAL HANDLER END##############################################################

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
	
}
