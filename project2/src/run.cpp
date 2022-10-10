#include "wfa.hpp"

#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <vector>
#include <thread>
#include <memory>
#include <random>
#include <mutex>

#include <iostream>

std::mutex mut;
std::condition_variable cv;
bool flag;
int readyCounter;

std::vector<std::uint64_t> counter;
std::random_device rd;

void worker(int rank, WFASnapshot& wfa)
{
    std::mt19937 engine(rd());

    {
        std::unique_lock lock(mut);
        --readyCounter;
        cv.wait(lock, [] { return flag; });
    }

    while (flag)
    {
        const std::int64_t value = engine();
        wfa.Update(rank, value);

        ++counter[rank];
    }
}

std::uint64_t run(int numWorkers)
{
    WFASnapshot wfa(numWorkers, 0);

    counter.resize(numWorkers);
    readyCounter = numWorkers;

    std::vector<std::thread> workers(numWorkers);
    for (int rank = 0; rank < numWorkers; ++rank)
    {
        counter[rank] = 0;
        workers[rank] = std::thread(worker, rank, std::ref(wfa));
    }

    while (true)
    {
        std::scoped_lock lock(mut);
        if (readyCounter == 0)
        {
            flag = true;
            cv.notify_all();
            break;
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(60));

    flag = false;

    std::vector<std::uint64_t> counterSnap(counter);
    std::uint64_t total = 0;
    for (const auto v : counterSnap)
        total += v;

    for (auto& worker : workers)
    {
        if (worker.joinable())
            worker.join();
    }

    return total;
}
