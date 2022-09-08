#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool final
{
 public:
    ThreadPool() : workers_(std::thread::hardware_concurrency())
    {
        instance_ = this;

        for (auto& worker : workers_)
        {
            worker = std::thread(&ThreadPool::workerThread, this);
        }
    }

    ~ThreadPool() noexcept
    {
        {
            std::scoped_lock lock(mutex_);
            running_ = false;
        }
        cv_.notify_all();

        for (auto& worker : workers_)
            if (worker.joinable())
                worker.join();
    }

    static ThreadPool& Get()
    {
        return *instance_;
    }

    void WaitAll()
    {
        std::unique_lock lock(waitMutex_);
        waitCv_.wait(lock, [this]() { return taskCounter_ == 0; });
    }

    template <typename Func, typename... Args>
    std::future<void> PushTask(Func&& f, Args&&... args)
    {
        auto task = std::make_shared<std::packaged_task<void()>>(
            std::bind(std::forward<Func>(f), std::forward<Args>(args)...));

        auto future = task->get_future();

        {
            std::scoped_lock lock(mutex_);
            tasks_.emplace_back([task]() { (*task)(); });
            std::atomic_fetch_add(&taskCounter_, 1);
        }

        cv_.notify_all();

        return future;
    }

 private:
    void workerThread()
    {
        while (true)
        {
            std::function<void()> task;

            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return !tasks_.empty() || !running_; });
                if (tasks_.empty() && !running_)
                    break;

                task = std::move(tasks_.front());
                tasks_.pop_front();
            }

            task();
            std::atomic_fetch_sub(&taskCounter_, 1);

            if (taskCounter_ == 0)
                waitCv_.notify_all();
        }
    }

 private:
    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> tasks_;

    // Thread control variables
    bool running_{ true };
    std::mutex mutex_;
    std::condition_variable cv_;

    // Waiting task variables
    std::atomic<int> taskCounter_{ 0 };
    std::mutex waitMutex_;
    std::condition_variable waitCv_;

    inline static ThreadPool* instance_{ nullptr };
};
