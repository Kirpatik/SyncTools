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

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

#include "utils/attributes.hpp"
#include "utils/cpu_relax.hpp"
#include "utils/likely.hpp"

namespace SyncTools
{
/**
 * @class BoundedMPSCQueue
 * @brief Bounded multi-producer, single-consumer queue with lock-free operations.
 *
 * A fixed-size circular buffer enabling multiple producers and one consumer.
 * Capacity must be a power of two. Sequence numbers track availability per cell.
 * Supports both non-blocking and blocking push/pop until stopped.
 *
 * @tparam T Type of stored elements.
 * @tparam _size Queue capacity; power-of-two value.
 */
template <typename T, std::size_t _size>
class BoundedMPSCQueue
{
    static_assert((_size & (_size - 1)) == 0, "size must be power of two");

    /**
     * @brief Internal cell for each slot in the ring buffer.
     */
    struct alignas(64) Cell
    {
        std::atomic<std::size_t> seq;                          /**< Sequence number for ABA prevention. */
        std::aligned_storage_t<sizeof(T), alignof(T)> storage; /**< Aligned storage for element. */
    };

    alignas(64) std::array<Cell, _size> _cells; /**< Ring buffer cells. */
    const std::size_t _mask{_size - 1};         /**< Mask for index wrap-around. */

    alignas(64) std::atomic<std::size_t> _head{0}; /**< Next position to push (producers). */
    alignas(64) std::atomic<std::size_t> _tail{0}; /**< Next position to pop (consumer).  */

    /**
     * @enum QueueStatus
     * @brief Operational state for blocking operations.
     */
    enum class QueueStatus : uint8_t
    {
        Running = 0, /**< Queue is accepting elements. */
        Stopped = 1  /**< Queue is stopped; blocking ops return immediately. */
    };
    alignas(64) std::atomic<QueueStatus> _queue_status{QueueStatus::Running}; /**< Current status. */

public:
    /**
     * @brief Construct a new BoundedMPSCQueue.
     *
     * Initializes sequence numbers for each cell.
     */
    BoundedMPSCQueue() noexcept
    {
        for (std::size_t i = 0; i < _size; ++i)
        {
            _cells[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Destroy the BoundedMPSCQueue.
     *
     * Stops the queue and drains remaining elements.
     */
    ~BoundedMPSCQueue()
    {
        stop();
        while (try_pop())
        {
        }
    }

    BoundedMPSCQueue(const BoundedMPSCQueue&) = delete;            /**< Non-copyable. */
    BoundedMPSCQueue& operator=(const BoundedMPSCQueue&) = delete; /**< Non-assignable. */
    BoundedMPSCQueue(BoundedMPSCQueue&&) = delete;                 /**< Non-movable. */
    BoundedMPSCQueue& operator=(BoundedMPSCQueue&&) = delete;

    /**
     * @brief Set queue to running state, allowing blocking operations.
     */
    SYNC_TOOLS_ALWAYS_INLINE void run() noexcept
    {
        _queue_status.store(QueueStatus::Running, std::memory_order_release);
    }

    /**
     * @brief Stop the queue; blocking operations will return immediately.
     */
    SYNC_TOOLS_ALWAYS_INLINE void stop() noexcept
    {
        _queue_status.store(QueueStatus::Stopped, std::memory_order_release);
    }

    /**
     * @brief Blocking push: waits until space is available or stopped.
     *
     * @param v Const reference to value to enqueue.
     * @return true if enqueued, false if queue stopped.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool blocking_push(const T& v) noexcept
    {
        for (;;)
        {
            QueueStatus status = _queue_status.load(std::memory_order_relaxed);
            if (SYNC_TOOLS_UNLIKELY(status == QueueStatus::Stopped) &&
                SYNC_TOOLS_LIKELY(_queue_status.load(std::memory_order_acquire) == QueueStatus::Stopped))
            {
                return false;
            }
            else if (SYNC_TOOLS_LIKELY(try_push(v)))
            {
                return true;
            }
            details::cpu_relax();
        }
    }

    /**
     * @brief Blocking push by moving: waits until space is available or stopped.
     *
     * @param v Rvalue reference to value to enqueue.
     * @return true if enqueued, false if queue stopped.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool blocking_push(T&& v) noexcept
    {
        for (;;)
        {
            QueueStatus status = _queue_status.load(std::memory_order_relaxed);
            if (SYNC_TOOLS_UNLIKELY(status == QueueStatus::Stopped) &&
                SYNC_TOOLS_LIKELY(_queue_status.load(std::memory_order_acquire) == QueueStatus::Stopped))
            {
                return false;
            }
            else if (SYNC_TOOLS_LIKELY(try_push(std::move(v))))
            {
                return true;
            }
            details::cpu_relax();
        }
    }

    /**
     * @brief Try to enqueue without blocking.
     *
     * @param v Const reference to value to enqueue.
     * @return true if successful, false if full or stopped.
     */
    [[nodiscard]] SYNC_TOOLS_ALWAYS_INLINE bool try_push(const T& v) noexcept
    {
        if (SYNC_TOOLS_UNLIKELY(_queue_status.load(std::memory_order_relaxed) == QueueStatus::Stopped))
        {
            return false;
        }

        std::size_t pos = _head.load(std::memory_order_relaxed);
        for (;;)
        {
            Cell& cell = _cells[pos & _mask];
            std::size_t seq = cell.seq.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0)
            {
                if (_head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                {
                    new (&cell.storage) T(v);
                    cell.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (diff < 0)
            {
                return false;
            }
            else
            {
                pos = _head.load(std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Try to enqueue by moving without blocking.
     *
     * @param v Rvalue reference to value to enqueue.
     * @return true if successful, false if full or stopped.
     */
    [[nodiscard]] SYNC_TOOLS_ALWAYS_INLINE bool try_push(T&& v) noexcept
    {
        if (SYNC_TOOLS_UNLIKELY(_queue_status.load(std::memory_order_relaxed) == QueueStatus::Stopped))
        {
            return false;
        }

        std::size_t pos = _head.load(std::memory_order_relaxed);
        for (;;)
        {
            Cell& cell = _cells[pos & _mask];
            std::size_t seq = cell.seq.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0)
            {
                if (_head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                {
                    if constexpr (std::is_move_constructible_v<T>)
                    {
                        new (&cell.storage) T(std::move(v));
                    }
                    else
                    {
                        new (&cell.storage) T(v);
                    }
                    cell.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (diff < 0)
            {
                return false;
            }
            else
            {
                pos = _head.load(std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Try to dequeue without blocking, returning optional.
     *
     * @return optional with value if successful, or nullopt if empty.
     */
    [[nodiscard]] SYNC_TOOLS_ALWAYS_INLINE std::optional<T> try_pop() noexcept
    {
        std::size_t pos = _tail.load(std::memory_order_relaxed);
        Cell& cell = _cells[pos & _mask];
        std::size_t seq = cell.seq.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
        if (SYNC_TOOLS_LIKELY(diff == 0))
        {
            _tail.store(pos + 1, std::memory_order_relaxed);
            T* ptr = std::launder(reinterpret_cast<T*>(&cell.storage));
            if constexpr (std::is_move_constructible_v<T>)
            {
                T result = std::move(*ptr);
                ptr->~T();
                cell.seq.store(pos + _size, std::memory_order_release);
                return std::make_optional(std::move(result));
            }
            else
            {
                T result = *ptr;
                ptr->~T();
                cell.seq.store(pos + _size, std::memory_order_release);
                return result;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Try to dequeue into output reference without blocking.
     *
     * @param out Reference to store dequeued value.
     * @return true if successful, false if empty.
     */
    [[nodiscard]] SYNC_TOOLS_ALWAYS_INLINE bool try_pop(T& out) noexcept
    {
        std::size_t pos = _tail.load(std::memory_order_relaxed);
        Cell& cell = _cells[pos & _mask];
        std::size_t seq = cell.seq.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
        if (SYNC_TOOLS_LIKELY(diff == 0))
        {
            _tail.store(pos + 1, std::memory_order_relaxed);
            T* ptr = std::launder(reinterpret_cast<T*>(&cell.storage));
            if constexpr (std::is_move_assignable_v<T>)
            {
                out = std::move(*ptr);
            }
            else
            {
                out = *ptr;
            }
            ptr->~T();
            cell.seq.store(pos + _size, std::memory_order_release);
            return true;
        }
        return false;
    }

    /**
     * @brief Blocking pop: waits until element is available or stopped.
     *
     * @return optional with value if successful, or nullopt if stopped and empty.
     */
    SYNC_TOOLS_ALWAYS_INLINE std::optional<T> blocking_pop() noexcept
    {
        for (;;)
        {
            auto opt = try_pop();
            if (SYNC_TOOLS_LIKELY(opt))
            {
                return opt;
            }

            QueueStatus status = _queue_status.load(std::memory_order_relaxed);
            if (SYNC_TOOLS_UNLIKELY(status == QueueStatus::Stopped) &&
                SYNC_TOOLS_LIKELY(_queue_status.load(std::memory_order_acquire) == QueueStatus::Stopped))
            {
                return std::nullopt;
            }
            details::cpu_relax();
        }
    }

    /**
     * @brief Blocking pop into output reference: waits until element is available or stopped.
     *
     * @param out Reference to store dequeued value.
     * @return true if successful, false if stopped and empty.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool blocking_pop(T& out) noexcept
    {
        for (;;)
        {
            if (SYNC_TOOLS_LIKELY(try_pop(out)))
            {
                return true;
            }

            QueueStatus status = _queue_status.load(std::memory_order_relaxed);
            if (SYNC_TOOLS_UNLIKELY(status == QueueStatus::Stopped) &&
                SYNC_TOOLS_LIKELY(_queue_status.load(std::memory_order_acquire) == QueueStatus::Stopped))
            {
                return false;
            }
            details::cpu_relax();
        }
    }

    /**
     * @brief Get current number of elements in queue.
     *
     * @return Count of stored elements.
     */
    SYNC_TOOLS_ALWAYS_INLINE size_t size() const noexcept
    {
        return _head.load(std::memory_order_acquire) - _tail.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if queue is empty.
     *
     * @return true if empty.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool empty() const noexcept { return size() == 0; }

    /**
     * @brief Check if queue is full.
     *
     * @return true if full.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool full() const noexcept { return size() >= _size; }
};

}  // namespace SyncTools
