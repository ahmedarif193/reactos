/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/mint.h
 * PURPOSE:     Internal NT memory manager integration interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <ntoskrnl.h>
#include <nvs/include/miproc.h>

#ifndef _1KB
#define _1KB ((SIZE_T)1024u)
#define _1MB (1024 * _1KB)
#define _1GB (1024 * _1MB)
#endif

#include <nvs/nt/mmkernel.h>

#define MI_NT_UNUSED_SEGMENT_LIMIT 1024
#define MI_NT_BOTTOM_UP_SLOTS 256
#define MI_NT_HIGH_ENTROPY_FLOOR 0x100000000ULL
#define MI_NT_HIGH_ENTROPY_LIMIT 0x10000000000ULL
#define MI_NT_TOP_DOWN_CEILING 0x7FF5FFFFFFFFULL
#define MI_NT_SECTION_TYPE_CODE 0x80
#define MI_NT_SECTION_PAGED_CHARGE 64

typedef struct _MI_CONTROL_AREA
{
    PMI_SEGMENT Segment;
    PFILE_OBJECT FileObject;
    BOOLEAN Image;
    BOOLEAN Image64;
    BOOLEAN Physical;
    PVOID BasedAddress;
    ULONG64 ImageSize;
    SECTION_IMAGE_INFORMATION ImageInformation;
} MI_CONTROL_AREA, *PMI_CONTROL_AREA;

typedef struct _MI_SECTION_OBJECT
{
    PMI_CONTROL_AREA Control;
    LARGE_INTEGER SizeOfSection;
    ULONG InitialProtection;
    ULONG Protection;
    ULONG AllocationAttributes;
    PVOID BasedAddress;
} MI_SECTION_OBJECT, *PMI_SECTION_OBJECT;

extern SIZE_T MmtotalCommitLimitMaximum;

extern MI_SYSTEM MiSystem;
extern MI_PROCESS_MANAGER MiProcessManager;

#define MI_PROCESS_OF(Process)  ((PMI_PROCESS)(Process)->Vm.Instance.VmWorkingSetList)
#define MI_IS_SYSTEM_VA(Va)     ((ULONG_PTR)(Va) >= (ULONG_PTR)MmSystemRangeStart)

FORCEINLINE
PMI_ADDRESS_SPACE
MiSpaceOfProcess(
    _In_ PEPROCESS Process)
{
    return &MI_PROCESS_OF(Process)->Space;
}

FORCEINLINE
BOOLEAN
MiProcessHasSecureRanges(
    _In_ PEPROCESS Process)
{
    PMI_PROCESS Native = MI_PROCESS_OF(Process);

    return (BOOLEAN)(Native != NULL && MI_ATOMIC_READ32(&Native->SecureRangeCount) != 0);
}

FORCEINLINE
PMI_ADDRESS_SPACE
MiSpaceForAddress(
    _In_ PVOID Address)
{
    PEPROCESS Process = PsGetCurrentProcess();

    if (!MI_IS_SYSTEM_VA(Address))
        return MiSpaceOfProcess(Process);

    if (MiArchIsSelfMapAddress((ULONG64)(ULONG_PTR)Address) && Process->Vm.Instance.VmWorkingSetList != NULL)
        return MiSpaceOfProcess(Process);

    return &MiSystem.SystemSpace;
}

FORCEINLINE
MI_CACHE_TYPE
MiCacheTypeFromNt(
    _In_ MEMORY_CACHING_TYPE CacheType)
{
    CacheType &= 0xFF;

    if (CacheType == MmCached || CacheType == MmHardwareCoherentCached)
        return MiCacheFull;

    if (CacheType == MmWriteCombined)
        return MiCacheWriteCombined;

    return MiCacheNone;
}

BOOLEAN MiProtectionFromWin32(_In_ ULONG Win32Protect, _Out_ PULONG Protection);
BOOLEAN MiAllocationProtectionFromWin32(_In_ ULONG Win32Protect, _Out_ PULONG Protection);
ULONG MiProtectionToWin32(_In_ ULONG Protection);
NTSTATUS MiWaitForMemory(_In_ NTSTATUS Status, _Inout_ PULONG Attempts);
NTSTATUS MiControlRead(_In_opt_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Length, _Out_ PVOID Buffer);
NTSTATUS MiControlReadPages(_In_opt_ PVOID Context, _In_ ULONG64 Offset,
                            _In_ const ULONG *Frames, _In_ ULONG PageCount);
NTSTATUS MiControlReadAsync(_In_opt_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Frame,
                            _In_ PVOID Buffer, _In_ MI_READ_COMPLETION Completion,
                            _In_opt_ PVOID CompletionContext);
NTSTATUS MiControlWrite(_In_opt_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Length, _In_ PVOID Buffer);
NTSTATUS MiControlWriteFrames(_In_opt_ PVOID Context, _In_ ULONG64 Offset, _In_ ULONG Length,
                              _In_ const ULONG *Frames, _In_ ULONG PageCount);
VOID MiMemoryEventInitialize(VOID);
PMI_CONTROL_AREA MiReferenceDataControlArea(_In_ PSECTION_OBJECT_POINTERS Pointers);
VOID MiDereferenceControlArea(_Inout_ PMI_CONTROL_AREA Control);
NTSTATUS MiCreateDataControlArea(_In_ PFILE_OBJECT FileObject, _In_ ULONG64 Size, _Out_ PMI_CONTROL_AREA *Control);
NTSTATUS MiSectionInitialize(VOID);
NTSTATUS MiPagingIo(_In_ PFILE_OBJECT FileObject, _In_ ULONG64 Offset, _In_ ULONG Length, _In_ PVOID Buffer,
                    _In_ BOOLEAN Write, _Out_ PULONG Transferred);
NTSTATUS MiSubmitPagingMdl(_In_ PFILE_OBJECT FileObject, _Inout_ PMDL Mdl, _In_ ULONG64 Offset,
                           _In_ BOOLEAN Write, _Out_ PULONG Transferred);
NTSTATUS MiPagingIoFrames(_In_ PFILE_OBJECT FileObject, _In_ ULONG64 Offset, _In_ const ULONG *Frames,
                          _In_ ULONG PageCount, _In_ BOOLEAN Write, _Out_ PULONG Transferred);
NTSTATUS MiWorkerThreadsInitialize(VOID);
VOID MiWakeBalanceSetManager(VOID);

extern SIZE_T MmTotalCommitLimit;
extern SIZE_T MmTotalCommitLimitMaximum;
extern SIZE_T MmTotalCommittedPages;
extern SIZE_T MmPeakCommitment;
VOID MiSyncProcessCounters(_Inout_ PEPROCESS Process);
BOOLEAN MiChargeProcessCommit(_In_ PVOID Owner, _In_ LONG64 Pages);
VOID MiReturnProcessCommit(_In_ PVOID Owner, _In_ LONG64 Pages);
BOOLEAN MiDynamicCodeBlocked(_In_ PEPROCESS Process);
NTSTATUS MiValidateImageSigningPolicy(_In_ PFILE_OBJECT FileObject);
BOOLEAN MiFrameIsRam(_In_ ULONG64 Frame);
BOOLEAN MiSecureRangeConflict(_In_ PEPROCESS Process, _In_ ULONG64 Start, _In_ ULONG64 End, _In_ BOOLEAN Release,
                              _In_ ULONG NewProtection);
VOID MiSecureRangePurgeProcess(_In_ PEPROCESS Process);
VOID MiVadRangeForAddress(_In_ PMI_ADDRESS_SPACE Space, _In_ ULONG64 Address, _Out_ PULONG64 Start,
                          _Out_ PULONG64 End);
NTSTATUS MiQuerySectionName(_In_ HANDLE ProcessHandle, _In_ PVOID BaseAddress, _Out_ PVOID MemoryInformation,
                            _In_ SIZE_T MemoryInformationLength, _Out_opt_ PSIZE_T ReturnLength);
VOID MiSessionAddProcess(_Inout_ PEPROCESS NewProcess);
VOID MiSessionRemoveProcess(_Inout_ PEPROCESS Process);
VOID MiSignalMemoryAvailable(VOID);
