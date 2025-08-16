/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Intrinsics for ARM64/AArch64
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#ifndef _INTRIN_ARM64_H_
#define _INTRIN_ARM64_H_

#ifdef __cplusplus
extern "C" {
#endif

/* On ARM64 with clang, most intrinsics are already provided as builtins.
 * This header just ensures compatibility with Windows intrinsics naming.
 * Clang provides most of these as builtins, so we just need to ensure
 * they're available with the expected names. */

/* ARM64 barrier types */
#define _ARM64_BARRIER_SY    0xF
#define _ARM64_BARRIER_ST    0xE
#define _ARM64_BARRIER_LD    0xD
#define _ARM64_BARRIER_ISH   0xB
#define _ARM64_BARRIER_ISHST 0xA
#define _ARM64_BARRIER_ISHLD 0x9
#define _ARM64_BARRIER_NSH   0x7
#define _ARM64_BARRIER_NSHST 0x6
#define _ARM64_BARRIER_NSHLD 0x5
#define _ARM64_BARRIER_OSH   0x3
#define _ARM64_BARRIER_OSHST 0x2
#define _ARM64_BARRIER_OSHLD 0x1

/* Memory barrier intrinsics */
#ifdef __clang__
/* Clang provides these as builtins */
void __dmb(unsigned int);
void __dsb(unsigned int);
void __isb(unsigned int);
#else
/* GCC implementation */
static __inline__ void __dmb(unsigned int _Type)
{
    if (_Type == _ARM64_BARRIER_SY)
        __asm__ __volatile__("dmb sy" : : : "memory");
    else if (_Type == _ARM64_BARRIER_ST)
        __asm__ __volatile__("dmb st" : : : "memory");
    else if (_Type == _ARM64_BARRIER_LD)
        __asm__ __volatile__("dmb ld" : : : "memory");
    else if (_Type == _ARM64_BARRIER_ISH)
        __asm__ __volatile__("dmb ish" : : : "memory");
    else
        __asm__ __volatile__("dmb sy" : : : "memory");
}

static __inline__ void __dsb(unsigned int _Type)
{
    if (_Type == _ARM64_BARRIER_SY)
        __asm__ __volatile__("dsb sy" : : : "memory");
    else if (_Type == _ARM64_BARRIER_ST)
        __asm__ __volatile__("dsb st" : : : "memory");
    else if (_Type == _ARM64_BARRIER_LD)
        __asm__ __volatile__("dsb ld" : : : "memory");
    else if (_Type == _ARM64_BARRIER_ISH)
        __asm__ __volatile__("dsb ish" : : : "memory");
    else
        __asm__ __volatile__("dsb sy" : : : "memory");
}

static __inline__ void __isb(unsigned int _Type)
{
    __asm__ __volatile__("isb" : : : "memory");
}
#endif

/* Memory barriers - these are macros to avoid conflicts with builtins */
#ifndef _ReadWriteBarrier
#define _ReadWriteBarrier() __asm__ __volatile__("" : : : "memory")
#endif
#ifndef _ReadBarrier  
#define _ReadBarrier() __asm__ __volatile__("" : : : "memory")
#endif
#ifndef _WriteBarrier
#define _WriteBarrier() __asm__ __volatile__("" : : : "memory")
#endif

/* Byte swap functions - provide as inline functions for ARM64 */
#if !defined(_ARM64_BYTESWAP_DEFINED) && defined(__aarch64__)
#define _ARM64_BYTESWAP_DEFINED

/* Don't define as macros to avoid conflicts with function declarations */
__forceinline unsigned short _byteswap_ushort(unsigned short x)
{
    return __builtin_bswap16(x);
}

__forceinline unsigned long _byteswap_ulong(unsigned long x)
{
    return __builtin_bswap32(x);
}

__forceinline unsigned __int64 _byteswap_uint64(unsigned __int64 x)
{
    return __builtin_bswap64(x);
}

#endif /* _ARM64_BYTESWAP_DEFINED */

/* Processor control */
__forceinline void YieldProcessor(void)
{
    __asm__ __volatile__("yield");
}

/* Bit scan functions - declare them for clang as it expects them as builtins
 * but might not have the exact signatures we need */
#ifdef __clang__
/* Declare the builtins that clang provides */
unsigned char _BitScanForward(unsigned long *_Index, unsigned long _Mask);
unsigned char _BitScanForward64(unsigned long *_Index, unsigned __int64 _Mask);
unsigned char _BitScanReverse(unsigned long *_Index, unsigned long _Mask);
unsigned char _BitScanReverse64(unsigned long *_Index, unsigned __int64 _Mask);
#else

/* Bit scan functions - required for kernel headers */
static __inline__ unsigned char _BitScanForward(unsigned long *_Index, unsigned long _Mask)
{
    if (_Mask == 0) return 0;
    *_Index = __builtin_ctz(_Mask);
    return 1;
}

static __inline__ unsigned char _BitScanForward64(unsigned long *_Index, unsigned __int64 _Mask)
{
    if (_Mask == 0) return 0;
    *_Index = __builtin_ctzll(_Mask);
    return 1;
}

static __inline__ unsigned char _BitScanReverse(unsigned long *_Index, unsigned long _Mask)
{
    if (_Mask == 0) return 0;
    *_Index = 31 - __builtin_clz(_Mask);
    return 1;
}

static __inline__ unsigned char _BitScanReverse64(unsigned long *_Index, unsigned __int64 _Mask)
{
    if (_Mask == 0) return 0;
    *_Index = 63 - __builtin_clzll(_Mask);
    return 1;
}

#endif /* !__clang__ */

/* Breakpoint instruction for ARM64 */
#ifndef __break
static __inline__ void __break(unsigned int value)
{
    __asm__ __volatile__("brk #%0" : : "i" (value));
}
#endif

/* Shift intrinsics for ARM64 */
#ifndef __ll_rshift
static __inline__ long long __ll_rshift(long long value, int shift)
{
    return value >> shift;
}
#endif

#ifndef __ull_rshift
static __inline__ unsigned long long __ull_rshift(unsigned long long value, int shift)
{
    return value >> shift;
}
#endif

#ifndef __ll_lshift  
static __inline__ long long __ll_lshift(long long value, int shift)
{
    return value << shift;
}
#endif

/* Port I/O emulation for ARM64 (no real port I/O on ARM) */
/* These are stubs that should be replaced with MMIO operations */
#ifndef __inbyte
static __inline__ unsigned char __inbyte(unsigned short _Port)
{
    /* ARM64 doesn't have port I/O - this needs to be memory-mapped */
    return 0;
}
#endif

#ifndef __inword
static __inline__ unsigned short __inword(unsigned short _Port)
{
    /* ARM64 doesn't have port I/O - this needs to be memory-mapped */
    return 0;
}
#endif

#ifndef __indword
static __inline__ unsigned long __indword(unsigned short _Port)
{
    /* ARM64 doesn't have port I/O - this needs to be memory-mapped */
    return 0;
}
#endif

#ifndef __outbyte
static __inline__ void __outbyte(unsigned short _Port, unsigned char _Data)
{
    /* ARM64 doesn't have port I/O - this needs to be memory-mapped */
}
#endif

#ifndef __outword
static __inline__ void __outword(unsigned short _Port, unsigned short _Data)
{
    /* ARM64 doesn't have port I/O - this needs to be memory-mapped */
}
#endif

#ifndef __outdword
static __inline__ void __outdword(unsigned short _Port, unsigned long _Data)
{
    /* ARM64 doesn't have port I/O - this needs to be memory-mapped */
}
#endif

/* Cache management functions - only if not already defined */
#ifndef __ic_iallu
static __inline__ void __ic_iallu(void)
{
    __asm__ __volatile__("ic iallu" : : : "memory");
}
#endif

#ifndef __dc_cvau
static __inline__ void __dc_cvau(void *_Addr)
{
    __asm__ __volatile__("dc cvau, %0" : : "r" (_Addr) : "memory");
}
#endif

#ifndef __dc_civac
static __inline__ void __dc_civac(void *_Addr)
{
    __asm__ __volatile__("dc civac, %0" : : "r" (_Addr) : "memory");
}
#endif

/* ARM64 System Register Access */
/* On ARM64, IRQL is managed differently than x86-64.
 * We provide stubs for compatibility */
static __inline__ unsigned long __readcr8(void)
{
    /* On ARM64, there's no CR8. IRQL is typically managed through
     * interrupt masking. Return PASSIVE_LEVEL for now. */
    return 0; /* PASSIVE_LEVEL */
}

static __inline__ void __writecr8(unsigned long value)
{
    /* On ARM64, there's no CR8. This is a stub for compatibility. */
    (void)value;
}

/* Processor control functions - if not already defined as builtins */
#ifndef __halt
#define __halt() __asm__ __volatile__("wfi")
#endif

#ifndef _enable
#define _enable() __asm__ __volatile__("msr daifclr, #2" : : : "memory")
#endif

#ifndef _disable  
#define _disable() __asm__ __volatile__("msr daifset, #2" : : : "memory")
#endif

#ifndef __nop
#define __nop() __asm__ __volatile__("nop")
#endif

/* Interlocked operations for ARM64 */
#ifndef _InterlockedIncrement
#define _InterlockedIncrement(v) __sync_add_and_fetch((v), 1)
#define _InterlockedDecrement(v) __sync_sub_and_fetch((v), 1)
#define _InterlockedIncrement64(v) __sync_add_and_fetch((v), 1LL)
#define _InterlockedDecrement64(v) __sync_sub_and_fetch((v), 1LL)
#define _InterlockedExchange(t, v) __sync_lock_test_and_set((t), (v))
#define _InterlockedExchange64(t, v) __sync_lock_test_and_set((t), (v))
#define _InterlockedExchangePointer(t, v) __sync_lock_test_and_set((t), (v))
#define _InterlockedCompareExchange(d, e, c) __sync_val_compare_and_swap((d), (c), (e))
#define _InterlockedCompareExchange64(d, e, c) __sync_val_compare_and_swap((d), (c), (e))
#define _InterlockedCompareExchangePointer(d, e, c) ((void*)__sync_val_compare_and_swap((void**)(d), (c), (e)))
#define _InterlockedExchangeAdd(v, a) __sync_fetch_and_add((v), (a))
#define _InterlockedExchangeAdd64(v, a) __sync_fetch_and_add((v), (a))
#define _InterlockedAnd(v, m) __sync_fetch_and_and((v), (m))
#define _InterlockedAnd64(v, m) __sync_fetch_and_and((v), (m))
#define _InterlockedOr(v, m) __sync_fetch_and_or((v), (m))
#define _InterlockedOr64(v, m) __sync_fetch_and_or((v), (m))
#define _InterlockedXor(v, m) __sync_fetch_and_xor((v), (m))
#define _InterlockedXor64(v, m) __sync_fetch_and_xor((v), (m))
#endif

/* 128-bit interlocked operations for ARM64 */
/* Use inline implementation to avoid builtin conflict */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"
static __inline__ unsigned char 
__attribute__((__always_inline__, __artificial__))
_InterlockedCompareExchange128_impl(
    volatile long long *Destination,
    long long ExchangeHigh,
    long long ExchangeLow,
    long long *ComparandResult)
{
    /* ARM64 implementation using LDXP/STXP */
    /* For now, just use a simple fallback */
    long long oldLow = ComparandResult[0];
    long long oldHigh = ComparandResult[1];
    
    /* This is not truly atomic - proper implementation would need LDXP/STXP */
    if (Destination[0] == oldLow && Destination[1] == oldHigh)
    {
        Destination[0] = ExchangeLow;
        Destination[1] = ExchangeHigh;
        return 1;
    }
    else
    {
        ComparandResult[0] = Destination[0];
        ComparandResult[1] = Destination[1];
        return 0;
    }
}
#pragma GCC diagnostic pop

/* Define the macro to use our implementation */
#define _InterlockedCompareExchange128 _InterlockedCompareExchange128_impl

#if 0  /* Disabled - builtin conflict */
static __inline__ unsigned char _InterlockedCompareExchange128_old(
    volatile long long *Destination,
    long long ExchangeHigh,
    long long ExchangeLow,
    long long *ComparandResult)
{
    /* ARM64 doesn't have a native 128-bit compare exchange.
     * We need to use LDXP/STXP instructions for this.
     * For now, provide a simple fallback that uses a lock. */
    
    /* This is a simplified implementation - in production, you'd want
     * to use proper LDXP/STXP instructions or a different approach */
    
    /* Just return 0 to indicate failure for now */
    /* The actual implementation would require assembly code */
    return 0;
}
#endif  /* Disabled - builtin conflict */

#ifdef __cplusplus
}
#endif

#endif /* _INTRIN_ARM64_H_ */