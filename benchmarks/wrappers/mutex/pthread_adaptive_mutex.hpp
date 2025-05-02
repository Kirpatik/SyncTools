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

#include <pthread.h>

class PThreadAdaptiveMutex
{
public:
    PThreadAdaptiveMutex()
    {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
        pthread_mutex_init(&_m, &attr);
        pthread_mutexattr_destroy(&attr);
    }

    ~PThreadAdaptiveMutex() { pthread_mutex_destroy(&_m); }

    void lock() { pthread_mutex_lock(&_m); }

    void unlock() { pthread_mutex_unlock(&_m); }

private:
    pthread_mutex_t _m;
};
