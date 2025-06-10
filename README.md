# SyncTools - Header-Only Concurrency Utilities

[![Coverage](https://img.shields.io/badge/coverage-83%25-brightgreen)](#)

> A small collection of header-only C++17 helpers - fast queues, a futex-powered mutex, and a couple of object pools.

## Table of Contents

* [Features](#features)
* [Quick Start](#quick-start)
* [Installation](#installation)
* [Usage](#usage)

  * [BoundedMPSCQueue](#boundedmpscqueue)
  * [Mutex](#mutex)
  * [ObjectPool](#objectpool)
  * [ThreadCachedObjectPool](#threadcachedobjectpool)
  * [BoundedSPSCQueue](#boundedspscqueue)
* [Benchmarks](#benchmarks)
* [License](#license)

## Features

* **Header-only** - just drop the `include/` folder into your project
* **Zero external dependencies** - relies only on the C++ Standard Library
* **Lock-free queues** - bounded MPSC & SPSC implementations for predictable performance
* **Object pools** - fast memory recycling with optional per-thread caches
* **Futex-based High-Performance Mutex** - lightweight spin-then-block design with optional ThreadSanitizer & deadlock checks
* Small footprint and comprehensive unit tests
* CI builds on GCC and Clang; coverage (~83%) is measured on GCC

## Quick Start

Below is a tiny producer -> consumer demo you can paste into `main.cpp` to make sure everything builds and runs:

```cpp
#include <iostream>
#include <mutex>
#include <sync_tools/mutex.hpp>
#include <sync_tools/spsc_queue.hpp>
#include <thread>

int main()
{
    // Single-producer / single-consumer queue with 1 Ki elements
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
```

Build & run (Linux / Clang):

```bash
clang++ main.cpp -std=c++17 -I/path/to/SyncTools/include -pthread -O3 -o demo
./demo
```

You should see `got 1` … `got 10` printed on the console.

---

## Installation

### FetchContent (recommended)

Add the library directly from GitHub and let CMake manage include paths:

```cmake
include(FetchContent)

FetchContent_Declare(
  SyncTools
  GIT_REPOSITORY https://github.com/Kirpatik/SyncTools.git
  GIT_TAG        v1.0.0
)
FetchContent_MakeAvailable(SyncTools)

target_link_libraries(your_target PRIVATE SyncTools::SyncTools)
```

### Submodule / Vendored

```bash
git submodule add https://github.com/Kirpatik/SyncTools.git external/SyncTools
```

Then in your `CMakeLists.txt`:

```cmake
add_subdirectory(external/SyncTools)

target_link_libraries(your_target PRIVATE SyncTools::SyncTools)
```

### System-wide install

Build and install once, then find it via `find_package`:

```bash
cmake -S SyncTools -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build --target install
```

```cmake
find_package(SyncTools REQUIRED)

target_link_libraries(your_target PRIVATE SyncTools::SyncTools)
```

All options are header-only; linking merely propagates include paths & compile flags.

---

## Building & Running Benchmarks

The project ships with Google Benchmark suites (disabled by default).

```bash
mkdir build && cd build
cmake .. -DENABLE_BENCHMARKS=ON
make -j$(nproc)
./benchmarks/benchmarks
```

## Building & Running Tests

Similarly, unit tests use GoogleTest:

```bash
mkdir build && cd build
cmake .. -DENABLE_TESTING=ON
make -j$(nproc)
./tests/tests
```

---

## Usage

### BoundedMPSCQueue

`SyncTools::BoundedMPSCQueue<T, N>` is a **lock-free multi-producer / single-consumer ring buffer** that uses per-slot sequence numbers (a-la Vyukov) to avoid locks and the ABA problem.

| Property        | Value                                                             |
| --------------- | ----------------------------------------------------------------- |
| Concurrency     | Many producers, exactly one consumer                              |
| lock-free push  | Producers CAS a shared `head`; no contention between consumer     |
| Memory ordering | `relaxed` on hot path, `acquire/release` only when crossing slots |
| Capacity        | Power-of-two `N`; memory footprint ≈ `N*sizeof(T)+overhead`       |
| Back-off        | Optional spinning via `blocking_push` before giving up            |

#### How it works

1. **Producer** reads `head`, checks cell’s sequence == `head` -> cell free. CAS head -> `head+1` and constructs object in-place.
2. **Consumer** reads `tail`, checks cell’s sequence == `tail+1` -> element ready. Destroys object, stores `seq = tail+N` to mark free.
3. Sequence numbers monotonically increase, so ABA is impossible within 2\*capacity.

#### Example - logging fan-in

```cpp
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
```

---

### Mutex

`SyncTools::Mutex` is a **lightweight futex-based lock** designed for the uncontended-first workloads typical in modern C++ servers and games.

| Property            | Value                                                  |
| ------------------- | ------------------------------------------------------ |
| Size                | 4 bytes data + padding to 64-byte cache line           |
| Contention strategy | spin (exponential back-off) -> `futex` sleep           |
| Fairness            | Wakes one waiter; prevents starvation                  |
| Debug aids          | Optional ThreadSanitizer annotations & deadlock checks |

#### How it works

1. **Fast path** - an atomic CAS tries to move the state from *Free*->*Locked*; succeeds for the uncontended case without syscalls.
2. **Spin-then-yield** - failing CAS enters a short adaptive spin (`1,2,4…1024` `cpu_relax` loops) to let the owner unlock.
3. **Blocking path** - after spins it marks the state *Contended* and calls `futex(FUTEX_WAIT_PRIVATE)`; kernel parks the thread.
4. **Unlock** - owner swaps state to *Free*; if previous state was *Contended* it issues `futex(FUTEX_WAKE_PRIVATE, 1)` to wake one waiter.

This design avoids kernel calls in the common case.

#### Example

```cpp
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
```

*(**`SyncTools::Mutex`** models the **\`\`** interface, so it integrates with **`std::lock_guard`**, **`std::scoped_lock`**).*

---

### ObjectPool

`SyncTools::ObjectPool<T>` is a **fixed-capacity arena allocator** that delivers constant-time acquire/release with minimal bookkeeping. Capacity is rounded to the next power-of-two so the circular index mask is a single `&`.

| Property        | Value                                                                           |
| --------------- | ------------------------------------------------------------------------------- |
| Capacity growth | Fixed (next power-of-two of constructor argument)                               |
| Thread safety   | Internal futex-based `Mutex` guarding a ring buffer of pointers                 |
| Batch API       | `acquire_batch` / `release_batch` fetch or return up to *N* objects in one lock |

#### Example

```cpp
#include <sync_tools/object_pool.hpp>

struct Bullet
{
    float x, y, vx, vy;
};

int main()
{
    SyncTools::ObjectPool<Bullet> pool{/*initial*/ 4096};

    Bullet* b = pool.acquire();
    if (b)
    {
        // initialise and use bullet …
        pool.release(b);
    }
}
```

---

### ThreadCachedObjectPool

`SyncTools::ThreadCachedObjectPool<T, BatchSize, FlushThreshold>` layers **per-thread caches** on top of a global `ObjectPool` to remove the mutex from the fast path.

| Property     | Value                                                                                |
| ------------ | ------------------------------------------------------------------------------------ |
| Fast path    | No locking - L1 resident array per thread                                            |
| Batch size   | `BatchSize` (defaults 1024) fetched from global pool when cache empties              |
| Flush policy | When cache hits `FlushThreshold` (defaults 2048) it flushes half back to global pool |
| Contention   | Only occurs when local cache empty/full, amortised O(BatchSize)                      |

#### Example

```cpp
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
```

---

### BoundedSPSCQueue

`SyncTools::BoundedSPSCQueue<T, N>` provides a **lock-free single-producer / single-consumer channel** with optional blocking helpers (`blocking_push`/`blocking_pop`) implemented as spin-waits.

| Property            | Value                                    |
| ------------------- | ---------------------------------------- |
| Concurrency         | Exactly one producer, one consumer       |
| Wait-free guarantee | Both push & pop                          |
| Capacity            | Compile-time constant power-of-two `N`   |
| Stop control        | `run()` / `stop()` gate blocking methods |
| Destructor          | Drains queue, calls `T` destructors      |

#### Example - audio sample pipe

```cpp
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
```

---

## Benchmarks

All measurements were taken on a 6-core (12-thread) **AMD Ryzen 5 5600** (3.5 GHz base, 32 MB L3, Ubuntu 22.04) compiled with **gcc 11.4.0**, flags `-O3 -march=native`, on **June 10 2025**.

> **How to reproduce**
> `mkdir build && cd build && cmake .. -DENABLE_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release && make -j$(nproc) && ./benchmarks/benchmarks`

---

### What each benchmark measures

| Group                  | Google-Benchmark fixture     | What it does & why it matters                                                                                                                                                                                                                         |
| ---------------------- | ---------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Short lock**         | `BM_ShortLock<Mutex>`        | Threads repeatedly take a *zero-work* critical section. Measures **pure lock/unlock latency** under different thread counts (1 -> 16). Ideal for uncontended or micro-contention scenarios such as reference-count updates or short atomic sections.  |
| **Mixed lock**         | `BM_MixedLock<Mutex>`        | Same as above but the thread spins \~100 NOPs inside the critical section, emulating a **read-dominant workload** (10% writes, 90% reads). Shows how the mutex behaves when the protected region is small but non-trivial.                            |
| **Fairness**           | `BM_MutexFairness<Mutex>`    | All threads compete for the same mutex while the test records **time-to-acquire** for every lock. Reports average wait and per-thread ops/s - reveals starvation or convoy effects.                                                                   |
| **Bounded MPSC queue** | `BM_Queue<BoundedMPSCQueue>` | 12 producer threads push 1M items each; one consumer drains them. The queue is optionally **prefilled (0/0.5/-1 capacity)** to stress the steady-state path. Reports **throughput (M items/s)**. Comparisons against Boost and TBB.                   |
| **Bounded SPSC queue** | `BM_SPSCQueue<…>`            | Single producer + single consumer, otherwise identical to MPSC test. Highlights cache-friendly, lock-free communication when only two threads are involved.                                                                                           |
| **Acquire/Release**    | `BM_AcquireRelease<Pool>`    | Each thread loops 1M times: acquire object from pool -> use -> release. Counts **raw allocation + recycle throughput**. Implementations: thread-cached pool (SyncTools), TBB scalable allocator wrapper, Boost.ObjectPool wrapper, native new/delete. |
| **Pipeline**           | `BM_Pipeline<Pool>`          | 3-stage hand-off: producer acquires object + pushes to per-thread SPSC queue -> consumer pops and recycles. Half threads are producers, half consumers. Simulates **real object flow** (e.g., packet processing).                                     |

Each Google-Benchmark fixture sets `state.SetIterationTime()` to wall-clock seconds (`UseRealTime`) so results are stable even if the OS steals CPU.

---

### 1. Lock micro-benchmarks — latency (ns, lower = better)

| Threads                  | std::mutex | SyncTools::Mutex | tbb::spin\_mutex | PThreadAdaptive | AbseilMutex |
| ------------------------ | ---------: | ---------------: | ---------------: | --------------: | ----------: |
| **Short lock**           |            |                  |                  |                 |             |
| 1                        |       5.10 |         **3.52** |             3.03 |            6.62 |        4.20 |
| 2                        |       34.2 |         **10.6** |             16.0 |            93.8 |         101 |
| 4                        |       77.9 |         **29.2** |             72.3 |             391 |         297 |
| 8                        |        182 |         **87.3** |              243 |            1006 |         576 |
| 16                       |        382 |          **188** |              461 |            1766 |        1068 |
| **Mixed lock**           |            |                  |                  |                 |             |
| 1                        |       46.6 |         **50.7** |             47.7 |            51.0 |        52.2 |
| 2                        |        244 |         **95.0** |             99.4 |             239 |         171 |
| 4                        |        462 |          **191** |              219 |             545 |         534 |
| 8                        |       1246 |          **490** |              547 |            1380 |         965 |
| 16                       |       3642 |          **964** |             1136 |            3645 |        1847 |
| **Fairness - avg. wait** |            |                  |                  |                 |             |
| 1                        |       24.5 |         **23.7** |             23.4 |            40.0 |        23.6 |
| 2                        |        100 |         **85.4** |             85.3 |            77.6 |        85.7 |
| 4                        |        405 |          **191** |              216 |             403 |         307 |
| 8                        |        580 |          **435** |              608 |             980 |         521 |
| 16                       |       1509 |          **792** |             1219 |            2552 |        1219 |

### 2. Queue throughput (million items/s, higher = better)

| Queue                        |    0-fill |   8k fill |  16k fill |
| ---------------------------- | --------: | --------: | --------: |
| **Bounded MPSC** (SyncTools) |  **8.84** |  **9.49** |  **9.76** |
| Boost lock-free              |      3.81 |      3.79 |      3.84 |
| TBB bounded                  |      4.49 |      5.19 |      5.29 |
| **Bounded SPSC** (SyncTools) |  **1.36** |  **1.42** |  **1.36** |
| Boost SPSC                   |      1.36 |      1.50 |      1.47 |

### 3. Object pools & pipeline (items/s, higher = better)

**Acquire & release**

| Threads | Thread-cached pool             | TBB pool | ObjectPool       | new/delete |
| ------- | -----------------------------: | -------: | ---------------: | ---------: |
| 2       |                      **1.46G** |    73.8M |            52.4M |      98.9M |
| 4       |                      **1.32G** |    70.1M |            19.2M |      84.4M |
| 6       |                      **1.12G** |    61.2M |            10.7M |      79.8M |
| 8       |                       **855M** |    52.9M |            6.90M |      66.2M |
| 10      |                       **727M** |    46.9M |            4.92M |      60.1M |
| 12      |                       **603M** |    40.1M |            3.84M |      52.3M |
| 14      |                       **501M** |    37.5M |            3.31M |      46.6M |
| 16      |                       **556M** |    38.3M |            2.90M |      50.0M |

**3-stage pipeline**

| Threads | Pipeline Thread-cached    | Pipeline TBB  | Pipeline ObjectPool   | Pipeline new/delete  |
| ------- | ------------------------: | ------------: | --------------------: | -------------------: |
| 2       |                 **57.6M** |         11.6M |                 15.6M |                8.55M |
| 4       |                 **49.9M** |         11.4M |                 7.59M |                8.32M |
| 6       |                 **48.9M** |         10.7M |                 4.94M |                8.13M |
| 8       |                 **45.2M** |         9.85M |                 3.71M |                7.46M |
| 10      |                 **44.4M** |         10.1M |                 2.81M |                7.75M |
| 12      |                 **39.8M** |         9.18M |                 2.27M |                7.24M |
| 14      |                 **35.5M** |         7.04M |                 2.11M |                5.81M |
| 16      |                 **26.2M** |         6.16M |                 1.82M |                4.96M |

### Key take-aways

* **SyncTools::Mutex** outperforms std::mutex by 1.9-3.7x across contention levels and beats TBB & Abseil. Fairness test confirms lower wait times under pressure.
* The **bounded MPSC queue** sustains \~10M items/s, giving \~2.5x Boost and \~2x TBB throughput even when prefilled.
* A **thread-cached object pool** delivers up to \~15x the throughput of TBB’s scalable allocator wrapper and is orders of magnitude ahead of naive allocation.
* In a realistic 3-stage pipeline SyncTools moves \~26M items/s, quadrupling TBB performance.

Raw numbers come from Google-Benchmark; five independent runs show +-3% variance.

---

## License

`SyncTools` is licensed under the Apache License 2.0. See the [LICENSE](LICENSE) file for details.
