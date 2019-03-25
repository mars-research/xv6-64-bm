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
extern uintp vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

#ifndef X64
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
#endif

void _dump_stack(uintp stack) {
  /* Assume that entire stack page is mapped */
  unsigned int roundup = PGROUNDUP(stack); 
  unsigned int counter = 0; 

  /* If we're exactly at the start of the page, 
     dump next page, well, really we don't know 
     whether the stack is empty or full, so the 
     next page might be unmapped 
   */
  if (roundup == stack)
    roundup = roundup + PGSIZE;

  cprintf("stack starting at:%x\n", stack); 

  /* Dump as words (4 bytes) in groups of 16, but dump the 
     last 1-4 bytes individually, in case we're spill out in the 
     next page that might be unmapped */
  cprintf("%x:", stack); 
  while (stack < roundup - sizeof(void *)) {
    cprintf("%x ", *(uintp *)stack); 
    stack += sizeof(void *); 
    if (counter == 15) {
      counter = 0;
      cprintf("\n");
      cprintf("%x:", stack);
    }
    counter ++; 
  }

  cprintf(" ");

  /* If any bytes left 1-4 dump them as bytes */
  while (stack < roundup) {
    cprintf("%x ", *(char*)stack); 
    stack ++; 
  }

  cprintf("\n");

}

void dump_stack(char *s) {
  uintp esp; 
#if X64
  asm volatile("mov %%rbp, %0" : "=r" (esp));  
#else
  esp = (uintp)&s;
#endif
  _dump_stack(esp);
}

void dump_stack_addr(uintp a) {
  _dump_stack(a);
}

#if X64
void dump_state(struct trapframe *tf) {
  cprintf("rax: %x, rbx: %x, rcx: %x, rdx: %x\n",
          tf->eax, tf->rbx, tf->rcx, tf->rdx);
  cprintf("rsp: %x, rbp: %x, rsi: %x, rdi: %x\n",
          tf->esp, tf->rbp, tf->rsi, tf->rdi);
  cprintf("r8: %x, r9: %x, r10: %x, r11: %x, r12: %x, r13: %x, r14: %x, r15: %x, ss: %x\n",
          tf->r8, tf->r9, tf->r10, tf->r11, tf->r12, tf->r13, tf->r14, tf->r15, tf->ds);
  cprintf("err: %x, eip: %x, cs: %x, esp: %x, eflags: %x\n",
          tf->err, tf->eip, tf->cs, tf->esp, tf->eflags);
}
#else
void dump_state(struct trapframe *tf) {
  cprintf("eax: %x, ebx: %x, ecx: %x, edx: %x\n",
          tf->eax, tf->ebx, tf->ecx, tf->edx);
  cprintf("esp: %x, ebp: %x, esi: %x, edi: %x\n",
          tf->esp, tf->ebp, tf->esi, tf->edi);
  cprintf("gs: %x, fs: %x, es: %x, ds: %x, ss: %x\n",
          tf->gs, tf->fs, tf->es, tf->ds, tf->ss);
  cprintf("err: %x, eip: %x, cs: %x, esp: %x, eflags: %x\n",
          tf->err, tf->eip, tf->cs, tf->esp, tf->eflags);
}
#endif

void dump_kernel(struct trapframe *tf) {

  dump_state(tf); 

  if (proc)
    cprintf("current process, id: %d, name:%s\n", 
          proc->pid, proc->name);
  else 
    cprintf("current process is NULL\n"); 

  if (proc && proc->tf != tf)
    dump(); 

  /* Inside the trap function, tf is on top of the stack */
  _dump_stack((uintp)tf);
  return;
};

void dump() {
  if (!proc) {
     cprintf("current process is NULL\n");
     return;
  }

  cprintf("state of the current process, id: %d, name:%s\n", 
          proc->pid, proc->name);

  dump_state(proc->tf); 
  return;
};

void sys_oops() {
  pushcli(); 
  cprintf("\nuser oops, pid:%d, name:%s\n", 
    proc->pid, proc->name);
  dump_state(proc->tf);
  dump_stack_addr(proc->tf->esp); 
  popcli();
};

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(proc->killed)
      exit();
    proc->tf = tf;
    syscall();
    if(proc->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpu->id == 0){
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
            cpu->id, tf->cs, tf->eip);
    lapiceoi();
    break;
   
  //PAGEBREAK: 13
  default:
    if(proc == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpu->id, tf->eip, rcr2());
      dump_kernel(tf); 
      dump();
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            proc->pid, proc->name, tf->trapno, tf->err, cpu->id, tf->eip, 
            rcr2());

    dump_state(tf);
    //dump_stack_addr(0);
    dump_stack_addr(tf->esp);
//    dump_pgdir(myproc()->pgdir, 0, KERNBASE);  

    proc->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running 
  // until it gets to the regular system call return.)
  if(proc && proc->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(proc && proc->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(proc && proc->killed && (tf->cs&3) == DPL_USER)
    exit();
}
