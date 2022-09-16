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

    // One thread is main thread
    ThreadPool batchTP(pool.NWORKER - 1);

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
    std::vector<std::future<std::string>> queryOutputs;
    queryOutputs.reserve(batchTP.NWORKER);
    while (getline(cin, line))
    {
        if (line == "F")
        {
            for (unsigned i = 0; i < turn; ++i)
            {
                std::cout << queryOutputs[i].get();
            }

            turn = 0;
            queryOutputs.clear();

            continue;  // End of a batch
        }

        auto promise = std::make_shared<std::promise<std::string>>();
        queryOutputs.emplace_back(promise->get_future());

        batchTP.Submit(
            [promise, &joiner](std::string query) {
                QueryInfo i;
                i.parseQuery(query);

                promise->set_value(joiner.join(i));
            }, std::move(line));

        ++turn;
    }

    return 0;
}
