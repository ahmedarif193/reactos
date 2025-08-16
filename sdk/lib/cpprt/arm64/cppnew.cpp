/*
 * ARM64 C++ new/delete operators
 */

#include <stdlib.h>

/* operator new */
void* operator new(size_t size)
{
    return malloc(size);
}

/* operator new[] */
void* operator new[](size_t size)
{
    return malloc(size);
}

/* operator delete */
void operator delete(void* ptr) noexcept
{
    free(ptr);
}

/* operator delete[] */
void operator delete[](void* ptr) noexcept
{
    free(ptr);
}

/* operator delete with size */
void operator delete(void* ptr, size_t) noexcept
{
    free(ptr);
}

/* operator delete[] with size */
void operator delete[](void* ptr, size_t) noexcept
{
    free(ptr);
}

/* std::terminate - called when exception handling fails */
namespace std {
    void terminate() noexcept
    {
        /* TODO: Call registered terminate handler */
        /* For now, just abort */
        while(1);
    }
}