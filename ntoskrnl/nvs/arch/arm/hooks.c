/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/arm/hooks.c
 * PURPOSE:     ARM memory manager backend entry points
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <ntoskrnl.h>
#include <nvs/include/miarch.h>

NTSTATUS
MiArchAdoptBootTables(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
MiArchProbePhysicalLayout(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
                          _Out_writes_(*Count) PMI_PHYSICAL_RANGE Ranges,
                          _Inout_ PULONG Count,
                          _Out_ PULONG64 HighestPage)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    UNREFERENCED_PARAMETER(Ranges);
    *Count = 0;
    *HighestPage = 0;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
MiArchInitMachineDependent(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
MiArchMapPfnDatabase(_In_ ULONG64 HighestPage)
{
    UNREFERENCED_PARAMETER(HighestPage);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
MiArchBuildNonPagedPool(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
MiArchBuildSystemPteSpace(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
MiArchEnsureRangeBacked(_In_ PVOID BaseAddress, _In_ SIZE_T Size)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(Size);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
MiArchInitSessionLayout(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
MiArchCreateAddressSpace(_In_ PEPROCESS Process, _Out_ PULONG64 DirectoryTableBase)
{
    UNREFERENCED_PARAMETER(Process);
    *DirectoryTableBase = 0;
    return STATUS_NOT_IMPLEMENTED;
}

VOID
MiArchDeleteAddressSpace(_In_ PEPROCESS Process)
{
    UNREFERENCED_PARAMETER(Process);
}

VOID
MiArchSwitchAddressSpace(_In_ ULONG64 DirectoryTableBase)
{
    UNREFERENCED_PARAMETER(DirectoryTableBase);
}

VOID
MiArchWritePte(_Inout_ PMMPTE Pte, _In_ MMPTE Value)
{
    UNREFERENCED_PARAMETER(Pte);
    UNREFERENCED_PARAMETER(Value);
}

MMPTE
MiArchClearPte(_Inout_ PMMPTE Pte)
{
    MMPTE Zero;

    UNREFERENCED_PARAMETER(Pte);
    Zero.u.Long = 0;
    return Zero;
}

MMPTE
MiArchReadPte(_In_ PMMPTE Pte)
{
    MMPTE Zero;

    UNREFERENCED_PARAMETER(Pte);
    Zero.u.Long = 0;
    return Zero;
}

ULONG64
MiArchMakeProtectionPte(_In_ ULONG Protection)
{
    UNREFERENCED_PARAMETER(Protection);
    return 0;
}

BOOLEAN
MiArchPteIsResident(_In_ MMPTE Pte)
{
    UNREFERENCED_PARAMETER(Pte);
    return FALSE;
}



BOOLEAN
MiArchConsumeDirtyState(_Inout_ PMMPTE Pte)
{
    UNREFERENCED_PARAMETER(Pte);
    return FALSE;
}

PVOID
MiArchMapPageTable(_In_ ULONG64 PageFrame, _In_ UCHAR Level)
{
    UNREFERENCED_PARAMETER(PageFrame);
    UNREFERENCED_PARAMETER(Level);
    return NULL;
}

VOID
MiArchUnmapPageTable(_In_ PVOID Mapping)
{
    UNREFERENCED_PARAMETER(Mapping);
}

VOID
MiArchInvalidateTlbSingle(_In_ PVOID VirtualAddress, _In_ MI_TLB_SCOPE Scope)
{
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(Scope);
}

VOID
MiArchInvalidateTlbRange(_In_ PVOID BaseAddress, _In_ SIZE_T Size, _In_ MI_TLB_SCOPE Scope)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(Scope);
}

VOID
MiArchInvalidateTlbAll(_In_ MI_TLB_SCOPE Scope)
{
    UNREFERENCED_PARAMETER(Scope);
}

VOID
MiArchSyncPageTableWrite(VOID)
{
}

VOID
MiArchCleanDataRange(_In_ PVOID BaseAddress, _In_ SIZE_T Size)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(Size);
}

VOID
MiArchInvalidateInstructionRange(_In_ PVOID BaseAddress, _In_ SIZE_T Size)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(Size);
}
