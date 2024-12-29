#ifndef __THREAD_POOL_H_
#define __THREAD_POOL_H_

#include <functional>
#include <future>
#include <thread>
#include <vector>
#include <queue>
#include <memory>
#include <stdexcept>
#include <mutex>
#include <condition_variable>
#include <atomic>


class ThreadPool
{
public:
    // 删除拷贝和移动操作符
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ThreadPool(size_t threads):stop(false)
    {
        for(size_t i = 0;i<threads;++i)
        {
            workers.emplace_back(
                [this] 
                {
                    for(;;)
                    {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(this->queue_mutex);
                            this->condition.wait(lock,[this]{return this->stop || !this->tasks.empty();});
                            if(this->stop.load() && this->tasks.empty())  return;

                            task = std::move(tasks.front());
                            tasks.pop();
                        }
                        if(task) task();
                    }
                }
            );
        }
    }
    template<class F,class... Args>
    auto addTask(F&&f,Args&&... args)->std::future<typename std::invoke_result_t<F,Args...>>
    {   
            // 不允许在关闭后加入新的任务
        if (stop.load()) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        using return_type = typename std::invoke_result_t<F,Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(this->queue_mutex);
            tasks.emplace([task](){(*task)();});
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() 
    {
        stop.store(true);
        condition.notify_all();
        for (std::thread &worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }


private:
    std::atomic<bool> stop;
    std::vector<std::thread>  workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
};


#endif