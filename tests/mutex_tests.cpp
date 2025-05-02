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

#include <thread>
#include <vector>

#include "mutex.hpp"

TEST(MutexTests, CounterIncrementsCorrectly)
{
    int counter = 0;
    SyncTools::Mutex mtx;

    auto worker = [&](int iterations)
    {
        for (int i = 0; i < iterations; ++i)
        {
            mtx.lock();
            ++counter;
            mtx.unlock();
        }
    };

    const int thread_count = std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 4;
    const int iterations = 100000;

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int i = 0; i < thread_count; ++i)
    {
        threads.emplace_back(worker, iterations);
    }
    for (auto& t : threads)
    {
        t.join();
    }

    EXPECT_EQ(counter, thread_count * iterations);
}