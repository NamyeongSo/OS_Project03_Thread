#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "elf.h"

extern char data[];  // defined by the kernel linker script in kernel.ld
pde_t *kpgdir;       // for use in scheduler()

// Function to free resources of a process (thread)
void free_proc(struct proc *curproc) {
  // Free the kernel stack
  if(curproc->kstack)
    kfree(curproc->kstack);
  curproc->kstack = 0;
  // Free the process memory
  if(curproc->sharePtr == 0) {
    freevm(curproc->sharePtr->pgdir);
  }
  curproc->pid = 0;
  curproc->parent = 0;
  curproc->tf = 0;
  curproc->context = 0;
  curproc->chan = 0;
  curproc->killed = 0;
  curproc->state = UNUSED;
  memset(curproc->name, 0, sizeof(curproc->name));
  curproc->sharePtr = 0;
  curproc->orderOfThread = 0;
  curproc->retval = 0;
}

// Kill all threads except the current one and free resources
void kill_other_threads(struct proc *curproc) {
  int i;
  acquire(&ptable.lock);
  for(i = 0; i < 6; i++) {
    if(curproc->sharePtr->isThere[i] && curproc->sharePtr->threads[i] != curproc) {
      struct proc *p = curproc->sharePtr->threads[i];
      p->killed = 1;
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      free_proc(p);
    }
  }
  release(&ptable.lock);
}

// Load the new program
int exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct proghdr ph;
  struct inode *ip;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

  kill_other_threads(curproc);

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->sharePtr->pgdir;
  curproc->sharePtr->pgdir = pgdir;
  curproc->sharePtr->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);
  
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
