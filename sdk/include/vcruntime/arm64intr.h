/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64 intriniscs
 * COPYRIGHT:   Copyright 2025 Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#pragma once

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum _tag_ARM64INTR_BARRIER_TYPE
{
    _ARM64_BARRIER_SY     = 0xF,
    _ARM64_BARRIER_ST     = 0xE,
    _ARM64_BARRIER_LD     = 0xD,
    _ARM64_BARRIER_ISH    = 0xB,
    _ARM64_BARRIER_ISHST  = 0xA,
    _ARM64_BARRIER_ISHLD  = 0x9,
    _ARM64_BARRIER_NSH    = 0x7,
    _ARM64_BARRIER_NSHST  = 0x6,
    _ARM64_BARRIER_NSHLD  = 0x5,
    _ARM64_BARRIER_OSH    = 0x3,
    _ARM64_BARRIER_OSHST  = 0x2,
    _ARM64_BARRIER_OSHLD  = 0x1
} _ARM64INTR_BARRIER_TYPE;

#if defined(__GNUC__)
#if !__has_builtin(__dmb)
__forceinline void __dmb(unsigned int _Type)
{
    (void)_Type;
    __asm__ __volatile__("dmb sy" ::: "memory");
}
#else
void __dmb(unsigned int);
#endif

#if !__has_builtin(__dsb)
__forceinline void __dsb(unsigned int _Type)
{
    (void)_Type;
    __asm__ __volatile__("dsb sy" ::: "memory");
}
#else
void __dsb(unsigned int);
#endif

#if !__has_builtin(__isb)
__forceinline void __isb(unsigned int _Type)
{
    (void)_Type;
    __asm__ __volatile__("isb sy" ::: "memory");
}
#else
void __isb(unsigned int);
#endif

#if !__has_builtin(_disable)
__forceinline void _disable(void)
{
    __asm__ __volatile__("msr daifset, #2" ::: "memory");
}
#else
void _disable(void);
#endif

#if !__has_builtin(_enable)
__forceinline void _enable(void)
{
    __asm__ __volatile__("msr daifclr, #2" ::: "memory");
}
#else
void _enable(void);
#endif
#if !__has_builtin(_rotl)
__forceinline unsigned int _rotl(unsigned int value, int shift)
{
    shift &= 31;
    return (value << shift) | (value >> (32 - shift));
}
#else
unsigned int _rotl(unsigned int, int);
#endif

#if !__has_builtin(_rotr)
__forceinline unsigned int _rotr(unsigned int value, int shift)
{
    shift &= 31;
    return (value >> shift) | (value << (32 - shift));
}
#else
unsigned int _rotr(unsigned int, int);
#endif

#if !__has_builtin(_rotl64)
__forceinline unsigned long long _rotl64(unsigned long long value, int shift)
{
    shift &= 63;
    return (value << shift) | (value >> (64 - shift));
}
#else
unsigned long long _rotl64(unsigned long long, int);
#endif

#if !__has_builtin(_rotr64)
__forceinline unsigned long long _rotr64(unsigned long long value, int shift)
{
    shift &= 63;
    return (value >> shift) | (value << (64 - shift));
}
#else
unsigned long long _rotr64(unsigned long long, int);
#endif

#if !__has_builtin(__fastfail)
__forceinline void __fastfail(unsigned int _Code)
{
    (void)_Code;
    __builtin_trap();
}
#else
void __fastfail(unsigned int);
#endif

#else
void __dmb(unsigned int _Type);
void __dsb(unsigned int _Type);
void __isb(unsigned int _Type);

#pragma intrinsic(__dmb)
#pragma intrinsic(__dsb)
#pragma intrinsic(__isb)
#pragma intrinsic(_disable)
#pragma intrinsic(_enable)
#pragma intrinsic(__fastfail)
#endif

#if defined(__cplusplus)
} // extern "C"
#endif
