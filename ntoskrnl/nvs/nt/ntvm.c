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

typedef struct _MI_PROCESS_REFERENCE
{
    PEPROCESS Process;
    KAPC_STATE ApcState;
    BOOLEAN Referenced;
    BOOLEAN Attached;
} MI_PROCESS_REFERENCE, *PMI_PROCESS_REFERENCE;

static
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
        KeStackAttachProcess(&Reference->Process->Pcb, &Reference->ApcState);
        Reference->Attached = TRUE;
    }

    return STATUS_SUCCESS;
}

static
VOID
MiReleaseTargetProcess(
    _Inout_ PMI_PROCESS_REFERENCE Reference)
{
    if (Reference->Attached)
        KeUnstackDetachProcess(&Reference->ApcState);

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
    Process->Vm.PageFaultCount = (ULONG)Counters.PageFaultCount;
    Process->Vm.WorkingSetSize = (ULONG)(Counters.WorkingSetSize >> PAGE_SHIFT);
    Process->Vm.PeakWorkingSetSize = (ULONG)(Counters.PeakWorkingSetSize >> PAGE_SHIFT);
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

static
ULONG64
MiHighestAddressFromZeroBits(
    _In_ ULONG_PTR ZeroBits)
{
    ULONG64 Highest = (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;

    if (ZeroBits == 0)
        return Highest;

    if (ZeroBits < 32)
        return min(Highest, ~0ULL >> (ZeroBits + 32));

    return min(Highest, (ULONG64)ZeroBits);
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
    MI_PROCESS_REFERENCE Target;
    ULONG64 Base, Size;
    ULONG Protection;
    ULONG Type = 0;
    ULONG Attempts = 0;
    PVOID BaseAddress;
    SIZE_T RegionSize;
    NTSTATUS Status;

    PAGED_CODE();

    if (AllocationType & ~(MEM_COMMIT | MEM_RESERVE | MEM_RESET | MEM_PHYSICAL | MEM_TOP_DOWN | MEM_WRITE_WATCH |
                           MEM_LARGE_PAGES))
    {
        return STATUS_INVALID_PARAMETER_5;
    }

    if (!(AllocationType & (MEM_COMMIT | MEM_RESERVE | MEM_RESET)))
        return STATUS_INVALID_PARAMETER_5;

    if ((AllocationType & MEM_RESET) && AllocationType != MEM_RESET)
        return STATUS_INVALID_PARAMETER_5;

    if (AllocationType & MEM_PHYSICAL)
    {
        if (AllocationType != (MEM_RESERVE | MEM_PHYSICAL))
            return STATUS_INVALID_PARAMETER_5;
        if (Protect != PAGE_READWRITE)
            return STATUS_INVALID_PAGE_PROTECTION;
    }

    if (AllocationType & MEM_LARGE_PAGES)
    {
        if ((AllocationType & (MEM_RESERVE | MEM_COMMIT)) != (MEM_RESERVE | MEM_COMMIT) ||
            (AllocationType & MEM_WRITE_WATCH))
        {
            return STATUS_INVALID_PARAMETER_5;
        }
        if (!SeSinglePrivilegeCheck(SeLockMemoryPrivilege, ExGetPreviousMode()))
            return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (!MiAllocationProtectionFromWin32(Protect, &Protection) || MI_PROT_IS_COPY(Protection))
        return STATUS_INVALID_PAGE_PROTECTION;

    Status = MiCaptureRegion(UBaseAddress, URegionSize, &BaseAddress, &RegionSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((ULONG_PTR)BaseAddress > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
        return STATUS_INVALID_PARAMETER_2;

    if (RegionSize == 0 || (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1 - (ULONG_PTR)BaseAddress < RegionSize)
        return STATUS_INVALID_PARAMETER_4;

    if (AllocationType & MEM_RESET)
        return STATUS_SUCCESS;

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

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

    do
    {
        Base = (ULONG64)(ULONG_PTR)BaseAddress;
        Size = RegionSize;

        if (Base == 0)
            Type |= MI_MEM_RESERVE;

        Status = MiAllocateVirtualMemoryEx(MiSpaceOfProcess(Target.Process), &Base, &Size, Type, Protection,
                                           MiHighestAddressFromZeroBits(ZeroBits));
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    if (NT_SUCCESS(Status))
    {
        if (Type & MI_MEM_RESERVE)
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
NtAllocateVirtualMemoryEx(
    _In_ HANDLE ProcessHandle,
    _Inout_ PVOID *BaseAddress,
    _Inout_ PSIZE_T RegionSize,
    _In_ ULONG AllocationType,
    _In_ ULONG PageProtection,
    _In_reads_opt_(ExtendedParameterCount) PMEM_EXTENDED_PARAMETER ExtendedParameters,
    _In_ ULONG ExtendedParameterCount)
{
    if (ExtendedParameters != NULL || ExtendedParameterCount != 0)
        return STATUS_NOT_SUPPORTED;

    return NtAllocateVirtualMemory(ProcessHandle, BaseAddress, 0, RegionSize, AllocationType, PageProtection);
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
    ULONG64 Base, Size;
    PVOID BaseAddress;
    SIZE_T RegionSize;
    NTSTATUS Status;

    PAGED_CODE();

    if (FreeType != MEM_DECOMMIT && FreeType != MEM_RELEASE)
        return STATUS_INVALID_PARAMETER_4;

    Status = MiCaptureRegion(UBaseAddress, URegionSize, &BaseAddress, &RegionSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((ULONG_PTR)BaseAddress > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
        return STATUS_INVALID_PARAMETER_2;

    if ((ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1 - (ULONG_PTR)BaseAddress < RegionSize)
        return STATUS_INVALID_PARAMETER_3;

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

    Base = (ULONG64)(ULONG_PTR)BaseAddress;
    Size = RegionSize;
    Status = MiFreeVirtualMemory(MiSpaceOfProcess(Target.Process), &Base, &Size,
                                 (FreeType == MEM_RELEASE) ? MI_MEM_RELEASE : MI_MEM_DECOMMIT);

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
    _Out_opt_ PULONG OldAccessProtection)
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
        Status = MiProtectVirtualMemory(MiSpaceOfProcess(Process), &Base, &Size, Protection, &Old);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    if (NT_SUCCESS(Status))
    {
        *BaseAddress = (PVOID)(ULONG_PTR)Base;
        *NumberOfBytesToProtect = (SIZE_T)Size;

        if (OldAccessProtection != NULL)
            *OldAccessProtection = MiProtectionToWin32(Old);
    }

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

    Status = MiProtectVirtualMemoryNt(Target.Process, &BaseAddress, &RegionSize, NewAccessProtection,
                                      &OldProtection);
    MiReleaseTargetProcess(&Target);

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
    NTSTATUS Status;

    RtlZeroMemory(Basic, sizeof(*Basic));

    if ((ULONG_PTR)BaseAddress > (ULONG_PTR)MmHighestUserAddress)
        return STATUS_INVALID_PARAMETER;

    if ((ULONG_PTR)BaseAddress > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS)
    {
        Basic->BaseAddress = PAGE_ALIGN(BaseAddress);
        Basic->AllocationBase = (PVOID)((ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1);
        Basic->AllocationProtect = PAGE_NOACCESS;
        Basic->RegionSize = (ULONG_PTR)MmHighestUserAddress + 1 - (ULONG_PTR)Basic->BaseAddress;
        Basic->State = MEM_RESERVE;
        Basic->Protect = PAGE_NOACCESS;
        Basic->Type = MEM_PRIVATE;
        return STATUS_SUCCESS;
    }

    Status = MiQueryVirtualMemory(MiSpaceOfProcess(Process), (ULONG64)(ULONG_PTR)BaseAddress, &Info);
    if (!NT_SUCCESS(Status))
        return Status;

    Basic->BaseAddress = (PVOID)(ULONG_PTR)Info.BaseAddress;
    Basic->RegionSize = (SIZE_T)Info.RegionSize;

    if (Info.State == MI_MEM_FREE)
    {
        if (Info.BaseAddress + Info.RegionSize > (ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1)
            Basic->RegionSize = (SIZE_T)((ULONG64)(ULONG_PTR)MM_HIGHEST_VAD_ADDRESS + 1 - Info.BaseAddress);

        Basic->State = MEM_FREE;
        Basic->Protect = PAGE_NOACCESS;
        return STATUS_SUCCESS;
    }

    Basic->AllocationBase = (PVOID)(ULONG_PTR)Info.AllocationBase;
    Basic->AllocationProtect = MiProtectionToWin32(Info.AllocationProtect);
    Basic->State = (Info.State == MI_MEM_COMMIT) ? MEM_COMMIT : MEM_RESERVE;
    Basic->Protect = (Info.State == MI_MEM_COMMIT) ? MiProtectionToWin32(Info.Protect) : 0;
    Basic->Type = (Info.Type == MI_MEM_PRIVATE) ? MEM_PRIVATE : ((Info.Type == MI_MEM_IMAGE) ? MEM_IMAGE : MEM_MAPPED);
    return STATUS_SUCCESS;
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

    if (MemoryInformationClass == MemorySectionName)
    {
        return MiQuerySectionName(ProcessHandle, BaseAddress, MemoryInformation, MemoryInformationLength,
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
    _In_ KPROCESSOR_MODE Mode)
{
    NTSTATUS Status = STATUS_SUCCESS;
    KAPC_STATE ApcState;

    KeStackAttachProcess(&Process->Pcb, &ApcState);

    _SEH2_TRY
    {
        if (FromProcess)
        {
            if (Mode != KernelMode)
                ProbeForRead(ProcessAddress, Length, sizeof(CHAR));

            RtlCopyMemory(Bounce, ProcessAddress, Length);
        }
        else
        {
            if (Mode != KernelMode)
                ProbeForWrite(ProcessAddress, Length, sizeof(CHAR));

            RtlCopyMemory(ProcessAddress, Bounce, Length);
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

        Status = MiCopyChunk(SourceProcess, (PUCHAR)SourceAddress + Done, Bounce, Length, TRUE, PreviousMode);
        if (NT_SUCCESS(Status))
            Status = MiCopyChunk(TargetProcess, (PUCHAR)TargetAddress + Done, Bounce, Length, FALSE, PreviousMode);

        if (NT_SUCCESS(Status))
            Done += Length;
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

        if (ExGetPreviousMode() != KernelMode && BaseAddress > MmHighestUserAddress)
            return STATUS_ACCESS_VIOLATION;
    }

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_WRITE, &Target);
    if (!NT_SUCCESS(Status))
        return Status;

    KeSweepICache(BaseAddress, FlushSize);
    MiReleaseTargetProcess(&Target);
    return STATUS_SUCCESS;
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
    NTSTATUS Status = MiFlushVirtualMemory(MiSpaceOfProcess(Process), &Base, &Size);

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
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(RegionSize);
    UNREFERENCED_PARAMETER(UserAddressArray);
    UNREFERENCED_PARAMETER(EntriesInUserAddressArray);
    UNREFERENCED_PARAMETER(Granularity);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtResetWriteWatch(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID BaseAddress,
    _In_ SIZE_T RegionSize)
{
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(RegionSize);
    return STATUS_NOT_IMPLEMENTED;
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
