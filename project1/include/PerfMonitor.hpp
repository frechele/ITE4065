#pragma once

#include <atomic>
#include <chrono>
#include <iostream>

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

    double ElapsedMilliseconds() const
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
 private:
    class AtomicAverage final
    {
     public:
        void add(double value)
        {
            auto old = sum.load();
            while (!sum.compare_exchange_weak(old, old + value))
                ;
            ++count;
        }

        void reset()
        {
            sum = 0;
            count = 0;
        }

        double get() const
        {
            return sum.load() / count.load();
        }

     private:
        std::atomic<double> sum{ 0 };
        std::atomic<uint64_t> count{ 0 };
    };

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

    void MonitorJoinBuildTime(double value)
    {
        joinBuildTime_.add(value);
    }

    void MonitorJoinResolveTime(double value)
    {
        joinResolveTime_.add(value);
    }

    void MonitorJoinBuildPhaseTime(double value)
    {
        joinBuildPhaseTime_.add(value);
    }

    void MonitorJoinProbePhaseQueinigDelay(double value)
    {
        joinProbePhaseQueinigDelay_.add(value);
    }

    void MonitorJoinProbePhaseTime(double value)
    {
        joinProbePhaseTime_.add(value);
    }

    void DumpMonitor() const
    {
        const double joinBuildTime = joinBuildTime_.get();
        const double joinResolveTime = joinResolveTime_.get();
        const double joinBuildPhaseTime = joinBuildPhaseTime_.get();
        const double joinProbePhaseQueinigDelay =
            joinProbePhaseQueinigDelay_.get();
        const double joinProbePhaseTime = joinProbePhaseTime_.get();
        const double totalTime = joinBuildTime + joinResolveTime +
                                 joinBuildPhaseTime + joinProbePhaseTime;

        std::cerr << "Join build time: " << joinBuildTime << " ms ("
                  << (100 * joinBuildTime / totalTime) << "%)" << std::endl;
        std::cerr << "Join resolve time: " << joinResolveTime << " ms ("
                  << (100 * joinResolveTime / totalTime) << "%)" << std::endl;
        std::cerr << "Join build phase time: " << joinBuildPhaseTime << " ms ("
                  << (100 * joinBuildPhaseTime / totalTime) << "%)"
                  << std::endl;
        std::cerr << "Join probe phase queinig delay: "
                  << joinProbePhaseQueinigDelay << " ms ("
                  << (100 * joinProbePhaseQueinigDelay / totalTime) << "%)"
                  << std::endl;
        std::cerr << "Join probe phase time: " << joinProbePhaseTime << " ms ("
                  << (100 * joinProbePhaseTime / totalTime) << "%)"
                  << std::endl;
    }

 private:
    inline static PerfMonitor* instance_{ nullptr };

    AtomicAverage joinBuildTime_;
    AtomicAverage joinResolveTime_;
    AtomicAverage joinBuildPhaseTime_;
    AtomicAverage joinProbePhaseQueinigDelay_;
    AtomicAverage joinProbePhaseTime_;
};
