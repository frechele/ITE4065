#pragma once

#include <cstdint>
#include <vector>

using SnapT = std::vector<std::int64_t>;

class StampedSnap final
{
 public:
    using Arr = std::vector<StampedSnap>;

 public:
    StampedSnap() = default;
    StampedSnap(std::int64_t value);
    StampedSnap(std::uint64_t stamp, std::int64_t value, SnapT snap);

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
    SnapT Scan();

 private:
    StampedSnap::Arr collect();

 private:
    StampedSnap::Arr table_;
};
