#include "threadpool.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// things needed for this threadpool
// a vector to spawn all the threads specificed by the user
// a queue to take all tasks that would be queued up and run inside the threads
// a mutex to lock and release threads
// an atomic bool value to help check that while true loop condition inside each thread
// a conditonal variable to help pause each thread and not waste cpu resources in the while loop

int ThreadPool::enqueued = 0;

struct ThreadPool::Impl {
  std::vector<std::thread> all_threads;
  std::queue<std::function<void()>> all_tasks;
  std::mutex threads_locker;
  std::atomic_bool stop;
  std::condition_variable condition;
  int thread_count;

  // void enqueue(std::function<void()> task);

  Impl(int thread_count) : stop(false), thread_count{thread_count} {
    // the goal of this is to spawn threads
    // make them run infinitely as long as the program exits (but to avoid 100% cpu usage everytime sleep the thread whenever
    // there is no task or the atomic bool is stop set to true with condition_variable)

    if (thread_count < 1) {
      std::cout << "You cannot have less than 1 thread in this threadpool... Changing thread_count to 1" << std::endl;
      thread_count = 1;
    }

    for (int i = 0; i < thread_count; i++) {
      // "this" keyword here will make me be able to use the struct properties easily
      all_threads.emplace_back([this]() {
        // make each of the thread in this vectors run infinitely till the struct object goes out of scope
        // or stop has been set to true by the destructor
        while (!stop) {
          // create a function pointer
          std::function<void()> task;

          {
            // use only std::unique_lock  to lock a mutex in this manner since we want condition.wait to either lock or release it
            // based on its condition (DO NOT USE std::lock_guard)
            std::unique_lock<std::mutex> lock(threads_locker);

            // this is where the thread will sleep if stop is false or there are no tasks to run
            // (for this !all_tasks.empty() will be the condition to make the thread continue its task most of the time)
            // the reason we check for if stop is true is because in the destructor we want to make sure all threads is awake
            // and complete running the while loop so if stop is true the next while loop iteration won't run
            // and we can join the threads in the destructor before exiting the program
            // condition.wait(lock) here acquire the lock when it is active and release when it is sleeping
            condition.wait(lock, [this]() { return stop || !all_tasks.empty(); });

            // check if program is asked to stop and all_task is empty and break the while loop
            // this won't run any function inside all_task again
            if (stop && all_tasks.empty()) {
              break;
            }

            // now we take the next task from the queue and run it
            // using move here is important because we do not need the function inside queue any longer
            // now the first element in the queue is empty
            if (!all_tasks.empty()) {
              task = std::move(all_tasks.front());
              all_tasks.pop();
            }
          }

          // is it important to run the task outside of the lock because running it inside will just further delay other threads
          // which is very bad
          task();
        }
      });
    }
  }

  // this is where each thread are pushed to the queue
  void enqueue(std::function<void()> task) {
    // make sure all_tasks is locked before use everywhere
    // this is for consistency
    {
      std::lock_guard<std::mutex> lock(threads_locker);
      all_tasks.push(std::move(task));
    }
    // ask a thread to handle this task if one or more threads are currently idle
    condition.notify_one();
  }

  // now handle the destructor
  ~Impl() {
    // make stop true (this is telling all the threads not to run anymore since their while loop would be false)
    stop = true;

    // notify all the threads since stop condition is now true
    condition.notify_all();

    // now join all the threads (each thread will join whenever the while loop breaks)
    std::cout << "waiting for all threads to clean up and join..." << std::endl;

    for (std::thread &worker : all_threads) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }
};

// initialize impl while calling the class constructor
ThreadPool::ThreadPool(int number_of_thread) { impl = std::make_unique<Impl>(number_of_thread); }

// push task to the threadpool
void ThreadPool::enqueue(std::function<void()> task) {
  // std::cout << "ThreadPool::enqueued: " << ThreadPool::enqueued++ << std::endl;
  this->impl->enqueue(std::move(task));
}

// this is the move constructor
// since move assignment will require the object be created first we do not need it
// I want to use just one single threadpool for this program
// so just transfer the impl struct to this new class
ThreadPool::ThreadPool(ThreadPool &&other) : impl(std::move(other.impl)) {}

ThreadPool::~ThreadPool() { std::cout << "Thread pool ended due to program exit" << std::endl; }
