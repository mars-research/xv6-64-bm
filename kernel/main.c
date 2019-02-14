#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void startothers(void);
static void mpmain(void)  __attribute__((noreturn));
extern pde_t *kpgdir;
extern char end[]; // first address after kernel loaded from ELF file

extern void syscall_entry();
void * get_rsp(){
  return *((void **)(&cpu->ts.esp0));
}
// Bootstrap processor starts running C code here.
// Allocate a real stack and switch to it, first
// doing some setup required for memory allocator to work.
void syscallinit(){
  unsigned long long l = ((SEG_UCODE-2)<<3 )<<16 | SEG_KCODE<<3;
  wrmsr(0xC0000081, l<<32);
  wrmsr(0xC0000082, syscall_entry);
  wrmsr(0xC0000084, (1<<9));
  rdmsr(0xC0000080, &l);
  wrmsr(0xC0000080, l | 1);
}
void temp_print(unsigned long long stack){
  cprintf("stack is %x%x;\n", (uint)(stack>>32), (uint)stack);
}
void print_test(){
  cprintf("enter syscall \n");
}
int
main(void)
{
  uartearlyinit();
  kinit1(end, P2V(4*1024*1024)); // phys page allocator
  kvmalloc();      // kernel page table
  //if (acpiinit()) // try to use acpi for machine info
  //  mpinit();      // otherwise use bios MP tables
  lapicinit();
  seginit();       // set up segments
  
  cprintf("\ncpu%d: starting xv6\n\n", cpu->id);
  picinit();       // interrupt controller
  ioapicinit();    // another interrupt controller
  consoleinit();   // I/O devices & their interrupts
  uartinit();      // serial port
  pinit();         // process table
  tvinit();        // trap vectors
  binit();         // buffer cache
  fileinit();      // file table
  iinit();         // inode cache
  ideinit();       // disk
  if(!ismp)
    timerinit();   // uniprocessor timer
  //startothers();   // start other processors
  kinit2(P2V(4*1024*1024), P2V(PHYSTOP)); // must come after startothers()
  syscallinit();
  userinit();      // first user process
  // Finish setting up this processor in mpmain.
  mpmain();
}

// Other CPUs jump here from entryother.S.
void
mpenter(void)
{
  switchkvm(); 
  seginit();
  lapicinit();
  mpmain();
}

// Common CPU setup code.
static void
mpmain(void)
{
  cprintf("cpu%d: starting\n", cpu->id);
  idtinit();       // load idt register
  xchg(&cpu->started, 1); // tell startothers() we're up
#ifdef PCID
  asm volatile("movq %%cr4, %%rax\n\t"
               "bts $17, %%rax\n\t"
               "movq %%rax, %%cr4":::"rax"); //enables pcids
#endif
  scheduler();     // start running processes
}

pde_t entrypgdir[];  // For entry.S
void entry32mp(void);

// Start the non-boot (AP) processors.
static void
startothers(void)
{
  extern uchar _binary_out_entryother_start[], _binary_out_entryother_size[];
  uchar *code;
  struct cpu *c;
  char *stack;

  // Write entry code to unused memory at 0x7000.
  // The linker has placed the image of entryother.S in
  // _binary_entryother_start.
  code = p2v(0x7000);
  memmove(code, _binary_out_entryother_start, (uintp)_binary_out_entryother_size);

  for(c = cpus; c < cpus+ncpu; c++){
    if(c == cpus+cpunum())  // We've started already.
      continue;

    // Tell entryother.S what stack to use, where to enter, and what 
    // pgdir to use. We cannot use kpgdir yet, because the AP processor
    // is running in low  memory, so we use entrypgdir for the APs too.
    stack = kalloc();
#if X64
    *(uint32*)(code-4) = 0x8000; // just enough stack to get us to entry64mp
    *(uint32*)(code-8) = v2p(entry32mp);
    *(uint64*)(code-16) = (uint64) (stack + KSTACKSIZE);
#else
    *(void**)(code-4) = stack + KSTACKSIZE;
    *(void**)(code-8) = mpenter;
    *(int**)(code-12) = (void *) v2p(entrypgdir);
#endif

    lapicstartap(c->apicid, v2p(code));

    // wait for cpu to finish mpmain()
    while(c->started == 0)
      ;
  }
}

#ifndef X64
// Boot page table used in entry.S and entryother.S.
// Page directories (and page tables), must start on a page boundary,
// hence the "__aligned__" attribute.  
// Use PTE_PS in page directory entry to enable 4Mbyte pages.
__attribute__((__aligned__(PGSIZE)))
pde_t entrypgdir[NPDENTRIES] = {
  // Map VA's [0, 4MB) to PA's [0, 4MB)
  [0] = (0) | PTE_P | PTE_W | PTE_PS,
  // Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
  [KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
};
#endif

//PAGEBREAK!
// Blank page.

