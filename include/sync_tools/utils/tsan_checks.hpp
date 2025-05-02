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

#if defined(__has_feature)

#if __has_feature(thread_sanitizer) || defined(SYNC_TOOLS_ENABLE_DEADLOCK_CHECKS)
#include <pthread.h>

#include <cassert>
#endif

#if __has_feature(thread_sanitizer)
extern "C"
{
    void __tsan_mutex_pre_lock(void* addr, unsigned flags);
    void __tsan_mutex_post_lock(void* addr, unsigned flags, int recursion);
    int __tsan_mutex_pre_unlock(void* addr, unsigned flags);
    void __tsan_mutex_post_unlock(void* addr, unsigned flags);
}
#define SYNC_TOOLS_HAS_TSAN 1
#define SYNC_TOOLS_TSAN_PRE_LOCK() __tsan_mutex_pre_lock(this, 0)
#define SYNC_TOOLS_TSAN_POST_LOCK() __tsan_mutex_post_lock(this, 0, 0)
#define SYNC_TOOLS_TSAN_PRE_UNLOCK() __tsan_mutex_pre_unlock(this, 0)
#define SYNC_TOOLS_TSAN_POST_UNLOCK() __tsan_mutex_post_unlock(this, 0)

#else
#define SYNC_TOOLS_TSAN_PRE_LOCK()
#define SYNC_TOOLS_TSAN_POST_LOCK()
#define SYNC_TOOLS_TSAN_PRE_UNLOCK()
#define SYNC_TOOLS_TSAN_POST_UNLOCK()
#endif

#else
#define SYNC_TOOLS_TSAN_PRE_LOCK()
#define SYNC_TOOLS_TSAN_POST_LOCK()
#define SYNC_TOOLS_TSAN_PRE_UNLOCK()
#define SYNC_TOOLS_TSAN_POST_UNLOCK()
#endif