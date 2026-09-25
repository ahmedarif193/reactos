/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntcompat.c
 * PURPOSE:     NT memory manager compatibility interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

PMMPFN MmPfnDatabase;
MMPFNLIST MmZeroedPageListHead;
MMPFNLIST MmFreePageListHead;
MMPFNLIST MmStandbyPageListHead;
MMPFNLIST MmModifiedPageListHead;
MMPFNLIST MmModifiedNoWritePageListHead;
MMSUPPORT MmSystemCacheWs;
MM_MEMORY_CONSUMER MiMemoryConsumers[MC_MAXIMUM];
PFN_NUMBER MmAvailablePages;
PFN_NUMBER MmResidentAvailablePages;
PFN_NUMBER MmLowMemoryThreshold;
PFN_NUMBER MmHighMemoryThreshold;
PVOID MmSystemPtesStart[2];
PVOID MmSystemPtesEnd[2];
PVOID MmNonPagedSystemStart;
PVOID MmSystemCacheStart;
PVOID MmSystemCacheEnd;
SIZE_T MmAllocationFragment;
SIZE_T MmDriverCommit;
ULONG MmNumberOfSystemPtes;
ULONG MmSecondaryColors;
ULONG MmSecondaryColorMask;
ULONG MmSystemPageColor;
ULONG MmReadClusterSize;
ULONG_PTR MmSubsectionBase;
BOOLEAN MmDynamicPfn;
BOOLEAN MmMirroring;
BOOLEAN MmTrackPtes;
BOOLEAN MmLargeSystemCache;

static ULONG MiExecuteOptions;

NTSTATUS
NTAPI
MmGetExecuteOptions(
    _Out_ PULONG ExecuteOptions)
{
    *ExecuteOptions = MiExecuteOptions;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmSetExecuteOptions(
    _In_ ULONG ExecuteOptions)
{
    MiExecuteOptions = ExecuteOptions;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtSetInformationVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _In_ VIRTUAL_MEMORY_INFORMATION_CLASS VmInformationClass,
    _In_ ULONG_PTR NumberOfEntries,
    _In_ PMEMORY_RANGE_ENTRY VirtualAddresses,
    _In_ PVOID VmInformation,
    _In_ ULONG VmInformationLength)
{
    MI_PROCESS_REFERENCE Target;
    PMEMORY_RANGE_ENTRY Ranges;
    ULONG Flag = 0;
    SIZE_T Bytes;
    ULONG_PTR Index;
    NTSTATUS Status;

    PAGED_CODE();

    if (VmInformationClass != VmPageDirtyStateInformation)
        return STATUS_SUCCESS;
    if (NumberOfEntries == 0 || NumberOfEntries > MAXULONG / sizeof(MEMORY_RANGE_ENTRY))
        return STATUS_INVALID_PARAMETER_3;
    if (VmInformation == NULL)
        return STATUS_INVALID_PARAMETER_5;
    if (VmInformationLength != sizeof(Flag))
        return STATUS_INVALID_PARAMETER_6;
    if (VirtualAddresses == NULL)
        return STATUS_ACCESS_VIOLATION;

    Bytes = NumberOfEntries * sizeof(MEMORY_RANGE_ENTRY);
    Ranges = ExAllocatePoolWithTag(PagedPool, Bytes, 'rMvN');
    if (Ranges == NULL)
        return STATUS_NO_MEMORY;

    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
        {
            ProbeForRead(VmInformation, sizeof(Flag), sizeof(Flag));
            ProbeForRead(VirtualAddresses, Bytes, sizeof(PVOID));
        }
        Flag = *(PULONG)VmInformation;
        RtlCopyMemory(Ranges, VirtualAddresses, Bytes);
        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (Flag != 0)
    {
        Status = STATUS_INVALID_PARAMETER_5;
        goto Cleanup;
    }

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    for (Index = 0; Index < NumberOfEntries; Index++)
    {
        Status = MiResetExecutableWriteTracking(MiSpaceOfProcess(Target.Process),
                                               (ULONG64)(ULONG_PTR)Ranges[Index].VirtualAddress,
                                               (ULONG64)Ranges[Index].NumberOfBytes);
        if (!NT_SUCCESS(Status))
            break;
    }

    MiReleaseTargetProcess(&Target);

Cleanup:
    ExFreePoolWithTag(Ranges, 'rMvN');
    return Status;
}

NTSTATUS
MmSetProcessExecutableWriteTracking(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Enable)
{
    if (MI_PROCESS_OF(Process) == NULL)
        return STATUS_PROCESS_IS_TERMINATING;

    return MiSetExecutableWriteTracking(MiSpaceOfProcess(Process), Enable);
}

#define MI_EC_CODE_BITMAP_SIZE (1ULL << 32)

BOOLEAN
MiIsEcCodeAddress(
    _In_ PEPROCESS Process,
    _In_ PVOID Address)
{
#if defined(_WIN64) && (NTDDI_VERSION >= NTDDI_WIN10)
    volatile ULONGLONG *BitmapWord;
    ULONG_PTR AddressValue = (ULONG_PTR)Address;
    ULONG_PTR BitmapAddress;
    ULONG_PTR BitmapOffset;
    BOOLEAN IsEcCode = FALSE;
    PVOID BitmapBase;

    if (Process != PsGetCurrentProcess() || Process->Peb == NULL ||
        AddressValue > (ULONG_PTR)MM_HIGHEST_USER_ADDRESS)
    {
        return FALSE;
    }

    BitmapOffset = (AddressValue >> 15) & (MI_EC_CODE_BITMAP_SIZE - sizeof(*BitmapWord));

    _SEH2_TRY
    {
        if (!MmIsAddressValid(Process->Peb))
            _SEH2_YIELD(return FALSE);

        BitmapBase = Process->Peb->EcCodeBitMap;
        BitmapAddress = (ULONG_PTR)BitmapBase + BitmapOffset;

        if (BitmapBase == NULL || BitmapAddress < (ULONG_PTR)BitmapBase ||
            BitmapAddress > (ULONG_PTR)MM_HIGHEST_USER_ADDRESS - sizeof(*BitmapWord) + 1)
        {
            _SEH2_YIELD(return FALSE);
        }

        BitmapWord = (volatile ULONGLONG *)BitmapAddress;
        if (!MmIsAddressValid((PVOID)BitmapWord))
            _SEH2_YIELD(return FALSE);

        IsEcCode = (BOOLEAN)((*BitmapWord & (1ULL << ((AddressValue >> PAGE_SHIFT) & 63))) != 0);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        IsEcCode = FALSE;
    }
    _SEH2_END;

    return IsEcCode;
#else
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(Address);
    return FALSE;
#endif
}
