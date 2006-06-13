#include "types.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"

extern char edata[], end[];

main()
{
  struct proc *p;
  
  // clear BSS
  memset(edata, 0, end - edata);

  // partially initizialize PIC
  outb(0x20+1, 0xFF); // IO_PIC1
  outb(0xA0+1, 0xFF); // IO_PIC2
  outb(0x20, 0x11);
  outb(0x20+1, 32);

  cprintf("\nxV6\n\n");

  kinit(); // physical memory allocator
  tinit(); // traps and interrupts

  // create fake process zero
  p = &proc[0];
  p->state = WAITING;
  p->sz = PAGE;
  p->mem = kalloc(p->sz);
  memset(p->mem, 0, p->sz);
  p->kstack = kalloc(KSTACKSIZE);
  p->tf = (struct Trapframe *) (p->kstack + KSTACKSIZE - sizeof(struct Trapframe));
  memset(p->tf, 0, sizeof(struct Trapframe));
  p->tf->tf_es = p->tf->tf_ds = p->tf->tf_ss = (SEG_UDATA << 3) | 3;
  p->tf->tf_cs = (SEG_UCODE << 3) | 3;
  p->tf->tf_eflags = FL_IF;
  setupsegs(p);

  p = newproc(&proc[0]);
  // xxx copy instructions to p->mem
  p->mem[0] = 0x90; // nop 
  p->mem[1] = 0x90; // nop 
  p->mem[2] = 0x42; // inc %edx
  p->mem[3] = 0x42; // inc %edx
  p->tf->tf_eip = 0;
  p->tf->tf_esp = p->sz;

  swtch(&proc[0]);
}