/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC 64-bit atomic operations
 *
 * 32-bit PowerPC has no 64-bit reservation. The compiler lowers 64-bit
 * atomics to these calls. The kernel runs on one processor, so masking
 * external interrupts makes each operation atomic, including against
 * interrupt handlers; compiler-rt's lock table would deadlock with them.
 */

#include <ntoskrnl.h>

typedef unsigned long long KI_PPC_U64;

#define KI_PPC_ATOMIC_BEGIN() ULONG Msr = KiPpcReadMsr(); KiPpcWriteMsr(Msr & ~MSR_EE)
#define KI_PPC_ATOMIC_END() KiPpcWriteMsr(Msr)

KI_PPC_U64 __atomic_load_8(const volatile void *Pointer, int Order)
{
    KI_PPC_U64 Value;
    KI_PPC_ATOMIC_BEGIN();

    UNREFERENCED_PARAMETER(Order);
    Value = *(const volatile KI_PPC_U64 *)Pointer;
    KI_PPC_ATOMIC_END();
    return Value;
}

void __atomic_store_8(volatile void *Pointer, KI_PPC_U64 Value, int Order)
{
    KI_PPC_ATOMIC_BEGIN();

    UNREFERENCED_PARAMETER(Order);
    *(volatile KI_PPC_U64 *)Pointer = Value;
    KI_PPC_ATOMIC_END();
}

KI_PPC_U64 __atomic_exchange_8(volatile void *Pointer, KI_PPC_U64 Value, int Order)
{
    KI_PPC_U64 Old;
    KI_PPC_ATOMIC_BEGIN();

    UNREFERENCED_PARAMETER(Order);
    Old = *(volatile KI_PPC_U64 *)Pointer;
    *(volatile KI_PPC_U64 *)Pointer = Value;
    KI_PPC_ATOMIC_END();
    return Old;
}

_Bool __atomic_compare_exchange_8(volatile void *Pointer, void *Expected, KI_PPC_U64 Desired, int Success, int Failure)
{
    KI_PPC_U64 Current;
    _Bool Swapped;
    KI_PPC_ATOMIC_BEGIN();

    UNREFERENCED_PARAMETER(Success);
    UNREFERENCED_PARAMETER(Failure);
    Current = *(volatile KI_PPC_U64 *)Pointer;
    Swapped = (Current == *(KI_PPC_U64 *)Expected);
    if (Swapped)
        *(volatile KI_PPC_U64 *)Pointer = Desired;
    else
        *(KI_PPC_U64 *)Expected = Current;
    KI_PPC_ATOMIC_END();
    return Swapped;
}

#define KI_PPC_FETCH_OP(Name, Operation) \
KI_PPC_U64 __atomic_fetch_##Name##_8(volatile void *Pointer, KI_PPC_U64 Value, int Order) \
{ \
    KI_PPC_U64 Old; \
    KI_PPC_ATOMIC_BEGIN(); \
    UNREFERENCED_PARAMETER(Order); \
    Old = *(volatile KI_PPC_U64 *)Pointer; \
    *(volatile KI_PPC_U64 *)Pointer = Operation; \
    KI_PPC_ATOMIC_END(); \
    return Old; \
}

KI_PPC_FETCH_OP(add, Old + Value)
KI_PPC_FETCH_OP(sub, Old - Value)
KI_PPC_FETCH_OP(and, Old & Value)
KI_PPC_FETCH_OP(or, Old | Value)
KI_PPC_FETCH_OP(xor, Old ^ Value)
KI_PPC_FETCH_OP(nand, ~(Old & Value))
