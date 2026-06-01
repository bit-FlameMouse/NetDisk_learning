#include "thread_pool.h"
#include "queue/task_queue.h"
#include "../log/log.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct thread_pool {
    int           num_threads;   /* 线程池中线程的数量 */
    task_queue_t *task_queue;    /* 任务队列 */
    pthread_t    *threads;       /* 线程数组 */
    pthread_mutex_t lock;        /* 互斥锁 */
    pthread_cond_t  cond;        /* 条件变量：通知 worker 有新任务 */
    int           running;       /* 1=运行中, 0=停止 */
};

/* Worker 线程主循环 */
static void *thread_pool_work(void *arg)
{
    thread_pool_t *pool = (thread_pool_t *)arg;

    while (1) {
        pthread_mutex_lock(&pool->lock);

        /* 等待任务或停止信号 */
        while (pool->running && task_queue_is_empty(pool->task_queue)) {
            pthread_cond_wait(&pool->cond, &pool->lock);
        }

        if (!pool->running && task_queue_is_empty(pool->task_queue)) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        task_t task = task_queue_dequeue(pool->task_queue);
        pthread_mutex_unlock(&pool->lock);

        /* 执行任务 */
        if (task.func) {
            task.func(task.arg);
        }
    }

    return NULL;
}

thread_pool_t *thread_pool_create(int num_threads)
{
    if (num_threads <= 0) num_threads = 4;

    thread_pool_t *pool = (thread_pool_t *)calloc(1, sizeof(thread_pool_t));
    if (!pool) return NULL;

    pool->task_queue = task_queue_create(TASK_QUEUE_MAX_CAPACITY);
    if (!pool->task_queue) {
        free(pool);
        return NULL;
    }

    pool->threads = (pthread_t *)calloc((size_t)num_threads, sizeof(pthread_t));
    if (!pool->threads) {
        task_queue_destroy(pool->task_queue);
        free(pool);
        return NULL;
    }

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);
    pool->num_threads = num_threads;
    pool->running     = 1;

    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, thread_pool_work, pool) != 0) {
            log_error("thread_pool: failed to create worker %d", i);
            thread_pool_destroy(pool);
            return NULL;
        }
    }

    log_info("Thread pool: %d workers started", num_threads);
    return pool;
}

int thread_pool_submit(thread_pool_t *pool, task_t task)
{
    if (!pool || !task.func) return -1;

    pthread_mutex_lock(&pool->lock);

    if (!pool->running) {
        pthread_mutex_unlock(&pool->lock);
        return -1;
    }

    /* 队列满时阻塞等待 */
    while (task_queue_is_full(pool->task_queue) && pool->running) {
        pthread_mutex_unlock(&pool->lock);
        usleep(1000);  /* 1ms 后重试 */
        pthread_mutex_lock(&pool->lock);
    }

    if (!pool->running) {
        pthread_mutex_unlock(&pool->lock);
        return -1;
    }

    int ret = task_queue_enqueue(pool->task_queue, task);
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);

    return ret;
}

int thread_pool_drain(thread_pool_t *pool)
{
    if (!pool) return -1;

    /* 等待队列清空 */
    while (1) {
        pthread_mutex_lock(&pool->lock);
        int empty = task_queue_is_empty(pool->task_queue);
        pthread_mutex_unlock(&pool->lock);
        if (empty) break;
        usleep(10000);  /* 10ms */
    }
    return 0;
}

int thread_pool_destroy(thread_pool_t *pool)
{
    if (!pool) return -1;

    /* 通知所有 worker 退出 */
    pthread_mutex_lock(&pool->lock);
    pool->running = 0;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->lock);

    /* 等待所有 worker 结束 */
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);
    task_queue_destroy(pool->task_queue);
    free(pool->threads);
    free(pool);

    log_info("Thread pool destroyed");
    return 0;
}
