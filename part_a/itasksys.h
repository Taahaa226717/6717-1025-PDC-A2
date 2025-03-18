#ifndef _ITASKSYS_H
#define _ITASKSYS_H

#include <vector>

typedef int TaskID;

class IRunnable {
public:
    virtual ~IRunnable();
    /*
      Executes an instance of the task as part of a bulk task launch.
      
       - task_id: the current task identifier. This value will be
          between 0 and num_total_tasks-1.
          
       - num_total_tasks: the total number of tasks in the bulk
         task launch.
     */
    virtual void runTask(int task_id, int num_total_tasks) = 0;
};

class ITaskSystem {
public:
    /*
      Instantiates a task system.
       - num_threads: the maximum number of threads that the task system
         can use.
     */
    ITaskSystem(int num_threads);
    virtual ~ITaskSystem();
    virtual const char* name() = 0;
    /*
      Executes a bulk task launch of num_total_tasks. Task
      execution is synchronous with the calling thread, so run()
      will return only when the execution of all tasks is
      complete.
    */
    virtual void run(IRunnable* runnable, int num_total_tasks) = 0;
    /*
      Executes an asynchronous bulk task launch of
      num_total_tasks, but with a dependency on prior launched
      tasks.
    */
    virtual TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                    const std::vector<TaskID>& deps) = 0;
    /*
      Blocks until all tasks created as a result of **any prior**
      runXXX calls are done.
     */
    virtual void sync() = 0;
};

#endif // _ITASKSYS_H
