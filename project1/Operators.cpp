#include <Operators.hpp>

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
    std::vector<std::vector<std::vector<uint64_t>>> subResults(bi.blockCount);
    for (auto& subResult : subResults)
        subResult.resize(tmpResults.size());

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
    std::vector<std::pair<HT::iterator, HT::iterator>> matches(
        right->resultSize);
    std::vector<uint64_t> matchCounts(right->resultSize);

    auto rightKeyColumn = rightInputData[rightColId];
    parallel_for(bi, [this, &rightKeyColumn, &matches, &atmResultSize, &matchCounts](
                         unsigned rank, uint64_t begin, uint64_t end) {
        uint64_t localResultSize = 0;
        for (uint64_t i = begin; i < end; ++i)
        {
            auto rightKey = rightKeyColumn[i];
            matches[i] = hashTable.equal_range(rightKey);

            const uint64_t matchCount = 
                std::distance(matches[i].first, matches[i].second);
            localResultSize += matchCount;

            if (i < right->resultSize - 1)
                matchCounts[i + 1] = matchCount;
        }
        std::atomic_fetch_add(&atmResultSize, localResultSize);
    });

    resultSize = atmResultSize.load();
    if (resultSize == 0)
        return;
    
    for (unsigned i = 1; i < right->resultSize - 1; ++i)
        matchCounts[i + 1] += matchCounts[i];

    const unsigned copyLeftSize = copyLeftData.size();
    const unsigned copyRightSize = copyRightData.size();
    const unsigned copyDataSize = copyLeftSize + copyRightSize;

    for (auto& tmpResult : tmpResults)
    {
        tmpResult.resize(resultSize);
    }

    parallel_for(bi, [this, &matches, copyLeftSize, copyRightSize, &matchCounts](
                         unsigned rank, uint64_t begin, uint64_t end) {
        for (uint64_t i = begin; i < end; ++i)
        {
            auto& range = matches[i];
            const auto startOffset = matchCounts[i];

            unsigned j = 0;
            for (auto iter = range.first; iter != range.second; ++iter, ++j)
            {
                unsigned relColId = 0;
                for (unsigned cId = 0; cId < copyLeftSize; ++cId)
                    tmpResults[relColId++][startOffset + j] =
                        copyLeftData[cId][iter->second];

                for (unsigned cId = 0; cId < copyRightSize; ++cId)
                    tmpResults[relColId++][startOffset + j] =
                        copyRightData[cId][i];
            }
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
    std::vector<std::vector<std::vector<uint64_t>>> subResults(bi.blockCount);
    for (auto& subResult : subResults)
        subResult.resize(tmpResults.size());

    auto leftCol = inputData[leftColId];
    auto rightCol = inputData[rightColId];
    parallel_for(bi, [this, &leftCol, &rightCol, &atmResultSize, &subResults](
                         unsigned rank, uint64_t begin, uint64_t end) {
        uint64_t localResultSize = 0;
        for (uint64_t i = begin; i < end; ++i)
        {
            if (leftCol[i] == rightCol[i])
            {
                for (unsigned cId = 0; cId < copyData.size(); ++cId)
                    subResults[rank][cId].push_back(copyData[cId][i]);
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
void Checksum::run()
// Run
{
    for (auto& sInfo : colInfo)
    {
        input->require(sInfo);
    }
    input->run();
    auto results = input->getResults();

    for (auto& sInfo : colInfo)
    {
        auto colId = input->resolve(sInfo);
        auto resultCol = results[colId];
        uint64_t sum = 0;
        resultSize = input->resultSize;
        for (auto iter = resultCol, limit = iter + input->resultSize;
             iter != limit; ++iter)
            sum += *iter;
        checkSums.push_back(sum);
    }
}
//---------------------------------------------------------------------------
