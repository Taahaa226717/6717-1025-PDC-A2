#include "tasksys.h"
#include <algorithm>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>
#include <chrono>

// Provide definitions for IRunnable and ITaskSystem virtual functions
IRunnable::~IRunnable() {}

ITaskSystem::ITaskSystem(int num_threads) {}

ITaskSystem::~ITaskSystem() {}

/*
 * ================================================================
 * Serial Task System Implementation
 * ================================================================
 */
TaskSystemSerial::TaskSystemSerial(int num_threads) : ITaskSystem(num_threads) {}

TaskSystemSerial::~TaskSystemSerial() {}

const char* TaskSystemSerial::name() { return "Serial"; }

void TaskSystemSerial::run(IRunnable* runnable, int num_total_tasks) {
    if (num_total_tasks <= 0 || runnable == nullptr) return;
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}

TaskID TaskSystemSerial::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                         const std::vector<TaskID>& deps) {
    run(runnable, num_total_tasks);
    return 0; // Serial system doesn't track task IDs
}

void TaskSystemSerial::sync() {}

/*
 * ================================================================
 * Parallel Task System Implementation: Spawn Threads
 * ================================================================
 */
TaskSystemParallelSpawn::TaskSystemParallelSpawn(int num_threads)
    : ITaskSystem(num_threads), num_threads(num_threads) {}

TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {}

const char* TaskSystemParallelSpawn::name() { return "Parallel + Always Spawn"; }

void TaskSystemParallelSpawn::run(IRunnable* runnable, int num_total_tasks) {
    if (num_total_tasks <= 0 || runnable == nullptr) return;
    int threads_to_use = std::min(num_threads, num_total_tasks);
    if (threads_to_use <= 0) return;

    std::vector<std::thread> threads;
    threads.reserve(threads_to_use);

    for (int i = 0; i < threads_to_use; i++) {
        int start = i * num_total_tasks / threads_to_use;
        int end = (i + 1) * num_total_tasks / threads_to_use;
        threads.emplace_back([=]() {
            for (int task_id = start; task_id < end; task_id++) {
                runnable->runTask(task_id, num_total_tasks);
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
    return 0; // No async support in this implementation
}

void TaskSystemParallelSpawn::sync() {}

/*
 * ================================================================
 * Parallel Thread Pool Spinning Task System Implementation
 * ================================================================
 */
TaskSystemParallelThreadPoolSpinning::TaskSystemParallelThreadPoolSpinning(int num_threads)
    : ITaskSystem(num_threads), num_threads(num_threads), shutdown(false),
      current_runnable(nullptr), num_total_tasks(0), next_task_id(0), completed_tasks(0) {
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(&TaskSystemParallelThreadPoolSpinning::worker, this);
    }
}

TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning() {
    shutdown.store(true);
    for (auto& thread : threads) {
        thread.join();
    }
}

const char* TaskSystemParallelThreadPoolSpinning::name() { return "Parallel + Thread Pool + Spin"; }

void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {
    if (num_total_tasks <= 0 || runnable == nullptr) return;
    next_task_id.store(0);
    completed_tasks.store(0);
    this->num_total_tasks.store(num_total_tasks);
    current_runnable.store(runnable);

    while (completed_tasks.load() < num_total_tasks) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    current_runnable.store(nullptr);
}

void TaskSystemParallelThreadPoolSpinning::worker() {
    while (!shutdown.load()) {
        IRunnable* runnable = current_runnable.load();
        if (runnable != nullptr) {
            int task_id = next_task_id.fetch_add(1);
            int total = num_total_tasks.load();
            if (task_id < total) {
                runnable->runTask(task_id, total);
                completed_tasks.fetch_add(1);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

TaskID TaskSystemParallelThreadPoolSpinning::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                             const std::vector<TaskID>& deps) {
    run(runnable, num_total_tasks);
    return 0; // No async support in this implementation
}

void TaskSystemParallelThreadPoolSpinning::sync() {}

/*
 * ================================================================
 * Parallel Thread Pool Sleeping Task System Implementation
 * ================================================================
 */
TaskSystemParallelThreadPoolSleeping::TaskSystemParallelThreadPoolSleeping(int num_threads)
    : ITaskSystem(num_threads), num_threads(num_threads), shutdown(false), tasks_in_flight(0) {
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(&TaskSystemParallelThreadPoolSleeping::worker, this);
    }
}

TaskSystemParallelThreadPoolSleeping::~TaskSystemParallelThreadPoolSleeping() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        shutdown = true;
    }
    cv_work.notify_all();
    for (auto& thread : threads) {
        thread.join();
    }
}

const char* TaskSystemParallelThreadPoolSleeping::name() { return "Parallel + Thread Pool + Sleep"; }

void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {
    if (num_total_tasks <= 0 || runnable == nullptr) return;
    {
        std::unique_lock<std::mutex> lock(mutex);
        for (int i = 0; i < num_total_tasks; i++) {
            task_queue.emplace(runnable, num_total_tasks, i);
        }
        tasks_in_flight = num_total_tasks;
        cv_work.notify_all();
        cv_done.wait(lock, [this]() { return tasks_in_flight == 0; });
    }
}

void TaskSystemParallelThreadPoolSleeping::worker() {
    while (true) {
        Task task(nullptr, 0, 0);
        bool have_task = false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv_work.wait(lock, [this]() { return !task_queue.empty() || shutdown; });
            if (shutdown && task_queue.empty()) {
                return;
            }
            if (!task_queue.empty()) {
                task = task_queue.front();
                task_queue.pop();
                have_task = true;
            }
        }
        if (have_task) {
            task.runnable->runTask(task.task_id, task.num_total_tasks);
            {
                std::lock_guard<std::mutex> lock(mutex);
                tasks_in_flight--;
                if (tasks_in_flight == 0) {
                    cv_done.notify_all();
                }
            }
        }
    }
}

TaskID TaskSystemParallelThreadPoolSleeping::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                            const std::vector<TaskID>& deps) {
    run(runnable, num_total_tasks);
    return 0; // No async support in this implementation
}

void TaskSystemParallelThreadPoolSleeping::sync() {}

/*
 * ================================================================
 * Parallel Thread Pool Sleeping with Dependencies Task System Implementation
 * ================================================================
 */
TaskSystemParallelThreadPoolSleepingDeps::TaskSystemParallelThreadPoolSleepingDeps(int num_threads)
    : ITaskSystem(num_threads), num_threads(num_threads), shutdown(false), next_task_set_id(1) {
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(&TaskSystemParallelThreadPoolSleepingDeps::worker, this);
    }
}

TaskSystemParallelThreadPoolSleepingDeps::~TaskSystemParallelThreadPoolSleepingDeps() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        shutdown = true;
    }
    cv_work.notify_all();
    for (auto& thread : threads) {
        thread.join();
    }
}

const char* TaskSystemParallelThreadPoolSleepingDeps::name() { return "Parallel + Thread Pool + Sleep + Deps"; }

void TaskSystemParallelThreadPoolSleepingDeps::run(IRunnable* runnable, int num_total_tasks) {
    if (num_total_tasks <= 0 || runnable == nullptr) return;
    std::vector<TaskID> no_deps;
    runAsyncWithDeps(runnable, num_total_tasks, no_deps);
    sync();
}

TaskID TaskSystemParallelThreadPoolSleepingDeps::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                                const std::vector<TaskID>& deps) {
    if (num_total_tasks <= 0 || runnable == nullptr) return 0;
    TaskID new_id;
    {
        std::lock_guard<std::mutex> lock(mutex);
        new_id = next_task_set_id++;
        task_sets[new_id] = std::make_shared<TaskSet>(runnable, num_total_tasks, deps);
        if (areAllDependenciesComplete(deps)) {
            for (int i = 0; i < num_total_tasks; i++) {
                task_queue.emplace(runnable, num_total_tasks, i, new_id);
            }
            cv_work.notify_all();
        }
    }
    return new_id;
}

void TaskSystemParallelThreadPoolSleepingDeps::sync() {
    std::unique_lock<std::mutex> lock(mutex);
    cv_done.wait(lock, [this]() { return task_sets.empty(); });
}

void TaskSystemParallelThreadPoolSleepingDeps::worker() {
    while (true) {
        Task task(nullptr, 0, 0, 0);
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv_work.wait(lock, [this]() { return !task_queue.empty() || shutdown; });
            if (shutdown && task_queue.empty()) {
                return;
            }
            if (!task_queue.empty()) {
                task = task_queue.front();
                task_queue.pop();
            }
        }
        if (task.runnable) {
            task.runnable->runTask(task.task_id, task.num_total_tasks);
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto& task_set = *task_sets[task.task_set_id];
                task_set.remaining_tasks--;
                if (task_set.remaining_tasks == 0) {
                    for (TaskID dependent_id : task_set.dependents) {
                        if (areAllDependenciesComplete(task_sets[dependent_id]->dependencies)) {
                            for (int i = 0; i < task_sets[dependent_id]->num_total_tasks; i++) {
                                task_queue.emplace(task_sets[dependent_id]->runnable,
                                                  task_sets[dependent_id]->num_total_tasks,
                                                  i, dependent_id);
                            }
                        }
                    }
                    task_sets.erase(task.task_set_id);
                    if (task_sets.empty()) {
                        cv_done.notify_all();
                    }
                }
                cv_work.notify_all();
            }
        }
    }
}

bool TaskSystemParallelThreadPoolSleepingDeps::areAllDependenciesComplete(const std::vector<TaskID>& deps) {
    for (TaskID dep_id : deps) {
        auto it = task_sets.find(dep_id);
        if (it != task_sets.end() && it->second->remaining_tasks > 0) {
            return false;
        }
    }
    return true;
}
