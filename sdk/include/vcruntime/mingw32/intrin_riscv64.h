/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V compiler and instruction intrinsics.
 */

#pragma once

#if !defined(__clang__)
#error RISC-V NT intrinsics currently require Clang
#endif

/* Compiler ordering is distinct from hardware fences. */
#define _ReadBarrier() __asm__ __volatile__("" ::: "memory")
#define _WriteBarrier() __asm__ __volatile__("" ::: "memory")
#define _ReadWriteBarrier() __asm__ __volatile__("" ::: "memory")
#define _ReturnAddress() __builtin_return_address(0)
#define __nop() __asm__ __volatile__("nop")

static __inline__ __attribute__((__always_inline__)) void __riscv_debugbreak(void)
{
    __asm__ __volatile__("c.ebreak" ::: "memory");
}
#define __debugbreak __riscv_debugbreak

/* Private NT trap ABI: a0 is the reason, t0 identifies a fast-fail ebreak.
 * This works in both user and supervisor mode, independently of SBI. */
#define RISCV_FAST_FAIL_TRAP 0x4641494cUL
static __inline__ __attribute__((__always_inline__, __noreturn__)) void __riscv_fastfail(unsigned int Code)
{
    register unsigned __int64 Reason __asm__("a0") = Code;
    register unsigned __int64 Service __asm__("t0") = RISCV_FAST_FAIL_TRAP;
    __asm__ __volatile__("1: ebreak\n\tj 1b" :: "r"(Reason), "r"(Service) : "memory");
    __builtin_unreachable();
}
#define __fastfail __riscv_fastfail

/* Supervisor interrupt-enable bit. These are kernel-only operations. */
#define _disable() __asm__ __volatile__("csrci sstatus, 2" ::: "memory")
#define _enable() __asm__ __volatile__("csrsi sstatus, 2" ::: "memory")

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

/* The NT Int64*Mod32 macros require the compiler helpers on non-ARM targets.
 * RV64 executes these operations directly and masks register shift counts to
 * the low six bits. */
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

static __inline__ unsigned char __riscv_bitscanforward(unsigned long *Index, unsigned long Mask)
{
    unsigned int Bits = (unsigned int)Mask;

    if (!Bits) return 0;
    *Index = __builtin_ctz(Bits);
    return 1;
}

static __inline__ unsigned char __riscv_bitscanreverse(unsigned long *Index, unsigned long Mask)
{
    unsigned int Bits = (unsigned int)Mask;

    if (!Bits) return 0;
    *Index = 31 - __builtin_clz(Bits);
    return 1;
}

static __inline__ unsigned char __riscv_bitscanforward64(unsigned long *Index, unsigned __int64 Mask)
{
    if (!Mask) return 0;
    *Index = __builtin_ctzll(Mask);
    return 1;
}

static __inline__ unsigned char __riscv_bitscanreverse64(unsigned long *Index, unsigned __int64 Mask)
{
    if (!Mask) return 0;
    *Index = 63 - __builtin_clzll(Mask);
    return 1;
}

/* The bit index addresses a bit string, not just the first machine word.
 * Clang's signed right shift selects the preceding word for negative indices. */
static __inline__ unsigned char __riscv_interlockedbittestandset64(__int64 volatile *Base, __int64 Bit)
{
    unsigned __int64 Mask = 1ULL << (Bit & 63);
    return (__atomic_fetch_or(Base + (Bit >> 6), Mask, __ATOMIC_SEQ_CST) & Mask) != 0;
}

static __inline__ unsigned char __riscv_interlockedbittestandreset64(__int64 volatile *Base, __int64 Bit)
{
    unsigned __int64 Mask = 1ULL << (Bit & 63);
    return (__atomic_fetch_and(Base + (Bit >> 6), ~Mask, __ATOMIC_SEQ_CST) & Mask) != 0;
}

#define _BitScanForward __riscv_bitscanforward
#define _BitScanReverse __riscv_bitscanreverse
#define _BitScanForward64 __riscv_bitscanforward64
#define _BitScanReverse64 __riscv_bitscanreverse64
#define _interlockedbittestandset64 __riscv_interlockedbittestandset64
#define _interlockedbittestandreset64 __riscv_interlockedbittestandreset64

static __inline__ long __riscv_interlockedadd(long volatile *Addend, long Value)
{
    return __atomic_add_fetch(Addend, Value, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __riscv_interlockedexchangeadd64(__int64 volatile *Addend, __int64 Value)
{
    return __atomic_fetch_add(Addend, Value, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __riscv_interlockedadd64(__int64 volatile *Addend, __int64 Value)
{
    return __atomic_add_fetch(Addend, Value, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __riscv_interlockedand64(__int64 volatile *Value, __int64 Mask)
{
    return __atomic_fetch_and(Value, Mask, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __riscv_interlockedincrement64(__int64 volatile *Addend)
{
    return __atomic_add_fetch(Addend, 1, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __riscv_interlockeddecrement64(__int64 volatile *Addend)
{
    return __atomic_sub_fetch(Addend, 1, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __riscv_interlockedexchange64(__int64 volatile *Target, __int64 Value)
{
    return __atomic_exchange_n(Target, Value, __ATOMIC_SEQ_CST);
}

static __inline__ __int64 __riscv_interlockedor64(__int64 volatile *Value, __int64 Mask)
{
    return __atomic_fetch_or(Value, Mask, __ATOMIC_SEQ_CST);
}

#define _InterlockedAdd __riscv_interlockedadd
#define _InterlockedExchangeAdd64 __riscv_interlockedexchangeadd64
#define _InterlockedAdd64 __riscv_interlockedadd64
#define _InterlockedAnd64 __riscv_interlockedand64
#define _InterlockedIncrement64 __riscv_interlockedincrement64
#define _InterlockedDecrement64 __riscv_interlockeddecrement64
#define _InterlockedExchange64 __riscv_interlockedexchange64
#define _InterlockedOr64 __riscv_interlockedor64
