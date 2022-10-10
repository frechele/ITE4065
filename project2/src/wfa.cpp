#include "wfa.hpp"

StampedSnap::StampedSnap(int capacity, std::int64_t value)
    : Stamp(0), Value(value), Snap(capacity, value)
{
}

StampedSnap::StampedSnap(std::uint64_t stamp, std::int64_t value, SnapT snap)
    : Stamp(stamp), Value(value), Snap(std::move(snap))
{
}

WFASnapshot::WFASnapshot(int capacity, std::int64_t initValue)
    : table_(capacity, StampedSnap(capacity, initValue))
{
}

int WFASnapshot::GetCapacity() const
{
    return table_.size();
}

void WFASnapshot::Update(int rank, std::int64_t value)
{
    SnapT snap = Scan();

    StampedSnap oldValue = table_[rank];
    StampedSnap newValue(oldValue.Stamp + 1, value, snap);
    table_[rank] = std::move(newValue);
}

SnapT WFASnapshot::Scan()
{
    StampedSnap::Arr oldCopy, newCopy;

    oldCopy = collect();

    const int capacity = GetCapacity();

    std::vector<int> moved(capacity);
    while (true)
    {
        newCopy = collect();
        bool pass = false;

        for (int i = 0; i < capacity; ++i)
        {
            if (oldCopy[i].Stamp != newCopy[i].Stamp)
            {
                if (moved[i])
                {
                    return newCopy[i].Snap;
                }

                moved[i] = true;
                oldCopy = newCopy;

                pass = true;
                break;
            }
        }

        if (!pass)
        {
            SnapT result(capacity);
            for (int i = 0; i < capacity; ++i)
            {
                result[i] = newCopy[i].Value;
            }
            return result;
        }
    }
}

StampedSnap::Arr WFASnapshot::collect()
{
    StampedSnap::Arr copy(table_);
    return copy;
}
