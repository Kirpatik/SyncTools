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
 * @class BoundedSPSCQueue
 * @brief A bounded single-producer single-consumer queue with lock-free operations.
 *
 * Implements a circular buffer of fixed size (power-of-two) for passing elements between
 * one producer and one consumer thread without locks. Supports both non-blocking and
 * blocking push/pop until stopped.
 *
 * @tparam T Type of elements stored in the queue.
 * @tparam _size Capacity of the queue; must be a power of two.
 */
template <typename T, std::size_t _size>
class BoundedSPSCQueue
{
    static_assert((_size & (_size - 1)) == 0, "size must be power of two");
    using storage_t = std::aligned_storage_t<sizeof(T), alignof(T)>;

    static constexpr std::size_t _mask = _size - 1; /**< Mask for index wrap-around. */
    alignas(alignof(T)) storage_t _buffer[_size];   /**< Aligned storage for elements. */

    alignas(64) std::atomic<std::size_t> _head{0}; /**< Next write index (producer). */
    alignas(64) std::atomic<std::size_t> _tail{0}; /**< Next read index (consumer).  */

    /**
     * @enum QueueStatus
     * @brief Running or stopped state for push/pop operations.
     */
    enum class QueueStatus : uint8_t
    {
        Running = 0, /**< Queue is operational. */
        Stopped = 1  /**< Queue is stopped; no further blocking operations. */
    };
    alignas(64) std::atomic<QueueStatus> _status{QueueStatus::Running}; /**< Current status. */

public:
    /**
     * @brief Default constructor.
     *
     * Initializes head, tail, and status to initial values.
     */
    BoundedSPSCQueue() noexcept = default;

    /**
     * @brief Destructor drains remaining elements after stopping the queue.
     */
    ~BoundedSPSCQueue()
    {
        stop();
        while (try_pop())
        {
        }
    }

    BoundedSPSCQueue(const BoundedSPSCQueue&) = delete;            /**< Non-copyable. */
    BoundedSPSCQueue& operator=(const BoundedSPSCQueue&) = delete; /**< Non-assignable. */
    BoundedSPSCQueue(BoundedSPSCQueue&&) = delete;                 /**< Non-movable. */
    BoundedSPSCQueue& operator=(BoundedSPSCQueue&&) = delete;

    /**
     * @brief Set queue to running state, allowing blocking operations.
     */
    SYNC_TOOLS_ALWAYS_INLINE void run() noexcept { _status.store(QueueStatus::Running, std::memory_order_release); }

    /**
     * @brief Stop the queue; blocking ops will return immediately.
     */
    SYNC_TOOLS_ALWAYS_INLINE void stop() noexcept { _status.store(QueueStatus::Stopped, std::memory_order_release); }

    /**
     * @brief Attempt to enqueue an element without blocking.
     *
     * @param v Const reference to the value to push.
     * @return true if enqueued, false if full or stopped.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool try_push(const T& v) noexcept
    {
        if (SYNC_TOOLS_UNLIKELY(_status.load(std::memory_order_relaxed) == QueueStatus::Stopped))
        {
            return false;
        }

        const auto head = _head.load(std::memory_order_relaxed);
        const auto next = (head + 1) & _mask;
        if (next == _tail.load(std::memory_order_acquire))
        {
            return false;
        }

        new (&_buffer[head]) T(v);
        _head.store(next, std::memory_order_release);
        return true;
    }

    /**
     * @brief Attempt to enqueue by moving value without blocking.
     *
     * @param v Rvalue reference to the value to push.
     * @return true if enqueued, false if full or stopped.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool try_push(T&& v) noexcept
    {
        if (SYNC_TOOLS_UNLIKELY(_status.load(std::memory_order_relaxed) == QueueStatus::Stopped))
        {
            return false;
        }

        const auto head = _head.load(std::memory_order_relaxed);
        const auto next = (head + 1) & _mask;
        if (SYNC_TOOLS_UNLIKELY(next == _tail.load(std::memory_order_acquire)))
        {
            return false;
        }

        if constexpr (std::is_move_constructible_v<T>)
        {
            new (&_buffer[head]) T(std::move(v));
        }
        else
        {
            new (&_buffer[head]) T(v);
        }

        _head.store(next, std::memory_order_release);
        return true;
    }

    /**
     * @brief Block until the element is enqueued or queue is stopped.
     *
     * @param v Const reference to the value to push.
     * @return true if enqueued, false if stopped.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool blocking_push(const T& v) noexcept
    {
        for (;;)
        {
            if (SYNC_TOOLS_LIKELY(try_push(v)))
            {
                return true;
            }
            if (SYNC_TOOLS_UNLIKELY(_status.load(std::memory_order_relaxed) == QueueStatus::Stopped &&
                                    _status.load(std::memory_order_acquire) == QueueStatus::Stopped))
            {
                return false;
            }
            details::cpu_relax();
        }
    }

    /**
     * @brief Block until the element is enqueued by moving or queue is stopped.
     *
     * @param v Rvalue reference to the value to push.
     * @return true if enqueued, false if stopped.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool blocking_push(T&& v) noexcept
    {
        for (;;)
        {
            if (SYNC_TOOLS_LIKELY(try_push(std::move(v))))
            {
                return true;
            }
            if (SYNC_TOOLS_UNLIKELY(_status.load(std::memory_order_relaxed) == QueueStatus::Stopped &&
                                    _status.load(std::memory_order_acquire) == QueueStatus::Stopped))
            {
                return false;
            }
            details::cpu_relax();
        }
    }

    /**
     * @brief Attempt to dequeue an element without output parameter.
     *
     * @return Optional containing the popped value, or nullopt if empty.
     */
    SYNC_TOOLS_ALWAYS_INLINE std::optional<T> try_pop() noexcept
    {
        const auto tail = _tail.load(std::memory_order_relaxed);
        if (SYNC_TOOLS_UNLIKELY(tail == _head.load(std::memory_order_acquire)))
        {
            return std::nullopt;
        }

        T* ptr = std::launder(reinterpret_cast<T*>(&_buffer[tail]));
        if constexpr (std::is_move_constructible_v<T>)
        {
            T result = std::move(*ptr);
            ptr->~T();
            _tail.store((tail + 1) & _mask, std::memory_order_release);
            return std::make_optional(std::move(result));
        }
        else
        {
            T result = *ptr;
            ptr->~T();
            _tail.store((tail + 1) & _mask, std::memory_order_release);
            return std::make_optional(result);
        }
    }

    /**
     * @brief Attempt to dequeue an element into an output reference.
     *
     * @param out Reference to store popped value.
     * @return true if an element was popped, false if empty.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool try_pop(T& out) noexcept
    {
        const auto tail = _tail.load(std::memory_order_relaxed);
        if (SYNC_TOOLS_UNLIKELY(tail == _head.load(std::memory_order_acquire)))
        {
            return false;
        }

        T* ptr = std::launder(reinterpret_cast<T*>(&_buffer[tail]));
        if constexpr (std::is_move_assignable_v<T>)
        {
            out = std::move(*ptr);
        }
        else
        {
            out = *ptr;
        }

        ptr->~T();
        _tail.store((tail + 1) & _mask, std::memory_order_release);
        return true;
    }

    /**
     * @brief Block until an element is dequeued or queue is stopped.
     *
     * @return Optional containing the popped value, or nullopt if stopped and empty.
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
            if (SYNC_TOOLS_UNLIKELY(_status.load(std::memory_order_relaxed) == QueueStatus::Stopped &&
                                    _status.load(std::memory_order_acquire) == QueueStatus::Stopped))
            {
                return std::nullopt;
            }
            details::cpu_relax();
        }
    }

    /**
     * @brief Block until an element is dequeued into an output reference or stopped.
     *
     * @param out Reference to store popped value.
     * @return true if an element was popped, false if stopped and empty.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool blocking_pop(T& out) noexcept
    {
        for (;;)
        {
            if (SYNC_TOOLS_LIKELY(try_pop(out)))
            {
                return true;
            }
            if (SYNC_TOOLS_UNLIKELY(_status.load(std::memory_order_relaxed) == QueueStatus::Stopped &&
                                    _status.load(std::memory_order_acquire) == QueueStatus::Stopped))
            {
                return false;
            }
            details::cpu_relax();
        }
    }

    /**
     * @brief Check if the queue is empty.
     *
     * @return true if empty, false otherwise.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool empty() const noexcept
    {
        return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if the queue is full.
     *
     * @return true if full, false otherwise.
     */
    SYNC_TOOLS_ALWAYS_INLINE bool full() const noexcept
    {
        const auto next = (_head.load(std::memory_order_relaxed) + 1) & _mask;
        return next == _tail.load(std::memory_order_acquire);
    }

    /**
     * @brief Get current number of elements in the queue.
     *
     * @return Number of items stored.
     */
    SYNC_TOOLS_ALWAYS_INLINE std::size_t size() const noexcept
    {
        const auto head = _head.load(std::memory_order_acquire);
        const auto tail = _tail.load(std::memory_order_acquire);
        return (head + _size - tail) & _mask;
    }
};

}  // namespace SyncTools
