#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool final
{
 public:
    ThreadPool() : workers_(GetNumWorkers())
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

    static unsigned GetNumWorkers()
    {
        return std::thread::hardware_concurrency();
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
    void PushTask(Func&& f, Args&&... args)
    {
        auto task =
            std::bind(std::forward<Func>(f), std::forward<Args>(args)...);

        {
            std::scoped_lock lock(mutex_);
            tasks_.emplace(task);
        }

        std::atomic_fetch_add(&taskCounter_, 1);
        cv_.notify_one();
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
                tasks_.pop();
            }

            task();
            std::atomic_fetch_sub(&taskCounter_, 1);

            if (taskCounter_ == 0)
                waitCv_.notify_all();
        }
    }

 private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

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

template <class IndexT, typename Func>
inline void parallel_for_nonblock(IndexT begin, IndexT end, Func&& f,
                                  unsigned minBlockSize = 1, unsigned numWorkers = 0)
{
    const IndexT totalSize = end - begin;
    numWorkers = numWorkers == 0 ? ThreadPool::GetNumWorkers() : numWorkers;

    const unsigned desiredNumBlocks = totalSize / minBlockSize;
    const unsigned numBlocks =
        std::min(desiredNumBlocks, numWorkers);

    if (numBlocks <= 1)
    {
        f(begin, end);
        return;
    }

    const unsigned blockSize = totalSize / numBlocks;

    for (IndexT blockID = 0; blockID < numBlocks; ++blockID)
    {
        const auto blockBegin = begin + blockID * blockSize;
        const auto blockEnd =
            (blockID == numBlocks - 1) ? end : blockBegin + blockSize;

        ThreadPool::Get().PushTask(
            [blockBegin, blockEnd, f]() { f(blockBegin, blockEnd); });
    }
}

template <class IndexT, typename Func>
inline void parallel_for(IndexT begin, IndexT end, Func&& f, unsigned minBlockSize = 64, unsigned numWorkers = 0)
{
    parallel_for_nonblock(begin, end, std::forward<Func>(f), minBlockSize, numWorkers);

    ThreadPool::Get().WaitAll();
}
