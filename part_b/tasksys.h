#ifndef _TASKSYS_H
#define _TASKSYS_H

#include "itasksys.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <unordered_map>
#include <unordered_set>

/*
 * TaskSystemSerial: This class is the student's implementation of a
 * serial task execution engine.  See definition of ITaskSystem in
 * itasksys.h for documentation of the ITaskSystem interface.
 */
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

/*
 * TaskSystemParallelSpawn: This class is the student's implementation of a
 * parallel task execution engine that spawns threads in every run()
 * call.  See definition of ITaskSystem in itasksys.h for documentation
 * of the ITaskSystem interface.
 */
class TaskSystemParallelSpawn: public ITaskSystem {
    public:
        TaskSystemParallelSpawn(int num_threads);
        ~TaskSystemParallelSpawn();
        const char* name();
        void run(IRunnable* runnable, int num_total_tasks);
        TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                const std::vector<TaskID>& deps);
        void sync();
    private:
        int num_threads;
};

/*
 * TaskSystemParallelThreadPoolSpinning: This class is the student's
 * implementation of a parallel task execution engine that uses a
 * thread pool. See definition of ITaskSystem in itasksys.h for
 * documentation of the ITaskSystem interface.
 */
class TaskSystemParallelThreadPoolSpinning: public ITaskSystem {
    public:
        TaskSystemParallelThreadPoolSpinning(int num_threads);
        ~TaskSystemParallelThreadPoolSpinning();
        const char* name();
        void run(IRunnable* runnable, int num_total_tasks);
        TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                const std::vector<TaskID>& deps);
        void sync();
    private:
        int num_threads;
        bool should_terminate;
        std::vector<std::thread> thread_pool;
        
        std::mutex queue_mutex;
        std::queue<std::pair<IRunnable*, int>> task_queue;
        
        std::atomic<int> tasks_completed;
        std::atomic<int> total_tasks;
};

/*
 * TaskSystemParallelThreadPoolSleeping: This class is the student's
 * optimized implementation of a parallel task execution engine that uses
 * a thread pool. See definition of ITaskSystem in
 * itasksys.h for documentation of the ITaskSystem interface.
 */
class TaskSystemParallelThreadPoolSleeping: public ITaskSystem {
    public:
        TaskSystemParallelThreadPoolSleeping(int num_threads);
        ~TaskSystemParallelThreadPoolSleeping();
        const char* name();
        void run(IRunnable* runnable, int num_total_tasks);
        TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                const std::vector<TaskID>& deps);
        void sync();
    
    private:
        // Structure to represent a bulk task launch
        struct BulkTask {
            IRunnable* runnable;
            int num_total_tasks;
            std::vector<TaskID> deps;
            std::atomic<int> tasks_remaining;
            std::vector<int> task_ids; // Tasks that are part of this bulk launch
            
            BulkTask(IRunnable* r, int n, const std::vector<TaskID>& d) 
                : runnable(r), num_total_tasks(n), deps(d), tasks_remaining(n) {}
        };
        
        // Structure to represent an individual task
        struct Task {
            TaskID bulk_id;
            int task_id;
            int total_tasks;
            IRunnable* runnable;
            
            Task(TaskID b, int t, int tot, IRunnable* r) 
                : bulk_id(b), task_id(t), total_tasks(tot), runnable(r) {}
        };
        
        int num_threads;
        std::atomic<bool> should_terminate;
        std::vector<std::thread> thread_pool;
        
        // For task graph execution
        std::mutex graph_mutex;
        std::condition_variable graph_cv;
        std::atomic<TaskID> next_task_id;
        std::unordered_map<TaskID, std::shared_ptr<BulkTask>> bulk_tasks;
        std::unordered_set<TaskID> completed_task_ids;
        
        // Ready queue for tasks ready to execute
        std::mutex queue_mutex;
        std::condition_variable queue_cv;
        std::queue<Task> ready_queue;
        
        // Sync tracking
        std::atomic<int> pending_syncs;
        std::condition_variable sync_cv;
        std::mutex sync_mutex;
        
        // Thread worker function
        void worker_thread();
        
        // Task completion handler
        void task_completed(TaskID bulk_id);
        
        // Check and queue tasks whose dependencies are satisfied
        void check_and_queue_ready_tasks();
};

#endif
