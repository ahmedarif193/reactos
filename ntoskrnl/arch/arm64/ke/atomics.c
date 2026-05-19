/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Minimal interlocked helper shims to satisfy MinGW CRT
 *                  atomics while a real A64 barrier-aware implementation is
 *                  brought up. These are intentionally simple wrappers around
 *                  the compiler builtins.
 */

#include <ntoskrnl.h>
#include <ndk/rtlfuncs.h>

#define ARM64_ID_AA64ISAR0_ATOMIC_LSE 2

static __inline __attribute__((always_inline))
BOOLEAN
Arm64UseLseAtomics(VOID)
{
    return Arm64CpuFeatures.AtomicSupported >= ARM64_ID_AA64ISAR0_ATOMIC_LSE;
}

static __inline __attribute__((always_inline))
CHAR
Arm64LseCas8(
    _In_ CHAR Comperand,
    _In_ CHAR Exchange,
    _Inout_ volatile CHAR *Destination)
{
    LONG Current = (UCHAR)Comperand;
    LONG NewValue = (UCHAR)Exchange;

    __asm__ __volatile__(".arch_extension lse\n"
                         "casalb %w[Current], %w[NewValue], %[Memory]"
                         : [Current] "+&r" (Current),
                           [Memory] "+Q" (*Destination)
                         : [NewValue] "r" (NewValue)
                         : "memory");

    return (CHAR)Current;
}

static __inline __attribute__((always_inline))
SHORT
Arm64LseCas16(
    _In_ SHORT Comperand,
    _In_ SHORT Exchange,
    _Inout_ volatile SHORT *Destination)
{
    LONG Current = (USHORT)Comperand;
    LONG NewValue = (USHORT)Exchange;

    __asm__ __volatile__(".arch_extension lse\n"
                         "casalh %w[Current], %w[NewValue], %[Memory]"
                         : [Current] "+&r" (Current),
                           [Memory] "+Q" (*Destination)
                         : [NewValue] "r" (NewValue)
                         : "memory");

    return (SHORT)Current;
}

static __inline __attribute__((always_inline))
LONG
Arm64LseCas32(
    _In_ LONG Comperand,
    _In_ LONG Exchange,
    _Inout_ volatile LONG *Destination)
{
    LONG Current = Comperand;

    __asm__ __volatile__(".arch_extension lse\n"
                         "casal %w[Current], %w[Exchange], %[Memory]"
                         : [Current] "+&r" (Current),
                           [Memory] "+Q" (*Destination)
                         : [Exchange] "r" (Exchange)
                         : "memory");

    return Current;
}

static __inline __attribute__((always_inline))
LONGLONG
Arm64LseCas64(
    _In_ LONGLONG Comperand,
    _In_ LONGLONG Exchange,
    _Inout_ volatile LONGLONG *Destination)
{
    LONGLONG Current = Comperand;

    __asm__ __volatile__(".arch_extension lse\n"
                         "casal %[Current], %[Exchange], %[Memory]"
                         : [Current] "+&r" (Current),
                           [Memory] "+Q" (*Destination)
                         : [Exchange] "r" (Exchange)
                         : "memory");

    return Current;
}

static __inline __attribute__((always_inline))
SHORT
Arm64LseLdAdd16Al(
    _In_ SHORT Value,
    _Inout_ volatile SHORT *Destination)
{
    LONG AddValue = (USHORT)Value;
    LONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldaddalh %w[AddValue], %w[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [AddValue] "r" (AddValue)
                         : "memory");

    return (SHORT)OldValue;
}

static __inline __attribute__((always_inline))
LONG
Arm64LseLdAdd32Al(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    LONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldaddal %w[Value], %w[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONG
Arm64LseLdAdd32A(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    LONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldadda %w[Value], %w[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONGLONG
Arm64LseLdAdd64Al(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    LONGLONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldaddal %[Value], %[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONG
Arm64LseLdSet32Al(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    LONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldsetal %w[Value], %w[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONG
Arm64LseLdSet32A(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    LONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldseta %w[Value], %w[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONGLONG
Arm64LseLdSet64Al(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    LONGLONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldsetal %[Value], %[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONGLONG
Arm64LseLdSet64A(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    LONGLONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldseta %[Value], %[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONG
Arm64LseLdClr32Al(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    LONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldclral %w[Value], %w[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONG
Arm64LseLdClr32A(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    LONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldclra %w[Value], %w[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONGLONG
Arm64LseLdClr64Al(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    LONGLONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldclral %[Value], %[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONGLONG
Arm64LseLdClr64A(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    LONGLONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldclra %[Value], %[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONG
Arm64LseLdEor32Al(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    LONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldeoral %w[Value], %w[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONGLONG
Arm64LseLdEor64Al(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    LONGLONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "ldeoral %[Value], %[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONG
Arm64LseSwp32Al(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    LONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "swpal %w[Value], %w[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONG
Arm64LseSwp32A(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    LONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "swpa %w[Value], %w[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONGLONG
Arm64LseSwp64Al(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    LONGLONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "swpal %[Value], %[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

static __inline __attribute__((always_inline))
LONGLONG
Arm64LseSwp64A(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    LONGLONG OldValue;

    __asm__ __volatile__(".arch_extension lse\n"
                         "swpa %[Value], %[OldValue], %[Memory]"
                         : [OldValue] "=&r" (OldValue),
                           [Memory] "+Q" (*Destination)
                         : [Value] "r" (Value)
                         : "memory");

    return OldValue;
}

/* TODO(ARM64): Replace these helpers with proper barrier-aware routines once
 * the full interlocked/atomic support layer lands. */

CHAR
__aarch64_cas1_sync(
    _In_ CHAR Comperand,
    _In_ CHAR Exchange,
    _Inout_ volatile CHAR *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseCas8(Comperand, Exchange, Destination);
    }

    CHAR Current = Comperand;
    __atomic_compare_exchange_n(Destination,
                                &Current,
                                Exchange,
                                FALSE,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return Current;
}

SHORT
__aarch64_cas2_sync(
    _In_ SHORT Comperand,
    _In_ SHORT Exchange,
    _Inout_ volatile SHORT *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseCas16(Comperand, Exchange, Destination);
    }

    SHORT Current = Comperand;
    __atomic_compare_exchange_n(Destination,
                                &Current,
                                Exchange,
                                FALSE,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return Current;
}

LONGLONG
__aarch64_cas8_sync(
    _In_ LONGLONG Comperand,
    _In_ LONGLONG Exchange,
    _Inout_ volatile LONGLONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseCas64(Comperand, Exchange, Destination);
    }

    LONGLONG Current = Comperand;
    __atomic_compare_exchange_n(Destination,
                                &Current,
                                Exchange,
                                FALSE,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return Current;
}

LONG
__aarch64_cas4_sync(
    _In_ LONG Comperand,
    _In_ LONG Exchange,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseCas32(Comperand, Exchange, Destination);
    }

    LONG Current = Comperand;
    __atomic_compare_exchange_n(Destination,
                                &Current,
                                Exchange,
                                FALSE,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return Current;
}

LONG
__aarch64_ldadd4_sync(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdAdd32Al(Value, Destination);
    }

    return __atomic_fetch_add(Destination, Value, __ATOMIC_SEQ_CST);
}

SHORT
__aarch64_ldadd2_sync(
    _In_ SHORT Value,
    _Inout_ volatile SHORT *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdAdd16Al(Value, Destination);
    }

    return __atomic_fetch_add(Destination, Value, __ATOMIC_SEQ_CST);
}

LONGLONG
__aarch64_ldadd8_sync(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdAdd64Al(Value, Destination);
    }

    return __atomic_fetch_add(Destination, Value, __ATOMIC_SEQ_CST);
}

LONG
__aarch64_swp4_sync(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseSwp32Al(Value, Destination);
    }

    return __atomic_exchange_n(Destination, Value, __ATOMIC_SEQ_CST);
}

LONGLONG
__aarch64_swp8_sync(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseSwp64Al(Value, Destination);
    }

    return __atomic_exchange_n(Destination, Value, __ATOMIC_SEQ_CST);
}

PVOID Arm64InterlockedCompareExchangePointer(
    _Inout_ PVOID volatile *Destination,
    _In_ PVOID Exchange,
    _In_ PVOID Comparand)
{
    if (Arm64UseLseAtomics())
    {
        return (PVOID)(ULONG_PTR)Arm64LseCas64((LONGLONG)(LONG_PTR)Comparand,
                                               (LONGLONG)(LONG_PTR)Exchange,
                                               (volatile LONGLONG *)Destination);
    }

    PVOID Expected = Comparand;
    __atomic_compare_exchange_n(Destination,
                                &Expected,
                                Exchange,
                                FALSE,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return Expected;
}

LONG Arm64InterlockedOr(
    _Inout_ volatile LONG *Destination,
    _In_ LONG Value)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdSet32Al(Value, Destination);
    }

    return __atomic_fetch_or(Destination, Value, __ATOMIC_SEQ_CST);
}

LONG Arm64InterlockedAnd(
    _Inout_ volatile LONG *Destination,
    _In_ LONG Value)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdClr32Al(~Value, Destination);
    }

    return __atomic_fetch_and(Destination, Value, __ATOMIC_SEQ_CST);
}

LONG Arm64InterlockedXor(
    _Inout_ volatile LONG *Destination,
    _In_ LONG Value)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdEor32Al(Value, Destination);
    }

    return __atomic_fetch_xor(Destination, Value, __ATOMIC_SEQ_CST);
}

LONGLONG Arm64InterlockedXor64(
    _Inout_ volatile LONGLONG *Destination,
    _In_ LONGLONG Value)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdEor64Al(Value, Destination);
    }

    return __atomic_fetch_xor(Destination, Value, __ATOMIC_SEQ_CST);
}

CHAR Arm64InterlockedCompareExchange8(
    _Inout_ volatile CHAR *Destination,
    _In_ CHAR Exchange,
    _In_ CHAR Comparand)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseCas8(Comparand, Exchange, Destination);
    }

    CHAR Expected = Comparand;
    __atomic_compare_exchange_n(Destination,
                                &Expected,
                                Exchange,
                                FALSE,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return Expected;
}

SHORT Arm64InterlockedCompareExchange16(
    _Inout_ volatile SHORT *Destination,
    _In_ SHORT Exchange,
    _In_ SHORT Comparand)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseCas16(Comparand, Exchange, Destination);
    }

    SHORT Expected = Comparand;
    __atomic_compare_exchange_n(Destination,
                                &Expected,
                                Exchange,
                                FALSE,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return Expected;
}

SHORT Arm64InterlockedDecrement16(
    _Inout_ volatile SHORT *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdAdd16Al(-1, Destination) - 1;
    }

    return __atomic_sub_fetch(Destination, 1, __ATOMIC_SEQ_CST);
}

SHORT Arm64InterlockedIncrement16(
    _Inout_ volatile SHORT *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdAdd16Al(1, Destination) + 1;
    }

    return __atomic_add_fetch(Destination, 1, __ATOMIC_SEQ_CST);
}

#if defined(__GNUC__)
__asm__(".globl _InterlockedCompareExchangePointer\n"
        "_InterlockedCompareExchangePointer = Arm64InterlockedCompareExchangePointer\n"
        ".globl _InterlockedOr\n"
        "_InterlockedOr = Arm64InterlockedOr\n"
        ".globl _InterlockedAnd\n"
        "_InterlockedAnd = Arm64InterlockedAnd\n"
        ".globl _InterlockedXor\n"
        "_InterlockedXor = Arm64InterlockedXor\n"
        ".globl _InterlockedXor64\n"
        "_InterlockedXor64 = Arm64InterlockedXor64\n"
        ".globl _InterlockedCompareExchange8\n"
        "_InterlockedCompareExchange8 = Arm64InterlockedCompareExchange8\n"
        ".globl _InterlockedCompareExchange16\n"
        "_InterlockedCompareExchange16 = Arm64InterlockedCompareExchange16\n"
        ".globl _InterlockedDecrement16\n"
        "_InterlockedDecrement16 = Arm64InterlockedDecrement16\n"
        ".globl _InterlockedIncrement16\n"
        "_InterlockedIncrement16 = Arm64InterlockedIncrement16\n");
#endif

LONG
__aarch64_swp4_acq_rel(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseSwp32Al(Value, Destination);
    }

    return __atomic_exchange_n(Destination, Value, __ATOMIC_ACQ_REL);
}

LONGLONG
__aarch64_swp8_acq_rel(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseSwp64Al(Value, Destination);
    }

    return __atomic_exchange_n(Destination, Value, __ATOMIC_ACQ_REL);
}

LONG
__aarch64_ldset4_sync(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdSet32Al(Value, Destination);
    }

    return __atomic_fetch_or(Destination, Value, __ATOMIC_SEQ_CST);
}

LONG
__aarch64_ldclr4_sync(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdClr32Al(Value, Destination);
    }

    return __atomic_fetch_and(Destination, ~Value, __ATOMIC_SEQ_CST);
}

LONG
__aarch64_ldset4_acq_rel(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdSet32Al(Value, Destination);
    }

    return __atomic_fetch_or(Destination, Value, __ATOMIC_ACQ_REL);
}

LONG
__aarch64_ldclr4_acq_rel(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdClr32Al(Value, Destination);
    }

    return __atomic_fetch_and(Destination, ~Value, __ATOMIC_ACQ_REL);
}

LONGLONG
__aarch64_ldset8_acq_rel(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdSet64Al(Value, Destination);
    }

    return __atomic_fetch_or(Destination, Value, __ATOMIC_ACQ_REL);
}

LONGLONG
__aarch64_ldclr8_acq_rel(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdClr64Al(Value, Destination);
    }

    return __atomic_fetch_and(Destination, ~Value, __ATOMIC_ACQ_REL);
}


CHAR
__aarch64_cas1_acq_rel(
    _In_ CHAR Comperand,
    _In_ CHAR Exchange,
    _Inout_ volatile CHAR *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseCas8(Comperand, Exchange, Destination);
    }

    CHAR Current = Comperand;
    __atomic_compare_exchange_n(Destination,
                                &Current,
                                Exchange,
                                FALSE,
                                __ATOMIC_ACQ_REL,
                                __ATOMIC_ACQUIRE);
    return Current;
}

SHORT
__aarch64_cas2_acq_rel(
    _In_ SHORT Comperand,
    _In_ SHORT Exchange,
    _Inout_ volatile SHORT *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseCas16(Comperand, Exchange, Destination);
    }

    SHORT Current = Comperand;
    __atomic_compare_exchange_n(Destination,
                                &Current,
                                Exchange,
                                FALSE,
                                __ATOMIC_ACQ_REL,
                                __ATOMIC_ACQUIRE);
    return Current;
}

LONG
__aarch64_cas4_acq_rel(
    _In_ LONG Comperand,
    _In_ LONG Exchange,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseCas32(Comperand, Exchange, Destination);
    }

    LONG Current = Comperand;
    __atomic_compare_exchange_n(Destination,
                                &Current,
                                Exchange,
                                FALSE,
                                __ATOMIC_ACQ_REL,
                                __ATOMIC_ACQUIRE);
    return Current;
}

LONGLONG
__aarch64_cas8_acq_rel(
    _In_ LONGLONG Comperand,
    _In_ LONGLONG Exchange,
    _Inout_ volatile LONGLONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseCas64(Comperand, Exchange, Destination);
    }

    LONGLONG Current = Comperand;
    __atomic_compare_exchange_n(Destination,
                                &Current,
                                Exchange,
                                FALSE,
                                __ATOMIC_ACQ_REL,
                                __ATOMIC_ACQUIRE);
    return Current;
}

SHORT
__aarch64_ldadd2_acq_rel(
    _In_ SHORT Value,
    _Inout_ volatile SHORT *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdAdd16Al(Value, Destination);
    }

    return __atomic_fetch_add(Destination, Value, __ATOMIC_ACQ_REL);
}

LONGLONG
__aarch64_ldadd8_acq_rel(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdAdd64Al(Value, Destination);
    }

    return __atomic_fetch_add(Destination, Value, __ATOMIC_ACQ_REL);
}


LONG
__aarch64_ldadd4_acq_rel(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdAdd32Al(Value, Destination);
    }

    return __atomic_fetch_add(Destination, Value, __ATOMIC_ACQ_REL);
}

LONG
__aarch64_ldadd4_acq(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdAdd32A(Value, Destination);
    }

    return __atomic_fetch_add(Destination, Value, __ATOMIC_ACQUIRE);
}

LONG
__aarch64_ldset4_acq(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdSet32A(Value, Destination);
    }

    return __atomic_fetch_or(Destination, Value, __ATOMIC_ACQUIRE);
}

LONG
__aarch64_ldclr4_acq(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdClr32A(Value, Destination);
    }

    return __atomic_fetch_and(Destination, ~Value, __ATOMIC_ACQUIRE);
}

LONGLONG
__aarch64_ldset8_acq(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdSet64A(Value, Destination);
    }

    return __atomic_fetch_or(Destination, Value, __ATOMIC_ACQUIRE);
}

LONGLONG
__aarch64_ldclr8_acq(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseLdClr64A(Value, Destination);
    }

    return __atomic_fetch_and(Destination, ~Value, __ATOMIC_ACQUIRE);
}

LONG
__aarch64_swp4_acq(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseSwp32A(Value, Destination);
    }

    return __atomic_exchange_n(Destination, Value, __ATOMIC_ACQUIRE);
}

LONGLONG
__aarch64_swp8_acq(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    if (Arm64UseLseAtomics())
    {
        return Arm64LseSwp64A(Value, Destination);
    }

    return __atomic_exchange_n(Destination, Value, __ATOMIC_ACQUIRE);
}

/*
 * ExpInterlocked*SList wrappers.
 * We provide explicit wrappers because #pragma redefine_extname behaves
 * inconsistently between GCC (renames symbol entirely, breaking callers)
 * and clang (doesn't work on COFF targets). The pragma is disabled for
 * ARM64 in sdk/lib/rtl/slist.c, so we must provide these here.
 */
PSLIST_ENTRY NTAPI RtlInterlockedPopEntrySList(PSLIST_HEADER ListHead);
PSLIST_ENTRY NTAPI RtlInterlockedPushEntrySList(PSLIST_HEADER ListHead, PSLIST_ENTRY ListEntry);
PSLIST_ENTRY NTAPI RtlInterlockedFlushSList(PSLIST_HEADER ListHead);

NTKERNELAPI
PSLIST_ENTRY
NTAPI
ExpInterlockedPopEntrySList(
    _Inout_ PSLIST_HEADER ListHead)
{
    return RtlInterlockedPopEntrySList(ListHead);
}

NTKERNELAPI
PSLIST_ENTRY
NTAPI
ExpInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER ListHead,
    _Inout_ PSLIST_ENTRY ListEntry)
{
    return RtlInterlockedPushEntrySList(ListHead, ListEntry);
}

NTKERNELAPI
PSLIST_ENTRY
NTAPI
ExpInterlockedFlushSList(
    _Inout_ PSLIST_HEADER ListHead)
{
    return RtlInterlockedFlushSList(ListHead);
}
