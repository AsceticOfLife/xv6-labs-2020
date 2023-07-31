#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;

struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
}

static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
  // 首先获取锁
  pthread_mutex_lock(&bstate.barrier_mutex);
  // 此时需要将到达barrier的线程数加1
  bstate.nthread++;

  // 如果当前到达线程数小于总的线程数就阻塞自己
  if (bstate.nthread != nthread) {
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  } else {
    // 当前达到进程数已经达到总的线程数
    bstate.round++;
    bstate.nthread = 0;
    // 唤醒所有被阻塞的线程
    pthread_cond_broadcast(&bstate.barrier_cond);
  }
  
  // 释放锁
  pthread_mutex_unlock(&bstate.barrier_mutex);
}

static void *
thread(void *xa)
{
  long n = (long) xa; // 当前线程序数
  long delay;
  int i;

  for (i = 0; i < 100; i++) {
    int t = bstate.round; // 当前轮数
    // 如果当前轮数与i不一致，说明round没有等待所有线程完成一次循环就增加
    // 即没有在一个统一位置等待所有进程完成操作，所以程序退出
    assert (i == t);   
    barrier();  // 调用barrier函数，需要等待所有的线程都调用这个函数之后才能继续进行
    usleep(random() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);  // 线程数
  tha = malloc(sizeof(pthread_t) * nthread);  // 线程指针数组
  srandom(0);

  barrier_init();

  // 对于每个线程都执行thread函数
  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}
