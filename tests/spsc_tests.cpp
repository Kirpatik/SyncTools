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

#include <gtest/gtest.h>

#include <atomic>
#include <boost/lockfree/queue.hpp>
#include <optional>
#include <thread>
#include <vector>

#include "spsc_queue.hpp"
#include "utils/test_classes.hpp"

static constexpr size_t PRODUCERS = 1;
static constexpr size_t ITEMS_PER_PRODUCER = 10000;
static constexpr size_t QUEUE_SIZE = 16 * 1024;
static constexpr uint64_t PREFILL_MARKER = 0xFFFFFFFFFFFFFFFFull;

template <typename Queue>
void stress_test_queue(size_t prefill)
{
    Queue q;

    for (size_t i = 0; i < prefill; ++i)
    {
        while (!q.try_push(PREFILL_MARKER))
        {
            std::this_thread::yield();
        }
    }

    std::atomic<bool> start{false};
    std::vector<size_t> counts(PRODUCERS, 0);
    std::vector<uint64_t> last_seq(PRODUCERS, UINT64_MAX);
    std::atomic<bool> ok{true};
    std::atomic<size_t> consumed{0};

    auto consumer = [&]()
    {
        while (!start.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        size_t local_count = 0;
        size_t target = PRODUCERS * ITEMS_PER_PRODUCER + prefill;
        uint64_t packed;
        while (local_count < target)
        {
            if (q.try_pop(packed))
            {
                ++local_count;
                if (packed != PREFILL_MARKER)
                {
                    int pid = int(packed >> 32);
                    uint64_t seq = packed & 0xFFFFFFFFull;
                    if (pid < 0 || size_t(pid) >= PRODUCERS)
                    {
                        ok.store(false, std::memory_order_relaxed);
                    }
                    if (last_seq[pid] != UINT64_MAX && seq <= last_seq[pid])
                    {
                        ok.store(false, std::memory_order_relaxed);
                    }
                    last_seq[pid] = seq;
                    ++counts[pid];
                }
            }
            else
            {
                std::this_thread::yield();
            }
        }
        consumed.store(local_count, std::memory_order_relaxed);
    };

    auto producer = [&](int id)
    {
        while (!start.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        for (size_t i = 0; i < ITEMS_PER_PRODUCER; ++i)
        {
            uint64_t packed = (uint64_t(id) << 32) | i;
            while (!q.try_push(packed))
            {
                std::this_thread::yield();
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(PRODUCERS + 1);
    threads.emplace_back(consumer);
    for (size_t i = 0; i < PRODUCERS; ++i)
    {
        threads.emplace_back(producer, int(i));
    }

    start.store(true, std::memory_order_release);
    for (auto& t : threads)
    {
        t.join();
    }

    EXPECT_EQ(consumed.load(std::memory_order_relaxed), PRODUCERS * ITEMS_PER_PRODUCER + prefill);
    for (size_t pid = 0; pid < PRODUCERS; ++pid)
    {
        EXPECT_EQ(counts[pid], ITEMS_PER_PRODUCER);
    }
    EXPECT_TRUE(ok.load());
}

TEST(SPSCQueueTests, Prefill1) { stress_test_queue<SyncTools::BoundedSPSCQueue<uint64_t, QUEUE_SIZE>>(1); }

TEST(SPSCQueueTests, PrefillHalf)
{
    stress_test_queue<SyncTools::BoundedSPSCQueue<uint64_t, QUEUE_SIZE>>(QUEUE_SIZE / 2);
}

TEST(SPSCQueueTests, PrefillAlmostFull)
{
    stress_test_queue<SyncTools::BoundedSPSCQueue<uint64_t, QUEUE_SIZE>>(QUEUE_SIZE - 1);
}

TEST(SPSCQueueTests, FIFOMixedAPIs)
{
    SyncTools::BoundedSPSCQueue<int, 8> q;
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_TRUE(q.try_push(i));
    }
    for (int i = 4; i < 7; ++i)
    {
        EXPECT_TRUE(q.blocking_push(i));
    }
    for (int i = 0; i < 4; ++i)
    {
        auto opt = q.blocking_pop();
        ASSERT_TRUE(opt.has_value());
        EXPECT_EQ(opt.value(), i);
    }
    for (int i = 4; i < 7; ++i)
    {
        int x;
        EXPECT_TRUE(q.try_pop(x));
        EXPECT_EQ(x, i);
    }
    EXPECT_TRUE(q.empty());
}

TEST(SPSCQueueTests, FullAndEmptyWithBlocking)
{
    SyncTools::BoundedSPSCQueue<int, 4> q;
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_TRUE(q.blocking_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.blocking_push(3));
    EXPECT_TRUE(q.full());
    EXPECT_FALSE(q.try_push(5));
    int y;
    EXPECT_TRUE(q.try_pop(y));
    EXPECT_EQ(y, 1);
    EXPECT_FALSE(q.full());
    EXPECT_FALSE(q.empty());
    auto o = q.blocking_pop();
    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o.value(), 2);
    EXPECT_TRUE(q.try_pop(y));
    EXPECT_EQ(y, 3);
    EXPECT_TRUE(q.empty());
}

TEST(SPSCQueueTests, StopBlocksAll)
{
    SyncTools::BoundedSPSCQueue<int, 4> q;
    q.stop();
    EXPECT_FALSE(q.try_push(1));
    EXPECT_FALSE(q.blocking_push(1));
    EXPECT_FALSE(q.try_pop());
    int x;
    EXPECT_FALSE(q.try_pop(x));
    EXPECT_FALSE(q.blocking_pop(x));
    EXPECT_FALSE(q.blocking_pop().has_value());
}

TEST(SPSCQueueTests, RunAfterStop)
{
    SyncTools::BoundedSPSCQueue<int, 4> q;
    q.stop();
    q.run();
    EXPECT_TRUE(q.blocking_push(7));
    int v;
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 7);
}

TEST(SPSCQueueTests, StopDrainsRemaining)
{
    SyncTools::BoundedSPSCQueue<int, 4> q;
    EXPECT_TRUE(q.try_push(10));
    EXPECT_TRUE(q.blocking_push(20));
    q.stop();
    auto a = q.blocking_pop();
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a.value(), 10);
    int z;
    EXPECT_TRUE(q.blocking_pop(z));
    EXPECT_EQ(z, 20);
    EXPECT_FALSE(q.try_pop());
    EXPECT_FALSE(q.blocking_pop().has_value());
}

TEST(SPSCQueueTests, MoveOnlyObject)
{
    SyncTools::BoundedSPSCQueue<OnlyMovable, QUEUE_SIZE> queue;

    OnlyMovable input_object;
    ASSERT_TRUE(queue.try_push(std::move(input_object)));
    ASSERT_TRUE(queue.blocking_push({}));
    ASSERT_TRUE(queue.blocking_push({}));
    ASSERT_TRUE(queue.blocking_push({}));

    EXPECT_TRUE(queue.try_pop() != std::nullopt);
    OnlyMovable output_object;
    EXPECT_TRUE(queue.try_pop(output_object));

    EXPECT_TRUE(queue.blocking_pop() != std::nullopt);
    EXPECT_TRUE(queue.blocking_pop(output_object));
}

TEST(SPSCQueueTests, CopyOnlyObject)
{
    SyncTools::BoundedSPSCQueue<OnlyCopyable, QUEUE_SIZE> queue;

    OnlyCopyable input_object;
    ASSERT_TRUE(queue.try_push(input_object));
    ASSERT_TRUE(queue.blocking_push({}));
    ASSERT_TRUE(queue.blocking_push({}));
    ASSERT_TRUE(queue.blocking_push({}));

    EXPECT_TRUE(queue.try_pop() != std::nullopt);
    OnlyCopyable output_object;
    EXPECT_TRUE(queue.try_pop(output_object));

    EXPECT_TRUE(queue.blocking_pop() != std::nullopt);
    EXPECT_TRUE(queue.blocking_pop(output_object));
}

TEST(SPSCQueueTests, NoDefaultConstructorObject)
{
    SyncTools::BoundedSPSCQueue<NoDefaultConstructor, QUEUE_SIZE> queue;

    NoDefaultConstructor input_object(1);
    ASSERT_TRUE(queue.try_push(input_object));
    ASSERT_TRUE(queue.blocking_push(NoDefaultConstructor{2}));
    ASSERT_TRUE(queue.blocking_push(NoDefaultConstructor{3}));
    ASSERT_TRUE(queue.blocking_push(NoDefaultConstructor{4}));

    EXPECT_TRUE(queue.try_pop() != std::nullopt);
    NoDefaultConstructor output_object(5);
    EXPECT_TRUE(queue.blocking_pop(output_object));

    EXPECT_TRUE(queue.blocking_pop() != std::nullopt);
    EXPECT_TRUE(queue.blocking_pop(output_object));
}
