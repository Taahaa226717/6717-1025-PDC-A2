#include "tasksys.h"
#include <thread>
#include <atomic>
#include <cstdio>
#include <vector>

IRunnable::~IRunnable() {}

ITaskSystem::ITaskSystem(int num_threads) {}
ITaskSystem::~ITaskSystem() {}

/*
 * ================================================================
 * Serial task system implementation
 * ================================================================
 */

const char *TaskSystemSerial::name()
{
    return "Serial";
}

TaskSystemSerial::TaskSystemSerial(int num_threads) : ITaskSystem(num_threads)
{
}

TaskSystemSerial::~TaskSystemSerial() {}

void TaskSystemSerial::run(IRunnable *runnable, int num_total_tasks)
{
    for (int i = 0; i < num_total_tasks; i++)
    {
        runnable->runTask(i, num_total_tasks);
    }
}

TaskID TaskSystemSerial::runAsyncWithDeps(IRunnable *runnable, int num_total_tasks,
                                          const std::vector<TaskID> &deps)
{
    // You do not need to implement this method.
    return 0;
}

void TaskSystemSerial::sync()
{
    // You do not need to implement this method.
    return;
}

/*
 * ================================================================
 * Parallel Task System Implementation
 * ================================================================
 */

const char *TaskSystemParallelSpawn::name()
{
    return "Parallel + Always Spawn";
}

TaskSystemParallelSpawn::TaskSystemParallelSpawn(int num_threads) : ITaskSystem(num_threads)
{
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    num_threads_ = num_threads;
    my_counter_ = 0;
}

TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {}

static void runTask(IRunnable *runnable, int id, int num_total_tasks)
{
    runnable->runTask(id, num_total_tasks);
}

void TaskSystemParallelSpawn::threadTask(IRunnable *runnable, int num_total_tasks)
{
    int id = my_counter_.fetch_add(1, std::memory_order_relaxed);
    while (id < num_total_tasks)
    {
        // printf("id: %d\n", id);
        runnable->runTask(id, num_total_tasks);
        id = my_counter_.fetch_add(1, std::memory_order_relaxed);
    }
}
void TaskSystemParallelSpawn::run(IRunnable *runnable, int num_total_tasks)
{

    //
    // TODO: CS149 students will modify the implementation of this
    // method in Part A.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    // for (int i = 0; i < num_total_tasks; i++) {
    //     runnable->runTask(i, num_total_tasks);
    // }

    // bad implementation
    // std::thread workers[num_threads_];
    // for(int i=0; i< num_total_tasks; i+= num_threads_) {
    //   int cur_end = (i+num_threads_ < num_total_tasks) ? i+num_threads_ : num_total_tasks;
    //   int cur_begin = i;
    //   int t_idx = 0;
    //   for(int j=cur_begin; j < cur_end; j++, t_idx++) {
    //      workers[t_idx]= std::thread(runTask, runnable, j, num_total_tasks);
    //   }
    //   t_idx =0;
    //   for(int j=cur_begin; j < cur_end; j++, t_idx++) {
    //     workers[t_idx].join();
    //   }
    // }
    //

    my_counter_ = 0;
    std::vector<std::thread> workers;
    for (int i = 0; i < num_threads_; i++)
    {
        workers.emplace_back([this, runnable, num_total_tasks]
                             { this->threadTask(runnable, num_total_tasks); });
    }
    for (int i = 0; i < num_threads_; i++)
    {
        workers[i].join();
    }
}

TaskID TaskSystemParallelSpawn::runAsyncWithDeps(IRunnable *runnable, int num_total_tasks,
                                                 const std::vector<TaskID> &deps)
{
    // You do not need to implement this method.
    return 0;
}

void TaskSystemParallelSpawn::sync()
{
    // You do not need to implement this method.
    return;
}

/*
 * ================================================================
 * Parallel Thread Pool Spinning Task System Implementation
 * ================================================================
 */

const char *TaskSystemParallelThreadPoolSpinning::name()
{
    return "Parallel + Thread Pool + Spin";
}

void TaskSystemParallelThreadPoolSpinning::threadTask()
{
    // bool done = done_.load();
    while (!done_.load())
    {
        // printf("id: %d\n", id);
        mutex_.lock();
        threadArg arg;
        if (!task_queue_.empty())
        {
            arg = task_queue_.front();
            task_queue_.pop_front();
        }
        mutex_.unlock();
        if (arg.runnable)
        {
            arg.runnable->runTask(arg.task_id, arg.num_total_tasks);
            finished_tasks_.fetch_add(1);
        }
    }
}
TaskSystemParallelThreadPoolSpinning::TaskSystemParallelThreadPoolSpinning(int num_threads) : ITaskSystem(num_threads)
{
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    done_ = false;
    num_threads_ = num_threads;
    for (int i = 0; i < num_threads_; i++)
    {
        workers_.emplace_back([this]
                              { this->threadTask(); });
    }
}

TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning()
{
    done_.store(true);
    for (int i = 0; i < num_threads_; i++)
    {
        workers_[i].join();
    }
}

void TaskSystemParallelThreadPoolSpinning::run(IRunnable *runnable, int num_total_tasks)
{

    //
    // TODO: CS149 students will modify the implementation of this
    // method in Part A.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    finished_tasks_ = 0;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < num_total_tasks; i++)
        {
            threadArg arg(i, num_total_tasks, runnable);
            task_queue_.emplace_back(arg);
        }
    }

    while (finished_tasks_.load() != num_total_tasks)
    {
    }
}

TaskID TaskSystemParallelThreadPoolSpinning::runAsyncWithDeps(IRunnable *runnable, int num_total_tasks,
                                                              const std::vector<TaskID> &deps)
{
    // You do not need to implement this method.
    return 0;
}

void TaskSystemParallelThreadPoolSpinning::sync()
{
    // You do not need to implement this method.
    return;
}

/*
 * ================================================================
 * Parallel Thread Pool Sleeping Task System Implementation
 * ================================================================
 */

const char *TaskSystemParallelThreadPoolSleeping::name()
{
    return "Parallel + Thread Pool + Sleep";
}

void TaskSystemParallelThreadPoolSleeping::threadTask()
{
    int num_task = 0;
    while (!done_.load())
    {
        std::unique_lock<std::mutex> lock(mutex_);
        threadArg arg;
        bool get_task = false;
        if (!task_queue_.empty())
        {
            arg = task_queue_.front();
            task_queue_.pop_front();
            get_task = true;
            // printf("get task\n");
        }
        else
        {
            cv_.wait(lock);
        }
        lock.unlock();
        if (get_task)
        {
            arg.runnable->runTask(arg.task_id, arg.num_total_tasks);
            num_task = arg.num_total_tasks;
            finished_tasks_.fetch_add(1);
            if (finished_tasks_.load() == num_task)
            {
                // printf("send notify\n");
                cv_.notify_all();
            }
        }
    }
}
TaskSystemParallelThreadPoolSleeping::TaskSystemParallelThreadPoolSleeping(int num_threads) : ITaskSystem(num_threads)
{
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    num_threads_ = num_threads;
    done_ = false;
    for (int i = 0; i < num_threads_; i++)
    {
        workers_.emplace_back([this]
                              { this->threadTask(); });
    }
}

TaskSystemParallelThreadPoolSleeping::~TaskSystemParallelThreadPoolSleeping()
{
    //
    // TODO: CS149 student implementations may decide to perform cleanup
    // operations (such as thread pool shutdown construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    done_.store(true);
    cv_.notify_all();
    for (int i = 0; i < num_threads_; i++)
    {
        if (workers_[i].joinable())
        {
            workers_[i].join();
        }
    }
}

void TaskSystemParallelThreadPoolSleeping::run(IRunnable *runnable, int num_total_tasks)
{

    //
    // TODO: CS149 students will modify the implementation of this
    // method in Parts A and B.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    std::unique_lock<std::mutex> lock(mutex_);
    finished_tasks_.store(0);
    for (int i = 0; i < num_total_tasks; i++)
    {
        threadArg arg(i, num_total_tasks, runnable);
        task_queue_.emplace_back(arg);
    }
    // printf("before notify all\n");
    lock.unlock();
    cv_.notify_all();
    lock.lock();

    while (finished_tasks_.load() != num_total_tasks)
    {
        cv_.wait(lock);
    }

    // printf("finished_tasks_:%d\n", finished_tasks_.load());
}

TaskID TaskSystemParallelThreadPoolSleeping::runAsyncWithDeps(IRunnable *runnable, int num_total_tasks,
                                                              const std::vector<TaskID> &deps)
{

    //
    // TODO: CS149 students will implement this method in Part B.
    //

    return 0;
}

void TaskSystemParallelThreadPoolSleeping::sync()
{

    //
    // TODO: CS149 students will modify the implementation of this method in Part B.
    //

    return;
}