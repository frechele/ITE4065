#include <Operators.hpp>

#include <atomic>
#include <cassert>
#include <iostream>

#include <PerfMonitor.hpp>
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
void FilterScan::runSequential()
// Run
{
    for (uint64_t i = 0; i < relation.size; ++i)
    {
        bool pass = true;
        for (auto& f : filters)
        {
            pass &= applyFilter(i, f);
        }
        if (pass)
            copy2Result(i);
    }
}

void FilterScan::run()
{
    ScopedMonitor monitor(PerfMonitor::Get().FilterScanMonitor);

    runSequential();
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
void Join::runSequential()
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
    auto rightKeyColumn = rightInputData[rightColId];
    for (uint64_t i = 0, limit = i + right->resultSize; i != limit; ++i)
    {
        auto rightKey = rightKeyColumn[i];
        auto range = hashTable.equal_range(rightKey);
        for (auto iter = range.first; iter != range.second; ++iter)
        {
            copy2Result(iter->second, i);
        }
    }
}

void Join::run()
{
    ScopedMonitor monitor(PerfMonitor::Get().JoinMonitor);

    runSequential();
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
void SelfJoin::processInput()
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
}

void SelfJoin::runSequential()
// Run
{
    processInput();

    auto leftColId = input->resolve(pInfo.left);
    auto rightColId = input->resolve(pInfo.right);

    auto leftCol = inputData[leftColId];
    auto rightCol = inputData[rightColId];
    for (uint64_t i = 0; i < input->resultSize; ++i)
    {
        if (leftCol[i] == rightCol[i])
            copy2Result(i);
    }

    std::cerr << input->resultSize << std::endl;
}

void SelfJoin::runParallel()
{
    processInput();

    auto leftColId = input->resolve(pInfo.left);
    auto rightColId = input->resolve(pInfo.right);

    auto leftCol = inputData[leftColId];
    auto rightCol = inputData[rightColId];

    auto bi = BlockInfo::CreateMinBlock(0, input->resultSize, 1 << 9);
    std::vector<std::vector<std::vector<std::uint64_t>>> subResults(
        bi.blockCount, tmpResults);

    std::atomic<uint64_t> atmResultSize = 0;

    parallel_for(bi, [this, &subResults, &atmResultSize, &leftCol, &rightCol](
                         unsigned rank, uint64_t beginIdx, uint64_t endIdx) {
        auto& subResult = subResults[rank];

        uint64_t localCounter = 0;

        for (uint64_t i = beginIdx; i < endIdx; ++i)
        {
            if (leftCol[i] == rightCol[i])
            {
                for (unsigned cId = 0; cId < copyData.size(); ++cId)
                {
                    subResult[cId].push_back(copyData[cId][i]);
                }

                ++localCounter;
            }
        }

        atmResultSize += localCounter;
    });

    for (auto& subResult : subResults)
    {
        for (unsigned cId = 0; cId < copyData.size(); ++cId)
        {
            tmpResults[cId].insert(end(tmpResults[cId]), begin(subResult[cId]),
                                   end(subResult[cId]));
        }
    }

    resultSize = atmResultSize.load();
}

void SelfJoin::run()
{
    ScopedMonitor monitor(PerfMonitor::Get().SelfJoinMonitor);

    runParallel();
}
//---------------------------------------------------------------------------
void Checksum::processInput()
{
    for (auto& sInfo : colInfo)
    {
        input->require(sInfo);
    }
    input->run();
}

void Checksum::processOutputParallel()
{
    auto results = input->getResults();

    checkSums.resize(colInfo.size());

    // colInfo is quite small,
    // bottleneck is summation
    for (uint64_t i = 0; i < colInfo.size(); ++i)
    {
        auto& sInfo = colInfo[i];
        auto colId = input->resolve(sInfo);
        auto resultCol = results[colId];

        auto bi = BlockInfo::CreateMinBlock(0, input->resultSize, 1 << 10);
        std::vector<uint64_t> partialSums(bi.blockCount);
        parallel_for(bi,
                     [this, resultCol, &partialSums](
                         unsigned rank, uint64_t beginIdx, uint64_t endIdx) {
                         uint64_t sum = 0;

                         for (uint64_t i = beginIdx; i < endIdx; ++i)
                         {
                             sum += resultCol[i];
                         }

                         partialSums[rank] = sum;
                     });

        uint64_t sum = 0;
        for (auto partialSum : partialSums)
        {
            sum += partialSum;
        }
        checkSums[i] = sum;
    }

    resultSize = input->resultSize;
}

void Checksum::runSequential()
// Run
{
    processInput();

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

void Checksum::runParallel()
{
    processInput();
    processOutputParallel();
}

void Checksum::run()
{
    ScopedMonitor monitor(PerfMonitor::Get().ChecksumMonitor);

    runParallel();
}
//---------------------------------------------------------------------------