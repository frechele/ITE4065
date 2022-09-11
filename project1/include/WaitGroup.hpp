#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <memory>

class WaitGroup final
{
 public:
    using Ptr = std::shared_ptr<WaitGroup>;

    static Ptr ZeroGroup()
    {
        return std::make_shared<WaitGroup>();
    }

    WaitGroup(int count = 0) : counter_(count)
    {
    }

    WaitGroup& operator=(WaitGroup&&) = default;

    void Done()
    {
        bool awake = false;

        {
            std::scoped_lock lock(mutex_);
            --counter_;

            awake = (counter_ == 0);
        }

        if (awake)
        {
            cv_.notify_all();
        }
    }

    void Wait()
    {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this]() { return counter_ == 0; });
    }

 private:
    int counter_;
    std::condition_variable cv_;
    std::mutex mutex_;
};
