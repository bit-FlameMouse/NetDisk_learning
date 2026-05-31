#include "thread_pool_work.h"
#include"thread_pool.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include"base/queue/queue.h"

void *thread_pool_work(void *arg) {
  // TODO: 线程工作函数
  thread_pool_t *pool = (thread_pool_t *)arg;  //线程池

  return NULL;
}
