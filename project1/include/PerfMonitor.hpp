#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
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
        return std::chrono::duration<double, std::milli>(
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
        inline static std::vector<Monitor*> monitors;

     public:
        Monitor(std::string name) : name_(std::move(name))
        {
            monitors.emplace_back(this);
        }

        void Update(const Timer& timer)
        {
            Update(timer.Elapsed());
        }

        void Update(double value)
        {
            auto old = sum.load();
            while (!sum.compare_exchange_weak(old, old + value))
                ;

            old = sumSq.load();
            while (!sumSq.compare_exchange_weak(old, old + value * value))
                ;

            ++count;
        }

        void Reset()
        {
            sum = 0;
            sumSq = 0;
            count = 0;
        }

        double Avg() const
        {
            return Sum() / Count();
        }

        double Std() const
        {
            const double mu = Avg();
            return std::sqrt(sumSq.load() / Count() - mu * mu);
        }

        double Sum() const
        {
            return sum.load();
        }

        uint64_t Count() const
        {
            return count.load();
        }

        const std::string& GetName() const
        {
            return name_;
        }

     private:
        std::atomic<double> sum{ 0 };
        std::atomic<double> sumSq{ 0 };
        std::atomic<uint64_t> count{ 0 };
        const std::string name_;
    };

    Monitor QueryParsingMonitor{ "Query parsing" };

    Monitor FilterScanMonitor{ "FilterScan::run" };

    Monitor JoinMonitor{ "Join::run" };
    Monitor JoinResolveMonitor{ "\tJoin resolve" };
    Monitor JoinBuildPhaseMonitor{ "\tJoin build phase" };
    Monitor JoinProbePhaseMonitor1{ "\tJoin probe phase1" };
    Monitor JoinProbePhaseMonitor2{ "\tJoin probe phase2" };
    Monitor JoinProbePhaseMonitor3{ "\tJoin probe phase2" };

    Monitor SelfJoinMonitor{ "SelfJoin::run" };

    Monitor ChecksumMonitor{ "Checksum::run" };

    Monitor QueuingDelayMonitor{ "Queuing delay" };

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
        for (auto monitor : Monitor::monitors)
        {
            std::cerr << monitor->GetName()
                      << " time(ms): avg=" << monitor->Avg()
                      << " std=" << monitor->Std() << " sum=" << monitor->Sum()
                      << "(" << monitor->Count() << ")" << std::endl;
        }
    }

 private:
    inline static PerfMonitor* instance_{ nullptr };
};