#include <cstdint>
#include <iostream>
#include <string>

using std::cout;
using std::cerr;
using std::endl;

std::uint64_t run(int numWorkers);

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        cerr << "usage: " << argv[0] << " <thread count>" << endl;
        return 1;
    }

    int numWorkers = 2;
    try
    {
        numWorkers = std::stoi(argv[1]);
    }
    catch(const std::exception& e)
    {
        cerr << "error: " << e.what() << endl;
        return 2;
    }

    if (numWorkers <= 0)
    {
        cerr << "error: invalid thread count" << endl;
        return 3;
    } 

    const auto count = run(numWorkers);

    cout << "#worker: " << numWorkers << " total: " << count << endl;
}
