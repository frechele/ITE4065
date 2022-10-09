#pragma once

#include <cstdint>
#include <vector>

using SnapT = std::vector<std::int64_t>;

class StampedSnap final
{
 public:
    StampedSnap(std::int64_t value);
    StampedSnap(std::uint64_t stamp, std::int64_t value, SnapT snap);

    const std::uint64_t Stamp;
    const std::int64_t Value;
    const SnapT Snap;
};

class WFASnapshot final
{
 public:
    WFASnapshot(int capacity, std::int64_t initValue);

    int GetCapacity() const;

    void Update(int rank, std::int64_t value);
    SnapT Scan();

 private:
    std::vector<StampedSnap> table_;
};
