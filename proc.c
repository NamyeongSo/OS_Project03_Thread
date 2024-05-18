#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include <stddef.h>

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

ptableStruct ptable;
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

// void free_proc(struct proc *p) {
//     acquire(&ptable.lock);

//     // Debugging output
//     cprintf("free_proc: Freeing process %d\n", p->pid);

//     if(p->kstack) {
//         kfree(p->kstack);
//         p->kstack = 0;
//     }

//     // Free user memory for the process if it is the last thread
//     if (p->sharePtr->numOfThread == 1) {
//         freevm(p->sharePtr->pgdir);
//     }

//     // Reset process structure
//     p->pid = 0;
//     p->parent = 0;
//     p->name[0] = 0;
//     p->killed = 0;
//     p->state = UNUSED;
//     p->sharePtr->numOfThread--;
    
//     release(&ptable.lock);
// }


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
// void procToThread(struct proc* p, struct thread* t){
//   t->sharePtr->sz = p->sz;
//   *t->sharePtr->pgdir = *p->pgdir;
//   *t->sharePtr->ofile = *p->ofile;
//   *t->sharePtr->cwd = *p->cwd;
//   *t->kstack = p->*kstack; 
//   t->state = p->state;   
//   t->pid = p->pid;     
//   *t->parent = *p->parent; 
//   *t->tf = *p->tf;  
//   *t->context = *p->context;
//   *t->chan = *p->chan;   
//   t->killed = p->killed;  
//   t->name[16] = p->name[16];
// }

//그냥 proc구조체가 존재하는 모든 곳을 수정해버려??
// struct thread*
// mythread(void) {
//   struct cpu *c;
//   struct proc *p;
//   struct thread *t;
//   pushcli();
//   c = mycpu();
//   p = c->proc;
//   procToThread(p,t);
//   popcli();
//   return p;
// }

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc* allocproc(void) {
    struct proc *p;
    char *sp;

    acquire(&ptable.lock);

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->state == UNUSED)
            goto found;
    }

    release(&ptable.lock);
    return 0;

found:
    p->state = EMBRYO;
    p->pid = nextpid++;

    release(&ptable.lock);

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
// Set up first user process. 여기서 최초 프로세스의 sharePtr초기화 할 거임.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  char* mem = kalloc();  // 메모리 할당
  memset(mem, 0, sizeof(struct sharedData));
  p->sharePtr = (struct sharedData*)mem;  // 타입 캐스팅하여 반환
  

  initproc = p;
  if((p->sharePtr->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->sharePtr->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sharePtr->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->sharePtr->cwd = namei("/");
  p->sharePtr->numOfThread = 1;
  p->sharePtr->threads[0] = p;
  p->sharePtr->isThere[0] = 1;
  p->orderOfThread = 0;
  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)//atomic하게 실행되어야함 겹치면 메모리 적게 할당될듯
{
  uint sz;
  struct proc *curproc = myproc();
  acquire(&ptable.lock);
  sz = curproc->sharePtr->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->sharePtr->pgdir, sz, sz + n)) == 0)
      return -1;
      
  } else if(n < 0){
    if((sz = deallocuvm(curproc->sharePtr->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sharePtr->sz = sz;
  switchuvm(curproc);
  release(&ptable.lock);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  // int i, pid;
  int pid;
  struct proc *np;
  struct proc *curproc = myproc();
  
  
  // Allocate process.
  if((np = allocproc()) == 0){//빈 프로세스가 없음. ptable에서 찾는 거 같은데...
    cprintf("allocproc Failed!\n");
    return -1;
  }

  // Copy process state from proc.
  //copyuvm: 현재 프로세스의 page와 memorysize를 기반으로 메모리를 카피함. 
  //thread를 구현하기 위해선 이 함수를 조금 손봐야 할 듯
  
  char* mem = kalloc();  // 메모리 할당
  if (mem == 0) {
    cprintf("kalloc Failed!\n");
    np->state = UNUSED;
    return -1;
  }
  memset(mem, 0, sizeof(struct sharedData));
  np->sharePtr = (struct sharedData*)mem;  // 타입 캐스팅하여 반환
  
  if((np->sharePtr->pgdir = copyuvm(curproc->sharePtr->pgdir, curproc->sharePtr->sz)) == 0){
    //카피 실패
    cprintf("copyuvm Failed!\n");
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  
  np->sharePtr->sz = curproc->sharePtr->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child. rax는 return value저장
  np->tf->eax = 0;

  for(int i = 0; i < NOFILE; i++)//nofile == 16
    if(curproc->sharePtr->ofile[i])
      np->sharePtr->ofile[i] = filedup(curproc->sharePtr->ofile[i]);//file에 대한 접근 레퍼런스 수 증가.
  np->sharePtr->cwd = idup(curproc->sharePtr->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);
  

  np->sharePtr->numOfThread = 1;
  int index;
  for(index = 1; index<6;index++){
    np->sharePtr->isThere[index] = 0;
  }
  np->orderOfThread = 0;
  np->sharePtr->isThere[0] = 1;
  np->sharePtr->threads[0] = np;
  np->imMaster = 1;

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void) {
    struct proc *curproc = myproc();
    struct proc *p;
    int fd;

    if(curproc == initproc)
        panic("init exiting");

    // Close all open files.
    for(fd = 0; fd < NOFILE; fd++) {
        if(curproc->sharePtr->ofile[fd]) {
            fileclose(curproc->sharePtr->ofile[fd]);
            curproc->sharePtr->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(curproc->sharePtr->cwd);
    end_op();
    curproc->sharePtr->cwd = 0;

    acquire(&ptable.lock);

    // Wake up the parent process.
    wakeup1(curproc->parent);

    // Pass abandoned children to init.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->parent == curproc) {
            p->parent = initproc;
            if(p->state == ZOMBIE)
                wakeup1(initproc);
        }
    }
    // Terminate all threads in the current process.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if(p->sharePtr == curproc->sharePtr && p != curproc) {
            p->state = ZOMBIE;
        }
    }


    // Set the current process as ZOMBIE.
    curproc->state = ZOMBIE;


    // Print current process table and states for debugging.
 
  struct proc *proc;
  for(proc = ptable.proc; proc < &ptable.proc[NPROC]; proc++) {
      if(proc->state != UNUSED) {
          cprintf("PID: %d, State: %d, Name: %s\n", proc->pid, proc->state, proc->name);
      }
  }
  
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void) {
    struct proc *p;
    int havekids, pid;
    struct proc *curproc = myproc();
  
    acquire(&ptable.lock);
    for(;;) {
        // Scan through table looking for exited children.
        havekids = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            if(p->parent != curproc){
                // cprintf("I dont want to wait!\n");
                continue;
              }
            havekids = 1;
            // cprintf("My baby is:%d\n", p->pid);
            if(p->state == ZOMBIE && p->imMaster == 1) {//exit으로 인한 wait은 프로세서 단으로 처리. 즉 master(tid = 0)이 처리해야함
                pid = p->pid;
                cprintf("p->sharePtr->threads[%d](pid:%d)->state:%d\n", p->orderOfThread, p->pid,p->state);                    
                for(int  i= 0; i<6; i++){
                  if(p->sharePtr->threads[i]->state == ZOMBIE && p->sharePtr->threads[i]->pid != 0){
                    cprintf("p->sharePtr->threads[%d](pid:%d)->state:%d\n", p->sharePtr->threads[i]->orderOfThread, p->sharePtr->threads[i]->pid,p->sharePtr->threads[i]->state);
                    if (p->kstack) {
                        kfree(p->kstack);
                        p->kstack = 0;
                    }
                    p->sharePtr->threads[i]->kstack = 0;
                    if (p->sharePtr->threads[i]->sharePtr->numOfThread == 1) {
                        freevm(p->sharePtr->threads[i]->sharePtr->pgdir);
                    }
                    p->sharePtr->threads[i]->pid = 0;
                    p->sharePtr->threads[i]->parent = 0;
                    p->sharePtr->threads[i]->name[0] = 0;
                    p->sharePtr->threads[i]->killed = 0;
                    p->sharePtr->threads[i]->state = UNUSED;
                    p->sharePtr->numOfThread--;
                  }
                }
                havekids = 0;
                release(&ptable.lock);
                return pid;
            }
        }
        // cprintf("have KIDs:%d", havekids);

        // No point waiting if we don't have any children.
        if(!havekids || curproc->killed) {
            release(&ptable.lock);
            return -1;
        }

        // Wait for children to exit.  (See wakeup1 call in proc_exit.)
        sleep(curproc, &ptable.lock);  // DOC: wait-sleep
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
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

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

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
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
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

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
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)//chan == parent
{
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      // cprintf("chan is:%d, p->chan is:%d\n", chan, p->chan);
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
    }
}


// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid) {
    struct proc *p;
    int killedCount = 0;
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      // cprintf("KILL TABLE SEARCH!!\n");
        if (p->pid == pid) {
            struct proc *killed_proc;
            for (int i = 0; i < 6; i++) {
                if (p->sharePtr->isThere[i]) {
                    killed_proc = p->sharePtr->threads[i];
                    killed_proc->killed = 1;
                    // Debugging output
                    cprintf("Killing process: PID: %d\n", killed_proc->pid);

                    // Wake process from sleep if necessary.
                    if (killed_proc->state == SLEEPING)
                        killed_proc->state = RUNNABLE;

                    killedCount++;
                }
            }
            release(&ptable.lock);
            return 0;
        }
    }
    release(&ptable.lock);
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
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
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


//API정의
// int thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg){
//   //start_routine()하면 실행된다는 인자로 넘어온 함수가 실행된다는 거 같은디.
//   //일단 fork하고 이게 새로 생긴 thread다 -> 연결 로직 실행하고
//   //그 다음에 start routine실행하면 끝?
//   struct proc* np;
//   int pid = fork();
//   //요기부터 shed까지 디버그용
//   for(struct proc* dp = ptable.proc; dp < &ptable.proc[NPROC]; dp++) {
//     if(dp->pid > 0){
//       cprintf("TOTAL Processes:%d AND its State is:%d\n",dp->pid, dp->state);
//     }
//   } 
//   yield();
//   //
//   if(pid < 0){
//     return -1;
//   }
  
//   if(pid == 0){
//     cprintf("is child?\n"); 
//     //자식 thread??
//     np = myproc();
//     int fd;
//     acquire(&ptable.lock);
//     //여기부터는 내 기우임.
//     for(fd = 0; fd < NOFILE; fd++){
//       if(np->sharePtr->ofile[fd]){
//         fileclose(np->sharePtr->ofile[fd]);
//         np->sharePtr->ofile[fd] = 0;
//       }
//     }
//     begin_op();
//     iput(np->sharePtr->cwd);
//     end_op();
//     np->sharePtr->cwd = 0; 
//     //여기까지
//     *thread = np->pid;
//     //start_routine에서 시작해야하는 걸 구현 못하겠음
//     np->tf->eip = (uint)*start_routine; // 함수의 시작 주소로 EIP 설정
//     //수정해야할 수도...esp코드
//     np->tf->esp = (uint)(np->kstack + KSTACKSIZE);  // 스택 포인터 초기화
//     *((uint*)(np->tf->esp)) = (uint)arg; // start_routine 인자 설정
//     release(&ptable.lock);
//     if(np->sharePtr == np->parent->sharePtr) return 0;
//     else return -1;
//   }else{
//     int index;
//     for(np = ptable.proc; np < &ptable.proc[NPROC]; np++) {
//       if(np->pid == pid && np->parent == myproc()) {
//         np->sharePtr = np->parent->sharePtr;
//         for(index =0; index<5 ; index++){
//           if(np->sharePtr->isThere[index] == 0){
//             np->sharePtr->isThere[index] = 1;
//             np->sharePtr->threads[index] = np;
//             np->sharePtr->numOfThread += 1;
//             np->orderOfThread = index;
//             break;
//           }
//         }
//       // cprintf("newProc's index is:%d\n",np->orderOfThread);
//       // cprintf("newProc is there:%d\n",np->sharePtr->isThere[index]);
//       // cprintf("newProc's pid is:%d\n",np->pid);
//       break;
//     }
//   }
//   cprintf("my PID is: %d, and my Parent ID is: %d, RETURN PID IS:%d\n", myproc()->pid, myproc()->parent->pid, pid);
//   return 0;
//   }
// }
int thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg) {
    struct proc *np;
    struct proc *curproc = myproc();
    uint sz, sp, ustack[2];

    // Allocate process.
    // if((int)arg < 4){//0번은 이미 만들어져있다  결국 상대가 원하는 거 -1개만큼 만들어야한다. 
    if ((np = allocproc()) == 0) {
        cprintf("allocproc Failed!\n");
        return -1;
    }

    // Share the same address space.
    acquire(&ptable.lock);
    np->sharePtr = curproc->sharePtr;
    //이게 키였네 -> np->parent도 curp의 parent로 해줌으로써 crop될 수 있도록 했다.
    np->parent = curproc->parent;
    *np->tf = *curproc->tf;

    // Clear %eax so that fork returns 0 in the child.
    np->tf->eax = 0;

    // Allocate user stack for the new thread.
    sz = np->sharePtr->sz;
    if((sz = allocuvm(np->sharePtr->pgdir, sz, sz + 2*PGSIZE)) == 0) {
        np->state = UNUSED;
        release(&ptable.lock);
        return -1;
    }
    clearpteu(np->sharePtr->pgdir, (char*)(sz - 2*PGSIZE));
    np->sharePtr->sz = sz;

    // Set up stack for new thread.
    sp = sz;
    sp -= 8;
    ustack[0] = 0xffffffff;
    ustack[1] = (uint)arg;
    if(copyout(np->sharePtr->pgdir, sp, ustack, 8) < 0) {
        np->state = UNUSED;
        release(&ptable.lock);
        return -1;
    }
    np->stack = sp;
    np->tf->eip = (uint)start_routine;
    np->tf->esp = sp;
    np->imMaster = 0;
    // Update state to RUNNABLE.
    np->state = RUNNABLE;

    // thread배정문 어디감.->내가 자식이면 연결하도록
    for(int index = 0; index < 6; index++) {
      if(np->sharePtr->isThere[index] == 0) {
         np->sharePtr->isThere[index] = 1;
         np->sharePtr->threads[index] = np;
         np->sharePtr->numOfThread += 1;
         np->orderOfThread = index;
         break;
      }
    }
    *thread = np->orderOfThread;
    // for(int i = 0; i<5; i++){
    //   cprintf("%dst setting in threadCreate\n np->sharePtr->isThere[index]:%d \n np->sharePtr->threads[index]:%d \nTID is%d\n",i,np->sharePtr->isThere[i],np->sharePtr->threads[i]->pid,np->sharePtr->threads[i]->orderOfThread);
    // }
    // cprintf("Thread Create!!!!\n");
    // for(struct proc* proc = ptable.proc; proc < &ptable.proc[NPROC]; proc++) {
    //   if(proc->state != UNUSED) {
    //     cprintf("PID: %d, State: %d, Name: %s\n", proc->pid, proc->state, proc->name);
    //   }
    // }
    release(&ptable.lock);

    return 0;
}
void thread_exit(void *retval) {
    struct proc *curproc = myproc();

    acquire(&ptable.lock);


    // Remove the thread from the sharedData structure.
    int i;
    for(i = 0; i<6; i++){
      if(curproc->sharePtr->threads[i]->imMaster == 1){
        break;
      }
    }
    wakeup1(curproc->sharePtr->threads[i]);
    // Mark the current thread as ZOMBIE.
    curproc->sharePtr->isThere[curproc->orderOfThread] = 0;
    curproc->state = ZOMBIE;
    curproc->retval = retval;

    // Wake up the parent process if it is waiting.

    sched();
    panic("zombie exit");
}
int thread_join(thread_t thread, void **retval) {
    struct proc *curproc = myproc();
    struct proc *p;

    if (curproc->imMaster != 1) {
        // cprintf("Current process %d is not a master process. Cannot join a thread.\n", curproc->pid);
        return -1;
    }
    if(curproc)
    acquire(&ptable.lock);
    // cprintf("ptable.lock acquired by thread_join\n");

    for (;;) {
        int thread_found = 0;

        // find expected thread
        for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
            // cprintf("Checking thread: pid=%d, state=%d, name=%s\n", p->pid, p->state, p->name);
            if (p->orderOfThread == thread && p->state == ZOMBIE && p->parent == curproc->parent&& p->imMaster != 1) {
                thread_found = 1;
                // cprintf("Found thread %d in ZOMBIE state\n", thread);
                if (retval != 0) {
                    *retval = p->retval;
                }
                p->retval = 0;

                // Deallocate resources
                // cprintf("Deallocating resources for thread %d\n", thread);
                kfree(p->kstack);
                p->kstack = 0;
                p->state = UNUSED;
                p->pid = 0;
                p->parent = 0;
                p->killed = 0;
                p->name[0] = 0;
                int numOfThread = curproc->sharePtr->numOfThread;
                p->imMaster = 0;    
                p->orderOfThread = 0;
                curproc->sharePtr->numOfThread--;
                p->sharePtr = 0;
                p->stack = 0;
                release(&ptable.lock);
                // cprintf("ptable.lock released after joining thread %d, numOfThread:%d\n", thread, numOfThread);
                
                for(struct proc* proc = ptable.proc; proc < &ptable.proc[NPROC]; proc++) {
                    if(proc->state != UNUSED) {
                        // cprintf("PID: %d, PPID: %d ,State: %d, Name: %s\n", proc->pid, proc->parent->pid,  proc->state, proc->name);
                    }
                }
                
                return 0;
            }
        }

        if (!thread_found) {
            // cprintf("Thread %d not found, sleeping...\n", thread);
            sleep(curproc, &ptable.lock);
            // cprintf("Woke up from sleep, retrying to find thread %d\n", thread);
        }

        // master process is terminated
        if (curproc->killed) {
            // cprintf("Current process %d is killed\n", curproc->pid);
            release(&ptable.lock);
            return -1;
        }
    }

    release(&ptable.lock);
    return -1;
}
