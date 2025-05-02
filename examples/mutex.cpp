#include <cstdio>
#include <mutex>
#include <sync_tools/mutex.hpp>
#include <thread>
#include <vector>

SyncTools::Mutex mtx;
int shared = 0;

void worker()
{
    for (int i = 0; i < 100'000; ++i)
    {
        std::scoped_lock lock{mtx};  // RAII wrapper works because Mutex satisfies BasicLockable
        ++shared;
    }
}

int main()
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back(worker);
    }
    for (auto& t : threads)
    {
        t.join();
    }
    std::printf("shared=%d\n", shared);
}