#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include <WaitGroup.hpp>

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

    template <typename Func, typename... Args>
    WaitGroup::Ptr PushTask(Func&& f, Args&&... args)
    {
        auto task =
            std::bind(std::forward<Func>(f), std::forward<Args>(args)...);
        auto wg = std::make_shared<WaitGroup>(1);

        {
            std::scoped_lock lock(mutex_);
            tasks_.emplace(std::make_pair(wg, task));
        }

        cv_.notify_one();

        return wg;
    }

    WaitGroup::Ptr BulkBegin(int jobCount)
    {
        auto wg = std::make_shared<WaitGroup>(jobCount);

        mutex_.lock();

        return wg;
    }

    template <typename Func, typename... Args>
    void BulkPushTask(WaitGroup::Ptr wg, Func&& f, Args&&... args)
    {
        auto task =
            std::bind(std::forward<Func>(f), std::forward<Args>(args)...);

        tasks_.emplace(std::make_pair(wg, task));
    }

    void BulkEnd()
    {
        mutex_.unlock();
        cv_.notify_all();
    }

 private:
    void workerThread()
    {
        while (true)
        {
            std::pair<WaitGroup::Ptr, std::function<void()>> taskInfo;

            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return !tasks_.empty() || !running_; });
                if (tasks_.empty() && !running_)
                    break;

                taskInfo = std::move(tasks_.front());
                tasks_.pop();
            }

            taskInfo.second();
            taskInfo.first->Done();
        }
    }

 private:
    std::vector<std::thread> workers_;
    std::queue<std::pair<WaitGroup::Ptr, std::function<void()>>> tasks_;

    // Thread control variables
    bool running_{ true };
    std::mutex mutex_;
    std::condition_variable cv_;

    inline static ThreadPool* instance_{ nullptr };
};

template <class IndexT, typename Func>
inline WaitGroup::Ptr parallel_for_nonblock(IndexT begin, IndexT end, Func&& f,
                                            unsigned minBlockSize = 1,
                                            unsigned numWorkers = 0)
{
    const IndexT totalSize = end - begin;
    numWorkers = numWorkers == 0 ? ThreadPool::GetNumWorkers() : numWorkers;

    const unsigned desiredNumBlocks = totalSize / minBlockSize;
    const unsigned numBlocks = std::min(desiredNumBlocks, numWorkers);

    if (numBlocks <= 1)
    {
        for (IndexT i = begin; i < end; ++i)
            f(i);
        return WaitGroup::ZeroGroup();
    }

    const unsigned blockSize = totalSize / numBlocks;

    auto wg = ThreadPool::Get().BulkBegin(numBlocks);

    for (IndexT blockID = 0; blockID < numBlocks; ++blockID)
    {
        const auto blockBegin = begin + blockID * blockSize;
        const auto blockEnd =
            (blockID == numBlocks - 1) ? end : blockBegin + blockSize;

        ThreadPool::Get().BulkPushTask(wg, [blockBegin, blockEnd, f]() {
            for (IndexT i = blockBegin; i < blockEnd; ++i)
                f(i);
        });
    }

    ThreadPool::Get().BulkEnd();

    return wg;
}

template <class IndexT, typename Func>
inline void parallel_for(IndexT begin, IndexT end, Func&& f,
                         unsigned minBlockSize = 64, unsigned numWorkers = 0)
{
    auto wg = parallel_for_nonblock(begin, end, std::forward<Func>(f),
                                    minBlockSize, numWorkers);

    wg->Wait();
}
