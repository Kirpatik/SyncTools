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

#pragma once

#include <tbb/scalable_allocator.h>

namespace SyncTools::Wrappers
{

template <typename T>
class TBBPoolWrapper
{
public:
    using value_type = T;

    explicit TBBPoolWrapper(std::size_t) {}

    value_type* acquire() { return _allocator.allocate(1); }

    void release(value_type* ptr) { _allocator.deallocate(ptr, 1); }

    void flush() {}

private:
    tbb::scalable_allocator<T> _allocator;
};

}  // namespace SyncTools::Wrappers
