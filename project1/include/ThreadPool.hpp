#pragma once

#include <cassert>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <thread>
#include <vector>

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

    ThreadPool() : NWORKER(std::thread::hardware_concurrency() * 2)
    {
        assert(instance_ == nullptr);
        instance_ = this;

        createWorkers();
    }

    ~ThreadPool() noexcept
    {
        assert(instance_ == this);

        destroyWorkers();
        instance_ = nullptr;
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

template <class IndexT>
void get_parallel_size(IndexT begin, IndexT end, unsigned& blockCount, unsigned& blockSize)
{
    const unsigned workSize = end - begin;
    blockCount = ThreadPool::Get().NWORKER;
    blockSize = workSize / blockCount;

    if (blockSize == 0)
    {
        blockSize = 1;
        blockCount = workSize;
    }
}


template <class IndexT, typename Func, typename... Args>
void parallel_for(IndexT begin, IndexT end, Func&& f, Args&&... args)
{
    unsigned blockCount, blockSize;
    get_parallel_size(begin, end, blockCount, blockSize);

    std::vector<TaskFuture> futures(blockCount);
    for (unsigned blockID = 0; blockID < blockCount; ++blockID)
    {
        const unsigned blockBegin = begin + blockID * blockSize;
        const unsigned blockEnd =
            (blockID == blockCount - 1) ? end : blockBegin + blockSize;

        futures[blockID] = ThreadPool::Get().Submit(f, blockID, blockBegin, blockEnd);
    }

    for (auto& future : futures)
        future.wait();
}
