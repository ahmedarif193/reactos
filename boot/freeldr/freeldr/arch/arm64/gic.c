/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Generic Interrupt Controller (GIC) stubs
 * COPYRIGHT:   Copyright 2024 Ahmed ARIF (contact@eotics.com)
 */

#include <freeldr.h>
#include <arch/arm64/arm64.h>
#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

/*
 * These are placeholder stubs to make the ARM64 interrupt path
 * explicit. They should be implemented for the target platform’s
 * GIC (usually GICv3 in AArch64 UEFI environments).
 */

VOID Arm64GicInitialize(VOID)
{
    /* Minimal CPU interface enable for GICv3 */
    ULONGLONG val;
    /* Priority mask: allow all priorities */
    val = 0xFF;
    __asm__ volatile("msr icc_pmr_el1, %0" :: "r" (val) : "memory");
    /* Binary point: no preemption groups */
    val = 0;
    __asm__ volatile("msr icc_bpr1_el1, %0" :: "r" (val) : "memory");
    /* Enable Group1 interrupts */
    val = 1;
    __asm__ volatile("msr icc_igrpen1_el1, %0" :: "r" (val) : "memory");
    __asm__ volatile("isb");
    TRACE("ARM64 GIC: CPU interface enabled (GICv3-style)\n");
}

ULONG Arm64GicGetVersion(VOID)
{
    /* Assume GICv3 in typical AArch64 UEFI environments */
    return 3;
}

ULONG Arm64GicAcknowledgeInterrupt(VOID)
{
    ULONGLONG iar;
    __asm__ volatile("mrs %0, icc_iar1_el1" : "=r" (iar));
    /* Spurious interrupt ID for GICv3 is 1023 */
    if ((iar & 0x3FF) == 1023)
        return (ULONG)~0U;
    return (ULONG)(iar & 0xFFFFFF);
}

VOID Arm64GicEndOfInterrupt(ULONG IntId)
{
    ULONGLONG eoi = (ULONGLONG)IntId;
    __asm__ volatile("msr icc_eoir1_el1, %0" :: "r" (eoi) : "memory");
}

VOID Arm64GicEnableInterrupt(ULONG IntId)
{
    /* Requires GIC distributor base; not configured in FreeLoader. */
    (void)IntId;
}

VOID Arm64GicDisableInterrupt(ULONG IntId)
{
    (void)IntId;
}

VOID Arm64GicSetPriorityMask(UCHAR Priority)
{
    ULONGLONG val = (ULONGLONG)Priority;
    __asm__ volatile("msr icc_pmr_el1, %0" :: "r" (val) : "memory");
    __asm__ volatile("isb");
}

VOID Arm64GicSendSGI(UCHAR SgiId, UCHAR TargetList, BOOLEAN ToAll)
{
    /* Without MPIDR routing details, we cannot reliably send SGIs. */
    (void)SgiId; (void)TargetList; (void)ToAll;
}
