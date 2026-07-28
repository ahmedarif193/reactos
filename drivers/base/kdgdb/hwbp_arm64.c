/*
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:         GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            drivers/base/kdgdb/hwbp_arm64.c
 * PURPOSE:         Hardware debug register support for ARM64.
 *
 * Unlike x86, ARM64 keeps two separate debug register files: eight instruction
 * breakpoints (DBGBVR/DBGBCR) that can only match on execution, and two data
 * watchpoints (DBGWVR/DBGWCR). Slots are therefore partitioned rather than
 * shared: the first ARM64_MAX_BREAKPOINTS are execute-only, the rest are
 * watchpoint-only.
 */

#include "kdgdb.h"

/* DBGBCR<n>_EL1 */
#define ARM64_BCR_ENABLE        (1UL << 0)
/* Privilege mode control: 0b11 matches both EL0 and EL1 */
#define ARM64_BCR_PMC_EL0_EL1   (3UL << 1)
/* Byte address select: all four bytes of the A64 instruction */
#define ARM64_BCR_BAS_ANY       (0xFUL << 5)

/* DBGWCR<n>_EL1 */
#define ARM64_WCR_ENABLE        (1UL << 0)
/* Privilege access control: 0b11 matches both EL0 and EL1 */
#define ARM64_WCR_PAC_EL0_EL1   (3UL << 1)
#define ARM64_WCR_LSC_LOAD      (1UL << 3)
#define ARM64_WCR_LSC_STORE     (2UL << 3)
#define ARM64_WCR_LSC_BOTH      (3UL << 3)
#define ARM64_WCR_BAS_SHIFT     5

#define ARM64_WATCHPOINT_SLOT(Slot) ((Slot) - ARM64_MAX_BREAKPOINTS)

const ULONG gdb_hw_breakpoint_count =
    ARM64_MAX_BREAKPOINTS + ARM64_MAX_WATCHPOINTS;

static
VOID
arm64_debug_register_counts(
    _Out_ PULONG BreakpointCount,
    _Out_ PULONG WatchpointCount)
{
    ULONG64 Dfr0;

    __asm__ __volatile__("mrs %0, id_aa64dfr0_el1" : "=r"(Dfr0));
    *BreakpointCount = (ULONG)(((Dfr0 >> 12) & 0xF) + 1);
    *WatchpointCount = (ULONG)(((Dfr0 >> 20) & 0xF) + 1);

    if (*BreakpointCount > ARM64_MAX_BREAKPOINTS)
        *BreakpointCount = ARM64_MAX_BREAKPOINTS;
    if (*WatchpointCount > ARM64_MAX_WATCHPOINTS)
        *WatchpointCount = ARM64_MAX_WATCHPOINTS;
}

BOOLEAN
gdb_arch_hw_slot_usable(
    _In_ ULONG Slot,
    _In_ UCHAR Type)
{
    ULONG BreakpointCount;
    ULONG WatchpointCount;

    if (Slot >= gdb_hw_breakpoint_count)
        return FALSE;

    arm64_debug_register_counts(&BreakpointCount, &WatchpointCount);

    /* Execution matches only in the breakpoint file, data only in the watchpoint one */
    if (Type == GDB_HW_EXECUTE)
        return Slot < BreakpointCount;

    return Slot >= ARM64_MAX_BREAKPOINTS &&
           ARM64_WATCHPOINT_SLOT(Slot) < WatchpointCount;
}

BOOLEAN
gdb_arch_hw_breakpoint_valid(
    _In_ const GDB_HARDWARE_BREAKPOINT* Breakpoint)
{
    if (Breakpoint->Type < GDB_HW_EXECUTE ||
        Breakpoint->Type > GDB_HW_ACCESS)
    {
        return FALSE;
    }

    if (Breakpoint->Type == GDB_HW_EXECUTE)
    {
        /* A64 instructions are always four bytes, on a four byte boundary */
        return Breakpoint->Kind == 4 && (Breakpoint->Address & 3) == 0;
    }

    /*
     * A watchpoint covers up to eight bytes, selected byte by byte through BAS,
     * and may not straddle a doubleword.
     */
    if (Breakpoint->Kind != 1 && Breakpoint->Kind != 2 &&
        Breakpoint->Kind != 4 && Breakpoint->Kind != 8)
    {
        return FALSE;
    }

    if ((Breakpoint->Address & 7) + Breakpoint->Kind > 8)
        return FALSE;

    return TRUE;
}

static
ULONG
watchpoint_control(
    _In_ const GDB_HARDWARE_BREAKPOINT* Breakpoint)
{
    ULONG Control = ARM64_WCR_ENABLE | ARM64_WCR_PAC_EL0_EL1;
    ULONG Bytes = Breakpoint->Kind;
    ULONG FirstByte;

    switch (Breakpoint->Type)
    {
        case GDB_HW_WRITE: Control |= ARM64_WCR_LSC_STORE; break;
        case GDB_HW_READ:  Control |= ARM64_WCR_LSC_LOAD; break;
        case GDB_HW_ACCESS: Control |= ARM64_WCR_LSC_BOTH; break;
        default: ASSERT(FALSE); break;
    }

    /*
     * DBGWVR holds the doubleword address, so BAS selects which bytes within
     * that doubleword the watchpoint actually covers.
     */
    FirstByte = (ULONG)(Breakpoint->Address & 7);
    Control |= (((1UL << Bytes) - 1) << FirstByte) << ARM64_WCR_BAS_SHIFT;

    return Control;
}

VOID
gdb_arch_program_hw_breakpoints(
    _Inout_ PKSPECIAL_REGISTERS Registers,
    _In_ const GDB_HARDWARE_BREAKPOINT* Breakpoints)
{
    ULONG Slot;

    for (Slot = 0; Slot < ARM64_MAX_BREAKPOINTS; Slot++)
    {
        if (Breakpoints[Slot].Active)
        {
            Registers->KernelBvr[Slot] = Breakpoints[Slot].Address & ~3ULL;
            Registers->KernelBcr[Slot] = ARM64_BCR_ENABLE | ARM64_BCR_PMC_EL0_EL1 |
                                         ARM64_BCR_BAS_ANY;
        }
        else
        {
            Registers->KernelBvr[Slot] = 0;
            Registers->KernelBcr[Slot] = 0;
        }
    }

    for (Slot = ARM64_MAX_BREAKPOINTS; Slot < gdb_hw_breakpoint_count; Slot++)
    {
        ULONG Index = ARM64_WATCHPOINT_SLOT(Slot);

        if (Breakpoints[Slot].Active)
        {
            Registers->KernelWvr[Index] = Breakpoints[Slot].Address & ~7ULL;
            Registers->KernelWcr[Index] = watchpoint_control(&Breakpoints[Slot]);
        }
        else
        {
            Registers->KernelWvr[Index] = 0;
            Registers->KernelWcr[Index] = 0;
        }
    }
}

VOID
gdb_arch_report_hw_breakpoints(
    _In_ const GDB_HARDWARE_BREAKPOINT* Breakpoints)
{
    /*
     * Unlike x86 DR7, ARM64 has no control value to send back through KD's
     * continue packet. KdpSetContextState fills Bvr/Wvr from the event that
     * actually stopped the processor.
     */
    UNREFERENCED_PARAMETER(Breakpoints);
}

BOOLEAN
gdb_arch_hw_breakpoint_hit(
    _In_ const GDB_HARDWARE_BREAKPOINT* Breakpoints,
    _In_ ULONG Slot)
{
    const EXCEPTION_RECORD64* ExceptionRecord;
    ULONG ClosestSlot;
    ULONG Candidate;
    ULONG64 MinimumDistance;
    ULONG64 Reported;

    if (Slot < ARM64_MAX_BREAKPOINTS)
    {
        Reported = CurrentStateChange.ControlReport.Bvr;
        return Reported != 0 && Reported == (Breakpoints[Slot].Address & ~3ULL);
    }

    /*
     * FAR_EL1 can name an address near the selected byte when one instruction
     * accesses both watched and unwatched bytes. Attribute the event to the
     * closest active watchpoint, preferring the first exact match.
     */
    ExceptionRecord = &CurrentStateChange.u.Exception.ExceptionRecord;
    if (ExceptionRecord->NumberParameters == 0)
        return FALSE;

    Reported = CurrentStateChange.ControlReport.Wvr;
    ClosestSlot = gdb_hw_breakpoint_count;
    MinimumDistance = MAXULONGLONG;

    for (Candidate = ARM64_MAX_BREAKPOINTS;
         Candidate < gdb_hw_breakpoint_count;
         Candidate++)
    {
        const GDB_HARDWARE_BREAKPOINT* Breakpoint = &Breakpoints[Candidate];
        ULONG64 LastByte;
        ULONG64 Distance;

        if (!Breakpoint->Active)
            continue;

        if (ExceptionRecord->NumberParameters > 1)
        {
            BOOLEAN IsWrite = ExceptionRecord->ExceptionInformation[1] != 0;

            if ((IsWrite && Breakpoint->Type == GDB_HW_READ) ||
                (!IsWrite && Breakpoint->Type == GDB_HW_WRITE))
            {
                continue;
            }
        }

        LastByte = Breakpoint->Address + Breakpoint->Kind - 1;
        if (Reported < Breakpoint->Address)
            Distance = Breakpoint->Address - Reported;
        else if (Reported > LastByte)
            Distance = Reported - LastByte;
        else
            Distance = 0;

        if (Distance < MinimumDistance)
        {
            MinimumDistance = Distance;
            ClosestSlot = Candidate;
        }
    }

    return Slot == ClosestSlot;
}

BOOLEAN
gdb_arch_prepare_resume(
    _Inout_ CONTEXT* Context,
    _In_ const GDB_HARDWARE_BREAKPOINT* Breakpoints)
{
    BOOLEAN DebugEnabled;
    ULONG Slot;

    /*
     * PSTATE.D masks self-hosted debug exceptions. Exception entry sets D in
     * the handler's PSTATE, and early ARM64 bring-up can also leave it set in
     * the saved context. Clear it before returning for either a single step or
     * an armed debug register.
     */
    DebugEnabled = (Context->Cpsr & ARM64_PSTATE_SS) != 0;
    for (Slot = 0; Slot < gdb_hw_breakpoint_count; Slot++)
    {
        if (Breakpoints[Slot].Active)
        {
            DebugEnabled = TRUE;
            break;
        }
    }

    if (!DebugEnabled || (Context->Cpsr & ARM64_PSTATE_D) == 0)
        return FALSE;

    Context->Cpsr &= ~ARM64_PSTATE_D;
    return TRUE;
}

VOID
gdb_arch_set_continue_control(
    _Inout_ DBGKD_MANIPULATE_STATE64* State,
    _In_ const GDB_HARDWARE_BREAKPOINT* Breakpoints)
{
    /*
     * ARM64_DBGKD_CONTROL_SET carries no trace flag and no debug register
     * image: single stepping travels in PSTATE.SS, and the debug registers are
     * pushed straight into the processor state by
     * gdb_arch_program_hw_breakpoints.
     */
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(Breakpoints);
}
