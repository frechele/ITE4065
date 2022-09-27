#pragma once

#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <thread>
#include <vector>

using TaskFuture = std::future<void>;

struct BlockInfo final
{
    BlockInfo(std::uint64_t begin_, std::uint64_t end_,
              unsigned minimumBlockSize = 1024)
        : begin(begin_), end(end_), workSize(end - begin)
    {
        assert(begin <= end);
        assert(minimumBlockSize > 0);

        blockCount = std::thread::hardware_concurrency();
        blockSize = workSize / blockCount;

        if (blockSize < minimumBlockSize)
        {
            blockCount = workSize / minimumBlockSize;
            blockSize = minimumBlockSize;
        }

        if (blockCount == 0)
        {
            blockCount = 1;
            blockSize = workSize;
        }
    }

    const std::uint64_t begin;
    const std::uint64_t end;
    const unsigned workSize;

    unsigned blockCount;
    unsigned blockSize;
};

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

    template <typename Func>
    std::vector<TaskFuture> ParallelFor(const BlockInfo& bi, Func&& f)
    {
        std::vector<TaskFuture> futures(bi.blockCount);

        mutex_.lock();
        for (unsigned blockID = 0; blockID < bi.blockCount; ++blockID)
        {
            const unsigned blockBegin = bi.begin + blockID * bi.blockSize;
            const unsigned blockEnd = (blockID == bi.blockCount - 1)
                                          ? bi.end
                                          : blockBegin + bi.blockSize;

            auto task = std::make_shared<std::packaged_task<void()>>(std::bind(
                std::forward<Func>(f), blockID, blockBegin, blockEnd));

            futures[blockID] = task->get_future();
            tasks_.emplace_back([task] { (*task)(); });
        }
        mutex_.unlock();

        cv_.notify_all();
        return futures;
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

template <typename Func, typename... Args>
void parallel_for(const BlockInfo& bi, Func&& f, Args&&... args)
{
    if (bi.blockCount == 1)
    {
        f(0, bi.begin, bi.end);
        return;
    }

    auto futures = ThreadPool::Get().ParallelFor(bi, std::forward<Func>(f));

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
