/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AES and carry-less multiplication intrinsics
 */

#pragma once
#ifndef _INCLUDED_WMM
#define _INCLUDED_WMM

#include "intrin_target.h"

/* When building with Clang, use Clang's own intrinsics headers instead. */
#if _VCRT_USE_CLANG_X86_INTRINSICS
#include_next <wmmintrin.h>
#elif _VCRT_ARM64_CODEGEN
/* ARM64: no x86 intrinsics */
#else

#include <emmintrin.h>

#ifdef __cplusplus
extern "C" {
#endif

extern __m128i __cdecl _mm_aesdec_si128(__m128i, __m128i);
extern __m128i __cdecl _mm_aesdeclast_si128(__m128i, __m128i);
extern __m128i __cdecl _mm_aesenc_si128(__m128i, __m128i);
extern __m128i __cdecl _mm_aesenclast_si128(__m128i, __m128i);
extern __m128i __cdecl _mm_aesimc_si128(__m128i);
extern __m128i __cdecl _mm_aeskeygenassist_si128(__m128i, const int);
extern __m128i __cdecl _mm_clmulepi64_si128(__m128i, __m128i, const int);

#if defined(_MSC_VER) && !defined(__clang__)

#pragma intrinsic(_mm_aesdec_si128)
#pragma intrinsic(_mm_aesdeclast_si128)
#pragma intrinsic(_mm_aesenc_si128)
#pragma intrinsic(_mm_aesenclast_si128)
#pragma intrinsic(_mm_aesimc_si128)
#pragma intrinsic(_mm_aeskeygenassist_si128)
#pragma intrinsic(_mm_clmulepi64_si128)

#else /* _MSC_VER */

#define __ATTRIBUTE_AES__ __attribute__((__target__("aes,sse2")))
#define __INTRIN_INLINE_AES __INTRIN_INLINE __ATTRIBUTE_AES__

__INTRIN_INLINE_AES __m128i __cdecl _mm_aesdec_si128(__m128i __A, __m128i __B)
{
    return (__m128i)__builtin_ia32_aesdec128((__v2di)__A, (__v2di)__B);
}

__INTRIN_INLINE_AES __m128i __cdecl _mm_aesdeclast_si128(__m128i __A, __m128i __B)
{
    return (__m128i)__builtin_ia32_aesdeclast128((__v2di)__A, (__v2di)__B);
}

__INTRIN_INLINE_AES __m128i __cdecl _mm_aesenc_si128(__m128i __A, __m128i __B)
{
    return (__m128i)__builtin_ia32_aesenc128((__v2di)__A, (__v2di)__B);
}

__INTRIN_INLINE_AES __m128i __cdecl _mm_aesenclast_si128(__m128i __A, __m128i __B)
{
    return (__m128i)__builtin_ia32_aesenclast128((__v2di)__A, (__v2di)__B);
}

__INTRIN_INLINE_AES __m128i __cdecl _mm_aesimc_si128(__m128i __A)
{
    return (__m128i)__builtin_ia32_aesimc128((__v2di)__A);
}

#define _mm_aeskeygenassist_si128(A, I) ((__m128i)__builtin_ia32_aeskeygenassist128((__v2di)(__m128i)(A), (int)(I)))
#define _mm_clmulepi64_si128(A, B, I) ((__m128i)__builtin_ia32_pclmulqdq128((__v2di)(__m128i)(A), (__v2di)(__m128i)(B), (int)(I)))

#undef __INTRIN_INLINE_AES
#undef __ATTRIBUTE_AES__

#endif /* _MSC_VER */

#ifdef __cplusplus
}
#endif

#endif /* x86 */
#endif /* _INCLUDED_WMM */
