#include <iostream>
#include <mutex>
#include <sync_tools/mutex.hpp>
#include <sync_tools/spsc_queue.hpp>
#include <thread>

int main()
{
    // Single‑producer / single‑consumer queue with 1 Ki elements
    SyncTools::BoundedSPSCQueue<int, 1024> q;
    SyncTools::Mutex print_mutex;  // Just to serialise std::cout

    std::thread producer(
        [&]
        {
            for (int i = 1; i <= 10; ++i)
            {
                q.try_push(i);
            }
            q.stop();  // tell consumer we're done
        });

    std::thread consumer(
        [&]
        {
            while (auto v = q.blocking_pop())
            {
                std::scoped_lock lk(print_mutex);
                std::cout << "got " << *v << '\n';
            }
        });

    producer.join();
    consumer.join();
}