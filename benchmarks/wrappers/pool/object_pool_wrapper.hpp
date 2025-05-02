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

#include "object_pool.hpp"

namespace SyncTools::Wrappers
{

template <typename T>
class ObjectPoolWrapper
{
public:
    using value_type = T;

    ObjectPoolWrapper(std::size_t size) : _pool(size) {}

    T* acquire() noexcept { return _pool.acquire(); }

    void release(T* obj) noexcept { _pool.release(obj); }

    void flush() {}

private:
    SyncTools::ObjectPool<T> _pool;
};

}  // namespace SyncTools::Wrappers