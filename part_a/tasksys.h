#ifndef _TASKSYS_H
#define _TASKSYS_H

#include "itasksys.h" // Include the assignment's interface header
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <memory>

// Serial Task System
class TaskSystemSerial : public ITaskSystem {
public:
    TaskSystemSerial(int num_threads);
    ~TaskSystemSerial() override;
    const char* name() override;
    void run(IRunnable* runnable, int num_total_tasks) override;
    TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                            const std::vector<TaskID>& deps) override;
    void sync() override;
};

// Parallel Task System with Thread Spawning
class TaskSystemParallelSpawn : public ITaskSystem {
private:
    int num_threads;
public:
    TaskSystemParallelSpawn(int num_threads);
    ~TaskSystemParallelSpawn() override;
    const char* name() override;
    void run(IRunnable* runnable, int num_total_tasks) override;
    TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                            const std::vector<TaskID>& deps) override;
    void sync() override;
};

// Parallel Thread Pool with Spinning
class TaskSystemParallelThreadPoolSpinning : public ITaskSystem {
private:
    int num_threads;
    std::vector<std::thread> threads;
    std::atomic<bool> shutdown;
    std::atomic<IRunnable*> current_runnable;
    std::atomic<int> num_total_tasks;
    std::atomic<int> next_task_id;
    std::atomic<int> completed_tasks;

public:
    TaskSystemParallelThreadPoolSpinning(int num_threads);
    ~TaskSystemParallelThreadPoolSpinning() override;
    const char* name() override;
    void run(IRunnable* runnable, int num_total_tasks) override;
    TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                            const std::vector<TaskID>& deps) override;
    void sync() override;

private:
    void worker();
};

// Parallel Thread Pool with Sleeping
class TaskSystemParallelThreadPoolSleeping : public ITaskSystem {
private:
    struct Task {
        IRunnable* runnable;
        int num_total_tasks;
        int task_id;
        Task(IRunnable* r, int n, int id) : runnable(r), num_total_tasks(n), task_id(id) {}
    };

    int num_threads;
    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable cv_work;
    std::condition_variable cv_done;
    bool shutdown;
    std::queue<Task> task_queue;
    int tasks_in_flight;

public:
    TaskSystemParallelThreadPoolSleeping(int num_threads);
    ~TaskSystemParallelThreadPoolSleeping() override;
    const char* name() override;
    void run(IRunnable* runnable, int num_total_tasks) override;
    TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                            const std::vector<TaskID>& deps) override;
    void sync() override;

private:
    void worker();
};

// Parallel Thread Pool with Sleeping and Dependencies
class TaskSystemParallelThreadPoolSleepingDeps : public ITaskSystem {
private:
    struct Task {
        IRunnable* runnable;
        int num_total_tasks;
        int task_id;
        TaskID task_set_id;
        Task(IRunnable* r, int n, int t, TaskID id)
            : runnable(r), num_total_tasks(n), task_id(t), task_set_id(id) {}
    };

    struct TaskSet {
        IRunnable* runnable;
        int num_total_tasks;
        int remaining_tasks;
        std::vector<TaskID> dependencies;
        std::vector<TaskID> dependents;
        TaskSet(IRunnable* r, int n, const std::vector<TaskID>& deps)
            : runnable(r), num_total_tasks(n), remaining_tasks(n), dependencies(deps) {}
    };

    int num_threads;
    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable cv_work;
    std::condition_variable cv_done;
    bool shutdown;
    std::queue<Task> task_queue;
    std::unordered_map<TaskID, std::shared_ptr<TaskSet>> task_sets;
    TaskID next_task_set_id;

public:
    TaskSystemParallelThreadPoolSleepingDeps(int num_threads);
    ~TaskSystemParallelThreadPoolSleepingDeps() override;
    const char* name() override;
    void run(IRunnable* runnable, int num_total_tasks) override;
    TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                            const std::vector<TaskID>& deps) override;
    void sync() override;

private:
    void worker();
    bool areAllDependenciesComplete(const std::vector<TaskID>& deps);
};

#endif // _TASKSYS_H
