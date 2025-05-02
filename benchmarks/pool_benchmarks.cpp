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

#include <atomic>
#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <cstddef>
#include <memory>
#include <vector>

#include "object_pool.hpp"
#include "spsc_queue.hpp"
#include "wrappers/pool/new_delete_wrapper.hpp"
#include "wrappers/pool/object_pool_wrapper.hpp"
#include "wrappers/pool/tbb_scalable_allocator_pool.hpp"

constexpr std::size_t pool_capacity = 1 << 20;
constexpr std::size_t queue_size = 1 << 10;
constexpr std::size_t iteration_per_cycle = 1 << 20;

static void CustomThreadRange(benchmark::internal::Benchmark* b)
{
    for (int i = 2; i <= 16; i += 2)
    {
        b->Threads(i);
    }
}

template <typename Pool>
static void BM_AcquireRelease(benchmark::State& state)
{
    static Pool pool(pool_capacity);

    for (auto _ : state)
    {
        for (std::size_t i = 0; i < iteration_per_cycle; ++i)
        {
            auto obj = pool.acquire();
            if (!obj)
            {
                state.SkipWithError("Pool is empty!");
            }
            benchmark::DoNotOptimize(obj);
            pool.release(obj);
        }
    }
    pool.flush();

    state.SetItemsProcessed(state.iterations() * iteration_per_cycle);
}

template <typename Pool>
static void BM_Pipeline(benchmark::State& state)
{
    static std::atomic<bool> init_flag{false};
    static std::atomic<bool> ready_flag{false};
    static std::atomic<int> producers_remaining{0};
    static std::atomic<bool> producers_done{false};
    static int producers = 0;
    static std::vector<std::unique_ptr<SyncTools::BoundedSPSCQueue<typename Pool::value_type*, queue_size>>> queues;
    static Pool pool(pool_capacity);

    int total_threads = state.threads();
    producers = total_threads / 2;

    if (!init_flag.exchange(true, std::memory_order_acquire))
    {
        queues.clear();
        queues.reserve(producers);
        for (int i = 0; i < producers; ++i)
        {
            queues.emplace_back(
                std::make_unique<SyncTools::BoundedSPSCQueue<typename Pool::value_type*, queue_size>>());
        }
        producers_remaining.store(producers, std::memory_order_relaxed);
        producers_done.store(false, std::memory_order_relaxed);
        ready_flag.store(true, std::memory_order_release);
    }

    while (!ready_flag.load(std::memory_order_acquire))
    {
    }

    int tid = state.thread_index();
    auto& q = (tid < producers) ? *queues[tid] : *queues[tid - producers];

    if (tid < producers)
    {
        for (auto _ : state)
        {
            for (std::size_t i = 0; i < iteration_per_cycle; ++i)
            {
                auto* obj = pool.acquire();
                if (!obj)
                {
                    state.SkipWithError("Pool is empty!");
                }
                benchmark::DoNotOptimize(obj);
                while (!q.try_push(obj))
                {
                }
            }
        }
        if (producers_remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            producers_done.store(true, std::memory_order_release);
        }

        state.SetItemsProcessed(state.iterations() * iteration_per_cycle);
    }
    else
    {
        for (auto _ : state)
        {
            for (std::size_t i = 0; i < iteration_per_cycle; ++i)
            {
                typename Pool::value_type* obj = nullptr;
                while (!q.try_pop(obj))
                {
                }
                benchmark::DoNotOptimize(obj);
                pool.release(obj);
            }
        }

        typename Pool::value_type* obj = nullptr;
        std::size_t last_count{0};
        while (!producers_done.load(std::memory_order_acquire) || q.try_pop(obj))
        {
            if (obj)
            {
                ++last_count;
                benchmark::DoNotOptimize(obj);
                pool.release(obj);
                obj = nullptr;
            }
        }
    }

    pool.flush();
    if (tid == 0)
    {
        init_flag.store(false, std::memory_order_release);
        ready_flag.store(false, std::memory_order_release);
    }
};

BENCHMARK_TEMPLATE(BM_AcquireRelease, SyncTools::ThreadCachedObjectPool<std::size_t>)
    ->Apply(CustomThreadRange)
    ->UseRealTime();
BENCHMARK_TEMPLATE(BM_AcquireRelease, SyncTools::Wrappers::TBBPoolWrapper<std::size_t>)
    ->Apply(CustomThreadRange)
    ->UseRealTime();
BENCHMARK_TEMPLATE(BM_AcquireRelease, SyncTools::Wrappers::ObjectPoolWrapper<std::size_t>)
    ->Apply(CustomThreadRange)
    ->UseRealTime();
BENCHMARK_TEMPLATE(BM_AcquireRelease, SyncTools::Wrappers::NewDeletePoolWrapper<std::size_t>)
    ->Apply(CustomThreadRange)
    ->UseRealTime();

BENCHMARK_TEMPLATE(BM_Pipeline, SyncTools::ThreadCachedObjectPool<std::size_t>)
    ->Apply(CustomThreadRange)
    ->UseRealTime();
BENCHMARK_TEMPLATE(BM_Pipeline, SyncTools::Wrappers::TBBPoolWrapper<std::size_t>)
    ->Apply(CustomThreadRange)
    ->UseRealTime();
BENCHMARK_TEMPLATE(BM_Pipeline, SyncTools::Wrappers::ObjectPoolWrapper<std::size_t>)
    ->Apply(CustomThreadRange)
    ->UseRealTime();
BENCHMARK_TEMPLATE(BM_Pipeline, SyncTools::Wrappers::NewDeletePoolWrapper<std::size_t>)
    ->Apply(CustomThreadRange)
    ->UseRealTime();