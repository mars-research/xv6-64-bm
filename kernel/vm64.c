/* vm64.c 
 *
 * Copyright (c) 2013 Brian Swetland
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

 struct cpu *cpu;
 struct proc *proc;

pde_t *kpml4;
static pde_t *kpdpt;
static pde_t *iopgdir;
static pde_t *kpgdir0;
static pde_t *kpgdir1;


void tvinit(void) {}
void idtinit(void) {}

static void mkgate(uint *idt, uint n, void *kva, uint pl, uint trap) {
  uint64 addr = (uint64) kva;
  n *= 4;
  trap = trap ? 0x8F00 : 0x8E00; // TRAP vs INTERRUPT gate;
  idt[n+0] = (addr & 0xFFFF) | ((SEG_KCODE << 3) << 16);
  idt[n+1] = (addr & 0xFFFF0000) | trap | ((pl & 3) << 13); // P=1 DPL=pl
  idt[n+2] = addr >> 32;
  idt[n+3] = 0;
}

static void tss_set_rsp(uint *tss, uint n, uint64 rsp) {
  tss[n*2 + 1] = rsp;
  tss[n*2 + 2] = rsp >> 32;
}

static void tss_set_ist(uint *tss, uint n, uint64 ist) {
  tss[n*2 + 7] = ist;
  tss[n*2 + 8] = ist >> 32;
}
extern void* vectors[];

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  uint64 *gdt;
  uint *tss;
  uint64 addr;
  void *local;
  struct cpu *c;
  uint *idt = (uint*) kalloc();
  int n;
  memset(idt, 0, PGSIZE);

  for (n = 0; n < 256; n++)
    mkgate(idt, n, vectors[n], 0, 0);
  mkgate(idt, 64, vectors[64], 3, 1);
  mkgate(idt, 0x02, vectors[0x02], 0, 0x02);
  mkgate(idt, 0x18, vectors[0x18], 0, 0x03);
  lidt((void*) idt, PGSIZE);

  // create a page for cpu local storage 
  local = kalloc();
  memset(local, 0, PGSIZE);

  gdt = (uint64*) local;
  tss = (uint*) (((char*) local) + 1024);
  *((unsigned long long *)(&(tss[11]))) = (unsigned long long)kalloc();
  *((unsigned long long *)(&(tss[13]))) = (unsigned long long)kalloc();
  tss[16] = 0x00680000; // IO Map Base = End of TSS

  // point FS smack in the middle of our local storage page
  wrmsr(0xC0000100, ((uint64) local) + (PGSIZE / 2));

  c = &cpus[cpunum()];
  c->local = local;

  cpu = c;
  proc = 0;

  addr = (uint64) tss;
  gdt[0] =         0x0000000000000000;
  gdt[SEG_KCODE] = 0x0020980000000000;  // Code, DPL=0, R/X
  gdt[SEG_UCODE] = 0x0020F80000000000;  // Code, DPL=3, R/X
  //gdt[SEG_UCODE_SYSCALL] = 0x0020F80000000000;  // Code, DPL=3, R/X
  gdt[SEG_KDATA] = 0x0000920000000000;  // Data, DPL=0, W
  gdt[SEG_KCPU]  = 0x0000000000000000;  // unused
  gdt[SEG_UDATA] = 0x0000F20000000000;  // Data, DPL=3, W
  gdt[SEG_TSS+0] = (0x0067) | ((addr & 0xFFFFFF) << 16) |
                   (0x00E9LL << 40) | (((addr >> 24) & 0xFF) << 56);
  gdt[SEG_TSS+1] = (addr >> 32);

  lgdt((void*) gdt, 8 * sizeof(uint64));

  ltr(SEG_TSS << 3);
};

// The core xv6 code only knows about two levels of page tables,
// so we will create all four, but only return the second level.
// because we need to find the other levels later, we'll stash
// backpointers to them in the top two entries of the level two
// table.
pde_t*
setupkvm(void)
{
  pde_t *pml4 = (pde_t*) kalloc();
  if(!pml4)
    panic("pml4 kalloc failed");  

  pde_t *pdpt = (pde_t*) kalloc();
  if(!pdpt)
    panic("pdpt kalloc failed");  

  pde_t *pgdir = (pde_t*) kalloc();
  if(!pgdir)
    panic("pgdir kalloc failed");  

  memset(pml4, 0, PGSIZE);
  memset(pdpt, 0, PGSIZE);
  memset(pgdir, 0, PGSIZE);
  pml4[511] = v2p(kpdpt) | PTE_P | PTE_W | PTE_U | PTE_G;
  pml4[0] = v2p(pdpt) | PTE_P | PTE_W | PTE_U | PTE_G;
  pdpt[0] = v2p(pgdir) | PTE_P | PTE_W | PTE_U | PTE_G; 

  // virtual backpointers
  pgdir[511] = ((uintp) pml4) | PTE_P;
  pgdir[510] = ((uintp) pdpt) | PTE_P;

  return pgdir;
};

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
//
// linear map the first 4GB of physical memory starting at 0xFFFFFFFF80000000
void
kvmalloc(void)
{
  int n;
  kpml4 = (pde_t*) kalloc();
  kpdpt = (pde_t*) kalloc();
  kpgdir0 = (pde_t*) kalloc();
  kpgdir1 = (pde_t*) kalloc();
  iopgdir = (pde_t*) kalloc();
  memset(kpml4, 0, PGSIZE);
  memset(kpdpt, 0, PGSIZE);
  memset(iopgdir, 0, PGSIZE);
  kpml4[511] = v2p(kpdpt) | PTE_P | PTE_W | PTE_G;
  kpdpt[511] = v2p(kpgdir1) | PTE_P | PTE_W | PTE_G;
  kpdpt[510] = v2p(kpgdir0) | PTE_P | PTE_W | PTE_G;
  kpdpt[509] = v2p(iopgdir) | PTE_P | PTE_W | PTE_G;
  for (n = 0; n < NPDENTRIES; n++) {
    kpgdir0[n] = (n << PDXSHIFT) | PTE_PS | PTE_P | PTE_W | PTE_G;
    kpgdir1[n] = ((n + 512) << PDXSHIFT) | PTE_PS | PTE_P | PTE_W | PTE_G;
  }
  for (n = 0; n < 16; n++)
    iopgdir[n] = (DEVSPACE + (n << PDXSHIFT)) | PTE_PS | PTE_P | PTE_W | PTE_PWT | PTE_PCD | PTE_G;
  switchkvm();
}

void
switchkvm()
{
#ifdef PCID
  lcr3(CR3_ENTRY_INVALIDATE(0,v2p(kpml4)));
#else
  lcr3(v2p(kpml4));
#endif
}

void
switchuvm(struct proc *p)
{
  void *pml4;
  uint *tss;
  pushcli();
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");
  tss = (uint*) (((char*) cpu->local) + 1024);
  tss_set_rsp(tss, 0, (uintp)proc->kstack + KSTACKSIZE);
  pml4 = (void*) PTE_ADDR(p->pgdir[511]);
#ifdef PCID
  if(unlikely(proc->pcid+NPCIDS<pcid_counter)){
    proc->pcid = pcid_counter;
    pcid_counter+=1;
    lcr3(CR3_ENTRY_INVALIDATE((proc->pcid%NPCIDS + 1), v2p(pml4)));
  }
  else{
    //outb(0xf4, 0x00);
    lcr3(CR3_ENTRY_PRESERVE((proc->pcid%NPCIDS + 1), v2p(pml4)));

    
  }
#else
  lcr3(v2p(pml4));
#endif
  popcli();
}

