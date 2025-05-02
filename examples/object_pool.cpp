#include <sync_tools/object_pool.hpp>

struct Bullet
{
    float x, y, vx, vy;
};

int main()
{
    SyncTools::ObjectPool<Bullet> pool{/*initial*/ 4096};

    Bullet* b = pool.acquire();
    if (b)
    {
        // initialise and use bullet …
        pool.release(b);
    }
}
