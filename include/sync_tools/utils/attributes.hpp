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

#if defined(__clang__)
#define SYNC_TOOLS_LOCKABLE __attribute__((lockable))
#define SYNC_TOOLS_SCOPED_LOCKABLE __attribute__((scoped_lockable))
#define SYNC_TOOLS_EXCLUSIVE_LOCK_FUNCTION(...) __attribute__((exclusive_lock_function(__VA_ARGS__)))
#define SYNC_TOOLS_EXCLUSIVE_TRYLOCK_FUNCTION(...) __attribute__((exclusive_trylock_function(__VA_ARGS__)))
#define SYNC_TOOLS_UNLOCK_FUNCTION(...) __attribute__((unlock_function(__VA_ARGS__)))
#define SYNC_TOOLS_NO_THREAD_SAFETY_ANALYSIS __attribute__((no_thread_safety_analysis))
#else
#define SYNC_TOOLS_LOCKABLE
#define SYNC_TOOLS_SCOPED_LOCKABLE
#define SYNC_TOOLS_EXCLUSIVE_LOCK_FUNCTION(...)
#define SYNC_TOOLS_EXCLUSIVE_TRYLOCK_FUNCTION(...)
#define SYNC_TOOLS_UNLOCK_FUNCTION(...)
#define SYNC_TOOLS_NO_THREAD_SAFETY_ANALYSIS
#endif

#define SYNC_TOOLS_ALWAYS_INLINE inline __attribute__((always_inline))
#define SYNC_TOOLS_NOINLINE __attribute__((noinline))