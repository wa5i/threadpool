/*
 * Copyright (c) 2014, Jusse Wang <wanyco@gmail.com>.
 * All rights reserved.
 *
 * @date: 2014-11-06 
 * @file threadpool.c
 * @brief Threadpool implementation file
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "mini-clist.h"
#include "threadpool.h"

#define TP_LIST_INIT(h) ({ (h)->count = 0; LIST_INIT(&(h)->head); })
#define TP_LIST_ADD(h, e) ({ (h)->count++; LIST_ADDQ(&(h)->head, e); })
#define TP_LIST_DEL(h, e) ({ (h)->count--; LIST_DEL(e); })
#define TP_LIST_ISEMPTY(h) ((h)->count == 0)

#define TP_LIST_ENQUEUE(h, e) TP_LIST_ADD(h, e)
#define TP_LIST_DEQUEUE(h, pt) ({ struct list *__e1 = (!TP_LIST_ISEMPTY(h)) ? TP_LIST_DEL(h, (h)->head.n) : NULL; (__e1 ? LIST_ELEM(__e1, pt, entry) : NULL); }) 

#define TP_TASK_ENQUEUE(h, e) TP_LIST_ENQUEUE(h, &((e)->entry))
#define TP_TASK_DEQUEUE(h) TP_LIST_DEQUEUE(h, threadpool_task_t *)

#define TP_THREAD_ENQUEUE TP_TASK_ENQUEUE
#define TP_THREAD_DEQUEUE(h) TP_LIST_DEQUEUE(h, threadpool_pthread_t *)

#define TP_QUEUE_COUNT(h) ((h)->count)

#define tp_queue_foreach(item, qhead) list_for_each_entry(item, &(qhead)->head, entry)
#define tp_queue_foreach_safe(item, item_tmp, qhead) list_for_each_entry_safe(item, item_tmp, &(qhead)->head, entry)

#define __SHUTDOWN_DEFAULT 0xEE

typedef struct tp_queue {
    unsigned int count;
    struct list head;
} tp_queue_t;

typedef struct threadpool_pthread {
    struct list entry;
    pthread_t thread;
} threadpool_pthread_t;

typedef struct threadpool_task {
    struct list entry;
    void (*function)(void *);
    void *argument;
    int flag;
} threadpool_task_t;

struct threadpool {
    pthread_mutex_t lock;
    pthread_cond_t wait;

    tp_queue_t thread_queue;
    tp_queue_t waiting_task_queue;
    tp_queue_t busying_task_queue;
    tp_queue_t idle_task_queue;

    int max_thread_count;
    int shutdown;
};

static void *threadpool_main_thread(void *threadpool);

int threadpool_free(threadpool_t *tp)
{
    threadpool_pthread_t *thread, *thread_tmp;
    threadpool_task_t *task, *task_tmp;

    if (tp == NULL) {
        return THREADPOOL_INVALID;
    }

    tp_queue_foreach_safe(thread, thread_tmp, &tp->thread_queue) {
        LIST_DEL(&thread->entry);
        free(thread);
    }

    tp_queue_foreach_safe(task, task_tmp, &tp->idle_task_queue) {
        LIST_DEL(&task->entry);
        free(task);
    }

    tp_queue_foreach_safe(task, task_tmp, &tp->waiting_task_queue) {
        LIST_DEL(&task->entry);
        free(task);
    }

    tp_queue_foreach_safe(task, task_tmp, &tp->busying_task_queue) {
        LIST_DEL(&task->entry);
        free(task);
    }

    pthread_mutex_lock(&tp->lock);
    pthread_mutex_destroy(&tp->lock);
    pthread_cond_destroy(&tp->wait);
    
    free(tp);

    return THREADPOOL_OK;
}

int threadpool_destroy(threadpool_t *tp, int shutdown)
{
    int err = THREADPOOL_OK;
    threadpool_pthread_t *thread;
    
    if (tp == NULL) {
        return THREADPOOL_INVALID;
    }

    if (pthread_mutex_lock(&tp->lock) != 0) {
        return THREADPOOL_LOCK_FAILURE;
    }

    do {
        /* Already shutting down */
        if (tp->shutdown != __SHUTDOWN_DEFAULT) {
            err = THREADPOOL_SHUTDOWN;
            break;
        }

        tp->shutdown = (shutdown & 0x1);

        /* Wake up all worker threads */
        if (pthread_cond_broadcast(&tp->wait) != 0 || pthread_mutex_unlock(&tp->lock) != 0) {
            err = THREADPOOL_LOCK_FAILURE;
            break;
        }
        
        /* Wait all worker thread exit */
        tp_queue_foreach(thread, &tp->thread_queue) {
            if (pthread_join(thread->thread, NULL) != 0) {
                err = THREADPOOL_LOCK_FAILURE;
            }
        }
    } while (0);

    if (pthread_mutex_unlock(&tp->lock) != 0) {
        err = THREADPOOL_LOCK_FAILURE;
    }

    /* Only if everything went well do we deallocate the pool */
    if (err == THREADPOOL_OK) {
        threadpool_free(tp);
    }

    return err;
}

static int threadpool_new_thread(threadpool_t *tp)
{
    threadpool_pthread_t *thread;
    int err = THREADPOOL_FAILURE;

    thread = (threadpool_pthread_t *)calloc(1, sizeof(threadpool_pthread_t));
    if (thread == NULL) {
        err = THREADPOOL_MEM_ERROR;
        goto ERR;
    }

    if (pthread_create(&thread->thread, NULL, threadpool_main_thread, (void *)tp) != 0) {
        goto ERR;
    }

    TP_THREAD_ENQUEUE(&tp->thread_queue, thread);

    err = THREADPOOL_OK;

ERR:
    if (err != THREADPOOL_OK && thread != NULL) {
        free(thread);
    }

    return err;
}

static int threadpool_new_task(threadpool_t *tp)
{
    threadpool_task_t *task;

    task = (threadpool_task_t *)calloc(1, sizeof(threadpool_task_t)) ;
    if (task == NULL) {
        return THREADPOOL_MEM_ERROR;
    }

    TP_TASK_ENQUEUE(&tp->idle_task_queue, task);

    return THREADPOOL_OK;
}

threadpool_t *threadpool_create(int count, int max_count)
{
    threadpool_t *tp;
    int i;

    tp = (threadpool_t *)calloc(1, sizeof(threadpool_t));
    if (tp == NULL) {
        goto ERR;
    }

    TP_LIST_INIT(&tp->thread_queue);
    TP_LIST_INIT(&tp->busying_task_queue);
    TP_LIST_INIT(&tp->waiting_task_queue);
    TP_LIST_INIT(&tp->idle_task_queue);

    for (i = 0; i < count; i++) {
        if (threadpool_new_thread(tp) != THREADPOOL_OK || threadpool_new_task(tp) != THREADPOOL_OK) {
            goto ERR;
        }
    }

    if (pthread_mutex_init(&tp->lock, NULL) != 0 || pthread_cond_init(&tp->wait, NULL) != 0) {
        goto ERR;
    }

    tp->max_thread_count = max_count;
    tp->shutdown = __SHUTDOWN_DEFAULT;

    return tp;

ERR:
    if (tp != NULL) {
        threadpool_free(tp);
    }

    return NULL;
}

int threadpool_add_task(threadpool_t *tp, void (*function)(void *), void *argument)
{
    int err = 0;
    threadpool_task_t *task;
    
    if (tp == NULL || function == NULL) {
        return err;
    }

    if (pthread_mutex_lock(&tp->lock) != 0) {
        return THREADPOOL_LOCK_FAILURE;
    }

    if (tp->thread_queue.count > tp->max_thread_count) {
        err = THREADPOOL_THREAD_MAX;
        goto ERR;
    }

    if (tp->idle_task_queue.count == 0 && threadpool_new_task(tp) != THREADPOOL_OK) {
        err = THREADPOOL_MEM_ERROR;
        goto ERR;
    }

    task = TP_TASK_DEQUEUE(&tp->idle_task_queue);
    TP_TASK_ENQUEUE(&tp->waiting_task_queue, task);

    task->function = function;
    task->argument = argument;

    if ((TP_QUEUE_COUNT(&tp->waiting_task_queue) + TP_QUEUE_COUNT(&tp->busying_task_queue)) >= TP_QUEUE_COUNT(&tp->thread_queue) &&
                threadpool_new_thread(tp) != THREADPOOL_OK) {
        err = THREADPOOL_MEM_ERROR; 
        goto ERR;
    }

    if (pthread_cond_signal(&tp->wait) != 0) {
        err = THREADPOOL_LOCK_FAILURE;
        goto ERR;
    }

ERR:
    if (pthread_mutex_unlock(&tp->lock) != 0) {
        err = THREADPOOL_LOCK_FAILURE;
    }
    return err;
}

static void *threadpool_main_thread(void *threadpool)
{
    threadpool_t *tp = (threadpool_t *)threadpool;
    threadpool_task_t *task;

    while (1) {
        pthread_mutex_lock(&tp->lock);

        if (tp->shutdown == SHUTDOWN_IMMEDIATE) {
            break;
        }

        while (TP_QUEUE_COUNT(&tp->waiting_task_queue) == 0) {
            if (tp->shutdown != __SHUTDOWN_DEFAULT) {
                goto EXIT;
            }
            pthread_cond_wait(&tp->wait, &tp->lock);
        }

        if (tp->shutdown == SHUTDOWN_IMMEDIATE) {
            break;
        }

        task = TP_TASK_DEQUEUE(&tp->waiting_task_queue);
        if (task == NULL) {
            pthread_mutex_unlock(&tp->lock);
            continue;
        }

        TP_TASK_ENQUEUE(&tp->busying_task_queue, task);

        pthread_mutex_unlock(&tp->lock);

        (*(task->function))(task->argument);

        pthread_mutex_lock(&tp->lock);

        TP_LIST_DEL(&tp->busying_task_queue, &task->entry);
        TP_TASK_ENQUEUE(&tp->idle_task_queue, task);

        pthread_mutex_unlock(&tp->lock);
    }

EXIT:

    pthread_mutex_unlock(&tp->lock);
    pthread_exit(NULL);

    return NULL;
}
