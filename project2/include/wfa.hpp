#pragma once

#include <cstdint>
#include <vector>

using SnapT = std::vector<std::int64_t>;

class StampedSnap final
{
 public:
    using Arr = std::vector<StampedSnap>;

 public:
    StampedSnap(int capacity, std::int64_t value);
    StampedSnap(std::uint64_t stamp, std::int64_t value, SnapT snap);

    StampedSnap(const StampedSnap&) = default;
    StampedSnap& operator=(const StampedSnap&) = default;

    std::uint64_t Stamp;
    std::int64_t Value;
    SnapT Snap;
};

class WFASnapshot final
{
 public:
    WFASnapshot(int capacity, std::int64_t initValue);

    int GetCapacity() const;

    void Update(int rank, std::int64_t value);
    SnapT Scan() const;

 private:
    StampedSnap::Arr collect() const;

 private:
    StampedSnap::Arr table_;
};
