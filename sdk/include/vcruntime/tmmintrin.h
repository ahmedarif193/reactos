/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Intrinsics for the SSSE3 instruction set
 */

#pragma once
#ifndef _INCLUDED_TMM
#define _INCLUDED_TMM

#include "intrin_target.h"

/* When building with Clang, use Clang's own intrinsics headers instead. */
#if _VCRT_USE_CLANG_X86_INTRINSICS
#include_next <tmmintrin.h>
#elif _VCRT_ARM64_CODEGEN
/* ARM64: no x86 intrinsics */
#else

#include <emmintrin.h>

#ifdef __cplusplus
extern "C" {
#endif

extern __m128i __cdecl _mm_shuffle_epi8(__m128i, __m128i);
extern __m128i __cdecl _mm_alignr_epi8(__m128i, __m128i, const int);

#if defined(_MSC_VER) && !defined(__clang__)

#pragma intrinsic(_mm_shuffle_epi8)
#pragma intrinsic(_mm_alignr_epi8)

#else

#ifdef __clang__
#define __ATTRIBUTE_SSSE3__ __attribute__((__target__("ssse3"), __min_vector_width__(128)))
#else
#define __ATTRIBUTE_SSSE3__ __attribute__((__target__("ssse3")))
#endif
#define __INTRIN_INLINE_SSSE3 __INTRIN_INLINE __ATTRIBUTE_SSSE3__

__INTRIN_INLINE_SSSE3 __m128i __cdecl _mm_shuffle_epi8(__m128i __A, __m128i __B)
{
    return (__m128i)__builtin_ia32_pshufb128((__v16qi)__A, (__v16qi)__B);
}

#define _mm_alignr_epi8(A, B, I) \
    ((__m128i)__builtin_ia32_palignr128((__v2di)(__m128i)(A), (__v2di)(__m128i)(B), (int)(I) * 8))

#undef __INTRIN_INLINE_SSSE3
#undef __ATTRIBUTE_SSSE3__

#endif /* _MSC_VER */

#ifdef __cplusplus
}
#endif

#endif /* x86 */
#endif /* _INCLUDED_TMM */
