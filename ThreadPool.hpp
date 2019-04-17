#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define DEFAULT_TIME 10        
#define MIN_WAIT_TASK_NUM 10   
#define DEFAULT_THREAD_VARY 10 
#define true 1
#define false 0

#include <iostream>
using namespace std;

class ThreadPool
{
protected:
    typedef struct
    {
        void *(*function)(void *); 
        void *arg;                 
    } threadpool_task_t;

    pthread_mutex_t lock;           
    pthread_mutex_t thread_counter; 

    pthread_cond_t queue_not_full;  
    pthread_cond_t queue_not_empty; 

    pthread_t *threads;            
    pthread_t adjust_tid;          
    threadpool_task_t *task_queue; 

    int min_thr_num;       
    int max_thr_num;       
    int live_thr_num;      
    int busy_thr_num;      
    int wait_exit_thr_num; 

    int queue_front;    
    int queue_rear;     
    int queue_size;     
    int queue_max_size; 

    int shutdown; 

protected:
    /**
     * @function threadpool_free
     * @desc Free thread pool resource.
     * @param pool  Thread pool to free.
     * @return 0 if destory success else -1
     */
    int threadpool_free();

    /**
     * @function threadpool_thread
     * @desc Work thread.
     * @param .
     * @return 0 if success else -1
     */
    static void *threadpool_thread(void *arg);

    /**
     * @function adjust_thread
     * @desc Manage and adjust threads number.
     * @param .
     * @return 0 if success else -1
     */
    static void *adjust_thread(void *arg);

public:
    // delegate constructor
    ThreadPool() : ThreadPool(10, 20, 30){};
    ThreadPool(int min_thr_num, int max_thr_num, int queue_max_size);

    /**
     * @desc Stops and destroys a thread pool.
     * @param pool  Thread pool to destroy.
     * @return 0 if destory success else -1
     */
    ~ThreadPool();

    /**
     * @function threadpool_add
     * @desc add a new task in the queue of a thread pool
     * @param pool     Thread pool to which add the task.
     * @param function Pointer to the function that will perform the task.
     * @param argument Argument to be passed to the function.
     * @return 0 if all goes well,else -1
     */
    int threadpool_add(void *(*function)(void *), void *arg);

    /**
     * @desc get the thread num
     * @pool pool threadpool
     * @return # of the thread
     */
    int threadpool_all_threadnum();

    /**
     * desc get the busy thread num
     * @param pool threadpool
     * return # of the busy thread
     */
    int threadpool_busy_threadnum();

    /**
     * @function is_thread_alive
     * @desc Check thread is alive or not.
     * @param tid pthread id.
     * @return 0 if success else -1
     */
    int is_thread_alive(pthread_t tid);
};

ThreadPool::ThreadPool(int min_thr_num, int max_thr_num, int queue_max_size)
{

    auto &&pool = this;
    do
    {
        pool->min_thr_num = min_thr_num;
        pool->max_thr_num = max_thr_num;
        pool->busy_thr_num = 0;
        pool->live_thr_num = min_thr_num; 
        pool->wait_exit_thr_num = 0;
        pool->queue_size = 0; 
        pool->queue_max_size = queue_max_size;
        pool->queue_front = 0;
        pool->queue_rear = 0;
        pool->shutdown = false; 

        
        pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * max_thr_num);
        if (pool->threads == NULL)
        {
            printf("malloc threads fail");
            threadpool_free();
            break;
        }
        memset(pool->threads, 0, sizeof(pthread_t) * max_thr_num);

        
        pool->task_queue = (threadpool_task_t *)malloc(sizeof(threadpool_task_t) * queue_max_size);
        if (pool->task_queue == NULL)
        {
            printf("malloc task_queue fail");
            threadpool_free();
            break;
        }

        
        if (pthread_mutex_init(&(pool->lock), NULL) != 0 || pthread_mutex_init(&(pool->thread_counter), NULL) != 0 || pthread_cond_init(&(pool->queue_not_empty), NULL) != 0 || pthread_cond_init(&(pool->queue_not_full), NULL) != 0)
        {
            printf("init the lock or cond fail");
            threadpool_free();
            break;
        }

        
        for (int i = 0; i < min_thr_num; i++)
        {
            pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void *)pool); 
        }
        pthread_create(&(pool->adjust_tid), NULL, adjust_thread, (void *)pool); 

    } while (0);
}

ThreadPool::~ThreadPool()
{
    auto &&pool = this;

    pool->shutdown = true;

    
    pthread_join(pool->adjust_tid, NULL);

    for (int i = 0; i < pool->live_thr_num; i++)
    {
        
        pthread_cond_broadcast(&(pool->queue_not_empty));
    }
    for (int i = 0; i < pool->live_thr_num; i++)
    {
        pthread_join(pool->threads[i], NULL);
    }

    threadpool_free();
}

int ThreadPool::threadpool_free()
{
    auto &&pool = this;

    if (pool->task_queue)
    {
        free(pool->task_queue);
    }

    if (pool->threads)
    {
        free(pool->threads);
        pthread_mutex_lock(&(pool->lock));
        pthread_mutex_destroy(&(pool->lock));
        pthread_mutex_lock(&(pool->thread_counter));
        pthread_mutex_destroy(&(pool->thread_counter));
        pthread_cond_destroy(&(pool->queue_not_empty));
        pthread_cond_destroy(&(pool->queue_not_full));
    }

    return 0;
}

int ThreadPool::is_thread_alive(pthread_t tid)
{
    int kill_rc = pthread_kill(tid, 0); 
    if (kill_rc == ESRCH)
    {
        return false;
    }

    return true;
}

int ThreadPool::threadpool_all_threadnum()
{
    auto &&pool = this;
    int all_threadnum = -1;
    pthread_mutex_lock(&(pool->lock));
    all_threadnum = pool->live_thr_num;
    pthread_mutex_unlock(&(pool->lock));
    return all_threadnum;
}

int ThreadPool::threadpool_busy_threadnum()
{
    auto &&pool = this;
    int busy_threadnum = -1;
    pthread_mutex_lock(&(pool->thread_counter));
    busy_threadnum = pool->busy_thr_num;
    pthread_mutex_unlock(&(pool->thread_counter));
    return busy_threadnum;
}

void *ThreadPool::threadpool_thread(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    threadpool_task_t task;

    while (true)
    {
        /* Lock must be taken to wait on conditional variable */
        pthread_mutex_lock(&(pool->lock));

        
        while ((pool->queue_size == 0) && (!pool->shutdown))
        {
            pthread_cond_wait(&(pool->queue_not_empty), &(pool->lock));

            
            if (pool->wait_exit_thr_num > 0)
            {
                pool->wait_exit_thr_num--;

                
                if (pool->live_thr_num > pool->min_thr_num)
                {
                    pool->live_thr_num--;
                    pthread_mutex_unlock(&(pool->lock));

                    pthread_exit(NULL);
                }
            }
        }

        
        if (pool->shutdown)
        {
            pthread_mutex_unlock(&(pool->lock));
            pthread_detach(pthread_self());
            pthread_exit(NULL); 
        }

        
        task.function = pool->task_queue[pool->queue_front].function;
        task.arg = pool->task_queue[pool->queue_front].arg;

        pool->queue_front = (pool->queue_front + 1) % pool->queue_max_size; 
        pool->queue_size--;

        
        pthread_cond_broadcast(&(pool->queue_not_full));

        
        pthread_mutex_unlock(&(pool->lock));

        
        pthread_mutex_lock(&(pool->thread_counter)); 
        pool->busy_thr_num++;                        
        pthread_mutex_unlock(&(pool->thread_counter));

        (*(task.function))(task.arg); 
        

        
        pthread_mutex_lock(&(pool->thread_counter));
        pool->busy_thr_num--; 
        pthread_mutex_unlock(&(pool->thread_counter));
    }

    pthread_exit(NULL);
}

void *ThreadPool::adjust_thread(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    while (!pool->shutdown)
    {

        sleep(DEFAULT_TIME); 

        pthread_mutex_lock(&(pool->lock));
        int queue_size = pool->queue_size;     
        int live_thr_num = pool->live_thr_num; 
        pthread_mutex_unlock(&(pool->lock));

        pthread_mutex_lock(&(pool->thread_counter));
        int busy_thr_num = pool->busy_thr_num; 
        pthread_mutex_unlock(&(pool->thread_counter));

        
        if (queue_size >= MIN_WAIT_TASK_NUM && live_thr_num < pool->max_thr_num)
        {
            pthread_mutex_lock(&(pool->lock));
            int add = 0;

            
            for (int i = 0; i < pool->max_thr_num && add < DEFAULT_THREAD_VARY && pool->live_thr_num < pool->max_thr_num; i++)
            {
                if (pool->threads[i] == 0 || !pool->is_thread_alive(pool->threads[i]))
                {
                    pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void *)pool);
                    add++;
                    pool->live_thr_num++;
                }
            }

            pthread_mutex_unlock(&(pool->lock));
        }

        
        if ((busy_thr_num * 2) < live_thr_num && live_thr_num > pool->min_thr_num)
        {

            
            pthread_mutex_lock(&(pool->lock));
            pool->wait_exit_thr_num = DEFAULT_THREAD_VARY; 
            pthread_mutex_unlock(&(pool->lock));

            for (int i = 0; i < DEFAULT_THREAD_VARY; i++)
            {
                
                pthread_cond_signal(&(pool->queue_not_empty));
            }
        }
    }

    return NULL;
}

int ThreadPool::threadpool_add(void *(*function)(void *), void *arg)
{
    auto &&pool = this;
    pthread_mutex_lock(&(pool->lock));

    
    while ((pool->queue_size == pool->queue_max_size) && (!pool->shutdown))
    {
        pthread_cond_wait(&(pool->queue_not_full), &(pool->lock));
    }
    if (pool->shutdown)
    {
        pthread_cond_broadcast(&(pool->queue_not_empty));
        pthread_mutex_unlock(&(pool->lock));
        return 0;
    }

   
    if (pool->task_queue[pool->queue_rear].arg != NULL)
    {
        pool->task_queue[pool->queue_rear].arg = NULL;
    }
  
    pool->task_queue[pool->queue_rear].function = function;
    pool->task_queue[pool->queue_rear].arg = arg;
    pool->queue_rear = (pool->queue_rear + 1) % pool->queue_max_size; 
    pool->queue_size++;


    pthread_cond_signal(&(pool->queue_not_empty));
    pthread_mutex_unlock(&(pool->lock));

    return 0;
}
