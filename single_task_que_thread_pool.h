#pragma once

#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

using namespace std;

template <typename T>
class SafeQue {
 public:
  SafeQue() {}
  SafeQue(SafeQue&&) {}
  ~SafeQue() {}

  bool isEmpty() {
    unique_lock<mutex> lock(mtx_);
    return que_.empty();
  }

  int size() const {
    unique_lock<mutex> lock(mtx_);
    return que_.size();
  }

  void add(T& _task) {
    unique_lock<mutex> lock(mtx_);
    que_.emplace(_task);
  }

  bool obtain(T& _task) {
    unique_lock<mutex> lock(mtx_);
    if (que_.empty()) return false;
    _task = move(que_.front());
    que_.pop();
    return true;
  }

 private:
  queue<T> que_;
  mutex mtx_;
};

class ThreadPool {
 public:
  explicit ThreadPool(const int _thread_size = 4)
      : threads_(vector<thread>(_thread_size)), shutdown_(false) {}

  // TODO why
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  ~ThreadPool() { shutdowm(); }

  /**
   * @brief 向线程池中构建工作线程
   */
  void init() {
    for (int i = 0; i < threads_.size(); i++) {
      threads_.at(i) = thread(ThreadWorker(this, i));
    }
  }

  /**
   * @brief 将所有可回收的线程进行join()
   */
  void shutdowm() {
    shutdown_ = true;
    cond_.notify_all();

    for (int i = 0; i < threads_.size(); i++) {
      if (threads_.at(i).joinable()) threads_.at(i).join();
    }
  }

  template <typename F, typename... Args>
  auto submit(F&& f, Args&&... args) -> future<decltype(f(args...))> {
    function<decltype(f(args...))()> func =
        bind(forward<F>(f), forward<Args>(args)...);

    auto task = make_shared<packaged_task<decltype(f(args...))()>>(func);

    function<void()> wrapper_func = [task]() { (*task)(); };

    que_.add(wrapper_func);
    // que_.add(move([task]() { (*task)(); }));
    cond_.notify_one();
    return task->get_future();
  }

 private:
  bool shutdown_;                  // 线程池是否关闭
  SafeQue<function<void()>> que_;  // 任务队列
  vector<thread> threads_;         // 工作线程队列
  mutex mtx_;                      // 线程休眠锁
  condition_variable cond_;  // 线程环境锁，让线程处于休眠 / 唤醒状态

  // 工作线程类
  class ThreadWorker {
   public:
    ThreadWorker(ThreadPool* _pool, const int _tid)
        : pool_(_pool), tid_(_tid) {}

    void operator()() {
      function<void()> func;
      // bool obtained;
      while (!pool_->shutdown_) {
        {
          unique_lock<mutex> lock(pool_->mtx_);
          if (pool_->que_.isEmpty()) {
            pool_->cond_.wait(lock);
          }
          // obtained = pool_->que_.obtain(func);
          // if (obtained) func();
          if (pool_->que_.obtain(func)) func();
        }
      }
    }

   private:
    int tid_;           // 线程id
    ThreadPool* pool_;  // 当前工作线程所属的线程池
  };
};
