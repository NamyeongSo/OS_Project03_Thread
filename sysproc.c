#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;
  struct proc* p;
  int found =  0;
  acquire(&ptable.lock);
  if(argint(0, &pid) < 0)
    return -1;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){//thread가 kill되는 조건 -> 다른 thread다 kill해줘야함.->threads[]를 이용하자.
      found = 1;
      break;
    }  
  }
  if (found) {
    // 대상 프로세스와 공유 데이터를 가진 모든 스레드를 종료합니다.
    for (struct proc* q = ptable.proc; q < &ptable.proc[NPROC]; q++) {
      if (q->sharePtr == p->sharePtr) {  // 같은 sharedPtr를 확인
        q->killed = 1;  // 스레드를 종료 상태로 설정
        if (q->state == SLEEPING)  // 대기 상태인 경우 실행 가능하도록 변경
          q->state = RUNNABLE;
      }
    }
  }
  release(&ptable.lock);
  // return kill(pid);
  return found ? 0 : -1;
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{ 
  int addr;
  int n;

  // acquire(&ptable.lock);
  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sharePtr->sz;
  if(growproc(n) < 0)
    return -1;
  // release(&ptable.lock);

  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
