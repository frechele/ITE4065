#include "wfa.hpp"

StampedSnap::StampedSnap(std::int64_t value) : Stamp(0), Value(value)
{
}

StampedSnap::StampedSnap(std::uint64_t stamp, std::int64_t value, SnapT snap)
    : Stamp(stamp), Value(value), Snap(std::move(snap))
{
}

WFASnapshot::WFASnapshot(int capacity, std::int64_t initValue)
    : table_(capacity, initValue)
{
}

int WFASnapshot::GetCapacity() const
{
    return table_.size();
}

void WFASnapshot::Update(int rank, std::int64_t value)
{

}

SnapT WFASnapshot::Scan()
{
    return SnapT();
}
