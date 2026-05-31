#pragma once

#ifndef QUEUE_H
#define QUEUE_H

#include <stddef.h>
#include <stdbool.h>

typedef int datatype;// 数据类型: int，也就是客户端的文件描述符，线程池中的线程会处理客户端请求
typedef struct queue queue_t;

/// 创建队列
///@brief void
///@return 新队列指针，失败返回 NULL
queue_t *queue_create(void);

/// 销毁队列
///@brief void
///@param q 队列指针
void queue_destroy(queue_t *q);

/// 入队
///@brief void
///@param q 队列
///@param data 数据指针
///@return 0 成功，-1 失败
int queue_enqueue(queue_t *q, void *data);


/// 出队
///@brief void
///@param q 队列
///@param data 输出参数，接收出队的数据指针
///@return 0 成功，QUEUE_CLOSED 表示队列已关闭
int queue_dequeue(queue_t *q, void *data);


/// 获取队列长度
///@brief void
///@param q 队列
///@return 队列长度
size_t queue_size(queue_t *q);

/// 判断队列是否为空
///@brief void
///@param q 队列
///@return true 队列为空，false 队列不为空
bool queue_empty(queue_t *q);

/// 销毁队列
///@brief void
///@param q 队列指针
void queue_destroy(queue_t *q);

#endif
