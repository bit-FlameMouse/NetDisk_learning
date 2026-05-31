#pragma once

#ifndef THREAD_POOL_H
#define THREAD_POOL_H


typedef struct thread_pool thread_pool_t;

thread_pool_t *thread_pool_create(int num_threads);

void thread_pool_destroy(thread_pool_t *pool);
#endif
