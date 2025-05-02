#include <sync_tools/object_pool.hpp>
#include <thread>

struct Particle
{
    float pos[3];
    float vel[3];
};

SyncTools::ThreadCachedObjectPool<Particle> tpool{/*global*/ 1 << 16};

void simulate()
{
    Particle* p = tpool.acquire();
    if (!p)
    {
        return;  // pool exhausted
    }

    // … simulate particle …

    tpool.release(p);
}

int main()
{
    std::vector<std::thread> workers;
    for (int i = 0; i < std::thread::hardware_concurrency(); ++i)
    {
        workers.emplace_back(simulate);
    }
    for (auto& t : workers)
    {
        t.join();
    }
}