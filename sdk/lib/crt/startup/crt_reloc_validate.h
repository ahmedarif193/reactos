#ifndef CRT_RELOC_VALIDATE_H
#define CRT_RELOC_VALIDATE_H

#include <stdint.h>

#ifndef USER_SPACE_UPPER_BOUND_X64
#define USER_SPACE_UPPER_BOUND_X64 0x0000800000000000ULL
#endif

#ifndef USER_SPACE_UPPER_BOUND_X86
/* Conservative 2GB default. Build with -DUSER_SPACE_UPPER_BOUND_X86=0xC0000000U for /3GB */
#define USER_SPACE_UPPER_BOUND_X86 0x80000000U
#endif

static inline int is_user_address(const void *ptr)
{
    uintptr_t addr = (uintptr_t)ptr;

#if defined(_WIN64)
    /* Check canonicality: top 16 bits must be all 0s or all 1s */
    uint64_t top = (uint64_t)addr >> 48;
    if (top != 0 && top != 0xFFFF)
        return 0;

    return addr < USER_SPACE_UPPER_BOUND_X64;
#else
    return addr < USER_SPACE_UPPER_BOUND_X86;
#endif
}

#endif /* CRT_RELOC_VALIDATE_H */
