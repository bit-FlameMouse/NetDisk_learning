#include "queue.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct queue_node {
  void *data;
  struct queue_node *next;
} queue_node_t;

struct queue {
  queue_node_t *front; /* 队头（出队端） */
  queue_node_t *rear;  /* 队尾（入队端） */
  size_t num;          /* 当前元素个数  */
};

queue_t *queue_create(void) {
  queue_t *q = (queue_t *)malloc(sizeof(queue_t));
  if (!q)
    return NULL;

  q->front = q->rear = NULL;
  q->num = 0;

  return q;
}
