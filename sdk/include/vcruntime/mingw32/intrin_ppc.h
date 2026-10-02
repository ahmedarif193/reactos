/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows NT PowerPC compiler and instruction intrinsics.
 *
 * Clang implements the 32-bit Microsoft interlocked, bit-scan and bit-test
 * intrinsics for every target. This header supplies the rest. PowerPC 32
 * has no 64-bit reservation instructions, so the 64-bit interlocked
 * operations become __atomic library calls.
 */

#pragma once

#if !defined(__clang__)
#error PowerPC NT intrinsics currently require Clang
#endif

/* Compiler ordering is distinct from hardware barriers. */
#define _ReadBarrier() __asm__ __volatile__("" ::: "memory")
#define _WriteBarrier() __asm__ __volatile__("" ::: "memory")
#define _ReadWriteBarrier() __asm__ __volatile__("" ::: "memory")
#define _ReturnAddress() __builtin_return_address(0)
#define __nop() __asm__ __volatile__("nop")

/* twi 31,0,0x16 is the Windows NT PowerPC breakpoint (DEBUG_STOP_BREAKPOINT). */
static __inline__ __attribute__((__always_inline__)) void __ppc_debugbreak(void)
{
    __asm__ __volatile__("twi 31, 0, 0x16" ::: "memory");
}
#define __debugbreak __ppc_debugbreak

/* Fast fail: r3 holds the reason, the trap immediate identifies the service. */
#define PPC_FAST_FAIL_TRAP 0x29
static __inline__ __attribute__((__always_inline__, __noreturn__)) void __ppc_fastfail(unsigned int Code)
{
    register unsigned long Reason __asm__("r3") = Code;
    __asm__ __volatile__("1: twi 31, 0, 0x29\n\tb 1b" :: "r"(Reason) : "memory");
    __builtin_unreachable();
}
#define __fastfail __ppc_fastfail

/* External interrupt enable (MSR[EE]). These are kernel-only operations. */
static __inline__ __attribute__((__always_inline__)) void __ppc_disable(void)
{
    unsigned long Msr;
    __asm__ __volatile__("mfmsr %0" : "=r"(Msr));
    __asm__ __volatile__("mtmsr %0" :: "r"(Msr & ~0x8000UL) : "memory");
}

static __inline__ __attribute__((__always_inline__)) void __ppc_enable(void)
{
    unsigned long Msr;
    __asm__ __volatile__("mfmsr %0" : "=r"(Msr));
    __asm__ __volatile__("mtmsr %0" :: "r"(Msr | 0x8000UL) : "memory");
}
#define _disable __ppc_disable
#define _enable __ppc_enable

__INTRIN_INLINE unsigned short __cdecl _byteswap_ushort(unsigned short Value)
{
    return __builtin_bswap16(Value);
}

__INTRIN_INLINE unsigned long __cdecl _byteswap_ulong(unsigned long Value)
{
    return __builtin_bswap32(Value);
}

__INTRIN_INLINE unsigned __int64 __cdecl _byteswap_uint64(unsigned __int64 Value)
{
    return __builtin_bswap64(Value);
}

__INTRIN_INLINE unsigned long long __ll_lshift(unsigned long long Mask, int Bit)
{
    return Mask << (Bit & 0x3F);
}

__INTRIN_INLINE long long __ll_rshift(long long Mask, int Bit)
{
    return Mask >> (Bit & 0x3F);
}

__INTRIN_INLINE unsigned long long __ull_rshift(unsigned long long Mask, int Bit)
{
    return Mask >> (Bit & 0x3F);
}

__INTRIN_INLINE long long __emul(int a, int b)
{
    return (long long)a * b;
}

__INTRIN_INLINE unsigned long long __emulu(unsigned int a, unsigned int b)
{
    return (unsigned long long)a * b;
}

static __inline__ unsigned char __ppc_bitscanforward(unsigned long *Index, unsigned long Mask)
{
    if (!Mask) return 0;
    *Index = __builtin_ctz(Mask);
    return 1;
}

static __inline__ unsigned char __ppc_bitscanreverse(unsigned long *Index, unsigned long Mask)
{
    if (!Mask) return 0;
    *Index = 31 - __builtin_clz(Mask);
    return 1;
}

static __inline__ unsigned char __ppc_bitscanforward64(unsigned long *Index, unsigned __int64 Mask)
{
    if (!Mask) return 0;
    *Index = __builtin_ctzll(Mask);
    return 1;
}

static __inline__ unsigned char __ppc_bitscanreverse64(unsigned long *Index, unsigned __int64 Mask)
{
    if (!Mask) return 0;
    *Index = 63 - __builtin_clzll(Mask);
    return 1;
}

#define _BitScanForward __ppc_bitscanforward
#define _BitScanReverse __ppc_bitscanreverse
#define _BitScanForward64 __ppc_bitscanforward64
#define _BitScanReverse64 __ppc_bitscanreverse64

static __inline__ long __ppc_interlockedadd(long volatile *Addend, long Value)
{
    return __atomic_add_fetch(Addend, Value, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __ppc_interlockedexchangeadd64(__int64 volatile *Addend, __int64 Value)
{
    return __atomic_fetch_add(Addend, Value, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __ppc_interlockedadd64(__int64 volatile *Addend, __int64 Value)
{
    return __atomic_add_fetch(Addend, Value, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __ppc_interlockedand64(__int64 volatile *Value, __int64 Mask)
{
    return __atomic_fetch_and(Value, Mask, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __ppc_interlockedor64(__int64 volatile *Value, __int64 Mask)
{
    return __atomic_fetch_or(Value, Mask, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __ppc_interlockedxor64(__int64 volatile *Value, __int64 Mask)
{
    return __atomic_fetch_xor(Value, Mask, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __ppc_interlockedincrement64(__int64 volatile *Addend)
{
    return __atomic_add_fetch(Addend, 1, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __ppc_interlockeddecrement64(__int64 volatile *Addend)
{
    return __atomic_sub_fetch(Addend, 1, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __ppc_interlockedexchange64(__int64 volatile *Target, __int64 Value)
{
    return __atomic_exchange_n(Target, Value, __ATOMIC_SEQ_CST);
}

#define _InterlockedAdd __ppc_interlockedadd
#define _InterlockedExchangeAdd64 __ppc_interlockedexchangeadd64
#define _InterlockedAdd64 __ppc_interlockedadd64
#define _InterlockedAnd64 __ppc_interlockedand64
#define _InterlockedOr64 __ppc_interlockedor64
#define _InterlockedXor64 __ppc_interlockedxor64
#define _InterlockedIncrement64 __ppc_interlockedincrement64
#define _InterlockedDecrement64 __ppc_interlockeddecrement64
#define _InterlockedExchange64 __ppc_interlockedexchange64
