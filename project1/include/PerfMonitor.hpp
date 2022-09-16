#pragma once

#include <atomic>
#include <chrono>
#include <iostream>
#include <vector>

class Timer final
{
 public:
    Timer() : start_(std::chrono::high_resolution_clock::now())
    {
    }

    void Start()
    {
        start_ = std::chrono::high_resolution_clock::now();
    }

    double Elapsed() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - start_)
            .count();
    }

 private:
    std::chrono::high_resolution_clock::time_point start_;
};

class PerfMonitor final
{
 public:
    class Monitor final
    {
     public:
        Monitor(std::string name) : name_(std::move(name))
        {
        }

        void Update(double value)
        {
            auto old = sum.load();
            while (!sum.compare_exchange_weak(old, old + value))
                ;
            ++count;
        }

        void Reset()
        {
            sum = 0;
            count = 0;
        }

        double Get() const
        {
            return sum.load() / count.load();
        }

        const std::string& GetName() const
        {
            return name_;
        }

     private:
        std::atomic<double> sum{ 0 };
        std::atomic<uint64_t> count{ 0 };
        const std::string name_;
    };

    Monitor FilterScanMonitor{ "FilterScan::run" };
    Monitor JoinMonitor{ "Join::run" };
    Monitor SelfJoinMonitor{ "SelfJoin::run" };
    Monitor ChecksumMonitor{ "Checksum::run" };

 public:
    static PerfMonitor& Get()
    {
        return *instance_;
    }

    PerfMonitor()
    {
        instance_ = this;
    }

    ~PerfMonitor() noexcept
    {
        instance_ = nullptr;
    }

    void DumpMonitor() const
    {
        for (auto monitor : monitors_)
        {
            std::cerr << monitor->GetName() << " time: " << monitor->Get() << " ms" << std::endl;
        }
    }

 private:
    inline static PerfMonitor* instance_{ nullptr };

    std::vector<Monitor*> monitors_ {
        &FilterScanMonitor,
        &JoinMonitor,
        &SelfJoinMonitor,
        &ChecksumMonitor,
    };
};