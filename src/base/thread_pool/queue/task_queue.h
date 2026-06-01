#pragma once
#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H
#include <stdbool.h>
#include <stddef.h>

#define TASK_QUEUE_MAX_CAPACITY 4096  // 任务队列最大容量

/** 任务结构：函数指针 + 参数 */
typedef struct task {
    void (*func)(void *arg);
    void  *arg;
} task_t;

typedef task_t datatype;

typedef struct task_queue task_queue_t;

// 创建任务队列
task_queue_t *task_queue_create(size_t capacity);

// 销毁任务队列
void task_queue_destroy(task_queue_t *queue);

// 入队
int task_queue_enqueue(task_queue_t *queue, datatype data);

// 出队
datatype task_queue_dequeue(task_queue_t *queue);

// 查询函数
bool task_queue_is_empty(task_queue_t *queue);   // 队列是否为空
bool task_queue_is_full(task_queue_t *queue);    // 队列是否已满
size_t task_queue_count(task_queue_t *queue);    // 队列中当前元素个数
size_t task_queue_capacity(task_queue_t *queue); // 队列总容量

#endif
