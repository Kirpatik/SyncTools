#include <gtest/gtest.h>

#include "object_pool.hpp"
#include "utils/test_classes.hpp"

using namespace SyncTools;

TEST(ObjectPoolTests, HandlesMoveOnly)
{
    ObjectPool<OnlyMovable> pool(8);
    OnlyMovable* a = pool.acquire();
    ASSERT_NE(a, nullptr);
    pool.release(a);
}

TEST(ObjectPoolTests, HandlesCopyOnly)
{
    ObjectPool<OnlyCopyable> pool(8);
    OnlyCopyable* a = pool.acquire();
    ASSERT_NE(a, nullptr);
    pool.release(a);
}

TEST(ObjectPoolTests, HandlesNoDefaultConstructor)
{
    ObjectPool<NoDefaultConstructor> pool(8);
    NoDefaultConstructor* a = pool.acquire(1);
    ASSERT_NE(a, nullptr);
    pool.release(a);
}

TEST(ObjectPoolTests, DestructorRunsOnRelease)
{
    int counter = 0;
    {
        ObjectPool<DestructorCounter> pool(2);
        DestructorCounter* obj = pool.acquire(&counter);
        ASSERT_NE(obj, nullptr);
        pool.release(obj);
    }
    EXPECT_EQ(counter, 1);
}

TEST(ThreadCachedObjectPoolTests, BasicAcquireRelease)
{
    ThreadCachedObjectPool<DestructorCounter> pool(4);
    int counter = 0;
    DestructorCounter* obj1 = pool.acquire(&counter);
    EXPECT_NE(obj1, nullptr);
    pool.release(obj1);
    pool.flush();
    EXPECT_EQ(counter, 1);
}
