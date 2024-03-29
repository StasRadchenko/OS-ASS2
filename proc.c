#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);
extern void call_sigret(void);
extern void call_sigret_end(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}
//-----------------New function section------------------------------------------------------------
//=================HELPER FUNCTIONS================================================================
uint setBit(uint bits_arr, int bit){
    bits_arr |= 1UL << bit;
    return bits_arr;
}
uint clearBit (uint bits_arr, int bit){
    bits_arr &= ~(1UL << bit);
    return bits_arr;
}

int isBitOn(uint bits_arr, int bit){
    return (bits_arr >> bit) & 1U;
}
//=================================================================================================
//=================System calls====================================================================
uint sigprocmask(uint sigmask){
    struct proc *p = myproc();
    uint old_mask;
    pushcli();
    old_mask = p->signal_mask;
    p->signal_mask = sigmask;
    popcli();
    return old_mask;
}

sighandler_t signal(int signum, sighandler_t handler)
{
    struct proc *p = myproc();
    pushcli();
    sighandler_t old_one = p->signal_handlers[signum];
    p->signal_handlers[signum] = handler;
    popcli();
    return old_one;
}

void sigret(void)
{
    struct proc *p = myproc();
    pushcli();
    memmove(p->tf,p->user_tf_backup,sizeof(*p->tf));
    p->signal_mask = p->signal_mask_backup;
    popcli();
}
//=================END System calls================================================================
//=================Handlers========================================================================
void default_handler(int signum){
    struct proc *p = myproc();

    if (signum == SIGSTOP){
        while(!(isBitOn(p->pending_signals,SIGCONT))) {
            yield();
        }
        return;
    }
    else if (signum == SIGCONT){
        if(isBitOn(p->pending_signals,SIGSTOP)){
            pushcli();
            uint cur_pending;
            do{
                cur_pending = p->pending_signals;
            }while(!cas(&p->pending_signals, cur_pending, clearBit(cur_pending,SIGSTOP)));
            popcli();
        }

    }
    else{
        pushcli();
        p->killed = 1;
        cas(&p->state, SLEEPING, RUNNABLE);
        popcli();
    }
    //p->signal_mask = p->signal_mask_backup;//????
    pushcli();
    uint cur_pending;
    do{
        cur_pending = p->pending_signals;
    }while(!cas(&p->pending_signals, cur_pending, clearBit(cur_pending,signum)));
    popcli();
    return;
}
void user_handler(int signum){
  struct proc *p = myproc();
  pushcli();
  uint sp = p->tf->esp;
  sp -= sizeof(struct trapframe);
  p->user_tf_backup = (struct trapframe *)sp;
  memmove(p->user_tf_backup, p->tf,sizeof(struct trapframe));
  uint func_size = call_sigret_end - call_sigret;
  sp -= func_size;
  uint sigret_address = sp;
  memmove((void*)sp, call_sigret, func_size);
  sp -= 4;
  *(int *)sp = signum;
  sp -= 4;
  *(uint *)sp = sigret_address;
  p->tf->esp = sp;
  p->tf->eip = (uint)p->signal_handlers[signum];
  //p->signal_mask = p->signal_mask_backup;
  uint cur_pending;
  do{
      cur_pending = p->pending_signals;
  }while(!cas(&p->pending_signals, cur_pending, clearBit(cur_pending,signum)));
  popcli();
  return;

}
//=================END HANDLERS====================================================================
//-------------------------------------------------------------------------------------------------

int 
allocpid(void) 
{
    int pid;

    do{
        pid = nextpid;
    } while(!cas(&nextpid, pid, pid+1));

    return pid;
}


//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  pushcli();
  do {
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
          if(p->state == UNUSED)
              break;
      if (p == &ptable.proc[NPROC]) {
          popcli();
          //FROM HERE BUG
          return 0; // ptable is full
      }
  } while (!cas(&p->state, UNUSED, EMBRYO));
  popcli();
  p->pid = allocpid();


  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");
  //---------------2.1.2 INIT OF PROCESS MASK,PENDING SIGNALS,HANDLERS,FRAME BACKUP----------------
  p->signal_mask = 0;
  p->pending_signals = 0;
  p->user_tf_backup = 0;
  int i;
  for (i = 0; i < 32; i++){
    p->signal_handlers[i] =(void *) SIG_DFL;

  }
  //---------------2.1.2 END-----------------------------------------------------------------------
  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.

  p->state = RUNNABLE;

}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  //---------------2.1.2 UPDATE FOR CHILD PROCESS--------------------------------------------------
    np->pending_signals = 0;
    np->signal_mask = curproc->signal_mask;
    for (i = 0; i < 32; i++){
        np->signal_handlers[i] = curproc->signal_handlers[i];
    }
  //---------------END UPDATE OF CHILD PROCESS-----------------------------------------------------
  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  if (!cas(&np->state, EMBRYO, RUNNABLE))
     panic("fork: cas failed");
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  pushcli();
  if(!cas(&curproc->state, RUNNING,NEG_ZOMBIE)){
      panic("cas failed in exit");
  }

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  pushcli();
  for(;;){
    if(!cas(&curproc->state, RUNNING, NEG_SLEEPING)){
        panic("first cas failed in wait");
    }
    curproc->chan = curproc;
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(cas(&p->state, ZOMBIE, NEG_UNUSED)){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        curproc->chan = 0;
        cas(&curproc->state, NEG_SLEEPING, RUNNING);
          // release(&ptable.lock);
        if(!cas(&p->state, NEG_UNUSED, UNUSED)){
            panic("CAS FAILED LINE 434 IN PROC.C");
        }
        popcli();
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      curproc->chan = 0;
      if(!cas(&curproc->state, NEG_SLEEPING, RUNNING)){
          panic("fourth cas faild in wait");
      }
      popcli();
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sched();
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    pushcli();
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(!cas(&p->state, RUNNABLE, RUNNING))
          continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      if (cas(&p->state, NEG_SLEEPING, SLEEPING)) {
          if (cas(&p->killed, 1, 0))
              p->state = RUNNABLE;
      }
      if(cas(&p->state, NEG_ZOMBIE, ZOMBIE)){
        wakeup1(p->parent);
      }
      if (cas(&p->state, NEG_RUNNABLE, RUNNABLE)) {

      }
    }
    popcli();

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
    int intena;
    struct proc *p = myproc();
    // if(!holding(&ptable.lock))
    //   panic("sched ptable.lock");
    if(mycpu()->ncli != 1)
        panic("sched locks");
    if(p->state == RUNNING)
        panic("sched running");
    if(readeflags()&FL_IF)
        panic("sched interruptible");
    intena = mycpu()->intena;
    swtch(&p->context, mycpu()->scheduler);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
    pushcli();
    if(!cas(&myproc()->state, RUNNING, NEG_RUNNABLE)){
        panic("cas failed in yield");
    }
    sched();
    popcli();
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  popcli();

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  pushcli();

  // Go to sleep.
  p->chan = chan;
  if(!cas(&p->state, RUNNING, NEG_SLEEPING)){
      panic("cas failed in sleep");
  }
  release(lk);
  sched();
  acquire(lk);

  popcli();


}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->chan == chan && (p->state == SLEEPING || p->state == NEG_SLEEPING)) {
         while (p->state == NEG_SLEEPING) {
                // busy-wait
          }
          if (cas(&p->state, SLEEPING, NEG_RUNNABLE)) {
             p->chan = 0;
             if (!cas(&p->state, NEG_RUNNABLE, RUNNABLE))
                 panic("wakeup1: cas failed");
          }
      }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  pushcli();
  wakeup1(chan);
  popcli();
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid, int signum)
{
  struct proc *p;
  pushcli();
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      if(p->state == ZOMBIE){
          popcli();
          return -1;
      }

      p->pending_signals = setBit(p->pending_signals,signum);
      popcli();
      return 0;
    }
  }
  popcli();
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie",
  [NEG_UNUSED] "neg_unused",
  [NEG_SLEEPING] "neg_sleep ",
  [NEG_RUNNABLE] "neg_runnable",
  [NEG_ZOMBIE] "neg_zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED || p->state == NEG_UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
