/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/exceptinit.c
 * PURPOSE:         Final exception/interrupt initialization stubs for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

BOOLEAN KiArm64FinalVectorsInstalled = FALSE;
BOOLEAN KiArm64SvcConfigured = FALSE;
BOOLEAN KiArm64IrqFiqConfigured = FALSE;
ULONG64 KiArm64BootSErrorDisr = 0;

extern const UINT64 KiArm64VectorTable[];

/* Lightweight vector log for IRQ/FIQ/SError permanent stubs */
VOID
KiArm64VectorLogOnly(
    _In_ ULONG Esr,
    _In_ ULONG_PTR Far,
    _In_ ULONG VectorId)
{
    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
               "[arm64] PermVector: id=%lu esr=0x%lx far=%p\n",
               VectorId, Esr, (PVOID)Far);
}

CODE_SEG("INIT")
VOID
NTAPI
KeInitExceptions(VOID)
{
    UINT64 Vbar = (UINT64)(ULONG_PTR)&KiArm64VectorTable;
    UINT64 Pfr0;
    UINT64 Disr;

    /* Program the permanent vector base and advertise readiness */
    __asm__ __volatile__("msr vbar_el1, %0" :: "r"(Vbar));
    __asm__ __volatile__("isb");

    __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(Pfr0));
    if (((Pfr0 >> 28) & 0xF) != 0)
    {
        __asm__ __volatile__("dsb sy" ::: "memory");
        __asm__ __volatile__("hint #16");
        __asm__ __volatile__("isb");
        __asm__ __volatile__("mrs %0, s3_0_c12_c1_1" : "=r"(Disr));
        if (Disr & (1ULL << 31))
        {
            KiArm64BootSErrorDisr = Disr;
            __asm__ __volatile__("msr s3_0_c12_c1_1, xzr");
            __asm__ __volatile__("isb");
        }
    }

    KiArm64FinalVectorsInstalled = TRUE;
    KiArm64SvcConfigured = TRUE;
    KiArm64IrqFiqConfigured = TRUE;
}
