#include <iostream>
#include <memory>
#include <mutex>
#include <vector>

#include "Joiner.hpp"
#include "Parser.hpp"

#include "ThreadPool.hpp"

using namespace std;
//---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    ThreadPool pool;
    pool.SetAsMainPool();

    ThreadPool batchTP(20);

    Joiner joiner;
    // Read join relations
    string line;
    while (getline(cin, line))
    {
        if (line == "Done")
            break;
        joiner.addRelation(line.c_str());
    }
    // Preparation phase (not timed)
    // Build histograms, indexes,...
    //
    unsigned turn = 0;
    std::vector<std::string> queryOutputs(batchTP.NWORKER);
    std::vector<TaskFuture> futures(batchTP.NWORKER);
    while (getline(cin, line))
    {
        const bool endOfBatch = (line == "F");
        const bool needFlush = (turn == batchTP.NWORKER) || endOfBatch;
        if (needFlush)
        {
            for (unsigned i = 0; i < turn; ++i)
            {
                futures[i].wait();
                std::cout << queryOutputs[i];
            }

            turn = 0;
            if (endOfBatch)
                continue;  // End of a batch
        }

        auto i = std::make_shared<QueryInfo>();
        i->parseQuery(line);

        futures[turn] = batchTP.Submit(
            [i, &joiner](std::string& output) {
                output = joiner.join(*i);
            }, std::ref(queryOutputs[turn]));

        ++turn;
    }
    return 0;
}
