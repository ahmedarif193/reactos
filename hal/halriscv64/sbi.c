/*
 * PROJECT:     ReactOS RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Supervisor Binary Interface calls
 */

#include <ntifs.h>
#include "halp.h"

ULONG HalpRiscvSbiVersion;
ULONG_PTR HalpRiscvSbiImplementationId;

static
RISCV_SBI_RETURN
HalpRiscvSbiCall(
    _In_ ULONG_PTR Extension,
    _In_ ULONG_PTR Function,
    _In_ ULONG_PTR Argument0,
    _In_ ULONG_PTR Argument1,
    _In_ ULONG_PTR Argument2)
{
    register ULONG_PTR A0 __asm__("a0") = Argument0;
    register ULONG_PTR A1 __asm__("a1") = Argument1;
    register ULONG_PTR A2 __asm__("a2") = Argument2;
    register ULONG_PTR A3 __asm__("a3") = 0;
    register ULONG_PTR A4 __asm__("a4") = 0;
    register ULONG_PTR A5 __asm__("a5") = 0;
    register ULONG_PTR A6 __asm__("a6") = Function;
    register ULONG_PTR A7 __asm__("a7") = Extension;
    RISCV_SBI_RETURN Result;

    __asm__ __volatile__("ecall"
                         : "+r"(A0), "+r"(A1)
                         : "r"(A2), "r"(A3), "r"(A4), "r"(A5),
                           "r"(A6), "r"(A7)
                         : "memory");
    Result.Error = (LONG_PTR)A0;
    Result.Value = A1;
    return Result;
}

BOOLEAN
HalpRiscvInitializeSbi(VOID)
{
    RISCV_SBI_RETURN Result;
    ULONG Version, Major, Minor;

    Result = HalpRiscvSbiCall(RISCV_SBI_EXTENSION_BASE,
                              RISCV_SBI_BASE_GET_VERSION,
                              0,
                              0,
                              0);
    if ((Result.Error != 0) || (Result.Value > MAXULONG))
        return FALSE;

    Version = (ULONG)Result.Value;
    if (Version & 0x80000000UL)
        return FALSE;
    Major = (Version >> 24) & 0x7F;
    Minor = Version & 0x00FFFFFF;
    if ((Major == 0) && (Minor < 2))
        return FALSE;

    Result = HalpRiscvSbiCall(RISCV_SBI_EXTENSION_BASE,
                              RISCV_SBI_BASE_PROBE_EXTENSION,
                              RISCV_SBI_EXTENSION_TIME,
                              0,
                              0);
    if ((Result.Error != 0) || (Result.Value == 0))
        return FALSE;

    HalpRiscvSbiVersion = Version;
    Result = HalpRiscvSbiCall(RISCV_SBI_EXTENSION_BASE,
                              RISCV_SBI_BASE_GET_IMPL_ID,
                              0,
                              0,
                              0);
    if (Result.Error != 0)
        return FALSE;
    HalpRiscvSbiImplementationId = Result.Value;
    return TRUE;
}

RISCV_SBI_RETURN
HalpRiscvSetTimer(
    _In_ ULONG64 Deadline)
{
    return HalpRiscvSbiCall(RISCV_SBI_EXTENSION_TIME,
                            RISCV_SBI_TIME_SET_TIMER,
                            (ULONG_PTR)Deadline,
                            0,
                            0);
}

ULONG64
HalpRiscvReadTime(VOID)
{
    ULONG_PTR Value;

    __asm__ __volatile__("csrr %0, time" : "=r"(Value) :: "memory");
    return Value;
}


/* SBI System Reset extension (SRST, SBI v0.3+). Returns only on failure. */
BOOLEAN
HalpRiscvSystemReset(
    _In_ ULONG_PTR ResetType)
{
    RISCV_SBI_RETURN Result;

    Result = HalpRiscvSbiCall(RISCV_SBI_EXTENSION_BASE,
                              RISCV_SBI_BASE_PROBE_EXTENSION,
                              RISCV_SBI_EXTENSION_SRST,
                              0,
                              0);
    if ((Result.Error != 0) || (Result.Value == 0))
        return FALSE;
    HalpRiscvSbiCall(RISCV_SBI_EXTENSION_SRST,
                     RISCV_SBI_SRST_SYSTEM_RESET,
                     ResetType,
                     RISCV_SBI_SRST_REASON_NONE,
                     0);
    return FALSE;
}
