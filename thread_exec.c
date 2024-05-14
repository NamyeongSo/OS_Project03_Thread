#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 5

void *thread_main(void *arg)//void 포인터는 다른 포인터로 자동 변환됨.
{
  int val = (int)arg;
  printf(1, "Thread %d start\n", val);
  if (arg == 0) {//0번 thread
    sleep(100);
    char *pname = "/hello_thread";
    char *args[2] = {pname, 0};
    printf(1, "Executing...\n");
    exec(pname, args);//hello_thread실행(thread?) args의 값이 thread 집합을 의미하나?
  }
  else {//1~4번 thread.
    sleep(200);
  }
  
  printf(1, "This code shouldn't be executed!!\n");
  exit();
  return 0;
}

thread_t thread[NUM_THREAD];

int main(int argc, char *argv[])
{
  int i;
  printf(1, "Thread exec test start\n");
  for (i = 0; i < NUM_THREAD; i++) {
    thread_create(&thread[i], thread_main, (void *)i);
  }
  sleep(200);
  printf(1, "This code shouldn't be executed!!\n");
  exit();
}
