#include "thread_pool.h"
#include "base/queue/queue.h"  
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "thread_pool_work.h"

struct thread_pool {
  int num_threads;      // 线程池中线程的数量
  queue_t *task_queue;  // 任务队列
  pthread_t *threads;   // 线程池队列
  pthread_cond_t cond;  // 线程条件变量
  pthread_mutex_t lock; // 线程锁
  int flag;             // 标志位，用于控制线程池的运行状态，当flag=1时，线程池退出
};

thread_pool_t *thread_pool_create(int num_threads) {
  thread_pool_t *pool = (thread_pool_t *)malloc(sizeof(thread_pool_t));
  if (!pool)
    return NULL;

  pool->num_threads = num_threads;
  pool->task_queue = queue_create();
  if (!pool->task_queue) {
    free(pool);
    return NULL;
  }

  pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * num_threads);
  if (!pool->threads) {
    queue_destroy(pool->task_queue);
    free(pool);
    return NULL;
  }

  pthread_mutex_init(&pool->lock, NULL);
  pthread_cond_init(&pool->cond, NULL);

  for (int i = 0; i < num_threads; i++) {
    pthread_create(&pool->threads[i], NULL, thread_pool_work, pool);
    if (pthread_create(&pool->threads[i], NULL, thread_pool_work, pool) != 0) {
      perror("pthread_create");
      thread_pool_destroy(pool);
      return NULL;
    }
  }

  return pool;
}
