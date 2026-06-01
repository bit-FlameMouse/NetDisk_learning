#pragma once

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "queue/task_queue.h"

typedef struct thread_pool thread_pool_t;

thread_pool_t *thread_pool_create(int num_threads);

int thread_pool_submit(thread_pool_t *pool, task_t task);

int thread_pool_drain(thread_pool_t *pool);

int thread_pool_destroy(thread_pool_t *pool);


#endif
