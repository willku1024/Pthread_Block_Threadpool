#include <unistd.h>
#include <stdio.h>
#include "ThreadPool.hpp"

void *process(void *arg)
{
    int num = *(int *)arg;
    // printf("thread 0x%x working on task %d\n ", (unsigned int)pthread_self(), num);
    printf("callback: print %d\n", num);
    // printf("thread 0x%x finish the task %d\n ", (unsigned int)pthread_self(), num);

    return NULL;
}

int main(void)
{
    // ThreadPool pool(min-thread-num, max-thread-num, queue-size);
    ThreadPool pool(3, 35, 15);

    //int *num = (int *)malloc(sizeof(int)*10);
    int num[20], i;
    for (i = 0; i < 20; i++)
    {
        num[i] = i;
        pool.threadpool_add(process, (void *)&num[i]); /* 向线程池中添加任务 */
    }

    getchar();
    return 0;
}
