/*
 * Copyright (c) 20019-2020, wanweiyingchuang
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-11-30     denghengli   the first version
 */

#ifndef __ALGO_ARRAY_QUEUE_H__
#define __ALGO_ARRAY_QUEUE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"

#define ARRAY_QUEUE_MALLOC(size)   pvPortMalloc(size)
#define ARRAY_QUEUE_CALLOC(n,size) pvPortMalloc(n*size)
#define ARRAY_QUEUE_FREE(p)        vPortFree(p)

#define ARRAY_QUEUE_SIZE(pqueue)     (pqueue->size)
#define ARRAY_QUEUE_NUM(pqueue)      (pqueue->num)
#define ARRAY_QUEUE_IS_EMPTY(pqueue) (pqueue->num == 0)
#define ARRAY_QUEUE_IS_FULL(pqueue)  (pqueue->num == pqueue->size)

struct array_queue
{
	int size; /* queue total size */
	int num;  /* queue used size rang:1-(size-1) */
	int head; /* points to the the next dequeue data  */
	int tail; /* points to the the next enqueue data  */
	int tpsz; /* data type size */
	void *p;  /* queue space */
};

extern struct array_queue* array_queue_creat(int size, int tpsz);
extern int array_queue_init   (struct array_queue *queue, int size, int tpsz);
extern int array_queue_empty  (struct array_queue *queue);
extern int array_queue_clear  (struct array_queue *queue);
extern int array_queue_destory(struct array_queue *queue);
extern int array_queue_enqueue(struct array_queue *queue, void *in_data);
extern int array_queue_dequeue(struct array_queue *queue, void *out_data);

#endif
