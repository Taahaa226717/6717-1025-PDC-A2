#ifndef _TASKSYS_H
#define _TASKSYS_H

#include "itasksys.h"
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <memory>

class TaskSystemSerial: public ITaskSystem {
public:
    TaskSystemSerial(int num_threads);
    ~TaskSystemSerial();
    const char* name();
    void run(IRunnable* runnable, int num_total_tasks);
    TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                            const std::vector<TaskID>& deps);
    void sync();
};

class TaskSystemParallelSpawn: public ITaskSystem {
private:
    int num_threads;
public:
    TaskSystemParallelSpawn(int num_threads);
    ~TaskSystemParallelSpawn();
    const char* name();
    void run(IRunnable* runnable, int num_total_tasks);
    TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                            const std::vector<TaskID>& deps);
    void sync();
};

class TaskSystemParallelThreadPoolSpinning: public ITaskSystem {
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
    ~TaskSystemParallelThreadPoolSpinning();
    const char* name();
    void run(IRunnable* runnable, int num_total_tasks);
    TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                            const std::vector<TaskID>& deps);
    void sync();
    
private:
    void worker();
};

class TaskSystemParallelThreadPoolSleeping: public ITaskSystem {
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
    ~TaskSystemParallelThreadPoolSleeping();
    const char* name();
    void run(IRunnable* runnable, int num_total_tasks);
    TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                            const std::vector<TaskID>& deps);
    void sync();
    
private:
    void worker();
};

class TaskSystemParallelThreadPoolSleepingDeps: public ITaskSystem {
private:
    struct TaskSet {
        IRunnable* runnable;
        int num_total_tasks;
        std::atomic<int> next_task_id;
        std::atomic<int> tasks_completed;
        std::vector<TaskID> deps;
        
        TaskSet(IRunnable* r, int n, const std::vector<TaskID>& d) 
            : runnable(r), num_total_tasks(n), next_task_id(0), tasks_completed(0), deps(d) {}
    };
    
    int num_threads;
    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable cv_work;
    std::condition_variable cv_done;
    bool shutdown;
    std::unordered_map<TaskID, std::shared_ptr<TaskSet>> task_sets;
    std::queue<TaskID> ready_queue;
    TaskID next_task_set_id;
    
public:
    TaskSystemParallelThreadPoolSleepingDeps(int num_threads);
    ~TaskSystemParallelThreadPoolSleepingDeps();
    const char* name();
    void run(IRunnable* runnable, int num_total_tasks);
    TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                            const std::vector<TaskID>& deps);
    void sync();
    
private:
    void worker();
    void checkAndEnqueueReadyTasks();
    bool areAllDependenciesComplete(const std::vector<TaskID>& deps);
};

#endif // _TASKSYS_H
