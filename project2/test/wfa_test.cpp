#include "gtest/gtest.h"

#include "wfa.hpp"

#include <random>
#include <vector>
#include <thread>

TEST(StampedSnap, Initialize)
{
    std::random_device rd;
    std::mt19937 engine(rd());

    const std::int64_t value = engine();

    StampedSnap snap(value);

    EXPECT_EQ(snap.Stamp, 0);
    EXPECT_EQ(snap.Value, value);
    EXPECT_TRUE(snap.Snap.empty());

    const std::int64_t value2 = engine();
    SnapT tmp = { 1, 2, 3, 4 };

    StampedSnap snap2(10, value2, tmp);

    EXPECT_EQ(snap2.Stamp, 10);
    EXPECT_EQ(snap2.Value, value2);
    EXPECT_EQ(snap2.Snap, tmp);
}

TEST(WFASnapshot, Initialize)
{
    std::random_device rd;
    std::mt19937 engine(rd());

    const int capacity = 32;
    const std::int64_t initValue = engine();

    WFASnapshot wfa(capacity, initValue);

    EXPECT_EQ(wfa.GetCapacity(), capacity);

    SnapT snap = wfa.Scan();
    EXPECT_EQ(snap.size(), capacity);
    for (const std::int64_t v : snap)
    {
        EXPECT_EQ(v, initValue);
    }
}

TEST(WFASnapshot, SingleThread)
{
    std::random_device rd;
    std::mt19937 engine(rd());

    WFASnapshot wfa(1, 0);

    for (int i = 0; i < 10; ++i)
    {
        const std::int64_t value = engine();
        wfa.Update(0, value);

        EXPECT_EQ(wfa.Scan()[0], value);
    }
}

TEST(WFASnapshot, MultiThread)
{
    std::random_device rd;

    WFASnapshot wfa(4, 0);

    std::vector<std::thread> workers(4);
    for (int i = 0; i < 4; ++i)
    {
        workers[i] = std::thread([&wfa](int rank) {
            for (int j = 0; j < 10; ++j)
            {
                wfa.Update(rank, j * rank);
            }
        }, i);
    }

    for (auto& worker : workers)
    {
        if (worker.joinable())
            worker.join();
    }

    SnapT snap = wfa.Scan();
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(snap[i], 9 * i);
    }
}
