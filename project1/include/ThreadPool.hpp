#pragma once

#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <thread>
#include <vector>

#include <PerfMonitor.hpp>

using TaskFuture = std::future<void>;

class ThreadPool final
{
 public:
    static ThreadPool& Get()
    {
        return *instance_;
    }

 public:
    const int NWORKER;

    ThreadPool() : ThreadPool(std::thread::hardware_concurrency())
    {
    }

    ThreadPool(int workers) : NWORKER(workers)
    {
        createWorkers();
    }

    ~ThreadPool() noexcept
    {
        destroyWorkers();
        instance_ = nullptr;
    }

    void SetAsMainPool()
    {
        instance_ = this;
    }

    template <typename Func, typename... Args>
    TaskFuture Submit(Func&& f, Args&&... args)
    {
        auto task = std::make_shared<std::packaged_task<void()>>(
            std::bind(std::forward<Func>(f), std::forward<Args>(args)...));

        auto future = task->get_future();

        {
            std::scoped_lock lock(mutex_);
            tasks_.emplace_back([task]() { (*task)(); });
        }

        cv_.notify_one();

        return future;
    }

 private:
    void createWorkers()
    {
        running_ = true;

        for (int i = 0; i < NWORKER; ++i)
        {
            workers_.emplace_back(&ThreadPool::workerThread, this);
        }
    }

    void destroyWorkers()
    {
        running_ = false;
        cv_.notify_all();

        for (auto& worker : workers_)
            if (worker.joinable())
                worker.join();
    }

    void workerThread()
    {
        while (true)
        {
            std::packaged_task<void()> task;

            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return !running_ || !tasks_.empty(); });

                if (!running_ && tasks_.empty())
                    break;

                task = std::move(tasks_.front());
                tasks_.pop_front();
            }

            task();
        }
    }

 private:
    inline static ThreadPool* instance_{ nullptr };

    std::vector<std::thread> workers_;
    std::deque<std::packaged_task<void()>> tasks_;

    // thread pool controllers
    bool running_{ false };
    std::mutex mutex_;
    std::condition_variable cv_;
};

struct BlockInfo final
{
    static BlockInfo CreateUniformBlock(std::uint64_t begin, std::uint64_t end, unsigned blockSize)
    {
        unsigned blockCount;

        const uint64_t workSize = end - begin;

        if (blockSize > workSize)
        {
            blockSize = workSize;
            blockCount = 1;
        }
        else
        {
            blockCount = workSize / blockSize;
            if (workSize % blockSize != 0)
                ++blockCount;
        }
        return BlockInfo(begin, end, workSize, blockCount, blockSize);
    }

    static BlockInfo CreateMinBlock(std::uint64_t begin, std::uint64_t end, unsigned minBlockSize)
    {
        assert(minBlockSize > 0);

        const uint64_t workSize = end - begin;

        unsigned blockCount = ThreadPool::Get().NWORKER;
        unsigned blockSize = workSize / blockCount;

        if (blockSize < minBlockSize)
        {
            blockSize = minBlockSize;
            blockCount = workSize / blockSize;
        }

        if (blockCount == 0)
        {
            blockCount = 1;
            blockSize = workSize;
        }
        return BlockInfo(begin, end, workSize, blockCount, blockSize);
    }

    const std::uint64_t begin;
    const std::uint64_t end;
    const unsigned workSize;

    unsigned blockCount;
    unsigned blockSize;

 private:
    BlockInfo(std::uint64_t begin_, std::uint64_t end_, unsigned workSize_,
              unsigned blockCount_, unsigned blockSize_)
        : begin(begin_),
          end(end_),
          workSize(workSize_),
          blockCount(blockCount_),
          blockSize(blockSize_)
    {
        assert(begin <= end);
    }
};

template <typename Func, typename... Args>
void parallel_for(const BlockInfo& bi, Func&& f, Args&&... args)
{
    if (bi.blockCount == 1)
    {
        f(0, bi.begin, bi.end);
        return;
    }

    Timer timer;

    std::vector<TaskFuture> futures(bi.blockCount);
    for (unsigned blockID = 0; blockID < bi.blockCount; ++blockID)
    {
        const unsigned blockBegin = bi.begin + blockID * bi.blockSize;
        const unsigned blockEnd =
            (blockID == bi.blockCount - 1) ? bi.end : blockBegin + bi.blockSize;

        futures[blockID] =
            ThreadPool::Get().Submit(f, blockID, blockBegin, blockEnd);
    }

    PerfMonitor::Get().QueuingDelayMonitor.Update(timer);

    for (auto& future : futures)
        future.wait();
}

template <typename Func, typename... Args>
void parallel_for(std::uint64_t begin, std::uint64_t end, Func&& f,
                  Args&&... args)
{
    BlockInfo info(begin, end);
    parallel_for(info, std::forward<Func>(f), std::forward<Args>(args)...);
}
