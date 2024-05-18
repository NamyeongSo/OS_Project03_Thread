#ifndef PROC_H
#define PROC_H

#include "spinlock.h"

//여러 syscall에 lock을 걸어주기 위함.

// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
//   struct proc {
//   uint sz;                     // Size of process memory (bytes)
//   pde_t* pgdir;                // Page table
//   char *kstack;                // Bottom of kernel stack for this process
//   enum procstate state;        // Process state
//   int pid;                     // Process ID
//   struct proc *parent;         // Parent process
//   struct trapframe *tf;        // Trap frame for current syscall
//   struct context *context;     // swtch() here to run process
//   void *chan;                  // If non-zero, sleeping on chan
//   int killed;                  // If non-zero, have been killed
//   struct file *ofile[NOFILE];  // Open files
//   struct inode *cwd;           // Current directory
//   char name[16];               // Process name (debugging)
// };

//이거에 접근하면 동기화 처리 해줘야함. tgid에 대해 구현해줄 필요가 있을 수도...
struct sharedData {
  uint sz;                     // Size of process memory (bytes)->공유가능
  pde_t* pgdir;                // Page table->공유가능
  struct file *ofile[NOFILE];  // Open files->공유가능
  struct inode *cwd;           // Current directory->공유가능
  int numOfThread;               // shared Data와 연결된 thread가 몇 개인지 판단.
  struct proc* threads[6];     // thread의 max 개수가 몇개일까나
  int isThere[6];              //threads의 특정 위치에 존재하는겨 아닌겨.
};

struct proc {
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  char name[16];               // Process name (debugging)
  struct sharedData* sharePtr; // 같은 프로세스의 thread는 같은 공유데이터를 가리켜야함
  int orderOfThread;             // 필요할 지는 모르겠음.
  void* retval;
  int imMaster;
};

void free_proc(struct proc *curproc);

typedef struct {
    struct spinlock lock;
    struct proc proc[NPROC];
} ptableStruct;


extern ptableStruct ptable;
int thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg);
void thread_exit(void *retval);
int thread_join(thread_t thread, void **retval);



// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

#endif
