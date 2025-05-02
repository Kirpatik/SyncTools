/*
 * Copyright 2025 Nikolai Libelt
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <benchmark/benchmark.h>
#include <tbb/concurrent_queue.h>

#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/queue.hpp>
#include <thread>

#include "mpsc_queue.hpp"

using namespace SyncTools;
using namespace std::chrono;

static constexpr size_t PRODUCERS = 12;
static constexpr size_t ITEMS_PER_PRODUCER = 1000000;
static constexpr size_t QUEUE_SIZE = 16 * 1024;

struct BoostQueue
{
    boost::lockfree::queue<size_t, boost::lockfree::capacity<QUEUE_SIZE>> q;

    BoostQueue() {}

    bool try_push(size_t v) { return q.push(v); }

    bool try_pop(size_t& out) { return q.pop(out); }
};

struct TBBBoundedQueue
{
    tbb::concurrent_bounded_queue<size_t> q;

    TBBBoundedQueue() { q.set_capacity(QUEUE_SIZE); }

    bool try_push(size_t v) { return q.try_push(v); }

    bool try_pop(size_t& out) { return q.try_pop(out); }
};

template <class Queue>
static void BM_Queue(benchmark::State& state)
{
    size_t prefill = state.range(0);
    for (auto _ : state)
    {
        Queue q;
        for (size_t i = 0; i < prefill; ++i)
        {
            benchmark::DoNotOptimize(q.try_push(i));
        }

        std::atomic<size_t> consumed{0};
        auto t0 = high_resolution_clock::now();

        std::thread consumer(
            [&]()
            {
                size_t val;
                size_t target = PRODUCERS * ITEMS_PER_PRODUCER + prefill;
                while (consumed.load(std::memory_order_relaxed) < target)
                {
                    if (q.try_pop(val))
                    {
                        consumed.fetch_add(1, std::memory_order_relaxed);
                    }
                    else
                    {
                        std::this_thread::yield();
                    }
                }
            });

        std::vector<std::thread> producers;
        producers.reserve(PRODUCERS);
        for (size_t i = 0; i < PRODUCERS; ++i)
        {
            producers.emplace_back(
                [&]()
                {
                    for (size_t j = 0; j < ITEMS_PER_PRODUCER; ++j)
                    {
                        while (!q.try_push(j))
                        {
                            std::this_thread::yield();
                        }
                    }
                });
        }

        for (auto& t : producers)
        {
            t.join();
        }
        consumer.join();

        auto t1 = high_resolution_clock::now();
        double elapsed = duration<double>(t1 - t0).count();
        state.SetIterationTime(elapsed);
        state.SetItemsProcessed(static_cast<long long>(PRODUCERS) * ITEMS_PER_PRODUCER);
    }
}

BENCHMARK_TEMPLATE(BM_Queue, BoundedMPSCQueue<size_t, QUEUE_SIZE>)
    ->Args({0})
    ->Args({QUEUE_SIZE / 2})
    ->Args({QUEUE_SIZE - 1})
    ->UseRealTime()
    ->Unit(benchmark::kSecond);

BENCHMARK_TEMPLATE(BM_Queue, BoostQueue)
    ->Args({0})
    ->Args({QUEUE_SIZE / 2})
    ->Args({QUEUE_SIZE - 1})
    ->UseRealTime()
    ->Unit(benchmark::kSecond);

BENCHMARK_TEMPLATE(BM_Queue, TBBBoundedQueue)
    ->Args({0})
    ->Args({QUEUE_SIZE / 2})
    ->Args({QUEUE_SIZE - 1})
    ->UseRealTime()
    ->Unit(benchmark::kSecond);