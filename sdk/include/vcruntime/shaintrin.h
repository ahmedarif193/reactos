/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Intrinsics for the Intel SHA instruction set
 */

#pragma once
#ifndef _INCLUDED_SHA
#define _INCLUDED_SHA

#include "intrin_target.h"

/* When building with Clang, use Clang's own intrinsics headers instead. */
#if _VCRT_USE_CLANG_X86_INTRINSICS
#include_next <shaintrin.h>
#elif _VCRT_ARM64_CODEGEN
/* ARM64: no x86 intrinsics */
#else

#include <emmintrin.h>

#ifdef __cplusplus
extern "C" {
#endif

extern __m128i __cdecl _mm_sha256msg1_epu32(__m128i, __m128i);
extern __m128i __cdecl _mm_sha256msg2_epu32(__m128i, __m128i);
extern __m128i __cdecl _mm_sha256rnds2_epu32(__m128i, __m128i, __m128i);

#if defined(_MSC_VER) && !defined(__clang__)

#pragma intrinsic(_mm_sha256msg1_epu32)
#pragma intrinsic(_mm_sha256msg2_epu32)
#pragma intrinsic(_mm_sha256rnds2_epu32)

#else

#ifdef __clang__
#define __ATTRIBUTE_SHA__ __attribute__((__target__("sha,sse2"), __min_vector_width__(128)))
#else
#define __ATTRIBUTE_SHA__ __attribute__((__target__("sha,sse2")))
#endif
#define __INTRIN_INLINE_SHA __INTRIN_INLINE __ATTRIBUTE_SHA__

__INTRIN_INLINE_SHA __m128i __cdecl _mm_sha256msg1_epu32(__m128i __A, __m128i __B)
{
    return (__m128i)__builtin_ia32_sha256msg1((__v4si)__A, (__v4si)__B);
}

__INTRIN_INLINE_SHA __m128i __cdecl _mm_sha256msg2_epu32(__m128i __A, __m128i __B)
{
    return (__m128i)__builtin_ia32_sha256msg2((__v4si)__A, (__v4si)__B);
}

__INTRIN_INLINE_SHA __m128i __cdecl _mm_sha256rnds2_epu32(__m128i __A, __m128i __B, __m128i __C)
{
    return (__m128i)__builtin_ia32_sha256rnds2((__v4si)__A, (__v4si)__B, (__v4si)__C);
}

#undef __INTRIN_INLINE_SHA
#undef __ATTRIBUTE_SHA__

#endif /* _MSC_VER */

#ifdef __cplusplus
}
#endif

#endif /* x86 */
#endif /* _INCLUDED_SHA */
