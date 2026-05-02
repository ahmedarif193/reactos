/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Minimal interlocked helper shims to satisfy MinGW CRT
 *                  atomics while a real A64 barrier-aware implementation is
 *                  brought up. These are intentionally simple wrappers around
 *                  the compiler builtins.
 */

#include <ntoskrnl.h>
#include <ndk/rtlfuncs.h>

/* TODO(ARM64): Replace these helpers with proper barrier-aware routines once
 * the full interlocked/atomic support layer lands. */

CHAR
__aarch64_cas1_sync(
    _In_ CHAR Comperand,
    _In_ CHAR Exchange,
    _Inout_ volatile CHAR *Destination)
{
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
    return __atomic_fetch_add(Destination, Value, __ATOMIC_SEQ_CST);
}

SHORT
__aarch64_ldadd2_sync(
    _In_ SHORT Value,
    _Inout_ volatile SHORT *Destination)
{
    return __atomic_fetch_add(Destination, Value, __ATOMIC_SEQ_CST);
}

LONGLONG
__aarch64_ldadd8_sync(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    return __atomic_fetch_add(Destination, Value, __ATOMIC_SEQ_CST);
}

LONG
__aarch64_swp4_sync(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    return __atomic_exchange_n(Destination, Value, __ATOMIC_SEQ_CST);
}

LONGLONG
__aarch64_swp8_sync(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    return __atomic_exchange_n(Destination, Value, __ATOMIC_SEQ_CST);
}

UCHAR Arm64InterlockedBitTestAndReset(
    _Inout_ volatile LONG *Base,
    _In_ LONG Bit)
{
    ULONG BitIndex = ((ULONG)Bit) & 31u;
    ULONG Mask = 1u << BitIndex;
    ULONG Previous = __atomic_fetch_and(Base, ~Mask, __ATOMIC_SEQ_CST);
    return (UCHAR)((Previous >> BitIndex) & 1u);
}

UCHAR Arm64InterlockedBitTestAndSet(
    _Inout_ volatile LONG *Base,
    _In_ LONG Bit)
{
    ULONG BitIndex = ((ULONG)Bit) & 31u;
    ULONG Mask = 1u << BitIndex;
    ULONG Previous = __atomic_fetch_or(Base, Mask, __ATOMIC_SEQ_CST);
    return (UCHAR)((Previous >> BitIndex) & 1u);
}

PVOID Arm64InterlockedCompareExchangePointer(
    _Inout_ PVOID volatile *Destination,
    _In_ PVOID Exchange,
    _In_ PVOID Comparand)
{
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
    return __atomic_fetch_or(Destination, Value, __ATOMIC_SEQ_CST);
}

LONG Arm64InterlockedAnd(
    _Inout_ volatile LONG *Destination,
    _In_ LONG Value)
{
    return __atomic_fetch_and(Destination, Value, __ATOMIC_SEQ_CST);
}

LONG Arm64InterlockedXor(
    _Inout_ volatile LONG *Destination,
    _In_ LONG Value)
{
    return __atomic_xor_fetch(Destination, Value, __ATOMIC_SEQ_CST);
}

LONGLONG Arm64InterlockedXor64(
    _Inout_ volatile LONGLONG *Destination,
    _In_ LONGLONG Value)
{
    return __atomic_xor_fetch(Destination, Value, __ATOMIC_SEQ_CST);
}

CHAR Arm64InterlockedCompareExchange8(
    _Inout_ volatile CHAR *Destination,
    _In_ CHAR Exchange,
    _In_ CHAR Comparand)
{
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
    return __atomic_sub_fetch(Destination, 1, __ATOMIC_SEQ_CST);
}

SHORT Arm64InterlockedIncrement16(
    _Inout_ volatile SHORT *Destination)
{
    return __atomic_add_fetch(Destination, 1, __ATOMIC_SEQ_CST);
}

#if defined(__GNUC__)
__asm__(".globl _interlockedbittestandreset\n"
        "_interlockedbittestandreset = Arm64InterlockedBitTestAndReset\n"
        ".globl _interlockedbittestandset\n"
        "_interlockedbittestandset = Arm64InterlockedBitTestAndSet\n"
        ".globl _InterlockedCompareExchangePointer\n"
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
    return __atomic_exchange_n(Destination, Value, __ATOMIC_ACQ_REL);
}

LONGLONG
__aarch64_swp8_acq_rel(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    return __atomic_exchange_n(Destination, Value, __ATOMIC_ACQ_REL);
}

LONG
__aarch64_ldset4_sync(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    return __atomic_fetch_or(Destination, Value, __ATOMIC_SEQ_CST);
}

LONG
__aarch64_ldclr4_sync(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    return __atomic_fetch_and(Destination, ~Value, __ATOMIC_SEQ_CST);
}

LONG
__aarch64_ldset4_acq_rel(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    return __atomic_fetch_or(Destination, Value, __ATOMIC_ACQ_REL);
}

LONG
__aarch64_ldclr4_acq_rel(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    return __atomic_fetch_and(Destination, ~Value, __ATOMIC_ACQ_REL);
}

LONGLONG
__aarch64_ldset8_acq_rel(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    return __atomic_fetch_or(Destination, Value, __ATOMIC_ACQ_REL);
}

LONGLONG
__aarch64_ldclr8_acq_rel(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    return __atomic_fetch_and(Destination, ~Value, __ATOMIC_ACQ_REL);
}


CHAR
__aarch64_cas1_acq_rel(
    _In_ CHAR Comperand,
    _In_ CHAR Exchange,
    _Inout_ volatile CHAR *Destination)
{
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
    return __atomic_fetch_add(Destination, Value, __ATOMIC_ACQ_REL);
}

LONGLONG
__aarch64_ldadd8_acq_rel(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    return __atomic_fetch_add(Destination, Value, __ATOMIC_ACQ_REL);
}


LONG
__aarch64_ldadd4_acq_rel(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    return __atomic_fetch_add(Destination, Value, __ATOMIC_ACQ_REL);
}

LONG
__aarch64_ldadd4_acq(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    return __atomic_fetch_add(Destination, Value, __ATOMIC_ACQUIRE);
}

LONG
__aarch64_ldset4_acq(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    return __atomic_fetch_or(Destination, Value, __ATOMIC_ACQUIRE);
}

LONG
__aarch64_ldclr4_acq(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    return __atomic_fetch_and(Destination, ~Value, __ATOMIC_ACQUIRE);
}

LONGLONG
__aarch64_ldset8_acq(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    return __atomic_fetch_or(Destination, Value, __ATOMIC_ACQUIRE);
}

LONGLONG
__aarch64_ldclr8_acq(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
    return __atomic_fetch_and(Destination, ~Value, __ATOMIC_ACQUIRE);
}

LONG
__aarch64_swp4_acq(
    _In_ LONG Value,
    _Inout_ volatile LONG *Destination)
{
    return __atomic_exchange_n(Destination, Value, __ATOMIC_ACQUIRE);
}

LONGLONG
__aarch64_swp8_acq(
    _In_ LONGLONG Value,
    _Inout_ volatile LONGLONG *Destination)
{
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
