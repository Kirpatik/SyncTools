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

#include <linux/futex.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <cerrno>
#include <exception>

#include "utils/attributes.hpp"
#include "utils/cpu_relax.hpp"
#include "utils/likely.hpp"
#include "utils/tsan_checks.hpp"

namespace SyncTools
{
/**
 * @enum MutexState
 * @brief Internal state of the futex-based mutex.
 */
enum class MutexState : int
{
    Free = 0,     /**< Mutex is unlocked and available. */
    Locked = 1,   /**< Mutex is locked by a thread. */
    Contended = 3 /**< Mutex is contended; threads are sleeping on futex. */
};

/**
 * @class Mutex
 * @brief A lightweight, high-performance mutex using Linux futex for thread synchronization.
 *
 * This mutex supports simple fast-path locking via an atomic exchange with optional spinning
 * before blocking on a futex. ThreadSanitizer and deadlock checks can be enabled via compile flags.
 */
class SYNC_TOOLS_LOCKABLE Mutex
{
public:
    /**
     * @brief Construct a new Mutex object.
     *
     * Initializes the internal state to Free.
     */
    constexpr Mutex() noexcept = default;

    /**
     * @brief Destroy the Mutex object.
     */
    ~Mutex() noexcept = default;

    Mutex(const Mutex&) = delete;            /**< Non-copyable. */
    Mutex& operator=(const Mutex&) = delete; /**< Non-assignable. */

    /**
     * @brief Acquire the mutex, blocking until it is available.
     *
     * This function first tries a fast-path lock via atomic compare-and-swap. If the mutex
     * is contended or already locked, it will spin up to a configured count and then block on a futex.
     *
     * @pre Mutex must not be already locked by the calling thread (no recursive locking).
     * @post Mutex is owned by the calling thread.
     * @note No exceptions are thrown; on deadlock detection, asserts. ThreadSanitizer annotations applied.
     */
    SYNC_TOOLS_ALWAYS_INLINE void lock() noexcept SYNC_TOOLS_EXCLUSIVE_LOCK_FUNCTION()
    {
        SYNC_TOOLS_TSAN_PRE_LOCK();
#if defined(SYNC_TOOLS_HAS_TSAN) || defined(SYNC_TOOLS_ENABLE_DEADLOCK_CHECKS)
        if (_owner_set && pthread_equal(_owner, pthread_self()))
        {
            assert(false && "Mutex deadlock: recursive lock");
        }
#endif
        MutexState expected = MutexState::Free;
        if (SYNC_TOOLS_LIKELY(_state.compare_exchange_strong(expected, MutexState::Locked, std::memory_order_acquire,
                                                             std::memory_order_relaxed)))
        {
#if defined(SYNC_TOOLS_HAS_TSAN) || defined(SYNC_TOOLS_ENABLE_DEADLOCK_CHECKS)
            _owner = pthread_self();
            _owner_set = true;
#endif
            SYNC_TOOLS_TSAN_POST_LOCK();
            return;
        }
        _lock_slow();
    }

    /**
     * @brief Attempt to acquire the mutex without blocking.
     *
     * Tries a single atomic compare-and-swap. Returns immediately.
     *
     * @return true if the lock was acquired, false otherwise.
     * @note No exceptions are thrown; on deadlock detection, asserts.
     */
    [[nodiscard]] SYNC_TOOLS_ALWAYS_INLINE bool try_lock() noexcept SYNC_TOOLS_EXCLUSIVE_TRYLOCK_FUNCTION(true)
    {
        SYNC_TOOLS_TSAN_PRE_LOCK();
#if defined(SYNC_TOOLS_HAS_TSAN) || defined(SYNC_TOOLS_ENABLE_DEADLOCK_CHECKS)
        if (_owner_set && pthread_equal(_owner, pthread_self()))
        {
            assert(false && "Mutex deadlock: recursive try_lock");
        }
#endif
        MutexState expected = MutexState::Free;
        bool got = SYNC_TOOLS_LIKELY(_state.compare_exchange_strong(
            expected, MutexState::Locked, std::memory_order_acquire, std::memory_order_relaxed));
        if (got)
        {
#if defined(SYNC_TOOLS_HAS_TSAN) || defined(SYNC_TOOLS_ENABLE_DEADLOCK_CHECKS)
            _owner = pthread_self();
            _owner_set = true;
#endif
            SYNC_TOOLS_TSAN_POST_LOCK();
        }
        return got;
    }

    /**
     * @brief Release the mutex.
     *
     * If the mutex is contended, wakes one waiting thread via futex. Validates ownership in debug builds.
     *
     * @pre Mutex must be owned by the calling thread.
     * @post Mutex is free; one waiter may be woken.
     */
    SYNC_TOOLS_ALWAYS_INLINE void unlock() noexcept SYNC_TOOLS_UNLOCK_FUNCTION()
    {
#if defined(SYNC_TOOLS_HAS_TSAN) || defined(SYNC_TOOLS_ENABLE_DEADLOCK_CHECKS)
        assert(_owner_set && pthread_equal(_owner, pthread_self()) && "Mutex unlock: wrong thread");
        _owner_set = false;
        _owner = pthread_t{};
#endif
        SYNC_TOOLS_TSAN_PRE_UNLOCK();

        MutexState prev = _state.exchange(MutexState::Free, std::memory_order_release);
        if (SYNC_TOOLS_UNLIKELY(prev == MutexState::Contended))
        {
            _futex_wake();
        }

        SYNC_TOOLS_TSAN_POST_UNLOCK();
    }

private:
    /**
     * @brief Slow path for lock when fast-path fails.
     *
     * Spins for a preset count; if still contended, sets state to Contended and waits on futex.
     */
    SYNC_TOOLS_NOINLINE void _lock_slow() noexcept SYNC_TOOLS_NO_THREAD_SAFETY_ANALYSIS
    {
        const int max_spin = _spin_count;
        for (int spin = 0; spin < max_spin; ++spin)
        {
            int backoff = spin < 10 ? spin : 10;
            for (int i = 0; i < (1 << backoff); ++i)
            {
                details::cpu_relax();
            }

            MutexState observed = MutexState::Free;
            if (SYNC_TOOLS_LIKELY(_state.compare_exchange_strong(observed, MutexState::Locked,
                                                                 std::memory_order_acquire, std::memory_order_relaxed)))
            {
#if defined(SYNC_TOOLS_HAS_TSAN) || defined(SYNC_TOOLS_ENABLE_DEADLOCK_CHECKS)
                _owner = pthread_self();
                _owner_set = true;
#endif
                SYNC_TOOLS_TSAN_POST_LOCK();
                return;
            }
        }

        MutexState old = _state.exchange(MutexState::Contended, std::memory_order_acquire);
        while (static_cast<int>(old) & static_cast<int>(MutexState::Locked))
        {
            _futex_wait();
            old = _state.exchange(MutexState::Contended, std::memory_order_acquire);
        }

#if defined(SYNC_TOOLS_HAS_TSAN) || defined(SYNC_TOOLS_ENABLE_DEADLOCK_CHECKS)
        _owner = pthread_self();
        _owner_set = true;
#endif
        SYNC_TOOLS_TSAN_POST_LOCK();
    }

    /**
     * @brief Wait on futex until the mutex state changes.
     */
    SYNC_TOOLS_NOINLINE void _futex_wait() noexcept SYNC_TOOLS_NO_THREAD_SAFETY_ANALYSIS
    {
        long ret = syscall(SYS_futex, reinterpret_cast<int*>(&_state), FUTEX_WAIT_PRIVATE,
                           static_cast<int>(MutexState::Contended), nullptr, nullptr, 0);
        if (ret == -1)
        {
            int e = errno;
            if (e != EAGAIN && e != EINTR)
            {
                std::terminate();
            }
        }
    }

    /**
     * @brief Wake one thread waiting on the futex.
     */
    SYNC_TOOLS_NOINLINE void _futex_wake() noexcept SYNC_TOOLS_NO_THREAD_SAFETY_ANALYSIS
    {
        long ret = syscall(SYS_futex, reinterpret_cast<int*>(&_state), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
        if (ret == -1)
        {
            std::terminate();
        }
    }

    alignas(64) std::atomic<MutexState> _state{MutexState::Free}; /**< Atomic futex state. */

    static constexpr std::size_t _spin_count{22}; /**< Max spin iterations before blocking. */

#if defined(SYNC_TOOLS_HAS_TSAN) || defined(SYNC_TOOLS_ENABLE_DEADLOCK_CHECKS)
    pthread_t _owner{};     /**< Thread ID of the owner for deadlock checks. */
    bool _owner_set{false}; /**< Whether _owner is valid. */
#endif
};

}  // namespace SyncTools
