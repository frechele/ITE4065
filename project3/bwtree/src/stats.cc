#include "stats.h"

#include <numeric>

constexpr double ztable[] = {
#include "tdist.def"
};
constexpr std::size_t ztable_size = sizeof(ztable) / sizeof(ztable[0]);

namespace bwtree
{
double LookupZTable(std::uint64_t n)
{
    if (n < 1)
    {
        return std::numeric_limits<double>::infinity();
    }

    if (n > ztable_size)
    {
        return ztable[ztable_size - 1];
    }

    return ztable[n - 1];
}
}
