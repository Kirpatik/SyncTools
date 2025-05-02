#include <atomic>
#include <cmath>
#include <iostream>
#include <sync_tools/spsc_queue.hpp>
#include <thread>

constexpr size_t CAP = 1024;
SyncTools::BoundedSPSCQueue<float, CAP> samples;
std::atomic<bool> running{true};

void producer()
{
    float phase = 0.0f;
    while (running.load())
    {
        float sample = std::sin(phase);
        phase += 0.01f;
        samples.blocking_push(sample);
    }
}

void consumer()
{
    while (running.load())
    {
        if (auto s = samples.blocking_pop())
        {
            std::cout << s.value() << '\n';
        }
    }
}

int main()
{
    std::thread prod(producer), cons(consumer);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    running.store(false);
    samples.stop();
    prod.join();
    cons.join();
}