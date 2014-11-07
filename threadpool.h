/*
 * Copyright (c) 2014, Jusse Wang <wanyco@gmail.com>.
 * All rights reserved.
 *
 * @date: 2014-11-06 
 * @file threadpool.h
 * @brief Threadpool Header File
 *
 */

#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

typedef enum {
    THREADPOOL_OK               = 0,
    THREADPOOL_FAILURE          = -1,
    THREADPOOL_INVALID          = -2,
    THREADPOOL_LOCK_FAILURE     = -3,
    THREADPOOL_THREAD_MAX       = -4,
    THREADPOOL_SHUTDOWN         = -5,
    THREADPOOL_THREAD_FAILURE   = -6,
    THREADPOOL_MEM_ERROR        = -7
} threadpool_error_t;

typedef enum {
    SHUTDOWN_GRACEFUL   = 0,
    SHUTDOWN_IMMEDIATE  = 1,
} threadpool_shutdown_t;

typedef struct threadpool threadpool_t;


/**
 * @function threadpool_create
 * @brief Creates a threadpool_t object.
 * @param count         Number of worker threads.
 * @param max_count     Max number of worker threads
 * @return a newly created thread pool or NULL
 */
threadpool_t *threadpool_create(int count, int max_count);

/**
 * @function threadpool_add_task
 * @brief add a new task in the queue of a thread pool
 * @param tp        Thread pool to which add the task.
 * @param function  Pointer to the function that will perform the task.
 * @param argument  Argument to be passed to the function.
 * @return 0 if all goes well, negative values in case of error (@see
 * threadpool_error_t for codes).
 */
int threadpool_add_task(threadpool_t *tp, void (*function)(void *), void *argument);

/**
 * @function threadpool_destroy
 * @brief Stops and destroys a thread pool.
 * @param tp        Thread pool to destroy.
 * @param shutdown  Shutdown or not
 *
 * SHUTDOWN_GRACEFUL(0): in which case the thread pool doesn't accept any new tasks but
 * processes all pending tasks before shutdown.
 * SHUTDOWN_IMMEDIATE(1): shutdown immediately.
 */
int threadpool_destroy(threadpool_t *tp, int shutdown);

/**
 * @function threadpool_all_done
 * @brief all task whether has been done
 * @param tp    Thread pool to judge
 * @retrun 1 if all task has been done, otherwise return 0 
 */
int threadpool_all_done(threadpool_t *tp);

#endif
