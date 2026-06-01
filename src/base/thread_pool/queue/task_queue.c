#include "task_queue.h"
#include <stddef.h>
#include <stdlib.h>

typedef struct task_queue_node {
  task_t task;
  struct task_queue_node *next;
} task_queue_node_t;

struct task_queue {
  task_queue_node_t *front;
  task_queue_node_t *rear;
  size_t num;
  size_t capacity;
};

task_queue_t *task_queue_create(size_t capacity) {
  task_queue_t *q = (task_queue_t *)malloc(sizeof(task_queue_t));
  if (!q)
    return NULL;
  q->front = q->rear = NULL;
  q->num = 0;
  q->capacity = capacity;
  return q;
}

void task_queue_destroy(task_queue_t *queue) {
  task_queue_node_t *node = queue->front;
  while (node) {
    task_queue_node_t *next = node->next;
    free(node);
    node = next;
  }
  free(queue);
}

int task_queue_enqueue(task_queue_t *queue, task_t task) {
  if (task_queue_is_full(queue))
    return -1;
  task_queue_node_t *node =
      (task_queue_node_t *)malloc(sizeof(task_queue_node_t));
  if (!node)
    return -1;
  node->task = task;
  node->next = NULL;
  if (task_queue_is_empty(queue))
    queue->front = node;
  else
    queue->rear->next = node;
  queue->rear = node;
  queue->num++;
  return 0;
}

task_t task_queue_dequeue(task_queue_t *queue) {
  if (task_queue_is_empty(queue))
    return (task_t){NULL, NULL};
  task_queue_node_t *node = queue->front;
  task_t task = node->task;
  queue->front = node->next;
  if (queue->front == NULL)
    queue->rear = NULL;
  free(node);
  queue->num--;
  return task;
}

size_t task_queue_capacity(task_queue_t *queue) { return queue->capacity; }

size_t task_queue_count(task_queue_t *queue) { return queue->num; }

bool task_queue_is_empty(task_queue_t *queue) { return queue->num == 0; }
bool task_queue_is_full(task_queue_t *queue) {
  return queue->num == queue->capacity;
}
