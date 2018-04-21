#include "mmu.h"
#include "syscall.h"
#include "traps.h"

  # vectors.S sends all traps here.
.globl alltraps
.globl calling_sigret
.globl end_of_calling
alltraps:
  # Build trap frame.
  pushl %ds
  pushl %es
  pushl %fs
  pushl %gs
  pushal
  
  # Set up data segments.
  movw $(SEG_KDATA<<3), %ax
  movw %ax, %ds
  movw %ax, %es

  # Call trap(tf), where tf=%esp
  pushl %esp
  call trap
  addl $4, %esp



  # Return falls through to trapret...
.globl trapret
trapret:
  # task 2.3
  push %esp
  call handle_signal
  addl $4 , %esp
  # task 2.3
  popal
  popl %gs
  popl %fs
  popl %es
  popl %ds
  addl $0x8, %esp  # trapno and errcode
  iret

 calling_sigret:
  movl $SYS_sigret, %eax
  int $T_SYSCALL
 end_of_calling:
  movl $0, %eax