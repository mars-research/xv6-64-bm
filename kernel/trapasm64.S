#include "param.h"
  # vectors.S sends all traps here.
.globl alltraps
.globl syscall_entry
.extern cpu
.extern syscalls_fast
.extern print_test
.extern pgs
.extern proc
.extern sysenter_dispatch_test
syscall_entry:
  pushq %rbp
  movq %rsp, %rbp
  movq (proc), %rsp
  movq 0x10(%rsp), %rsp
#addq $KSTACKSIZE, %rsp
  push %rcx
  pushq %r11
  callq sysenter_dispatch_test
  callq *syscalls_fast(,%rax,8)
  popq %r11
  popq %rcx
  movq %rbp, %rsp
  popq %rbp  
  sysretq
alltraps:
  # Build trap frame.
  push %r15
  push %r14
  push %r13
  push %r12
  push %r11
  push %r10
  push %r9
  push %r8
  push %rdi
  push %rsi
  push %rbp
  push %rdx
  push %rcx
  push %rbx
  push %rax

  mov  %rsp, %rdi  # frame in arg1
  call trap

  # Return falls through to trapret...
.globl trapret
trapret:
  pop %rax
  pop %rbx
  pop %rcx
  pop %rdx
  pop %rbp
  pop %rsi
  pop %rdi
  pop %r8
  pop %r9
  pop %r10
  pop %r11
  pop %r12
  pop %r13
  pop %r14
  pop %r15

  # discard trapnum and errorcode
  add $16, %rsp
  iretq
