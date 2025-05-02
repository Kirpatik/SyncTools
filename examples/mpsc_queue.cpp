#include <iostream>
#include <sync_tools/mpsc_queue.hpp>
#include <thread>
#include <vector>

struct LogMsg
{
    std::string text;
};

constexpr size_t QUEUE_CAP = 1 << 12;
SyncTools::BoundedMPSCQueue<LogMsg, QUEUE_CAP> logq;

void logger()
{
    while (auto m = logq.blocking_pop())
    {
        std::cout << m->text << '\n';
    }
}

void worker(int id)
{
    for (int i = 0; i < 1000; ++i)
    {
        logq.blocking_push(LogMsg{"msg " + std::to_string(id) + ":" + std::to_string(i)});
    }
}

int main()
{
    std::thread consumer(logger);
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i)
    {
        producers.emplace_back(worker, i);
    }
    for (auto& t : producers)
    {
        t.join();
    }
    logq.stop();
    consumer.join();
}