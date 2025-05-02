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

#include <algorithm>
#include <climits>
#include <cstring>
#include <mutex>
#include <vector>

#include "mutex.hpp"
#include "utils/attributes.hpp"
#include "utils/likely.hpp"

namespace SyncTools
{
/**
 * @brief A pool of pre-allocated objects offering constant-time acquisition and release.
 *
 * The pool size is rounded up to the next power of two. Thread-safe via an internal mutex.
 * @tparam T Type of object to store in the pool.
 */
template <typename T>
class ObjectPool
{
public:
    /// Value type stored in the pool
    using value_type = T;

    /**
     * @brief Construct a new ObjectPool.
     *
     * @param size Initial number of objects to pre-allocate.
     *             Internally rounded up to the next power of two.
     */
    ObjectPool(std::size_t size)
        : _pool_size(_round_up_pow2(size)),
          _available_objects(_pool_size),
          _head_index(0),
          _available_count(_pool_size),
          _nodes(_pool_size)
    {
        for (std::size_t i = 0; i < _pool_size; ++i)
        {
            _available_objects[i] = &_nodes[i];
        }
    }

    ObjectPool(const ObjectPool&) = delete;             ///< Non-copyable
    ObjectPool& operator=(const ObjectPool&) = delete;  ///< Non-assignable
    ObjectPool(ObjectPool&&) = delete;                  ///< Non-movable
    ObjectPool& operator=(ObjectPool&&) = delete;       ///< Non-movable
    ~ObjectPool() = default;                            ///< Destructor

    /**
     * @brief Acquire a single object from the pool.
     *
     * @return Pointer to an available object, or nullptr if none are available.
     */
    SYNC_TOOLS_ALWAYS_INLINE T* acquire() noexcept
    {
        std::scoped_lock lock(_mutex);
        if (SYNC_TOOLS_UNLIKELY(_available_count == 0))
        {
            return nullptr;
        }
        T* obj = _available_objects[_head_index];
        _head_index = (_head_index + 1) & (_pool_size - 1);
        --_available_count;
        return obj;
    }

    /**
     * @brief Return an object back to the pool.
     *
     * @param obj Pointer to the object previously acquired from this pool.
     */
    SYNC_TOOLS_ALWAYS_INLINE void release(T* obj) noexcept
    {
        std::scoped_lock lock(_mutex);
        std::size_t tail = (_head_index + _available_count) & (_pool_size - 1);
        _available_objects[tail] = obj;
        ++_available_count;
    }

    /**
     * @brief Acquire up to max_count objects in a batch.
     *
     * @param out Array to receive object pointers.
     * @param max_count Maximum number of objects to acquire.
     * @return Actual number of objects acquired.
     */
    SYNC_TOOLS_ALWAYS_INLINE std::size_t acquire_batch(T** out, std::size_t max_count) noexcept
    {
        std::scoped_lock lock(_mutex);
        std::size_t num = std::min(max_count, _available_count);
        if (SYNC_TOOLS_UNLIKELY(num == 0))
        {
            return 0;
        }
        std::size_t endPos = _head_index + num;
        if (endPos <= _pool_size)
        {
            std::memcpy(out, _available_objects.data() + _head_index, num * sizeof(T*));
        }
        else
        {
            std::size_t firstPart = _pool_size - _head_index;
            std::memcpy(out, _available_objects.data() + _head_index, firstPart * sizeof(T*));
            std::memcpy(out + firstPart, _available_objects.data(), (num - firstPart) * sizeof(T*));
        }
        _head_index = (_head_index + num) & (_pool_size - 1);
        _available_count -= num;
        return num;
    }

    /**
     * @brief Return multiple objects back to the pool in a batch.
     *
     * @param items Array of object pointers to release.
     * @param num Number of items to release.
     */
    SYNC_TOOLS_ALWAYS_INLINE void release_batch(T** items, std::size_t num) noexcept
    {
        if (SYNC_TOOLS_UNLIKELY(num == 0))
        {
            return;
        }
        std::scoped_lock lock(_mutex);
        std::size_t tail = (_head_index + _available_count) & (_pool_size - 1);
        std::size_t endPos = tail + num;
        if (endPos <= _pool_size)
        {
            std::memcpy(_available_objects.data() + tail, items, num * sizeof(T*));
        }
        else
        {
            std::size_t firstPart = _pool_size - tail;
            std::memcpy(_available_objects.data() + tail, items, firstPart * sizeof(T*));
            std::memcpy(_available_objects.data(), items + firstPart, (num - firstPart) * sizeof(T*));
        }
        _available_count += num;
    }

    /**
     * @brief Get the total capacity of the pool.
     *
     * @return Pool size (power-of-two rounded).
     */
    SYNC_TOOLS_ALWAYS_INLINE std::size_t size() const noexcept { return _pool_size; }

private:
    /**
     * @brief Round up to the next power of two.
     *
     * @param v Input value.
     * @return Next power-of-two >= v.
     */
    static SYNC_TOOLS_ALWAYS_INLINE std::size_t _round_up_pow2(std::size_t v) noexcept
    {
        if (v <= 1)
        {
            return 1;
        }

        unsigned long long x = static_cast<unsigned long long>(v - 1);
        int leading = __builtin_clzll(x);
        int bits = static_cast<int>(sizeof(x) * CHAR_BIT);
        int shift = bits - leading;
        return static_cast<std::size_t>(1ULL << shift);
    }

    const std::size_t _pool_size;       /**< Rounded-up capacity of the pool. */
    std::vector<T*> _available_objects; /**< Circular buffer of available object pointers. */
    std::size_t _head_index;            /**< Index of the next available object. */
    std::size_t _available_count;       /**< Number of free objects. */
    Mutex _mutex;                       /**< Mutex protecting pool operations. */

    std::vector<T> _nodes; /**< Underlying storage of objects. */
};

/**
 * @brief Thread-local cached object pool for fast, low-contention allocation.
 *
 * Combines a global ObjectPool and per-thread cache to reduce contention on the mutex.
 * @tparam T Type of object stored.
 * @tparam BatchSize Number of objects fetched from global pool when cache is empty.
 * @tparam CacheFlushThreshold Threshold to flush half the cache back to global pool.
 */
template <typename T, std::size_t BatchSize = 1024, std::size_t CacheFlushThreshold = 2048>
class ThreadCachedObjectPool
{
public:
    /// Value type stored in the pool
    using value_type = T;

    /**
     * @brief Construct new thread-cached pool.
     *
     * @param size Initial size for the underlying global pool.
     */
    ThreadCachedObjectPool(std::size_t size) : _global_pool(size) {}

    ThreadCachedObjectPool(const ThreadCachedObjectPool&) = delete;             ///< Non-copyable
    ThreadCachedObjectPool& operator=(const ThreadCachedObjectPool&) = delete;  ///< Non-assignable
    ThreadCachedObjectPool(ThreadCachedObjectPool&&) = delete;                  ///< Non-movable
    ThreadCachedObjectPool& operator=(ThreadCachedObjectPool&&) = delete;       ///< Non-movable
    ~ThreadCachedObjectPool() = default;                                        ///< Destructor

    /**
     * @brief Acquire an object, first from thread-local cache, then global pool.
     *
     * @return Pointer to object or nullptr if none available.
     */
    SYNC_TOOLS_ALWAYS_INLINE T* acquire() noexcept
    {
        ThreadCache& cache = _get_thread_cache();
        if (SYNC_TOOLS_UNLIKELY(cache.cache_size == 0))
        {
            std::size_t acquired = _global_pool.acquire_batch(cache.cache.data(), BatchSize);
            cache.cache_size = acquired;
        }
        if (SYNC_TOOLS_LIKELY(cache.cache_size > 0))
        {
            return cache.cache[--cache.cache_size];
        }
        return nullptr;
    }

    /**
     * @brief Release object back to thread-local cache, flushing if threshold exceeded.
     *
     * @param obj Pointer to object previously acquired.
     */
    SYNC_TOOLS_ALWAYS_INLINE void release(T* obj) noexcept
    {
        ThreadCache& cache = _get_thread_cache();
        if (SYNC_TOOLS_UNLIKELY(cache.cache_size >= CacheFlushThreshold))
        {
            _flush_half(cache);
        }
        cache.cache[cache.cache_size++] = obj;
    }

    /**
     * @brief Flush all cached objects back to the global pool.
     */
    SYNC_TOOLS_ALWAYS_INLINE void flush() noexcept
    {
        ThreadCache& cache = _get_thread_cache();
        if (SYNC_TOOLS_LIKELY(cache.cache_size > 0))
        {
            _global_pool.release_batch(cache.cache.data(), cache.cache_size);
            cache.cache_size = 0;
        }
    }

    /**
     * @brief Get total capacity of the underlying global pool.
     *
     * @return Size of global pool.
     */
    SYNC_TOOLS_ALWAYS_INLINE std::size_t size() const noexcept { return _global_pool.size(); }

private:
    ObjectPool<T> _global_pool;  ///< Shared global pool of objects

    /**
     * @brief Per-thread cache structure.
     */
    struct ThreadCache
    {
        std::array<T*, CacheFlushThreshold> cache{};  ///< Cached object pointers
        std::size_t cache_size = 0;                   ///< Number of items in cache
    };

    /**
     * @brief Get reference to thread-local cache.
     *
     * @return Reference to this thread's cache
     */
    static SYNC_TOOLS_ALWAYS_INLINE ThreadCache& _get_thread_cache() noexcept
    {
        thread_local ThreadCache cache;
        return cache;
    }

    /**
     * @brief Flush half of the thread cache back to the global pool.
     *
     * @param cache ThreadCache to flush from
     */
    SYNC_TOOLS_ALWAYS_INLINE void _flush_half(ThreadCache& cache) noexcept
    {
        std::size_t half = cache.cache_size / 2;
        if (SYNC_TOOLS_LIKELY(half > 0))
        {
            _global_pool.release_batch(&cache.cache[cache.cache_size - half], half);
            cache.cache_size -= half;
        }
    }
};

}  // namespace SyncTools
