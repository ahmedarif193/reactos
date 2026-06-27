//
// isa_available.c
//
//      Copyright (c) 2024 Timo Kreuzer
//
// Implementation of __isa_available_init.
//
// SPDX-License-Identifier: MIT
//

#include <isa_availability.h>
#include <intrin.h>
#include <windef.h>
#include <winbase.h>

extern "C" { int __isa_available = 0; }

extern "C"
int
__cdecl
__isa_available_init(void)
{
#if _VCRT_X86_INTRINSICS
    if (IsProcessorFeaturePresent(PF_AVX512F_INSTRUCTIONS_AVAILABLE))
    {
        __isa_available = __ISA_AVAILABLE_AVX512;
    }
    else if (IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE))
    {
        __isa_available = __ISA_AVAILABLE_AVX2;
    }
    else if (IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE))
    {
        __isa_available = __ISA_AVAILABLE_AVX;
    }
    else if (IsProcessorFeaturePresent(PF_SSE4_2_INSTRUCTIONS_AVAILABLE))
    {
        __isa_available = __ISA_AVAILABLE_SSE42;
    }
    else if (IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE))
    {
        __isa_available = __ISA_AVAILABLE_SSE2;
    }
    else
    {
        __isa_available = __ISA_AVAILABLE_X86;
    }
#elif defined(_M_ARM) || _VCRT_ARM64_INTRINSICS
    // CHECKME: Is this correct?
    if (IsProcessorFeaturePresent(PF_ARM_V8_INSTRUCTIONS_AVAILABLE))
    {
#if _VCRT_ARM64_INTRINSICS
        __isa_available = __ISA_AVAILABLE_NEON_ARM64;
#else
        __isa_available = __ISA_AVAILABLE_NEON;
#endif
    }
    else
    {
        __isa_available = __ISA_AVAILABLE_ARMNT;
    }
#endif

    return 0;
}
