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

class OnlyCopyable
{
public:
    OnlyCopyable() = default;

    OnlyCopyable(const OnlyCopyable&) = default;
    OnlyCopyable& operator=(const OnlyCopyable&) = default;

    OnlyCopyable(OnlyCopyable&&) = delete;
    OnlyCopyable& operator=(OnlyCopyable&&) = delete;
};

class OnlyMovable
{
public:
    OnlyMovable() = default;

    OnlyMovable(const OnlyMovable&) = delete;
    OnlyMovable& operator=(const OnlyMovable&) = delete;

    OnlyMovable(OnlyMovable&&) noexcept = default;
    OnlyMovable& operator=(OnlyMovable&&) noexcept = default;
};

class NoDefaultConstructor
{
public:
    explicit NoDefaultConstructor(int x) {}

    NoDefaultConstructor() = delete;
};

class DestructorCounter
{
public:
    explicit DestructorCounter(int* counter) : _counter(counter) {}

    DestructorCounter(const DestructorCounter&) = delete;
    DestructorCounter& operator=(const DestructorCounter&) = delete;

    DestructorCounter(DestructorCounter&& other) noexcept : _counter(other._counter) { other._counter = nullptr; }

    DestructorCounter& operator=(DestructorCounter&& other) noexcept
    {
        if (this != &other)
        {
            _counter = other._counter;
            other._counter = nullptr;
        }
        return *this;
    }

    ~DestructorCounter()
    {
        if (_counter)
        {
            ++(*_counter);
        }
    }

private:
    int* _counter;
};
