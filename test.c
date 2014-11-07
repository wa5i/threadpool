#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "threadpool.h"

struct func_arg {
    int data;
};

void func(void *data)
{
    struct func_arg *d = (struct func_arg *)data;

    printf("data: %d\n", d->data);
    sleep(1);
}

int main(int argc, char *argv[])
{
    threadpool_t *tp;
    struct func_arg *d;

    tp = threadpool_create(2, 4);
    if (tp == NULL) {
        return -1;
    }

    d = (struct func_arg *)calloc(5, sizeof(struct func_arg));
    if (d == NULL) {
        return -1;
    }

    d[0].data = 11;
    d[1].data = 22;
    d[2].data = 33;
    d[3].data = 44;
    d[4].data = 55;

    threadpool_add_task(tp, func, d);
    threadpool_add_task(tp, func, d+1);
    threadpool_add_task(tp, func, d+2);
    threadpool_add_task(tp, func, d+3);
    threadpool_add_task(tp, func, d+4);

    printf("all task has been done: %d\n", threadpool_all_done(tp));

    sleep(2);
    printf("all task has been done: %d\n", threadpool_all_done(tp));

    threadpool_destroy(tp, 0);
    //threadpool_destroy(tp, 1);

    free(d);

    return 0;
}
