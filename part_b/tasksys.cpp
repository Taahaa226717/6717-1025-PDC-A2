#include "tasksys.h"
#include <algorithm>

IRunnable::~IRunnable() {}
ITaskSystem::ITaskSystem(int num_threads) {}
ITaskSystem::~ITaskSystem() {}

/*
 * Serial Task System Implementation
 */
const char* TaskSystemSerial::name() { return "Serial"; }
TaskSystemSerial::TaskSystemSerial(int num_threads) : ITaskSystem(num_threads) {}
TaskSystemSerial::~TaskSystemSerial() {}
void TaskSystemSerial::run(IRunnable* runnable, int num_total_tasks) {
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}
TaskID TaskSystemSerial::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                          const std::vector<TaskID>& deps) {
    run(runnable, num_total_tasks);
    return 0;
}
void TaskSystemSerial::sync() {}

/*
 * Parallel Spawn Task System Implementation
 */
const char* TaskSystemParallelSpawn::name() { return "Parallel + Always Spawn"; }
TaskSystemParallelSpawn::TaskSystemParallelSpawn(int num_threads) : ITaskSystem(num_threads), num_threads(num_threads) {}
TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {}
void TaskSystemParallelSpawn::run(IRunnable* runnable, int num_total_tasks) {
    std::vector<std::thread> threads;
    int tasks_per_thread = (num_total_tasks + num_threads - 1) / num_threads;
    for (int i = 0; i < num_threads && i * tasks_per_thread < num_total_tasks; i++) {
        int start_task = i * tasks_per_thread;
        int end_task = std::min((i + 1) * tasks_per_thread, num_total_tasks);
        threads.emplace_back([runnable, start_task, end_task, num_total_tasks]() {
            for (int j = start_task; j < end_task; j++) {
                runnable->runTask(j, num_total_tasks);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
}
TaskID TaskSystemParallelSpawn::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                 const std::vector<TaskID>& deps) {
    run(runnable, num_total_tasks);
    return 0;
}
void TaskSystemParallelSpawn::sync() {}

/*
 * Parallel Thread Pool Spinning Task System Implementation
 */
const char* TaskSystemParallelThreadPoolSpinning::name() { return "Parallel + Thread Pool + Spin"; }
TaskSystemParallelThreadPoolSpinning::TaskSystemParallelThreadPoolSpinning(int num_threads)
    : ITaskSystem(num_threads), num_threads(num_threads), should_terminate(false), tasks_completed(0), total_tasks(0) {
    for (int i = 0; i < num_threads; i++) {
        thread_pool.emplace_back([this]() {
            while (!should_terminate) {
                std::pair<IRunnable*, int> task;
                bool got_task = false;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    if (!task_queue.empty()) {
                        task = task_queue.front();
                        task_queue.pop();
                        got_task = true;
                    }
                }
                if (got_task) {
                    task.first->runTask(task.second, total_tasks);
                    tasks_completed++;
                }
            }
        });
    }
}
TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning() {
    should_terminate = true;
    for (auto& thread : thread_pool) {
        thread.join();
    }
}
void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {
    tasks_completed = 0;
    total_tasks = num_total_tasks;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        for (int i = 0; i < num_total_tasks; i++) {
            task_queue.push(std::make_pair(runnable, i));
        }
    }
    while (tasks_completed < num_total_tasks) {}
}
TaskID TaskSystemParallelThreadPoolSpinning::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                              const std::vector<TaskID>& deps) {
    run(runnable, num_total_tasks);
    return 0;
}
void TaskSystemParallelThreadPoolSpinning::sync() {}

/*
 * Parallel Thread Pool Sleeping Task System Implementation
 */
const char* TaskSystemParallelThreadPoolSleeping::name() { return "Parallel + Thread Pool + Sleep"; }
TaskSystemParallelThreadPoolSleeping::TaskSystemParallelThreadPoolSleeping(int num_threads)
    : ITaskSystem(num_threads), num_threads(num_threads), should_terminate(false), next_task_id(1), pending_syncs(0) {
    for (int i = 0; i < num_threads; i++) {
        thread_pool.emplace_back(&TaskSystemParallelThreadPoolSleeping::worker_thread, this);
    }
}
TaskSystemParallelThreadPoolSleeping::~TaskSystemParallelThreadPoolSleeping() {
    should_terminate = true;
    queue_cv.notify_all();
    for (auto& thread : thread_pool) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}
void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {
    std::vector<TaskID> no_deps;
    TaskID id = runAsyncWithDeps(runnable, num_total_tasks, no_deps);
    sync();
}
TaskID TaskSystemParallelThreadPoolSleeping::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                              const std::vector<TaskID>& deps) {
    if (num_total_tasks <= 0) return 0;
    TaskID new_task_id = next_task_id++;
    auto bulk_task = std::make_shared<BulkTask>(runnable, num_total_tasks, deps);
    {
        std::lock_guard<std::mutex> lock(graph_mutex);
        bulk_tasks[new_task_id] = bulk_task;
        pending_syncs++;
        for (int i = 0; i < num_total_tasks; i++) {
            bulk_task->task_ids.push_back(i); // Track individual tasks
        }
    }
    check_and_queue_ready_tasks();
    return new_task_id;
}
void TaskSystemParallelThreadPoolSleeping::sync() {
    std::unique_lock<std::mutex> lock(sync_mutex);
    sync_cv.wait(lock, [this]() { return pending_syncs == 0; });
}
void TaskSystemParallelThreadPoolSleeping::worker_thread() {
    while (!should_terminate) {
        Task task(0, 0, 0, nullptr);
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this]() { return !ready_queue.empty() || should_terminate; });
            if (should_terminate && ready_queue.empty()) break;
            if (!ready_queue.empty()) {
                task = ready_queue.front();
                ready_queue.pop();
            }
        }
        if (task.runnable) {
            task.runnable->runTask(task.task_id, task.total_tasks);
            task_completed(task.bulk_id);
        }
    }
}
void TaskSystemParallelThreadPoolSleeping::task_completed(TaskID bulk_id) {
    bool bulk_completed = false;
    {
        std::lock_guard<std::mutex> lock(graph_mutex);
        auto& bulk_task = bulk_tasks[bulk_id];
        if (--bulk_task->tasks_remaining == 0) {
            bulk_completed = true;
            completed_task_ids.insert(bulk_id);
        }
    }
    if (bulk_completed) {
        check_and_queue_ready_tasks();
        {
            std::lock_guard<std::mutex> lock(sync_mutex);
            pending_syncs--;
        }
        sync_cv.notify_all();
    }
}
void TaskSystemParallelThreadPoolSleeping::check_and_queue_ready_tasks() {
    std::vector<TaskID> ready_task_ids;
    {
        std::lock_guard<std::mutex> lock(graph_mutex);
        for (const auto& pair : bulk_tasks) {
            TaskID task_id = pair.first;
            auto& bulk_task = pair.second;
            if (bulk_task->tasks_remaining <= 0 || !bulk_task->task_ids.size()) continue;
            bool deps_satisfied = true;
            for (TaskID dep : bulk_task->deps) {
                if (completed_task_ids.find(dep) == completed_task_ids.end()) {
                    deps_satisfied = false;
                    break;
                }
            }
            if (deps_satisfied) {
                ready_task_ids.push_back(task_id);
            }
        }
    }
    if (!ready_task_ids.empty()) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        for (TaskID bulk_id : ready_task_ids) {
            auto& bulk_task = bulk_tasks[bulk_id];
            for (int i : bulk_task->task_ids) {
                ready_queue.emplace(bulk_id, i, bulk_task->num_total_tasks, bulk_task->runnable);
            }
            bulk_task->task_ids.clear(); // Prevent re-queueing
        }
        queue_cv.notify_all();
    }
}
