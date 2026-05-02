/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/exceptinit.c
 * PURPOSE:         Final exception/interrupt initialization stubs for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

BOOLEAN KiArm64FinalVectorsInstalled = FALSE;
BOOLEAN KiArm64SvcConfigured = FALSE;
BOOLEAN KiArm64IrqFiqConfigured = FALSE;

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

    /* Program the permanent vector base and advertise readiness */
    __asm__ __volatile__("msr vbar_el1, %0" :: "r"(Vbar));
    __asm__ __volatile__("isb");

    KiArm64FinalVectorsInstalled = TRUE;
    KiArm64SvcConfigured = TRUE;
    KiArm64IrqFiqConfigured = TRUE;
}
