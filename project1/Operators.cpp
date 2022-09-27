#include <Operators.hpp>

#include <memory.h>
#include <atomic>
#include <cassert>
#include <iostream>

#include <ThreadPool.hpp>
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
bool Scan::require(SelectInfo info)
// Require a column and add it to results
{
    if (info.binding != relationBinding)
        return false;
    assert(info.colId < relation.columns.size());
    resultColumns.push_back(relation.columns[info.colId]);
    select2ResultColId[info] = resultColumns.size() - 1;
    return true;
}
//---------------------------------------------------------------------------
void Scan::run()
// Run
{
    // Nothing to do
    resultSize = relation.size;
}
//---------------------------------------------------------------------------
vector<uint64_t*> Scan::getResults()
// Get materialized results
{
    return resultColumns;
}
//---------------------------------------------------------------------------
bool FilterScan::require(SelectInfo info)
// Require a column and add it to results
{
    if (info.binding != relationBinding)
        return false;
    assert(info.colId < relation.columns.size());
    if (select2ResultColId.find(info) == select2ResultColId.end())
    {
        // Add to results
        inputData.push_back(relation.columns[info.colId]);
        tmpResults.emplace_back();
        unsigned colId = tmpResults.size() - 1;
        select2ResultColId[info] = colId;
    }
    return true;
}
//---------------------------------------------------------------------------
void FilterScan::copy2Result(uint64_t id)
// Copy to result
{
    for (unsigned cId = 0; cId < inputData.size(); ++cId)
        tmpResults[cId].push_back(inputData[cId][id]);
    ++resultSize;
}
//---------------------------------------------------------------------------
bool FilterScan::applyFilter(uint64_t i, FilterInfo& f)
// Apply filter
{
    auto compareCol = relation.columns[f.filterColumn.colId];
    auto constant = f.constant;
    switch (f.comparison)
    {
        case FilterInfo::Comparison::Equal:
            return compareCol[i] == constant;
        case FilterInfo::Comparison::Greater:
            return compareCol[i] > constant;
        case FilterInfo::Comparison::Less:
            return compareCol[i] < constant;
    };
    return false;
}
//---------------------------------------------------------------------------
void FilterScan::run()
// Run
{
    BlockInfo bi(0, relation.size);

    std::atomic<uint64_t> atmResultSize = 0;
    std::vector<std::vector<std::vector<uint64_t>>> subResults(bi.blockCount,
                                                               tmpResults);

    parallel_for(bi, [this, &atmResultSize, &subResults](
                         unsigned rank, uint64_t begin, uint64_t end) {
        uint64_t localResultSize = 0;
        for (unsigned i = begin; i < end; ++i)
        {
            bool pass = true;
            for (auto& f : filters)
            {
                pass &= applyFilter(i, f);
            }
            if (pass)
            {
                for (unsigned cId = 0; cId < inputData.size(); ++cId)
                    subResults[rank][cId].push_back(inputData[cId][i]);
                ++localResultSize;
            }
        }
        std::atomic_fetch_add(&atmResultSize, localResultSize);
    });

    for (const auto& subResult : subResults)
    {
        const unsigned totalSize = tmpResults.size();
        for (unsigned i = 0; i < totalSize; ++i)
            tmpResults[i].insert(end(tmpResults[i]), begin(subResult[i]),
                                 end(subResult[i]));
    }

    resultSize = atmResultSize.load();
}
//---------------------------------------------------------------------------
vector<uint64_t*> Operator::getResults()
// Get materialized results
{
    vector<uint64_t*> resultVector;
    for (auto& c : tmpResults)
    {
        resultVector.push_back(c.data());
    }
    return resultVector;
}
//---------------------------------------------------------------------------
bool Join::require(SelectInfo info)
// Require a column and add it to results
{
    if (requestedColumns.count(info) == 0)
    {
        bool success = false;
        if (left->require(info))
        {
            requestedColumnsLeft.emplace_back(info);
            success = true;
        }
        else if (right->require(info))
        {
            success = true;
            requestedColumnsRight.emplace_back(info);
        }
        if (!success)
            return false;

        tmpResults.emplace_back();
        requestedColumns.emplace(info);
    }
    return true;
}
//---------------------------------------------------------------------------
void Join::copy2Result(uint64_t leftId, uint64_t rightId)
// Copy to result
{
    unsigned relColId = 0;
    for (unsigned cId = 0; cId < copyLeftData.size(); ++cId)
        tmpResults[relColId++].push_back(copyLeftData[cId][leftId]);

    for (unsigned cId = 0; cId < copyRightData.size(); ++cId)
        tmpResults[relColId++].push_back(copyRightData[cId][rightId]);
    ++resultSize;
}
//---------------------------------------------------------------------------
void Join::run()
// Run
{
    left->require(pInfo.left);
    right->require(pInfo.right);

    left->run();
    right->run();

    // Use smaller input for build
    if (left->resultSize > right->resultSize)
    {
        swap(left, right);
        swap(pInfo.left, pInfo.right);
        swap(requestedColumnsLeft, requestedColumnsRight);
    }

    auto leftInputData = left->getResults();
    auto rightInputData = right->getResults();

    // Resolve the input columns
    unsigned resColId = 0;
    for (auto& info : requestedColumnsLeft)
    {
        copyLeftData.push_back(leftInputData[left->resolve(info)]);
        select2ResultColId[info] = resColId++;
    }
    for (auto& info : requestedColumnsRight)
    {
        copyRightData.push_back(rightInputData[right->resolve(info)]);
        select2ResultColId[info] = resColId++;
    }

    auto leftColId = left->resolve(pInfo.left);
    auto rightColId = right->resolve(pInfo.right);

    // Build phase
    auto leftKeyColumn = leftInputData[leftColId];
    hashTable.reserve(left->resultSize * 2);
    for (uint64_t i = 0, limit = i + left->resultSize; i != limit; ++i)
    {
        hashTable.emplace(leftKeyColumn[i], i);
    }

    // Probe phase
    BlockInfo bi(0, right->resultSize);

    std::atomic<uint64_t> atmResultSize = 0;

    auto rightKeyColumn = rightInputData[rightColId];
    const uint64_t rightResultSize = right->resultSize;

    std::vector<std::vector<std::pair<uint64_t, uint64_t>>> matches(
        bi.blockCount);
    std::vector<uint64_t> offsets(bi.blockCount);

    parallel_for(bi, [this, rightKeyColumn, &matches, &atmResultSize,
                      rightResultSize,
                      &offsets](unsigned rank, uint64_t begin, uint64_t end) {
        uint64_t localResultSize = 0;
        matches[rank].reserve(end - begin);
        for (uint64_t i = begin; i < end; ++i)
        {
            auto rightKey = rightKeyColumn[i];
            auto range = hashTable.equal_range(rightKey);

            const uint64_t matchCount =
                std::distance(range.first, range.second);
            localResultSize += matchCount;

            for (auto it = range.first; it != range.second; ++it)
            {
                matches[rank].emplace_back(it->second, i);
            }
        }
        offsets[rank] = std::atomic_fetch_add(&atmResultSize, localResultSize);
    });

    resultSize = atmResultSize.load();
    if (resultSize == 0)
        return;

    const unsigned copyLeftSize = copyLeftData.size();
    const unsigned copyRightSize = copyRightData.size();
    const unsigned copyDataSize = copyLeftSize + copyRightSize;

    for (auto& tmpResult : tmpResults)
    {
        tmpResult.resize(resultSize);
    }

    parallel_for(bi, [this, &offsets, &matches, copyLeftSize, copyRightSize](
                         unsigned rank, uint64_t begin, uint64_t end) {
        const uint64_t offset = offsets[rank];

        uint64_t j = 0;
        for (auto it : matches[rank])
        {
            unsigned relColId = 0;
            for (unsigned cId = 0; cId < copyLeftSize; ++cId)
                tmpResults[relColId++][offset + j] =
                    copyLeftData[cId][it.first];

            for (unsigned cId = 0; cId < copyRightSize; ++cId)
                tmpResults[relColId++][offset + j] =
                    copyRightData[cId][it.second];

            ++j;
        }
    });
}
//---------------------------------------------------------------------------
void SelfJoin::copy2Result(uint64_t id)
// Copy to result
{
    for (unsigned cId = 0; cId < copyData.size(); ++cId)
        tmpResults[cId].push_back(copyData[cId][id]);
    ++resultSize;
}
//---------------------------------------------------------------------------
bool SelfJoin::require(SelectInfo info)
// Require a column and add it to results
{
    if (requiredIUs.count(info))
        return true;
    if (input->require(info))
    {
        tmpResults.emplace_back();
        requiredIUs.emplace(info);
        return true;
    }
    return false;
}
//---------------------------------------------------------------------------
void SelfJoin::run()
// Run
{
    input->require(pInfo.left);
    input->require(pInfo.right);
    input->run();
    inputData = input->getResults();

    for (auto& iu : requiredIUs)
    {
        auto id = input->resolve(iu);
        copyData.emplace_back(inputData[id]);
        select2ResultColId.emplace(iu, copyData.size() - 1);
    }

    auto leftColId = input->resolve(pInfo.left);
    auto rightColId = input->resolve(pInfo.right);

    BlockInfo bi(0, input->resultSize);

    std::atomic<uint64_t> atmResultSize = 0;
    std::vector<std::vector<std::vector<uint64_t>>> subResults(bi.blockCount,
                                                               tmpResults);

    auto leftCol = inputData[leftColId];
    auto rightCol = inputData[rightColId];
    const uint64_t copyDataSize = copyData.size();
    parallel_for(
        bi, [this, leftCol, rightCol, &atmResultSize, &subResults,
             copyDataSize](unsigned rank, uint64_t begin, uint64_t end) {
            uint64_t localResultSize = 0;
            for (uint64_t i = begin; i < end; ++i)
            {
                if (leftCol[i] == rightCol[i])
                {
                    for (unsigned cId = 0; cId < copyDataSize; ++cId)
                        subResults[rank][cId].push_back(copyData[cId][i]);
                    ++localResultSize;
                }
            }
            std::atomic_fetch_add(&atmResultSize, localResultSize);
        });

    resultSize = atmResultSize.load();

    for (auto& tmpResult : tmpResults)
        tmpResult.resize(resultSize);

    const unsigned totalSize = tmpResults.size();
    std::vector<uint64_t> offsets(totalSize);
    for (const auto& subResult : subResults)
    {
        for (unsigned i = 0; i < totalSize; ++i)
        {
            const uint64_t subSize = subResult[i].size();
            memcpy(&tmpResults[i][offsets[i]], subResult[i].data(),
                   subSize * sizeof(uint64_t));
            offsets[i] += subSize;
        }
    }
}
//---------------------------------------------------------------------------
void Checksum::run()
// Run
{
    for (auto& sInfo : colInfo)
    {
        input->require(sInfo);
    }
    input->run();
    auto results = input->getResults();

    checkSums.resize(colInfo.size());

    const uint64_t colInfoSize = colInfo.size();
    for (uint64_t i = 0; i < colInfoSize; ++i)
    {
        auto& sInfo = colInfo[i];
        auto colId = input->resolve(sInfo);
        auto resultCol = results[colId];

        std::atomic<uint64_t> sum = 0;

        BlockInfo bi(0, input->resultSize);
        parallel_for(bi, [this, resultCol, &sum] (unsigned rank, uint64_t beginIdx, uint64_t endIdx) {
            uint64_t localSum = 0;
            for (uint64_t i = beginIdx; i < endIdx; ++i)
                localSum += resultCol[i];
            std::atomic_fetch_add(&sum, localSum);
        });

        checkSums[i] = sum.load();
    }
    resultSize = input->resultSize;
}
//---------------------------------------------------------------------------
