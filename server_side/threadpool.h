#include <functional>

#pragma once

class ThreadPool {
  public:
  ThreadPool(int number_of_threads);
  ~ThreadPool();

  void enqueue(std::function<void()> task);

  // avoid copy constuctor
  ThreadPool(const ThreadPool &) = delete;

  // avoid copy assignment
  ThreadPool &operator=(const ThreadPool &) = delete;

  // allow move constructor
  ThreadPool(ThreadPool &&);

  // avoid move assignment
  // I want only one threadpool to exist at a time
  ThreadPool &operator=(ThreadPool &&) = delete;

  // I am going to use pointer to implementation here to acheive this logic because of the move constructor
  // doing it this way it the easiest what I just need to do is create a new std::unique_ptr<Impl> impl; in the move constructor
  private:
  struct Impl;
  std::unique_ptr<Impl> impl;
  static int enqueued;
};
