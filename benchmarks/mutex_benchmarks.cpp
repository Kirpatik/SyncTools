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
#include <tbb/spin_mutex.h>

#include <atomic>
#include <mutex>

#include "mutex.hpp"
#include "wrappers/mutex/abseil_mutex.hpp"
#include "wrappers/mutex/pthread_adaptive_mutex.hpp"

static void CustomThreadRange(benchmark::internal::Benchmark* b)
{
    for (int i = 1; i <= 16; i *= 2)
    {
        b->Threads(i);
    }
}

template <typename Mutex>
static void BM_ShortLock(benchmark::State& state)
{
    static Mutex m;

    for (auto _ : state)
    {
        m.lock();
        benchmark::DoNotOptimize(0);
        m.unlock();
    }
}

BENCHMARK_TEMPLATE(BM_ShortLock, std::mutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_ShortLock, SyncTools::Mutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_ShortLock, tbb::spin_mutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_ShortLock, PThreadAdaptiveMutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_ShortLock, AbseilMutex)->Apply(CustomThreadRange)->UseRealTime();

template <typename Mutex>
static void BM_MixedLock(benchmark::State& state)
{
    static Mutex m;

    for (auto _ : state)
    {
        m.lock();
        for (volatile int i = 0; i < 100; i += 1)
        {
            benchmark::DoNotOptimize(i);
        }
        m.unlock();
    }
}

BENCHMARK_TEMPLATE(BM_MixedLock, std::mutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_MixedLock, SyncTools::Mutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_MixedLock, tbb::spin_mutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_MixedLock, PThreadAdaptiveMutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_MixedLock, AbseilMutex)->Apply(CustomThreadRange)->UseRealTime();

template <typename Mutex>
static void BM_MutexFairness(benchmark::State& state)
{
    const int num_threads = state.threads();

    static Mutex m;
    static std::atomic<bool> init_done{false};
    static std::atomic<bool> start_flag{false};
    static std::atomic<int> turn{0};
    static std::atomic<int> ready{0};

    if (state.thread_index() == 0)
    {
        turn.store(0, std::memory_order_relaxed);
        ready.store(0, std::memory_order_relaxed);
        start_flag.store(false, std::memory_order_relaxed);
        init_done.store(true, std::memory_order_release);
    }
    else
    {
        while (!init_done.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    }

    ready.fetch_add(1, std::memory_order_acq_rel);

    if (state.thread_index() == 0)
    {
        while (ready.load(std::memory_order_acquire) < num_threads)
        {
            std::this_thread::yield();
        }
        start_flag.store(true, std::memory_order_release);
    }
    else
    {
        while (!start_flag.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    }

    int local_turns = 0;
    std::vector<int64_t> lat;
    lat.reserve(state.iterations());

    for (auto _ : state)
    {
        turn.fetch_add(1, std::memory_order_relaxed);
        auto t0 = std::chrono::high_resolution_clock::now();
        m.lock();
        auto t1 = std::chrono::high_resolution_clock::now();
        lat.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        m.unlock();
        ++local_turns;
    }

    if (!lat.empty())
    {
        int64_t sum = 0;
        for (auto v : lat)
        {
            sum += v;
        }
        state.counters["AvgLockLatency(ns)"] =
            benchmark::Counter(double(sum) / lat.size(), benchmark::Counter::kAvgThreads);
    }

    state.counters["OpsPerThread"] = benchmark::Counter(local_turns, benchmark::Counter::kAvgThreads);

    init_done.store(false, std::memory_order_relaxed);
}

BENCHMARK_TEMPLATE(BM_MutexFairness, std::mutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_MutexFairness, SyncTools::Mutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_MutexFairness, tbb::spin_mutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_MutexFairness, PThreadAdaptiveMutex)->Apply(CustomThreadRange)->UseRealTime();
BENCHMARK_TEMPLATE(BM_MutexFairness, AbseilMutex)->Apply(CustomThreadRange)->UseRealTime();

BENCHMARK_MAIN();