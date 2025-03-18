#include "tasksys.h"
#include <thread>
#include <atomic>
#include <cstdio>

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
    thread_count = num_threads;
}

TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {}

void TaskSystemParallelSpawn::workerFunction(IRunnable *runnable, int num_total_tasks,
                                             std::atomic<int> *next_task_idx)
{
    while (true)
    {
        // Get the next task index atomically
        int task_id = next_task_idx->fetch_add(1);

        // If we've processed all tasks, exit
        if (task_id >= num_total_tasks)
        {
            break;
        }

        // Run the task
        runnable->runTask(task_id, num_total_tasks);
    }
}

void TaskSystemParallelSpawn::run(IRunnable *runnable, int num_total_tasks)
{
    //
    // TODO: CS149 students will modify the implementation of this
    // method in Part A.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    std::atomic<int> next_task_idx(0);
    std::vector<std::thread> workers;

    // Create and start worker threads
    for (int i = 0; i < thread_count; i++)
    {
        workers.push_back(std::thread(&TaskSystemParallelSpawn::workerFunction,
                                      this, runnable, num_total_tasks, &next_task_idx));
    }

    // Wait for all threads to complete
    for (auto &thread : workers)
    {
        thread.join();
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

TaskSystemParallelThreadPoolSpinning::TaskSystemParallelThreadPoolSpinning(int num_threads) : ITaskSystem(num_threads)
{
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    thread_count = num_threads;
    should_terminate = false;
    tasks_completed = 0;

    // Create and start the thread pool
    for (int i = 0; i < thread_count; i++)
    {
        thread_pool.push_back(std::thread(&TaskSystemParallelThreadPoolSpinning::workerLoop, this));
    }
}

TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning()
{
    // Signal all threads to terminate
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        should_terminate = true;
    }

    // Join all threads
    for (auto &thread : thread_pool)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
}

void TaskSystemParallelThreadPoolSpinning::workerLoop()
{
    while (true)
    {
        // Check if we should terminate
        if (should_terminate)
        {
            break;
        }

        // Try to get a task
        TaskInfo task;
        bool got_task = false;

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (!task_queue.empty())
            {
                task = task_queue.front();
                task_queue.pop_front();
                got_task = true;
            }
        }

        // If we got a task, execute it
        if (got_task)
        {
            task.runnable->runTask(task.task_id, task.num_total_tasks);

            // Increment the completed count
            tasks_completed.fetch_add(1);
        }
    }
}

void TaskSystemParallelThreadPoolSpinning::run(IRunnable *runnable, int num_total_tasks)
{
    //
    // TODO: CS149 students will modify the implementation of this
    // method in Part A.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    // Reset the completed tasks counter
    tasks_completed = 0;

    // Add all tasks to the queue
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        for (int i = 0; i < num_total_tasks; i++)
        {
            task_queue.push_back({runnable, i, num_total_tasks});
        }
    }

    // Spin until all tasks are completed
    while (tasks_completed.load() < num_total_tasks)
    {
        // Just spin - busy waiting
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

TaskSystemParallelThreadPoolSleeping::TaskSystemParallelThreadPoolSleeping(int num_threads) : ITaskSystem(num_threads)
{
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    thread_count = num_threads;
    should_terminate = false;
    tasks_completed = 0;
    total_tasks_in_bulk_launch = 0;

    // Create and start the thread pool
    for (int i = 0; i < thread_count; i++)
    {
        thread_pool.push_back(std::thread(&TaskSystemParallelThreadPoolSleeping::workerLoop, this));
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

    // Signal all threads to terminate
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        should_terminate = true;
    }

    // Wake up all threads
    work_cv.notify_all();

    // Join all threads
    for (auto &thread : thread_pool)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
}

void TaskSystemParallelThreadPoolSleeping::workerLoop()
{
    while (true)
    {
        // Try to get a task, wait if there are none
        TaskInfo task;
        bool got_task = false;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // Wait until there's a task or termination is requested
            work_cv.wait(lock, [this]
                         { return !task_queue.empty() || should_terminate; });

            // Check if we should terminate
            if (should_terminate)
            {
                break;
            }

            // Get the task
            if (!task_queue.empty())
            {
                task = task_queue.front();
                task_queue.pop_front();
                got_task = true;
            }
        }

        // If we got a task, execute it
        if (got_task)
        {
            task.runnable->runTask(task.task_id, task.num_total_tasks);

            // Increment the completed count
            int completed = tasks_completed.fetch_add(1) + 1;

            // If this was the last task, notify the main thread
            if (completed == total_tasks_in_bulk_launch)
            {
                completion_cv.notify_all();
            }
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

    // Reset the completed tasks counter
    tasks_completed = 0;
    total_tasks_in_bulk_launch = num_total_tasks;

    // Add all tasks to the queue
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        for (int i = 0; i < num_total_tasks; i++)
        {
            task_queue.push_back({runnable, i, num_total_tasks});
        }
    }

    // Notify all worker threads that there's work to do
    work_cv.notify_all();

    // Wait for all tasks to complete
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        completion_cv.wait(lock, [this, num_total_tasks]
                           { return tasks_completed.load() == num_total_tasks; });
    }
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