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
#include <stdio.h>

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
    if (num_total_tasks <= 0) return;
    
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}
TaskID TaskSystemSerial::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                         const std::vector<TaskID>& deps) { 
    run(runnable, num_total_tasks);
    return 0; 
}
void TaskSystemSerial::sync() { return; }

/*
 * Parallel Task System Implementation: Spawn Threads
 */
const char* TaskSystemParallelSpawn::name() { return "Parallel + Always Spawn"; }
TaskSystemParallelSpawn::TaskSystemParallelSpawn(int num_threads)
    : ITaskSystem(num_threads), num_threads(num_threads) {}
TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {}
void TaskSystemParallelSpawn::run(IRunnable* runnable, int num_total_tasks) {
    if (num_total_tasks <= 0) return;
    
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
    return 0; 
}
void TaskSystemParallelSpawn::sync() { return; }

/*
 * Parallel Thread Pool Spinning Task System Implementation
 */
const char* TaskSystemParallelThreadPoolSpinning::name() { return "Parallel + Thread Pool + Spin"; }
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

void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {
    if (num_total_tasks <= 0) return;

    // Reset counters
    next_task_id.store(0);
    completed_tasks.store(0);
    
    // Set up the task
    current_runnable.store(runnable);
    this->num_total_tasks.store(num_total_tasks);

    // Wait for all tasks to complete
    while (completed_tasks.load() < num_total_tasks) {
        // Adding a small sleep to reduce CPU usage from constant spinning
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
            // Sleep briefly to reduce CPU usage when idle
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

TaskID TaskSystemParallelThreadPoolSpinning::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                             const std::vector<TaskID>& deps) { 
    run(runnable, num_total_tasks);
    return 0; 
}

void TaskSystemParallelThreadPoolSpinning::sync() { return; }

/*
 * Parallel Thread Pool Sleeping Task System Implementation
 */
const char* TaskSystemParallelThreadPoolSleeping::name() { return "Parallel + Thread Pool + Sleep"; }
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

void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {
    if (num_total_tasks <= 0) return;

    {
        std::unique_lock<std::mutex> lock(mutex);
        
        // Add all tasks to the queue
        for (int i = 0; i < num_total_tasks; i++) {
            task_queue.emplace(runnable, num_total_tasks, i);
        }
        
        tasks_in_flight = num_total_tasks;
    }
    
    // Wake up worker threads
    cv_work.notify_all();
    
    // Wait for all tasks to complete
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv_done.wait(lock, [this]() { return tasks_in_flight == 0; });
    }
}

void TaskSystemParallelThreadPoolSleeping::worker() {
    while (true) {
        Task task(nullptr, 0, 0);
        bool have_task = false;
        
        {
            std::unique_lock<std::mutex> lock(mutex);
            
            // Wait until there's work or we're shutting down
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
        
        // Execute task if we got one
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
    return 0;
}

void TaskSystemParallelThreadPoolSleeping::sync() { return; }

/*
 * Parallel Thread Pool Sleeping with Dependencies Task System Implementation
 */
const char* TaskSystemParallelThreadPoolSleepingDeps::name() { return "Parallel + Thread Pool + Sleep + Deps"; }
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

void TaskSystemParallelThreadPoolSleepingDeps::run(IRunnable* runnable, int num_total_tasks) {
    if (num_total_tasks <= 0) return;
    
    std::vector<TaskID> no_deps;
    TaskID id = runAsyncWithDeps(runnable, num_total_tasks, no_deps);
    
    // Wait for this specific task set to complete
    {
        std::unique_lock<std::mutex> lock(mutex);
        auto it = task_sets.find(id);
        if (it != task_sets.end()) {
            auto task_set = it->second;
            cv_done.wait(lock, [task_set, num_total_tasks]() { 
                return task_set->tasks_completed.load() == num_total_tasks; 
            });
            task_sets.erase(id);
        }
    }
}

TaskID TaskSystemParallelThreadPoolSleepingDeps::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                                const std::vector<TaskID>& deps) {
    if (num_total_tasks <= 0) return 0;
    
    TaskID new_id;
    {
        std::lock_guard<std::mutex> lock(mutex);
        new_id = next_task_set_id++;
        
        // Create a new task set
        auto task_set = std::make_shared<TaskSet>(runnable, num_total_tasks, deps);
        task_sets[new_id] = task_set;
        
        // If all dependencies are satisfied, add to ready queue
        if (areAllDependenciesComplete(deps)) {
            ready_queue.push(new_id);
            cv_work.notify_all();
        }
    }
    
    return new_id;
}

void TaskSystemParallelThreadPoolSleepingDeps::sync() {
    std::unique_lock<std::mutex> lock(mutex);
    cv_done.wait(lock, [this]() { 
        return task_sets.empty() || 
               std::all_of(task_sets.begin(), task_sets.end(), 
                           [](const std::pair<const int, std::shared_ptr<TaskSet>>& pair) { 
                               return pair.second->tasks_completed.load() == pair.second->num_total_tasks; 
                           });
    });
    
    // Clear completed task sets
    for (auto it = task_sets.begin(); it != task_sets.end();) {
        if (it->second->tasks_completed.load() == it->second->num_total_tasks) {
            it = task_sets.erase(it);
        } else {
            ++it;
        }
    }
}

void TaskSystemParallelThreadPoolSleepingDeps::worker() {
    while (true) {
        TaskID current_task_id = 0;
        std::shared_ptr<TaskSet> current_task_set;
        bool have_task = false;
        
        {
            std::unique_lock<std::mutex> lock(mutex);
            
            // Wait for work or shutdown
            cv_work.wait(lock, [this]() { return !ready_queue.empty() || shutdown; });
            
            if (shutdown && ready_queue.empty()) {
                return;
            }
            
            if (!ready_queue.empty()) {
                current_task_id = ready_queue.front();
                ready_queue.pop();
                
                auto it = task_sets.find(current_task_id);
                if (it != task_sets.end()) {
                    current_task_set = it->second;
                    have_task = true;
                }
            }
        }
        
        if (have_task) {
            // Get task ID within the task set
            int task_id = current_task_set->next_task_id.fetch_add(1);
            
            // Execute if this task ID is within range
            if (task_id < current_task_set->num_total_tasks) {
                current_task_set->runnable->runTask(task_id, current_task_set->num_total_tasks);
                
                // Mark task as completed
                int tasks_completed = current_task_set->tasks_completed.fetch_add(1) + 1;
                
                // If there are more tasks in this set, put the task set back in the queue
                if (tasks_completed < current_task_set->num_total_tasks) {
                    std::lock_guard<std::mutex> lock(mutex);
                    ready_queue.push(current_task_id);
                    cv_work.notify_one();
                } 
                // If this task set is complete, check if it was a dependency for other tasks
                else {
                    std::lock_guard<std::mutex> lock(mutex);
                    checkAndEnqueueReadyTasks();
                    cv_done.notify_all();
                }
            } 
            // If we got a task ID out of range, put the task set back in case other threads need work
            else {
                std::lock_guard<std::mutex> lock(mutex);
                if (current_task_set->tasks_completed.load() < current_task_set->num_total_tasks) {
                    ready_queue.push(current_task_id);
                    cv_work.notify_one();
                }
            }
        }
    }
}

void TaskSystemParallelThreadPoolSleepingDeps::checkAndEnqueueReadyTasks() {
    // Check all task sets to see if their dependencies are now satisfied
    for (const auto& pair : task_sets) {
        TaskID id = pair.first;
        const auto& task_set = pair.second;
        
        // Skip task sets that are already in the queue or completed
        if (task_set->tasks_completed.load() == task_set->num_total_tasks || 
            task_set->next_task_id.load() > 0) {
            continue;
        }
        
        // Check if all dependencies are now satisfied
        if (areAllDependenciesComplete(task_set->deps)) {
            ready_queue.push(id);
            cv_work.notify_one();
        }
    }
}

bool TaskSystemParallelThreadPoolSleepingDeps::areAllDependenciesComplete(const std::vector<TaskID>& deps) {
    if (deps.empty()) {
        return true;
    }
    
    for (TaskID dep_id : deps) {
        auto it = task_sets.find(dep_id);
        if (it == task_sets.end()) {
            continue; // Dependency doesn't exist (might have been removed after completion)
        }
        
        const auto& dep_task_set = it->second;
        if (dep_task_set->tasks_completed.load() < dep_task_set->num_total_tasks) {
            return false; // Found an incomplete dependency
        }
    }
    
    return true;
}
