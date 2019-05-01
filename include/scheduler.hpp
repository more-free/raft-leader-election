#ifndef scheduler_hpp
#define scheduler_hpp

#include <chrono>
#include <functional>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include "concurrent.hpp"
#include "utils.hpp"

namespace raft {
    namespace scheduler {
        using raft::utils::TimeUtils;

        class Task {
        public:
            using Callable = std::function<void()>;
            using IntervalSupplier = std::function<uint64_t()>;

            Task(IntervalSupplier interval, Callable callable, int max_calls) : 
                max_calls_(max_calls), interval_(interval), callable_(callable) {
                due_time_ = TimeUtils::now_ms() + interval();
            }
            
            ~Task() {}
            
            uint64_t due_time() const { return due_time_; }
            int max_calls() const { return max_calls_; }
            
            void call() {
                std::unique_lock<std::mutex> lock(mtx_);

                if (!cancelled() && !paused()) {
                    callable_();

                    if (max_calls_ > 0) {
                        max_calls_--;
                        if (max_calls_ == 0) {
                            cancel();
                        }
                    }
                }
            }

            void renew() {
                std::unique_lock<std::mutex> lock(mtx_);
                due_time_ = TimeUtils::now_ms() + interval_();
            }
            
            void cancel() {
                cancelled_.store(true);
            }
    
            void pause() {
                paused_.store(true);
            }

            void resume() {
                paused_.store(false);
            }

            bool cancelled() { return cancelled_.load(); }

            bool paused() { return paused_.load(); }
            
            IntervalSupplier interval() { return interval_; }
            Callable callable() { return callable_; }
            
        private:
            int max_calls_;
            IntervalSupplier interval_;
            Callable callable_;
            uint64_t due_time_;
            std::atomic<bool> cancelled_ { false } ;
            std::atomic<bool> paused_ { false };
            std::mutex mtx_;
        };

        class ScheduledTask {
        public:
            ScheduledTask(std::shared_ptr<Task> task, std::function<void(std::shared_ptr<Task>&)> task_replacer) :
                task_(task), task_replacer_(task_replacer) {}
            
            // cancel the task permenantly. the task will be removed from the 
            // scheduler and cannot be re-used.
            void cancel() {
                std::unique_lock<std::mutex> lock(mtx_);
                task_->cancel();
            }

            // pause the execution of the task (not the timer)
            void pause() {
                std::unique_lock<std::mutex> lock(mtx_);
                task_->pause();
            }

            // resume the execution of the task.
            void resume() {
                std::unique_lock<std::mutex> lock(mtx_);
                task_->resume();
            }

            // reset the timer of the task. ex., if the current task has an interval of 10s,
            // call this method will reset the timer back to 10s again, no matter how much
            // time elapsed.
            void reset() {
                std::unique_lock<std::mutex> lock(mtx_);

                bool cancelled = task_->cancelled();
                bool paused = task_->paused();
                task_->cancel();
                task_replacer_(task_);
                if (cancelled) {
                    task_->cancel();
                }
                if (paused) {
                    task_->pause();
                }
            }

            // return milliseconds before the time expires.
            uint64_t ttl() {
                return task_->due_time() - TimeUtils::now_ms();
            }
            
        private:
            std::mutex mtx_;
            std::shared_ptr<Task> task_;
            std::function<void(std::shared_ptr<Task>&)> task_replacer_;
        };

        using namespace std::chrono_literals;

        class Scheduler {
        public:
            using TaskPtr = std::shared_ptr<Task>;
            
            Scheduler() {
                auto latch = std::make_shared<raft::concurrent::CountDownLatch>(1);
                executor = std::make_unique<std::thread> ( [this, latch] {
                    uint64_t wait_time = 1000 * 60;
                    while (running.load()) {
                        std::unique_lock<std::mutex> lock(mtx);
                        latch->count_down();
                        cond.wait_for(lock, wait_time * 1ms, [this] { 
                            return !task_queue.empty() || !running.load(); 
                        });
                        if (!running.load()) break;
                        
                        uint64_t now = TimeUtils::now_ms();
                        if (now < task_queue.top()->due_time()) {
                            wait_time = task_queue.top()->due_time() - now;
                        } else {
                            execute_due_tasks();
                        }
                    }
                });
                latch->await();
            }
            
            ~Scheduler() {
                running.store(false);
                cond.notify_all();
                executor->join();
            }
            
            // run the scheduled task at give interval (in millisecond).
            std::shared_ptr<ScheduledTask> schedule(Task::IntervalSupplier interval, Task::Callable callable, int max_calls = -1) {
                TaskPtr task = run_after(interval, callable, max_calls);
                return std::make_shared<ScheduledTask>(
                    task,
                    [this] (std::shared_ptr<Task>& old_task) {
                        old_task = run_after(
                            old_task->interval(), 
                            old_task->callable(), 
                            old_task->max_calls());
                    }
                );
            }
            
        private:
            TaskPtr run_after(Task::IntervalSupplier interval, Task::Callable callable, int max_calls) {
                TaskPtr task = std::make_shared<Task>(interval, callable, max_calls);
                std::unique_lock<std::mutex> lock(mtx);
                task_queue.push(task);
                cond.notify_one();
                
                return task;
            }
            
            void execute_due_tasks() {
                if (!running.load()) {
                    return;
                }
                
                auto now = TimeUtils::now_ms();
                while (!task_queue.empty() && task_queue.top()->due_time() <= now) {
                    auto next_expired = task_queue.top();
                    task_queue.pop();
                    
                    if (!next_expired->cancelled()) {
                        next_expired->call();
                        next_expired->renew();
                        task_queue.push(next_expired);
                    }
                }
            }
   
            std::atomic<bool> running { true };
            std::unique_ptr<std::thread> executor;
            std::mutex mtx;
            std::condition_variable cond;
            
            // lower due_time first.
            std::priority_queue<TaskPtr, std::vector<TaskPtr>, std::function<bool(TaskPtr, TaskPtr)>> task_queue { [](TaskPtr t1, TaskPtr t2) {
                return t1->due_time() > t2->due_time();
            }};
        };

    }
}

#endif