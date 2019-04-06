#include "syscall.h"
#include "traps.h"
.globl call_print_temp
.extern panic_print
.globl call_print_temp_r
#define SYSCALL(name) \
  .globl name; \
  name: \
    movq $SYS_ ## name, %rax; \
    int $T_SYSCALL; \
    retq
#define SYSCALL_FAST(name) \
  .globl name; \
  name: \
  movq $SYS_ ## name, %rax; \
  syscall; \
  retq

SYSCALL(fork)
SYSCALL(exit)
SYSCALL(wait)
SYSCALL(pipe)
SYSCALL(read)
SYSCALL(write)
SYSCALL(close)
SYSCALL(kill)
SYSCALL(exec)
SYSCALL(open)
SYSCALL(mknod)
SYSCALL(unlink)
SYSCALL(fstat)
SYSCALL(link)
SYSCALL(mkdir)
SYSCALL(chdir)
SYSCALL(dup)
SYSCALL(getpid)
SYSCALL(sbrk)
SYSCALL(sleep)
SYSCALL(uptime)
SYSCALL_FAST(send)
SYSCALL_FAST(send_recv)
SYSCALL_FAST(recv)
SYSCALL_FAST(cr3_test)
SYSCALL_FAST(cr3_kernel)
SYSCALL_FAST(null_call)
SYSCALL(set_size)
SYSCALL_FAST(touch_pages)
