#ifndef concurrent_hpp
#define concurrent_hpp

#include <future>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace raft {
    namespace concurrent {
        template <typename T>
        class BlockingQueue {
        public:
            BlockingQueue() {
                quit.store(false);
            }
            
            ~BlockingQueue() {
                clear();
            }
            
            void put(const T& t) {
                std::unique_lock<std::mutex> lock(mtx);
                q.push(t);
                cond.notify_one();
            }
            
            void get(T& t) {
                std::unique_lock<std::mutex> lock(mtx);
                cond.wait(lock, [this] { return quit.load() || !q.empty(); });
                
                if (!quit.load()) {
                    t = q.front();
                    q.pop();
                }
            }
            
            void clear() {
                quit.store(true);
                cond.notify_all();
            }

            size_t size() {
                return q.size();
            }
            
        private:
            std::mutex mtx;
            std::condition_variable cond;
            std::queue<T> q;
            std::atomic<bool> quit;
        };

        class CountDownLatch {
        public:
            CountDownLatch(int count) : count(count) {}
            ~CountDownLatch() = default;
            CountDownLatch(const CountDownLatch&) = delete;
            CountDownLatch(CountDownLatch&&) = delete;
            CountDownLatch& operator=(const CountDownLatch&) = delete;
            
            void count_down() {
                std::unique_lock<std::mutex> lock (mutex);
                if (count > 0) {
                    count--;
                    cond.notify_all();
                }
            }
            
            void await() {
                std::unique_lock<std::mutex> lock (mutex);
                cond.wait(lock, [this] { return count <= 0; });
            }

        private:
            int count;
            std::mutex mutex;
            std::condition_variable cond;
        };
    }
}

#endif