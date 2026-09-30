/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntvm.c
 * PURPOSE:     NT virtual memory system call interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

#define MI_COPY_CHUNK (64 * 1024)

PVOID MmHighestUserAddress;
PVOID MmSystemRangeStart;
ULONG_PTR MmUserProbeAddress;
PVOID MiHighestUserPte;
SIZE_T MmTotalCommitLimit;
SIZE_T MmTotalCommitLimitMaximum;
SIZE_T MmtotalCommitLimitMaximum;
SIZE_T MmTotalCommittedPages;
SIZE_T MmSharedCommit;
SIZE_T MmPeakCommitment;

SIZE_T
NTAPI
MmQuerySystemCommitCharge(
    _Out_opt_ PSIZE_T PeakCommitment)
{
    SIZE_T Committed;
    SIZE_T Peak;

    Committed = (SIZE_T)MI_ATOMIC_READ64(&MiSystem.CommittedPages);
    MmTotalCommittedPages = Committed;

    Peak = MmPeakCommitment;
    while (Committed > Peak)
    {
        if (InterlockedCompareExchangeSizeT(&MmPeakCommitment, Committed, Peak) == Peak)
        {
            Peak = Committed;
            break;
        }
        Peak = MmPeakCommitment;
    }

    if (PeakCommitment != NULL)
        *PeakCommitment = Peak;

    return Committed;
}

NTSTATUS
MiReferenceTargetProcess(
    _In_ HANDLE ProcessHandle,
    _In_ ACCESS_MASK Access,
    _Out_ PMI_PROCESS_REFERENCE Reference)
{
    NTSTATUS Status;

    RtlZeroMemory(Reference, sizeof(*Reference));

    if (ProcessHandle == NtCurrentProcess())
    {
        Reference->Process = PsGetCurrentProcess();
        return STATUS_SUCCESS;
    }

    Status = ObReferenceObjectByHandle(ProcessHandle, Access, PsProcessType, ExGetPreviousMode(),
                                       (PVOID *)&Reference->Process, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Reference->Referenced = TRUE;

    if (MI_PROCESS_OF(Reference->Process) == NULL)
    {
        ObDereferenceObject(Reference->Process);
        return STATUS_PROCESS_IS_TERMINATING;
    }

    if (Reference->Process != PsGetCurrentProcess())
    {
        if (!ExAcquireRundownProtection(&Reference->Process->RundownProtect))
        {
            ObDereferenceObject(Reference->Process);
            return STATUS_PROCESS_IS_TERMINATING;
        }

        Reference->RundownAcquired = TRUE;
    }

    if (Reference->Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Reference->Process->Pcb, &Reference->ApcState);
        Reference->Attached = TRUE;
    }

    return STATUS_SUCCESS;
}

VOID
MiReleaseTargetProcess(
    _Inout_ PMI_PROCESS_REFERENCE Reference)
{
    if (Reference->Attached)
        KeUnstackDetachProcess(&Reference->ApcState);

    if (Reference->RundownAcquired)
        ExReleaseRundownProtection(&Reference->Process->RundownProtect);

    if (Reference->Referenced)
        ObDereferenceObject(Reference->Process);
}

VOID
MiSyncProcessCounters(
    _Inout_ PEPROCESS Process)
{
    PMI_PROCESS Native = MI_PROCESS_OF(Process);
    MI_PROCESS_COUNTERS Counters;

    if (Native == NULL)
        return;

    MiProcessQueryCounters(Native, &Counters);
    Process->Vm.Instance.PageFaultCount = (ULONG)Counters.PageFaultCount;
    Process->Vm.Instance.WorkingSetSize = (SIZE_T)(Counters.WorkingSetSize >> PAGE_SHIFT);
    Process->Vm.Instance.PeakWorkingSetSize = (SIZE_T)(Counters.PeakWorkingSetSize >> PAGE_SHIFT);
    Process->CommitCharge = (SIZE_T)(Counters.PagefileUsage >> PAGE_SHIFT);
    Process->NumberOfPrivatePages = (SIZE_T)MI_ATOMIC_READ64(&Native->Space.PrivatePages);

    if (Process->CommitCharge > Process->CommitChargePeak)
        Process->CommitChargePeak = Process->CommitCharge;
}

static
NTSTATUS
MiCaptureRegion(
    _In_ PVOID *UBaseAddress,
    _In_ PSIZE_T URegionSize,
    _Out_ PVOID *BaseAddress,
    _Out_ PSIZE_T RegionSize)
{
    NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
        {
            ProbeForWritePointer(UBaseAddress);
            ProbeForWriteSize_t(URegionSize);
        }

        *BaseAddress = *UBaseAddress;
        *RegionSize = *URegionSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
NTSTATUS
MiReturnRegion(
    _Out_ PVOID *UBaseAddress,
    _Out_ PSIZE_T URegionSize,
    _In_ ULONG64 BaseAddress,
    _In_ ULONG64 RegionSize)
{
    NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        *UBaseAddress = (PVOID)(ULONG_PTR)BaseAddress;
        *URegionSize = (SIZE_T)RegionSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

VOID
MiVadRangeForAddress(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Address,
    _Out_ PULONG64 Start,
    _Out_ PULONG64 End)
{
    PMI_VAD Vad;

    MI_RW_ACQUIRE_SHARED(&Space->Lock);
    Vad = MiVadLocate(Space, Address);
    *Start = (Vad != NULL) ? (Vad->Node.StartingVpn << PAGE_SHIFT) : (Address & ~((ULONG64)PAGE_SIZE - 1));
    *End = (Vad != NULL) ? ((Vad->Node.EndingVpn + 1) << PAGE_SHIFT) : *Start + PAGE_SIZE;
    MI_RW_RELEASE_SHARED(&Space->Lock);
}

BOOLEAN
MiDynamicCodeBlocked(
    _In_ PEPROCESS Process)
{
    PETHREAD Thread;

    if (!Process->MitigationFlagsValues.DisableDynamicCode)
        return FALSE;

    Thread = PsGetCurrentThread();
    return !(THREAD_TO_PROCESS(Thread) == Process &&
             Process->MitigationFlagsValues.DisableDynamicCodeAllowOptOut &&
             ReadAcquire(&Thread->DynamicCodeOptOut));
}

static
BOOLEAN
MiHighestAddressFromZeroBits(
    _In_ ULONG_PTR ZeroBits,
    _Out_ PULONG64 HighestAddress)
{
    ULONG64 Highest = (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;

    if (ZeroBits != 0 && ZeroBits < 32)
    {
        if (ZeroBits > 21)
            return FALSE;

        Highest = min(Highest, ~0ULL >> (ZeroBits + 32));
    }
    else if (ZeroBits != 0)
    {
        ULONG Shift;

        if (sizeof(ZeroBits) < sizeof(ULONG64))
            return FALSE;

        for (Shift = 1; Shift < sizeof(ZeroBits) * 8; Shift <<= 1)
            ZeroBits |= ZeroBits >> Shift;

        if (ZeroBits < (MAXULONG_PTR >> MI_MAX_ZERO_BITS))
            return FALSE;

        Highest = min(Highest, (ULONG64)ZeroBits);
    }

    *HighestAddress = Highest;
    return TRUE;
}

static
NTSTATUS
MiAllocateVirtualMemoryNt(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *UBaseAddress,
    _In_ ULONG_PTR ZeroBits,
    _Inout_ PSIZE_T URegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG Protect,
    _In_ ULONG64 LowestAddress,
    _In_ ULONG64 HighestEndingAddress,
    _In_ ULONG64 Alignment,
    _In_ BOOLEAN EcCode,
    _In_ BOOLEAN Extended)
{
    MI_PROCESS_REFERENCE Target;
    ULONG64 Base, Size, Highest;
    ULONG Protection;
    ULONG Type = 0;
    ULONG Attempts = 0;
    PVOID BaseAddress;
    SIZE_T RegionSize;
    BOOLEAN DenyDynamicCode;
    NTSTATUS Status;

    PAGED_CODE();

    if (!MiHighestAddressFromZeroBits(ZeroBits, &Highest))
        return STATUS_INVALID_PARAMETER;

    if (HighestEndingAddress != 0 && HighestEndingAddress < Highest)
        Highest = HighestEndingAddress;

    if (AllocationType & ~(MEM_COMMIT | MEM_RESERVE | MEM_RESET | MEM_PHYSICAL | MEM_TOP_DOWN | MEM_WRITE_WATCH |
                           MEM_LARGE_PAGES | MEM_ROTATE |
                           (Extended ? (MEM_RESERVE_PLACEHOLDER | MEM_REPLACE_PLACEHOLDER) : 0)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((AllocationType & MEM_RESERVE_PLACEHOLDER) &&
        ((AllocationType & (MEM_RESERVE | MEM_COMMIT | MEM_RESET | MEM_REPLACE_PLACEHOLDER | MEM_PHYSICAL |
                            MEM_LARGE_PAGES | MEM_WRITE_WATCH | MEM_ROTATE)) != MEM_RESERVE ||
         Protect != PAGE_NOACCESS))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((AllocationType & MEM_WRITE_WATCH) && !(AllocationType & MEM_RESERVE))
        return STATUS_INVALID_PARAMETER;

    if ((AllocationType & MEM_REPLACE_PLACEHOLDER) &&
        (!(AllocationType & MEM_RESERVE) || (AllocationType & (MEM_RESET | MEM_PHYSICAL | MEM_LARGE_PAGES | MEM_ROTATE))))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((AllocationType & MEM_ROTATE) &&
        (!(AllocationType & MEM_RESERVE) || (AllocationType & (MEM_PHYSICAL | MEM_LARGE_PAGES | MEM_WRITE_WATCH))))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!(AllocationType & (MEM_COMMIT | MEM_RESERVE | MEM_RESET)))
        return STATUS_INVALID_PARAMETER;

    if ((AllocationType & MEM_RESET) && AllocationType != MEM_RESET)
        return STATUS_INVALID_PARAMETER;

    if (AllocationType & MEM_PHYSICAL)
    {
        if (AllocationType != (MEM_RESERVE | MEM_PHYSICAL))
            return STATUS_INVALID_PARAMETER;
        if (Protect != PAGE_READWRITE)
            return STATUS_INVALID_PAGE_PROTECTION;
    }

    if (AllocationType & MEM_LARGE_PAGES)
    {
        if ((AllocationType & (MEM_RESERVE | MEM_COMMIT)) != (MEM_RESERVE | MEM_COMMIT) ||
            (AllocationType & MEM_WRITE_WATCH))
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (!SeSinglePrivilegeCheck(SeLockMemoryPrivilege, ExGetPreviousMode()))
            return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (Protect & (PAGE_GRAPHICS_NOACCESS | PAGE_GRAPHICS_READONLY | PAGE_GRAPHICS_READWRITE | PAGE_GRAPHICS_EXECUTE |
                   PAGE_GRAPHICS_EXECUTE_READ | PAGE_GRAPHICS_EXECUTE_READWRITE | PAGE_GRAPHICS_COHERENT |
                   PAGE_GRAPHICS_NOCACHE))
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (!MiAllocationProtectionFromWin32(Protect, &Protection) ||
        (MI_PROT_IS_COPY(Protection) && (AllocationType & MEM_RESERVE)))
        return STATUS_INVALID_PAGE_PROTECTION;

    Status = MiCaptureRegion(UBaseAddress, URegionSize, &BaseAddress, &RegionSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((ULONG_PTR)BaseAddress > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
        return STATUS_INVALID_PARAMETER;

    if (RegionSize == 0 || (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1 - (ULONG_PTR)BaseAddress < RegionSize)
        return STATUS_INVALID_PARAMETER;

    if (BaseAddress != NULL && (LowestAddress | HighestEndingAddress | Alignment) != 0)
        return STATUS_INVALID_PARAMETER;

    if (BaseAddress != NULL && (ULONG_PTR)BaseAddress < (ULONG_PTR)MM_LOWEST_USER_ADDRESS)
        return STATUS_CONFLICTING_ADDRESSES;

    if (BaseAddress != NULL)
        Highest = (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;
    else if (Highest + 1 < PAGE_SIZE)
        return STATUS_INVALID_PARAMETER;

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

    if (AllocationType & MEM_RESET)
    {
        MI_MEMORY_INFORMATION Info;
        ULONG64 Va = (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress);
        ULONG64 End = ((ULONG64)(ULONG_PTR)BaseAddress + RegionSize + PAGE_SIZE - 1) & ~((ULONG64)PAGE_SIZE - 1);
        ULONG64 AllocationBase = 0;

        Status = STATUS_SUCCESS;
        while (Va < End)
        {
            Status = MiQueryVirtualMemory(MiSpaceOfProcess(Target.Process), Va, &Info);
            if (!NT_SUCCESS(Status) || Info.State == MI_MEM_FREE ||
                (AllocationBase != 0 && Info.AllocationBase != AllocationBase) ||
                Info.RegionSize == 0)
            {
                Status = STATUS_NOT_MAPPED_VIEW;
                break;
            }
            AllocationBase = Info.AllocationBase;
            Va = Info.BaseAddress + Info.RegionSize;
        }
        MiReleaseTargetProcess(&Target);
        return Status;
    }

    if (EcCode && (!MiSystem.Arch->SupportsEcCode ||
                   (Target.Process->Machine != IMAGE_FILE_MACHINE_AMD64 &&
                    Target.Process->Machine != IMAGE_FILE_MACHINE_ARM64EC)))
    {
        MiReleaseTargetProcess(&Target);
        return STATUS_INVALID_PARAMETER;
    }

    DenyDynamicCode = (BOOLEAN)(ExGetPreviousMode() != KernelMode && MiDynamicCodeBlocked(Target.Process));

    if (AllocationType & MEM_COMMIT)
        Type |= MI_MEM_COMMIT;
    if (AllocationType & MEM_RESERVE)
        Type |= MI_MEM_RESERVE;
    if (AllocationType & MEM_TOP_DOWN)
        Type |= MI_MEM_TOP_DOWN;
    if (AllocationType & MEM_LARGE_PAGES)
        Type |= MI_MEM_LARGE_PAGES;
    if (AllocationType & MEM_PHYSICAL)
        Type |= MI_MEM_PHYSICAL;
    if (AllocationType & MEM_ROTATE)
        Type |= MI_MEM_ROTATE;
    if (AllocationType & MEM_RESERVE_PLACEHOLDER)
        Type |= MI_MEM_RESERVE_PLACEHOLDER;
    if (AllocationType & MEM_REPLACE_PLACEHOLDER)
        Type |= MI_MEM_REPLACE_PLACEHOLDER;
    if (AllocationType & MEM_WRITE_WATCH)
        Type |= MI_MEM_WRITE_WATCH;

    do
    {
        Base = (ULONG64)(ULONG_PTR)BaseAddress;
        Size = RegionSize;

        if (Base == 0)
            Type |= MI_MEM_RESERVE;

        Status = MiAllocateVirtualMemoryBounded(MiSpaceOfProcess(Target.Process), &Base, &Size, Type, Protection,
                                                LowestAddress, Highest, Alignment, DenyDynamicCode);
    } while ((Type & MI_MEM_COMMIT) && NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    if (NT_SUCCESS(Status))
    {
        if (EcCode && (Type & MI_MEM_RESERVE))
        {
            PMI_ADDRESS_SPACE Space = MiSpaceOfProcess(Target.Process);
            PMI_VAD Vad;

            MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
            Vad = MiVadLocate(Space, Base);
            if (Vad != NULL && Vad->Node.StartingVpn == (Base >> PAGE_SHIFT))
                Vad->EcCode = TRUE;
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        }

        if ((Type & MI_MEM_COMMIT) && (Protect & PAGE_GUARD))
            MiShrinkUserStackToGuard(Target.Process, Base, Size);

        if ((Type & MI_MEM_RESERVE) && !(Type & MI_MEM_REPLACE_PLACEHOLDER))
        {
            Target.Process->VirtualSize += (SIZE_T)Size;
            if (Target.Process->VirtualSize > Target.Process->PeakVirtualSize)
                Target.Process->PeakVirtualSize = Target.Process->VirtualSize;
        }

        MiSyncProcessCounters(Target.Process);
    }

    MiReleaseTargetProcess(&Target);

    if (NT_SUCCESS(Status))
        Status = MiReturnRegion(UBaseAddress, URegionSize, Base, Size);

    return Status;
}

NTSTATUS
NTAPI
NtAllocateVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *UBaseAddress,
    _In_ ULONG_PTR ZeroBits,
    _Inout_ PSIZE_T URegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG Protect)
{
    return MiAllocateVirtualMemoryNt(ProcessHandle, UBaseAddress, ZeroBits, URegionSize, AllocationType, Protect,
                                     0, 0, 0, FALSE, FALSE);
}

static
NTSTATUS
MiCaptureAddressRequirements(
    _In_reads_(Count) PMEM_EXTENDED_PARAMETER Parameters,
    _In_ ULONG Count,
    _Out_ PULONG64 LowestAddress,
    _Out_ PULONG64 HighestEndingAddress,
    _Out_ PULONG64 Alignment,
    _Out_ PBOOLEAN EcCode)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Present = 0;
    ULONG Index;

    *LowestAddress = 0;
    *HighestEndingAddress = 0;
    *Alignment = 0;
    *EcCode = FALSE;

    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForRead(Parameters, Count * sizeof(MEM_EXTENDED_PARAMETER), sizeof(ULONG64));

        for (Index = 0; Index < Count; Index++)
        {
            MEM_EXTENDED_PARAMETER Parameter = Parameters[Index];
            PMEM_ADDRESS_REQUIREMENTS Requirements;
            MEM_ADDRESS_REQUIREMENTS Captured;

            if (Parameter.Reserved != 0 || Parameter.Type == MemExtendedParameterInvalidType ||
                Parameter.Type >= MemExtendedParameterMax || (Present & (1u << Parameter.Type)) != 0)
            {
                _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
            }
            Present |= 1u << Parameter.Type;

            if (Parameter.Type == MemExtendedParameterNumaNode)
                continue;

            if (Parameter.Type == MemExtendedParameterAttributeFlags)
            {
                if ((Parameter.ULong64 & ~(ULONG64)MEM_EXTENDED_PARAMETER_EC_CODE) != 0)
                    _SEH2_YIELD(return STATUS_NOT_SUPPORTED);
                *EcCode = (BOOLEAN)((Parameter.ULong64 & MEM_EXTENDED_PARAMETER_EC_CODE) != 0);
                continue;
            }

            if (Parameter.Type != MemExtendedParameterAddressRequirements)
                _SEH2_YIELD(return STATUS_NOT_SUPPORTED);

            Requirements = Parameter.Pointer;
            if (Requirements == NULL)
                _SEH2_YIELD(return STATUS_INVALID_PARAMETER);

            if (ExGetPreviousMode() != KernelMode)
                ProbeForRead(Requirements, sizeof(*Requirements), sizeof(PVOID));

            Captured = *Requirements;
            *LowestAddress = (ULONG64)(ULONG_PTR)Captured.LowestStartingAddress;
            *HighestEndingAddress = (ULONG64)(ULONG_PTR)Captured.HighestEndingAddress;
            *Alignment = (ULONG64)Captured.Alignment;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        return Status;

    if (*Alignment != 0 &&
        ((*Alignment & (*Alignment - 1)) != 0 || *Alignment < MI_ALLOCATION_GRANULARITY))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((*LowestAddress & (MI_ALLOCATION_GRANULARITY - 1)) != 0)
        return STATUS_INVALID_PARAMETER;

    if (*HighestEndingAddress != 0)
    {
        if (((*HighestEndingAddress + 1) & (PAGE_SIZE - 1)) != 0 ||
            *HighestEndingAddress > (ULONG64)(ULONG_PTR)MmHighestUserAddress ||
            *HighestEndingAddress < *LowestAddress)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (*HighestEndingAddress > (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
            *HighestEndingAddress = (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;
    }

    if (*LowestAddress > (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtAllocateVirtualMemoryEx(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG PageProtection,
    _In_reads_opt_(ExtendedParameterCount) PMEM_EXTENDED_PARAMETER ExtendedParameters,
    _In_ ULONG ExtendedParameterCount)
{
    ULONG64 LowestAddress = 0;
    ULONG64 HighestEndingAddress = 0;
    ULONG64 Alignment = 0;
    BOOLEAN EcCode = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    if (ExtendedParameterCount != 0)
    {
        if (ExtendedParameters == NULL || ExtendedParameterCount > MemExtendedParameterMax)
            return STATUS_INVALID_PARAMETER;

        Status = MiCaptureAddressRequirements(ExtendedParameters, ExtendedParameterCount, &LowestAddress,
                                              &HighestEndingAddress, &Alignment, &EcCode);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    else if (ExtendedParameters != NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return MiAllocateVirtualMemoryNt(ProcessHandle, BaseAddress, 0, RegionSize, AllocationType, PageProtection,
                                     LowestAddress, HighestEndingAddress, Alignment, EcCode, TRUE);
}

NTSTATUS
NTAPI
NtFreeVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *UBaseAddress,
    _Inout_ PSIZE_T URegionSize,
    _In_ ULONG FreeType)
{
    MI_PROCESS_REFERENCE Target;
    ULONG64 Base, Size, VadStart;
    PVOID BaseAddress;
    SIZE_T RegionSize;
    NTSTATUS Status;

    PAGED_CODE();

    if (FreeType != MEM_DECOMMIT && FreeType != MEM_RELEASE &&
        FreeType != (MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) && FreeType != (MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS))
    {
        return STATUS_INVALID_PARAMETER_4;
    }

    Status = MiCaptureRegion(UBaseAddress, URegionSize, &BaseAddress, &RegionSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((FreeType & (MEM_PRESERVE_PLACEHOLDER | MEM_COALESCE_PLACEHOLDERS)) && RegionSize == 0)
        return STATUS_INVALID_PARAMETER_3;

    if ((ULONG_PTR)BaseAddress > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
        return STATUS_INVALID_PARAMETER;

    if ((ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1 - (ULONG_PTR)BaseAddress < RegionSize)
        return STATUS_INVALID_PARAMETER;

    if ((ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress) == MI_SHARED_USER_DATA_VA)
        return STATUS_INVALID_PAGE_PROTECTION;

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

    if (MiProcessHasSecureRanges(Target.Process))
    {
        Base = (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress);
        if (RegionSize == 0)
            MiVadRangeForAddress(MiSpaceOfProcess(Target.Process), Base, &VadStart, &Size);
        else
            Size = ((ULONG64)(ULONG_PTR)BaseAddress + RegionSize + PAGE_SIZE - 1) & ~((ULONG64)PAGE_SIZE - 1);

        if (MiSecureRangeConflict(Target.Process, Base, Size, TRUE, 0))
        {
            MiReleaseTargetProcess(&Target);
            return (FreeType == MEM_RELEASE) ? STATUS_UNABLE_TO_FREE_VM : STATUS_UNABLE_TO_DECOMMIT_VM;
        }
    }

    Base = (ULONG64)(ULONG_PTR)BaseAddress;
    Size = RegionSize;
    Status = MiFreeVirtualMemory(MiSpaceOfProcess(Target.Process), &Base, &Size,
                                 (FreeType == MEM_DECOMMIT) ? MI_MEM_DECOMMIT :
                                 (FreeType & MEM_PRESERVE_PLACEHOLDER) ? (MI_MEM_RELEASE | MI_MEM_PRESERVE_PLACEHOLDER) :
                                 (FreeType & MEM_COALESCE_PLACEHOLDERS) ? (MI_MEM_RELEASE | MI_MEM_COALESCE_PLACEHOLDERS) :
                                 MI_MEM_RELEASE);

    if (NT_SUCCESS(Status))
    {
        if (FreeType == MEM_RELEASE)
            Target.Process->VirtualSize -= min(Target.Process->VirtualSize, (SIZE_T)Size);

        MiSyncProcessCounters(Target.Process);
    }

    MiReleaseTargetProcess(&Target);

    if (NT_SUCCESS(Status))
        Status = MiReturnRegion(UBaseAddress, URegionSize, Base, Size);

    return Status;
}

NTSTATUS
NTAPI
MiProtectVirtualMemoryNt(
    _In_ PEPROCESS Process,
    _Inout_ PVOID *BaseAddress,
    _Inout_ PSIZE_T NumberOfBytesToProtect,
    _In_ ULONG NewAccessProtection,
    _Out_opt_ PULONG OldAccessProtection,
    _In_ BOOLEAN DenyDynamicCode)
{
    ULONG64 Base = (ULONG64)(ULONG_PTR)*BaseAddress;
    ULONG64 Size = *NumberOfBytesToProtect;
    ULONG Protection, Old;
    ULONG Attempts = 0;
    NTSTATUS Status;

    if (!MiProtectionFromWin32(NewAccessProtection, &Protection))
        return STATUS_INVALID_PAGE_PROTECTION;

    do
    {
        Status = MiProtectVirtualMemoryEx(MiSpaceOfProcess(Process), &Base, &Size, Protection, &Old,
                                          DenyDynamicCode);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    if (NT_SUCCESS(Status))
    {
        if (NewAccessProtection & PAGE_GUARD)
            MiShrinkUserStackToGuard(Process, Base, Size);

        *BaseAddress = (PVOID)(ULONG_PTR)Base;
        *NumberOfBytesToProtect = (SIZE_T)Size;
    }

    if ((NT_SUCCESS(Status) || Status == STATUS_SECTION_PROTECTION) && OldAccessProtection != NULL)
        *OldAccessProtection = MiProtectionToWin32(Old);

    return Status;
}

NTSTATUS
NTAPI
NtProtectVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *UnsafeBaseAddress,
    _Inout_ SIZE_T *UnsafeNumberOfBytesToProtect,
    _In_ ULONG NewAccessProtection,
    _Out_ PULONG UnsafeOldAccessProtection)
{
    MI_PROCESS_REFERENCE Target;
    ULONG OldProtection = 0;
    ULONG Protection;
    PVOID BaseAddress;
    SIZE_T RegionSize;
    NTSTATUS Status;

    PAGED_CODE();

    if (!MiProtectionFromWin32(NewAccessProtection, &Protection))
        return STATUS_INVALID_PAGE_PROTECTION;

    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForWriteUlong(UnsafeOldAccessProtection);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = MiCaptureRegion(UnsafeBaseAddress, UnsafeNumberOfBytesToProtect, &BaseAddress, &RegionSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((ULONG_PTR)BaseAddress > (ULONG_PTR)MmHighestUserAddress)
        return STATUS_INVALID_PARAMETER_2;

    if (RegionSize == 0 || (ULONG_PTR)MmHighestUserAddress + 1 - (ULONG_PTR)BaseAddress < RegionSize)
        return STATUS_INVALID_PARAMETER_3;

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

    if (MiSecureRangeConflict(Target.Process, (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress),
                              ((ULONG64)(ULONG_PTR)BaseAddress + RegionSize + PAGE_SIZE - 1) &
                                  ~((ULONG64)PAGE_SIZE - 1),
                              FALSE, Protection))
    {
        MiReleaseTargetProcess(&Target);
        return STATUS_INVALID_PAGE_PROTECTION;
    }

    Status = MiProtectVirtualMemoryNt(Target.Process, &BaseAddress, &RegionSize, NewAccessProtection,
                                      &OldProtection,
                                      (BOOLEAN)(ExGetPreviousMode() != KernelMode &&
                                                MiDynamicCodeBlocked(Target.Process)));
    MiReleaseTargetProcess(&Target);

    if (Status == STATUS_SECTION_PROTECTION)
    {
        _SEH2_TRY
        {
            *UnsafeOldAccessProtection = OldProtection;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }

    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        *UnsafeOldAccessProtection = OldProtection;
        *UnsafeBaseAddress = BaseAddress;
        *UnsafeNumberOfBytesToProtect = RegionSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
NTSTATUS
MiQueryBasicInformation(
    _In_ PEPROCESS Process,
    _In_ PVOID BaseAddress,
    _Out_ PMEMORY_BASIC_INFORMATION Basic)
{
    MI_MEMORY_INFORMATION Info;
    ULONG64 Lowest, Query;
    NTSTATUS Status;

    RtlZeroMemory(Basic, sizeof(*Basic));

    if ((ULONG_PTR)BaseAddress > (ULONG_PTR)MmHighestUserAddress)
        return STATUS_INVALID_PARAMETER;

    Lowest = MiSpaceOfProcess(Process)->LowestVa;
    Query = ((ULONG64)(ULONG_PTR)BaseAddress < Lowest) ? Lowest : (ULONG64)(ULONG_PTR)BaseAddress;
    Status = MiQueryVirtualMemory(MiSpaceOfProcess(Process), Query, &Info);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Query != (ULONG64)(ULONG_PTR)BaseAddress && Info.State != MI_MEM_FREE)
    {
        Info.BaseAddress = Lowest;
        Info.RegionSize = 0;
        Info.State = MI_MEM_FREE;
    }

    if (Info.State == MI_MEM_FREE && Query != (ULONG64)(ULONG_PTR)BaseAddress)
    {
        Info.RegionSize += Info.BaseAddress - (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress);
        Info.BaseAddress = (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress);
    }

    Basic->BaseAddress = (PVOID)(ULONG_PTR)Info.BaseAddress;
    Basic->RegionSize = (SIZE_T)Info.RegionSize;

    if (Info.State == MI_MEM_FREE)
    {
        if (Info.BaseAddress + Info.RegionSize > (ULONG64)(ULONG_PTR)MmHighestUserAddress + 1)
            Basic->RegionSize = (SIZE_T)((ULONG64)(ULONG_PTR)MmHighestUserAddress + 1 - Info.BaseAddress);

        Basic->State = MEM_FREE;
        Basic->Protect = PAGE_NOACCESS;
        return STATUS_SUCCESS;
    }

    Basic->AllocationBase = (PVOID)(ULONG_PTR)Info.AllocationBase;
    Basic->AllocationProtect = MiProtectionToWin32(Info.AllocationProtect);
    Basic->State = (Info.State == MI_MEM_COMMIT) ? MEM_COMMIT : MEM_RESERVE;
    Basic->Protect = (Info.State == MI_MEM_COMMIT) ? MiProtectionToWin32(Info.Protect) : 0;
    Basic->Type = (Info.Type == MI_MEM_PRIVATE || Info.AllocationBase == MI_SHARED_USER_DATA_VA)
                      ? MEM_PRIVATE : ((Info.Type == MI_MEM_IMAGE) ? MEM_IMAGE : MEM_MAPPED);
    return STATUS_SUCCESS;
}

static
NTSTATUS
MiQueryRegionInformation(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _Out_ PVOID MemoryInformation,
    _In_ SIZE_T MemoryInformationLength,
    _Out_opt_ PSIZE_T ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    MEMORY_REGION_INFORMATION Region;
    MI_PROCESS_REFERENCE Target;
    MI_MEMORY_INFORMATION Info;
    PMI_ADDRESS_SPACE Space;
    ULONG64 AllocationBase;
    ULONG64 Query;
    SIZE_T Length;
    NTSTATUS Status;

    if (MemoryInformationLength < FIELD_OFFSET(MEMORY_REGION_INFORMATION, CommitSize))
        return STATUS_INFO_LENGTH_MISMATCH;

    Length = min(MemoryInformationLength, sizeof(Region));

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWrite(MemoryInformation, Length, sizeof(ULONG_PTR));
            if (ReturnLength != NULL)
                ProbeForWriteSize_t(ReturnLength);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_QUERY_INFORMATION, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Region, sizeof(Region));
    Space = MiSpaceOfProcess(Target.Process);
    Status = MiQueryVirtualMemory(Space, (ULONG64)(ULONG_PTR)BaseAddress, &Info);
    if (NT_SUCCESS(Status) && Info.State == MI_MEM_FREE)
        Status = STATUS_INVALID_ADDRESS;

    if (NT_SUCCESS(Status))
    {
        AllocationBase = Info.AllocationBase;
        Region.AllocationBase = (PVOID)(ULONG_PTR)AllocationBase;
        Region.AllocationProtect = MiProtectionToWin32(Info.AllocationProtect);

        Query = AllocationBase;
        while (NT_SUCCESS(MiQueryVirtualMemory(Space, Query, &Info)) &&
               Info.State != MI_MEM_FREE &&
               Info.AllocationBase == AllocationBase &&
               Info.RegionSize != 0)
        {
            Region.RegionSize += (SIZE_T)Info.RegionSize;
            if (Info.State == MI_MEM_COMMIT)
                Region.CommitSize += (SIZE_T)Info.RegionSize;
            Query = Info.BaseAddress + Info.RegionSize;
        }
    }

    MiReleaseTargetProcess(&Target);

    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        RtlCopyMemory(MemoryInformation, &Region, Length);
        if (ReturnLength != NULL)
            *ReturnLength = sizeof(Region);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

#define MI_WS_EX_CHUNK            64
#define MI_WS_EX_VALID            0x00000001
#define MI_WS_EX_SHARE_SHIFT      1
#define MI_WS_EX_PROTECTION_SHIFT 4
#define MI_WS_EX_PROTECTION_MASK  0x7FF
#define MI_WS_EX_SHARED           0x00008000
#define MI_WS_EX_LOCKED           0x00400000
#define MI_WS_EX_LARGE_PAGE       0x00800000

typedef struct _MI_WS_EX_ENTRY
{
    PVOID VirtualAddress;
    ULONG_PTR Attributes;
} MI_WS_EX_ENTRY, *PMI_WS_EX_ENTRY;

static
ULONG_PTR
MiWorkingSetExAttributes(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ PVOID VirtualAddress)
{
    MI_WORKING_SET_EX_INFORMATION Ws;
    ULONG_PTR Attributes;

    if ((ULONG_PTR)VirtualAddress > (ULONG_PTR)MmHighestUserAddress ||
        !NT_SUCCESS(MiQueryWorkingSetEx(Space, (ULONG64)(ULONG_PTR)VirtualAddress, &Ws)))
        return 0;

    if (!Ws.Valid)
        return Ws.Shared ? MI_WS_EX_SHARED : 0;

    Attributes = MI_WS_EX_VALID | ((ULONG_PTR)Ws.ShareCount << MI_WS_EX_SHARE_SHIFT) |
                 (((ULONG_PTR)MiProtectionToWin32(Ws.Protection) & MI_WS_EX_PROTECTION_MASK)
                  << MI_WS_EX_PROTECTION_SHIFT);
    if (Ws.Shared)
        Attributes |= MI_WS_EX_SHARED;
    if (Ws.Locked)
        Attributes |= MI_WS_EX_LOCKED;
    if (Ws.LargePage)
        Attributes |= MI_WS_EX_LARGE_PAGE;
    return Attributes;
}

static
NTSTATUS
MiQueryWorkingSetExList(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID MemoryInformation,
    _In_ SIZE_T MemoryInformationLength,
    _Out_opt_ PSIZE_T ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PMI_WS_EX_ENTRY User = MemoryInformation;
    MI_WS_EX_ENTRY Entries[MI_WS_EX_CHUNK];
    MI_PROCESS_REFERENCE Target;
    SIZE_T Count, Index, Batch, Entry;
    NTSTATUS Status = STATUS_SUCCESS;

    if (MemoryInformationLength < sizeof(MI_WS_EX_ENTRY))
        return STATUS_INFO_LENGTH_MISMATCH;

    Count = MemoryInformationLength / sizeof(MI_WS_EX_ENTRY);

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWrite(MemoryInformation, Count * sizeof(MI_WS_EX_ENTRY), sizeof(ULONG_PTR));
            if (ReturnLength != NULL)
                ProbeForWriteSize_t(ReturnLength);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    for (Index = 0; Index < Count; Index += Batch)
    {
        Batch = min(Count - Index, (SIZE_T)MI_WS_EX_CHUNK);

        _SEH2_TRY
        {
            RtlCopyMemory(Entries, &User[Index], Batch * sizeof(MI_WS_EX_ENTRY));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;

        Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_QUERY_INFORMATION, &Target);
        if (!NT_SUCCESS(Status))
            return Status;

        for (Entry = 0; Entry < Batch; Entry++)
            Entries[Entry].Attributes = MiWorkingSetExAttributes(MiSpaceOfProcess(Target.Process),
                                                                 Entries[Entry].VirtualAddress);

        MiReleaseTargetProcess(&Target);

        _SEH2_TRY
        {
            for (Entry = 0; Entry < Batch; Entry++)
                User[Index + Entry].Attributes = Entries[Entry].Attributes;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    _SEH2_TRY
    {
        if (ReturnLength != NULL)
            *ReturnLength = Count * sizeof(MI_WS_EX_ENTRY);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtQueryVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _In_ MEMORY_INFORMATION_CLASS MemoryInformationClass,
    _Out_ PVOID MemoryInformation,
    _In_ SIZE_T MemoryInformationLength,
    _Out_opt_ PSIZE_T ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    MI_PROCESS_REFERENCE Target;
    MEMORY_BASIC_INFORMATION Basic;
    NTSTATUS Status;

    PAGED_CODE();

    if ((ULONG_PTR)BaseAddress > (ULONG_PTR)MmHighestUserAddress)
        return STATUS_INVALID_PARAMETER;

    if (MemoryInformationClass == MemorySectionName)
    {
        return MiQuerySectionName(ProcessHandle, BaseAddress, MemoryInformation, MemoryInformationLength,
                                  ReturnLength);
    }

    if (MemoryInformationClass == MemoryRegionInformation)
    {
        return MiQueryRegionInformation(ProcessHandle, BaseAddress, MemoryInformation, MemoryInformationLength,
                                        ReturnLength);
    }

    if (MemoryInformationClass == MemoryWorkingSetExList)
    {
        return MiQueryWorkingSetExList(ProcessHandle, MemoryInformation, MemoryInformationLength,
                                       ReturnLength);
    }

    if (MemoryInformationClass != MemoryBasicInformation)
        return STATUS_INVALID_INFO_CLASS;

    if (MemoryInformationLength < sizeof(MEMORY_BASIC_INFORMATION))
        return STATUS_INFO_LENGTH_MISMATCH;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWrite(MemoryInformation, MemoryInformationLength, sizeof(ULONG_PTR));
            if (ReturnLength != NULL)
                ProbeForWriteSize_t(ReturnLength);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_QUERY_INFORMATION, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = MiQueryBasicInformation(Target.Process, BaseAddress, &Basic);
    MiReleaseTargetProcess(&Target);

    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        *(PMEMORY_BASIC_INFORMATION)MemoryInformation = Basic;
        if (ReturnLength != NULL)
            *ReturnLength = sizeof(MEMORY_BASIC_INFORMATION);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static
NTSTATUS
MiCopyChunk(
    _In_ PEPROCESS Process,
    _In_ PVOID ProcessAddress,
    _Inout_ PVOID Bounce,
    _In_ SIZE_T Length,
    _In_ BOOLEAN FromProcess,
    _In_ KPROCESSOR_MODE Mode,
    _Out_ PSIZE_T Copied)
{
    NTSTATUS Status = STATUS_SUCCESS;
    KAPC_STATE ApcState;
    volatile SIZE_T Done = 0;

    *Copied = 0;
    KeStackAttachProcess(&Process->Pcb, &ApcState);

    _SEH2_TRY
    {
        if (FromProcess)
        {
            if (Mode != KernelMode)
                ProbeForRead(ProcessAddress, Length, sizeof(CHAR));

            RtlCopyMemory(Bounce, ProcessAddress, Length);
            Done = Length;
        }
        else
        {
            if (Mode != KernelMode &&
                ((ULONG_PTR)ProcessAddress + Length < (ULONG_PTR)ProcessAddress ||
                 (ULONG_PTR)ProcessAddress + Length > MmUserProbeAddress))
            {
                ExRaiseStatus(STATUS_ACCESS_VIOLATION);
            }

            while (Done < Length)
            {
                SIZE_T Piece = PAGE_SIZE - (((ULONG_PTR)ProcessAddress + Done) & (PAGE_SIZE - 1));

                if (Piece > Length - Done)
                    Piece = Length - Done;
                RtlCopyMemory((PUCHAR)ProcessAddress + Done, (PUCHAR)Bounce + Done, Piece);
                Done += Piece;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = FromProcess ? STATUS_PARTIAL_COPY : _SEH2_GetExceptionCode();
        if (Status == STATUS_ACCESS_VIOLATION || Status == STATUS_GUARD_PAGE_VIOLATION)
            Status = STATUS_PARTIAL_COPY;
    }
    _SEH2_END;

    KeUnstackDetachProcess(&ApcState);
    *Copied = FromProcess ? (NT_SUCCESS(Status) ? Length : 0) : Done;
    return Status;
}

NTSTATUS
NTAPI
MmCopyVirtualMemory(
    _In_ PEPROCESS SourceProcess,
    _In_ PVOID SourceAddress,
    _In_ PEPROCESS TargetProcess,
    _Out_ PVOID TargetAddress,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PSIZE_T ReturnSize)
{
    PEPROCESS Remote = (SourceProcess == PsGetCurrentProcess()) ? TargetProcess : SourceProcess;
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T Chunk = min(BufferSize, (SIZE_T)MI_COPY_CHUNK);
    SIZE_T Done = 0;
    PVOID Bounce;

    *ReturnSize = 0;

    if (BufferSize == 0)
        return STATUS_SUCCESS;

    if (!ExAcquireRundownProtection(&Remote->RundownProtect))
        return STATUS_PROCESS_IS_TERMINATING;

    Bounce = ExAllocatePoolWithTag(NonPagedPool, Chunk, 'wRmM');
    if (Bounce == NULL)
    {
        ExReleaseRundownProtection(&Remote->RundownProtect);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    while (Done < BufferSize && NT_SUCCESS(Status))
    {
        SIZE_T Length = min(Chunk, BufferSize - Done);
        SIZE_T Copied;

        Status = MiCopyChunk(SourceProcess, (PUCHAR)SourceAddress + Done, Bounce, Length, TRUE, PreviousMode, &Copied);
        if (NT_SUCCESS(Status))
        {
            Status = MiCopyChunk(TargetProcess, (PUCHAR)TargetAddress + Done, Bounce, Length, FALSE, PreviousMode, &Copied);
            Done += Copied;
        }
    }

    ExFreePoolWithTag(Bounce, 'wRmM');
    ExReleaseRundownProtection(&Remote->RundownProtect);

    *ReturnSize = Done;
    return Status;
}

static
NTSTATUS
MiReadWriteVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _In_ PVOID Buffer,
    _In_ SIZE_T NumberOfBytes,
    _Out_opt_ PSIZE_T NumberOfBytesDone,
    _In_ BOOLEAN Write)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    SIZE_T Done = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        if ((ULONG_PTR)BaseAddress + NumberOfBytes < (ULONG_PTR)BaseAddress ||
            (ULONG_PTR)Buffer + NumberOfBytes < (ULONG_PTR)Buffer ||
            (ULONG_PTR)BaseAddress + NumberOfBytes > MmUserProbeAddress ||
            (ULONG_PTR)Buffer + NumberOfBytes > MmUserProbeAddress)
        {
            return STATUS_ACCESS_VIOLATION;
        }

        _SEH2_TRY
        {
            if (NumberOfBytesDone != NULL)
                ProbeForWriteSize_t(NumberOfBytesDone);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    if (NumberOfBytes == 0)
    {
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = ObReferenceObjectByHandle(ProcessHandle, Write ? PROCESS_VM_WRITE : PROCESS_VM_READ, PsProcessType,
                                           PreviousMode, (PVOID *)&Process, NULL);
        if (!NT_SUCCESS(Status))
            return Status;

        if (Write)
        {
            Status = MmCopyVirtualMemory(PsGetCurrentProcess(), Buffer, Process, BaseAddress, NumberOfBytes,
                                         PreviousMode, &Done);
        }
        else
        {
            Status = MmCopyVirtualMemory(Process, BaseAddress, PsGetCurrentProcess(), Buffer, NumberOfBytes,
                                         PreviousMode, &Done);
        }

        ObDereferenceObject(Process);
    }

    if (NumberOfBytesDone != NULL)
    {
        _SEH2_TRY
        {
            *NumberOfBytesDone = Done;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }

    return Status;
}

NTSTATUS
NTAPI
NtReadVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _Out_ PVOID Buffer,
    _In_ SIZE_T NumberOfBytesToRead,
    _Out_opt_ PSIZE_T NumberOfBytesRead)
{
    return MiReadWriteVirtualMemory(ProcessHandle, BaseAddress, Buffer, NumberOfBytesToRead, NumberOfBytesRead,
                                    FALSE);
}

NTSTATUS
NTAPI
NtWriteVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _In_ PVOID Buffer,
    _In_ SIZE_T NumberOfBytesToWrite,
    _Out_opt_ PSIZE_T NumberOfBytesWritten)
{
    return MiReadWriteVirtualMemory(ProcessHandle, BaseAddress, Buffer, NumberOfBytesToWrite,
                                    NumberOfBytesWritten, TRUE);
}

NTSTATUS
NTAPI
NtFlushInstructionCache(
    _In_ HANDLE ProcessHandle,
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    MI_PROCESS_REFERENCE Target;
    NTSTATUS Status;

    PAGED_CODE();

    if (BaseAddress != NULL)
    {
        if (FlushSize == 0)
            return STATUS_SUCCESS;

        if (ExGetPreviousMode() != KernelMode &&
            (BaseAddress > MmHighestUserAddress ||
             (ULONG_PTR)MmHighestUserAddress - (ULONG_PTR)BaseAddress < FlushSize - 1))
        {
            return STATUS_ACCESS_VIOLATION;
        }
    }

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_WRITE, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        KeSweepICache(BaseAddress, FlushSize);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    MiReleaseTargetProcess(&Target);
    return Status;
}

NTSTATUS
NTAPI
MmFlushVirtualMemory(
    _In_ PEPROCESS Process,
    _Inout_ PVOID *BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    ULONG64 Base = (ULONG64)(ULONG_PTR)*BaseAddress;
    ULONG64 Size = *RegionSize;
    NTSTATUS Status = MiFlushVirtualMemory(MiSpaceOfProcess(Process), &Base, &Size, TRUE);

    if (NT_SUCCESS(Status))
    {
        *BaseAddress = (PVOID)(ULONG_PTR)Base;
        *RegionSize = (SIZE_T)Size;
    }

    IoStatusBlock->Status = Status;
    IoStatusBlock->Information = 0;
    return Status;
}

NTSTATUS
NTAPI
NtFlushVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *UBaseAddress,
    _Inout_ PSIZE_T URegionSize,
    _Out_ PIO_STATUS_BLOCK UIoStatusBlock)
{
    MI_PROCESS_REFERENCE Target;
    IO_STATUS_BLOCK IoStatus;
    PVOID BaseAddress;
    SIZE_T RegionSize;
    NTSTATUS Status;

    PAGED_CODE();

    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForWriteIoStatusBlock(UIoStatusBlock);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = MiCaptureRegion(UBaseAddress, URegionSize, &BaseAddress, &RegionSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((ULONG_PTR)BaseAddress > (ULONG_PTR)MmHighestUserAddress)
        return STATUS_INVALID_PARAMETER_2;

    if ((ULONG_PTR)MmHighestUserAddress + 1 - (ULONG_PTR)BaseAddress < RegionSize)
        return STATUS_INVALID_PARAMETER_2;

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = MmFlushVirtualMemory(Target.Process, &BaseAddress, &RegionSize, &IoStatus);
    MiReleaseTargetProcess(&Target);

    _SEH2_TRY
    {
        *UBaseAddress = PAGE_ALIGN(BaseAddress);
        *URegionSize = RegionSize;
        *UIoStatusBlock = IoStatus;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;

    return Status;
}

static
NTSTATUS
MiLockUnlockVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *UBaseAddress,
    _Inout_ PSIZE_T URegionSize,
    _In_ ULONG MapType,
    _In_ BOOLEAN Lock)
{
    MI_PROCESS_REFERENCE Target;
    PVOID BaseAddress;
    SIZE_T RegionSize;
    NTSTATUS Status;
    ULONG64 Va, End;

    PAGED_CODE();

    if (MapType & ~(MAP_PROCESS | MAP_SYSTEM) || MapType == 0)
        return STATUS_INVALID_PARAMETER_4;

    Status = MiCaptureRegion(UBaseAddress, URegionSize, &BaseAddress, &RegionSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((ULONG_PTR)BaseAddress > (ULONG_PTR)MmHighestUserAddress)
        return STATUS_INVALID_PARAMETER_2;

    if (RegionSize == 0 || (ULONG_PTR)MmHighestUserAddress + 1 - (ULONG_PTR)BaseAddress < RegionSize)
        return STATUS_INVALID_PARAMETER_3;

    if ((MapType & MAP_SYSTEM) && !SeSinglePrivilegeCheck(SeLockMemoryPrivilege, ExGetPreviousMode()))
        return STATUS_PRIVILEGE_NOT_HELD;

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

    Va = (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress);
    End = ((ULONG64)(ULONG_PTR)BaseAddress + RegionSize + PAGE_SIZE - 1) & ~((ULONG64)PAGE_SIZE - 1);

    for (; Lock && Va < End && NT_SUCCESS(Status); Va += PAGE_SIZE)
    {
        MI_MEMORY_INFORMATION Info;

        Status = MiQueryVirtualMemory(MiSpaceOfProcess(Target.Process), Va, &Info);
        if (NT_SUCCESS(Status) && (Info.State != MI_MEM_COMMIT || !MI_PROT_IS_ACCESSIBLE(Info.Protect)))
            Status = STATUS_ACCESS_VIOLATION;

        if (NT_SUCCESS(Status))
        {
            ULONG Attempts = 0;

            do
            {
                Status = MiFault(MiSpaceOfProcess(Target.Process), Va, MiFaultRead, TRUE);
            } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

            if (Status == STATUS_GUARD_PAGE_VIOLATION)
                Status = STATUS_SUCCESS;
        }
    }

    MiReleaseTargetProcess(&Target);

    if (NT_SUCCESS(Status))
    {
        Status = MiReturnRegion(UBaseAddress, URegionSize, (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress),
                                End - (ULONG64)(ULONG_PTR)PAGE_ALIGN(BaseAddress));
    }

    return Status;
}

NTSTATUS
NTAPI
NtLockVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *BaseAddress,
    _Inout_ PSIZE_T NumberOfBytesToLock,
    _In_ ULONG MapType)
{
    return MiLockUnlockVirtualMemory(ProcessHandle, BaseAddress, NumberOfBytesToLock, MapType, TRUE);
}

NTSTATUS
NTAPI
NtUnlockVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *BaseAddress,
    _Inout_ PSIZE_T NumberOfBytesToUnlock,
    _In_ ULONG MapType)
{
    return MiLockUnlockVirtualMemory(ProcessHandle, BaseAddress, NumberOfBytesToUnlock, MapType, FALSE);
}

NTSTATUS
NTAPI
NtGetWriteWatch(
    _In_ HANDLE ProcessHandle,
    _In_ ULONG Flags,
    _In_ PVOID BaseAddress,
    _In_ SIZE_T RegionSize,
    _In_ PVOID *UserAddressArray,
    _Out_ PULONG_PTR EntriesInUserAddressArray,
    _Out_ PULONG Granularity)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    MI_PROCESS_REFERENCE Target;
    ULONG_PTR Capacity = 0;
    ULONG_PTR Index;
    ULONG64 Base = (ULONG64)(ULONG_PTR)BaseAddress;
    ULONG64 Count;
    PULONG64 Addresses;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Flags & ~WRITE_WATCH_FLAG_RESET)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWrite(EntriesInUserAddressArray, sizeof(ULONG_PTR), sizeof(ULONG_PTR));
            ProbeForWriteUlong(Granularity);
        }

        Capacity = *EntriesInUserAddressArray;
        if (Capacity > MAXULONG_PTR / sizeof(PVOID))
            _SEH2_YIELD(return STATUS_INVALID_PARAMETER);

        if (PreviousMode != KernelMode && Capacity != 0)
            ProbeForWrite(UserAddressArray, Capacity * sizeof(PVOID), sizeof(PVOID));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Capacity == 0 || RegionSize == 0 || Base > (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS ||
        (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1 - Base < RegionSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Count = (((Base + RegionSize + PAGE_SIZE - 1) & ~((ULONG64)PAGE_SIZE - 1)) -
             (Base & ~((ULONG64)PAGE_SIZE - 1))) >> PAGE_SHIFT;
    if (Count > Capacity)
        Count = Capacity;

    Addresses = ExAllocatePoolWithTag(PagedPool, (SIZE_T)Count * sizeof(ULONG64), 'wWmM');
    if (Addresses == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (NT_SUCCESS(Status))
    {
        Status = MiGetWriteWatch(MiSpaceOfProcess(Target.Process), Base, RegionSize,
                                 (BOOLEAN)((Flags & WRITE_WATCH_FLAG_RESET) != 0), Addresses, &Count);
        MiReleaseTargetProcess(&Target);
    }

    if (NT_SUCCESS(Status))
    {
        _SEH2_TRY
        {
            for (Index = 0; Index < Count; Index++)
                UserAddressArray[Index] = (PVOID)(ULONG_PTR)Addresses[Index];
            *EntriesInUserAddressArray = (ULONG_PTR)Count;
            *Granularity = PAGE_SIZE;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    ExFreePoolWithTag(Addresses, 'wWmM');
    return Status;
}

NTSTATUS
NTAPI
NtResetWriteWatch(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _In_ SIZE_T RegionSize)
{
    MI_PROCESS_REFERENCE Target;
    ULONG64 Base = (ULONG64)(ULONG_PTR)BaseAddress;
    NTSTATUS Status;

    PAGED_CODE();

    if (RegionSize == 0 || Base > (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS ||
        (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1 - Base < RegionSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = MiResetWriteWatch(MiSpaceOfProcess(Target.Process), Base, RegionSize);
    MiReleaseTargetProcess(&Target);
    return Status;
}

static NTSTATUS
MiCaptureAweCount(PULONG_PTR NumberOfPages, PULONG Count)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG_PTR Value;

    *Count = 0;
    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForWrite(NumberOfPages, sizeof(*NumberOfPages), TYPE_ALIGNMENT(ULONG_PTR));
        Value = *NumberOfPages;
        if (Value == 0 || Value > MAXULONG || Value > MAXULONG_PTR / sizeof(MI_FRAME_NUMBER))
            Status = STATUS_INVALID_PARAMETER;
        else
            *Count = (ULONG)Value;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}

static NTSTATUS
MiCaptureAweFrames(PULONG_PTR UserPfnArray, ULONG Count, PMI_FRAME_NUMBER Frames)
{
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T Bytes = (SIZE_T)Count * sizeof(*Frames);

    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForRead(UserPfnArray, Bytes, TYPE_ALIGNMENT(ULONG_PTR));
        RtlCopyMemory(Frames, UserPfnArray, Bytes);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}

NTSTATUS
NTAPI
NtAllocateUserPhysicalPages(
    _In_ HANDLE ProcessHandle,
    _Inout_ PULONG_PTR NumberOfPages,
    _Inout_ PULONG_PTR UserPfnArray)
{
    MI_PROCESS_REFERENCE Target;
    PMI_FRAME_NUMBER Frames;
    ULONG Count;
    NTSTATUS Status;

    PAGED_CODE();
    if (!SeSinglePrivilegeCheck(SeLockMemoryPrivilege, ExGetPreviousMode()))
        return STATUS_PRIVILEGE_NOT_HELD;
    Status = MiCaptureAweCount(NumberOfPages, &Count);
    if (!NT_SUCCESS(Status))
        return Status;
    Count = min(Count, MiSystem.Pfn.FrameCount);
    Frames = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Count * sizeof(*Frames), 'aAmM');
    if (Frames == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForWrite(UserPfnArray, (SIZE_T)Count * sizeof(*Frames), TYPE_ALIGNMENT(ULONG_PTR));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = MiAweAllocatePages(MiSpaceOfProcess(Target.Process), &Count, Frames);
    if (Target.Attached)
    {
        KeUnstackDetachProcess(&Target.ApcState);
        Target.Attached = FALSE;
    }
    if (NT_SUCCESS(Status))
    {
        _SEH2_TRY
        {
            RtlCopyMemory(UserPfnArray, Frames, (SIZE_T)Count * sizeof(*Frames));
            *NumberOfPages = Count;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status))
        {
            if (Target.Process != PsGetCurrentProcess())
            {
                KeStackAttachProcess(&Target.Process->Pcb, &Target.ApcState);
                Target.Attached = TRUE;
            }
            MiAweFreePages(MiSpaceOfProcess(Target.Process), &Count, Frames);
        }
    }
    MiReleaseTargetProcess(&Target);
Done:
    ExFreePoolWithTag(Frames, 'aAmM');
    return Status;
}

static NTSTATUS
MiMapUserPhysicalPagesNt(PVOID Base, PVOID *VirtualAddresses, ULONG_PTR NumberOfPages,
                         PULONG_PTR UserPfnArray, BOOLEAN Scatter)
{
    PMI_FRAME_NUMBER Frames = NULL;
    PULONG64 Addresses = NULL;
    ULONG Count, i;
    NTSTATUS Status = STATUS_SUCCESS;

    if (NumberOfPages == 0 || NumberOfPages > MAXULONG ||
        NumberOfPages > MAXULONG_PTR / sizeof(ULONG64))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Count = (ULONG)NumberOfPages;
    if (UserPfnArray != NULL)
    {
        Frames = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Count * sizeof(*Frames), 'aAmM');
        if (Frames == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        Status = MiCaptureAweFrames(UserPfnArray, Count, Frames);
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    if (Scatter)
    {
        Addresses = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Count * sizeof(*Addresses), 'aAmM');
        if (Addresses == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }
        _SEH2_TRY
        {
            if (ExGetPreviousMode() != KernelMode)
                ProbeForRead(VirtualAddresses, (SIZE_T)Count * sizeof(*VirtualAddresses), TYPE_ALIGNMENT(PVOID));
            for (i = 0; i < Count; i++)
                Addresses[i] = (ULONG64)(ULONG_PTR)VirtualAddresses[i];
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    Status = MiAweMapPages(MiSpaceOfProcess(PsGetCurrentProcess()), (ULONG64)(ULONG_PTR)Base,
                            Addresses, Count, Frames);
Done:
    if (Addresses != NULL)
        ExFreePoolWithTag(Addresses, 'aAmM');
    if (Frames != NULL)
        ExFreePoolWithTag(Frames, 'aAmM');
    return Status;
}

NTSTATUS
NTAPI
NtMapUserPhysicalPages(
    _In_ PVOID VirtualAddresses,
    _In_ ULONG_PTR NumberOfPages,
    _Inout_ PULONG_PTR UserPfnArray)
{
    PAGED_CODE();
    return MiMapUserPhysicalPagesNt(VirtualAddresses, NULL, NumberOfPages, UserPfnArray, FALSE);
}

NTSTATUS
NTAPI
NtMapUserPhysicalPagesScatter(
    _In_ PVOID *VirtualAddresses,
    _In_ ULONG_PTR NumberOfPages,
    _Inout_ PULONG_PTR UserPfnArray)
{
    PAGED_CODE();
    return MiMapUserPhysicalPagesNt(NULL, VirtualAddresses, NumberOfPages, UserPfnArray, TRUE);
}

NTSTATUS
NTAPI
NtFreeUserPhysicalPages(
    _In_ HANDLE ProcessHandle,
    _Inout_ PULONG_PTR NumberOfPages,
    _Inout_ PULONG_PTR UserPfnArray)
{
    MI_PROCESS_REFERENCE Target;
    PMI_FRAME_NUMBER Frames;
    ULONG Count;
    NTSTATUS Status, ResultStatus;

    PAGED_CODE();
    Status = MiCaptureAweCount(NumberOfPages, &Count);
    if (!NT_SUCCESS(Status))
        return Status;
    Frames = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Count * sizeof(*Frames), 'aAmM');
    if (Frames == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = MiCaptureAweFrames(UserPfnArray, Count, Frames);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = MiAweFreePages(MiSpaceOfProcess(Target.Process), &Count, Frames);
    MiReleaseTargetProcess(&Target);
    ResultStatus = Status;
    _SEH2_TRY
    {
        *NumberOfPages = Count;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ResultStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    Status = ResultStatus;
Done:
    ExFreePoolWithTag(Frames, 'aAmM');
    return Status;
}
