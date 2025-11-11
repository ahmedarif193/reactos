#include <intrin.h>

/*
 * Provide out-of-line fallbacks for __readmsr/__writemsr so Clang-built
 * binaries still link even when the compiler decides not to inline the
 * intrinsic bodies from intrin_x86.h.
 */

unsigned __int64 __readmsr(unsigned long Register)
{
    unsigned int low, high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(Register));
    return ((unsigned __int64)high << 32) | low;
}

void __writemsr(unsigned long Register, unsigned __int64 Value)
{
    unsigned int low = (unsigned int)Value;
    unsigned int high = (unsigned int)(Value >> 32);
    __asm__ __volatile__("wrmsr" : : "c"(Register), "a"(low), "d"(high));
}
