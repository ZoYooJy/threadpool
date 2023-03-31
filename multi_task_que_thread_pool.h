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
class SafeQueWithCond {
 public:
  SafeQueWithCond() {}
  SafeQueWithCond(T&&) {}
  ~SafeQueWithCond() {}

  void push(const T& _task) {
    {
      unique_lock<mutex> lock(mtx_);
      que_.push(_task);
    }
    cond_.notify_one();
  }

  // 模版类具体化后，T是一个具体的类型，T&&不再是万能引用、而是右值
  void push(const T&& _task) {
    {
      unique_lock<mutex> lock(mtx_);
      que_.push(move(_task));
    }
    cond_.notify_one();
  }

  bool pop(T& _task) {
    unique_lock<mutex> lock(mtx_);
    // lambda的判断条件为真--继续执行；为假--解锁 & 阻塞当前线程
    cond_.wait(lock, [&]() { return que_.empty() || shutdown_; });
    if (que_.empty()) return false;
    _task = move(que_.front());
    que_.pop();
    return true;
  }

  bool isEmpty() const {
    unique_lock<mutex> lock(mtx_);
    return que_.empty();
  }

  int size() const {
    unique_lock<mutex> lock(mtx_);
    return que_.size();
  }

  void shutdowm() {
    {
      unique_lock<mutex> lock(mtx_);
      shutdown_ = true;
    }
    cond_.notify_all();
  }

 private:
  queue<T> que_;
  mutex mtx_;
  condition_variable cond_;
  bool shutdown_ = false;
};

class ThreadPool {
 public:
  using Task = function<void()>;

  explicit ThreadPool(size_t _thread_size)
      : thread_size_(_thread_size), ques_(_thread_size) {
    auto worker = [this](size_t id) {
      // 工作线程循环等待任务、从任务队列中取出并处理
      while (1) {
        Task task;
        if (!ques_[id].pop(task)) break;
        if (task) task();
      }
    };

    worker_threads_.reserve(_thread_size);
    for (size_t i = 0; i < _thread_size; i++) {
      // thread t(worker, i);
      // worker_threads_.push_back(t);
      worker_threads_.emplace_back(worker, i);
    }
  }

  ~ThreadPool() {
    for (auto& que : ques_) {
      que.shutdowm();
    }
    for (auto& worker : worker_threads_) {
      if (worker.joinable()) worker.join();
    }
  }

  // 随机将任务插入线程id对应任务队列
  int submit(Task _task, size_t _id = -1) {
    if (_task == nullptr) return -1;
    if (_id == -1) {
      _id = rand() % thread_size_;
      ques_[_id].push(move(_task));
    } else {
      assert(_id < thread_size_);
      ques_[_id].push(move(_task));
    }

    return 0;
  }

 private:
  // 一个线程对应一个任务队列
  vector<SafeQueWithCond<Task>> ques_;
  vector<thread> worker_threads_;
  size_t thread_size_;
};

void testFunc() { cout << "test multi task queue thread pool" << endl; }

void test() {
  ThreadPool pool(4);
  // 添加单次任务到任务队列、并唤醒一个工作线程进行处理
  // make_shared: 阻止packaged_task的拷贝操作（已禁止）
  auto task_ptr = make_shared<packaged_task<void()>>(testFunc);
  pool.submit([task_ptr = move(task_ptr)]() { (*task_ptr)(); });
  //   all to implicitly-deleted copy constructor
  //   auto task = packaged_task<void()>(testFunc);
  //   pool.submit([task = move(task)]() {});
}
