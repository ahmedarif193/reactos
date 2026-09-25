/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntfault.c
 * PURPOSE:     NT page-fault dispatch and memory manager integration
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

#ifndef MM_USER_STACK_GUARD_PAGES
#define MM_USER_STACK_GUARD_PAGES 1
#endif

static
NTSTATUS
MiCheckForUserStackOverflow(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ PVOID Address)
{
    PETHREAD Thread = PsGetCurrentThread();
    PTEB Teb = Thread->Tcb.Teb;
    ULONG64 StackBase, StackLimit, DeallocationStack, Guaranteed, Page, Next, Size;
    ULONG64 GuardSize = MM_USER_STACK_GUARD_PAGES * PAGE_SIZE;
    NTSTATUS Status = STATUS_GUARD_PAGE_VIOLATION;
#ifdef _WIN64
    PTEB32 Wow64Teb = NULL;
#endif

    if (Teb == NULL || KeIsAttachedProcess() || Thread->AddressSpaceOwner)
        return STATUS_GUARD_PAGE_VIOLATION;

    _SEH2_TRY
    {
        StackBase = (ULONG64)(ULONG_PTR)Teb->NtTib.StackBase;
        StackLimit = (ULONG64)(ULONG_PTR)Teb->NtTib.StackLimit;
        DeallocationStack = (ULONG64)(ULONG_PTR)Teb->DeallocationStack;
        Guaranteed = Teb->GuaranteedStackBytes;

#ifdef _WIN64
        if (Teb->WowTebOffset != 0)
        {
            Wow64Teb = (PTEB32)((PUCHAR)Teb + Teb->WowTebOffset);

            if ((ULONG64)(ULONG_PTR)Address < Wow64Teb->NtTib.StackBase &&
                (ULONG64)(ULONG_PTR)Address >= Wow64Teb->DeallocationStack)
            {
                StackBase = Wow64Teb->NtTib.StackBase;
                StackLimit = Wow64Teb->NtTib.StackLimit;
                DeallocationStack = Wow64Teb->DeallocationStack;
                Guaranteed = Wow64Teb->GuaranteedStackBytes;
                GuardSize = PAGE_SIZE;
            }
            else
            {
                Wow64Teb = NULL;
            }
        }
#endif

        Guaranteed = ROUND_TO_PAGES(Guaranteed);
        if (Guaranteed == 0 || Guaranteed >= StackBase - DeallocationStack)
            Guaranteed = PAGE_SIZE;

        Page = (ULONG64)(ULONG_PTR)PAGE_ALIGN(Address);

        if ((ULONG64)(ULONG_PTR)Address >= StackBase || (ULONG64)(ULONG_PTR)Address < DeallocationStack)
            _SEH2_YIELD(return STATUS_GUARD_PAGE_VIOLATION);

        if (Page - DeallocationStack <= GuardSize + Guaranteed)
        {
            Next = (DeallocationStack & ~((ULONG64)PAGE_SIZE - 1)) + PAGE_SIZE;
            Size = Guaranteed;

            if (StackLimit > Next && StackLimit <= StackBase && StackLimit - Next > Size)
                Size = StackLimit - Next;

            if (NT_SUCCESS(MiAllocateVirtualMemory(Space, &Next, &Size, MI_MEM_COMMIT, MI_PROT_READWRITE)))
            {
#ifdef _WIN64
                if (Wow64Teb != NULL)
                    Wow64Teb->NtTib.StackLimit = (ULONG)Next;
                else
#endif
                    Teb->NtTib.StackLimit = (PVOID)(ULONG_PTR)Next;
            }

            _SEH2_YIELD(return STATUS_STACK_OVERFLOW);
        }

        Next = Page - GuardSize;
        Size = GuardSize;
        Status = MiAllocateVirtualMemory(Space, &Next, &Size, MI_MEM_COMMIT, MI_PROT_READWRITE | MI_PROT_GUARD);

        if (NT_SUCCESS(Status))
        {
            ULONG64 CommitBase = Page + PAGE_SIZE;

            if (GuardSize > PAGE_SIZE && CommitBase < StackLimit)
            {
                ULONG64 CommitSize = min(StackLimit - CommitBase, GuardSize - PAGE_SIZE);
                ULONG Old;

                if (!NT_SUCCESS(MiProtectVirtualMemory(Space, &CommitBase, &CommitSize, MI_PROT_READWRITE, &Old)))
                    _SEH2_YIELD(return STATUS_STACK_OVERFLOW);
            }

#ifdef _WIN64
            if (Wow64Teb != NULL)
                Wow64Teb->NtTib.StackLimit = (ULONG)Page;
            else
#endif
                Teb->NtTib.StackLimit = (PVOID)(ULONG_PTR)Page;

            Status = STATUS_PAGE_FAULT_GUARD_PAGE;
        }
        else
        {
            Status = STATUS_STACK_OVERFLOW;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_GUARD_PAGE_VIOLATION;
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
MmAccessFaultEx(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation,
    _In_ BOOLEAN AddressSpaceLocked)
{
    MI_FAULT_ACCESS Access = MI_IS_INSTRUCTION_FETCH(FaultCode) ? MiFaultExecute
                                                                : (MI_IS_WRITE_ACCESS(FaultCode) ? MiFaultWrite
                                                                                                 : MiFaultRead);
    PMI_ADDRESS_SPACE Space;
    ULONG Attempts = 0;
    PEPROCESS Process;
    NTSTATUS Status;
    BOOLEAN AllowExecutableWrite = (PsGetCurrentThread()->ExecutableWriteAllowed != 0);

    UNREFERENCED_PARAMETER(TrapInformation);
    UNREFERENCED_PARAMETER(AddressSpaceLocked);

    if (MiArchIsSelfMapAddress((ULONG64)(ULONG_PTR)Address))
        return STATUS_ACCESS_VIOLATION;

    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        ULONG64 Physical;
        MI_PTE Leaf;

        if (MI_IS_SYSTEM_VA(Address) &&
            MiPtTranslate(&MiSystem.SystemSpace, (ULONG64)(ULONG_PTR)Address, &Physical, &Leaf) &&
            (Access != MiFaultWrite || (MiArchPteIsWritable(Leaf) && MiArchPteIsDirty(Leaf))) &&
            (Access != MiFaultExecute || MiArchPteIsExecutable(Leaf, FALSE)))
        {
            return STATUS_SUCCESS;
        }

        return STATUS_IN_PAGE_ERROR;
    }

    if (MI_IS_SYSTEM_VA(Address))
    {
        if (Mode != KernelMode)
            return STATUS_ACCESS_VIOLATION;

        Space = &MiSystem.SystemSpace;
        Process = NULL;
    }
    else
    {
        Process = PsGetCurrentProcess();
        if (MI_PROCESS_OF(Process) == NULL)
            return STATUS_ACCESS_VIOLATION;

        Space = MiSpaceOfProcess(Process);
    }

    do
    {
        Status = MiFaultWithWriteAllowance(Space, (ULONG64)(ULONG_PTR)Address, Access,
                                           (BOOLEAN)(Mode != KernelMode),
                                           AllowExecutableWrite);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    if (Process != NULL)
    {
        Process->Vm.Instance.PageFaultCount++;
        Process->Vm.Instance.WorkingSetSize = (ULONG)MI_ATOMIC_READ64(&Space->ResidentPages);
        Process->NumberOfPrivatePages = (SIZE_T)MI_ATOMIC_READ64(&Space->PrivatePages);

        if (Process->Vm.Instance.WorkingSetSize > Process->Vm.Instance.PeakWorkingSetSize)
            Process->Vm.Instance.PeakWorkingSetSize = Process->Vm.Instance.WorkingSetSize;
    }

    if (Status == STATUS_GUARD_PAGE_VIOLATION && Process != NULL)
        Status = MiCheckForUserStackOverflow(Space, Address);

    if (Status == STATUS_NO_MEMORY)
        Status = STATUS_IN_PAGE_ERROR;

    if (!NT_SUCCESS(Status) && Process == NULL)
        DbgPrint("MM: system fault %p access %u failed %08lx\n", Address, (ULONG)Access, Status);

    return Status;
}

NTSTATUS
NTAPI
MmAccessFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation)
{
    return MmAccessFaultEx(FaultCode, Address, Mode, TrapInformation, FALSE);
}

BOOLEAN
NTAPI
MmIsRecursiveIoFault(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();

    return (BOOLEAN)(Thread->DisablePageFaultClustering | Thread->ForwardClusterOnly);
}

BOOLEAN
NTAPI
MmSetAddressRangeModified(
    _In_ PVOID Address,
    _In_ SIZE_T Length)
{
    return (BOOLEAN)NT_SUCCESS(MiSetRangeModified(MiSpaceForAddress(Address), (ULONG64)(ULONG_PTR)Address, Length));
}

NTSTATUS
NTAPI
MmDbgCopyMemory(
    _In_ ULONG64 Address,
    _In_ PVOID Buffer,
    _In_ ULONG Size,
    _In_ ULONG Flags)
{
    PVOID Source;
    ULONG64 Physical;

    if (Size != 1 && Size != 2 && Size != 4 && Size != 8)
        return STATUS_INVALID_PARAMETER_3;

    if (Flags & MMDBG_COPY_PHYSICAL)
    {
        Physical = Address;
    }
    else if (!MiTranslateCurrentAddress(Address, &Physical, NULL))
    {
        return STATUS_UNSUCCESSFUL;
    }

    if ((Physical & (PAGE_SIZE - 1)) + Size > PAGE_SIZE)
        return STATUS_INVALID_PARAMETER_3;

    Source = MiArchMapFrame(Physical >> PAGE_SHIFT);
    if (Source == NULL)
        return STATUS_UNSUCCESSFUL;
    Source = (PUCHAR)Source + (Physical & (PAGE_SIZE - 1));

    if (Flags & MMDBG_COPY_WRITE)
        RtlCopyMemory(Source, Buffer, Size);
    else
        RtlCopyMemory(Buffer, Source, Size);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmCopyMemory(
    _Out_ PVOID TargetAddress,
    _In_ MM_COPY_ADDRESS SourceAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Flags,
    _Out_ PSIZE_T NumberOfBytesTransferred)
{
    SIZE_T Done = 0;

    *NumberOfBytesTransferred = 0;

    if (!(Flags & (MM_COPY_MEMORY_PHYSICAL | MM_COPY_MEMORY_VIRTUAL)))
        return STATUS_INVALID_PARAMETER;

    while (Done < NumberOfBytes)
    {
        ULONG64 Physical;
        SIZE_T Chunk;
        PVOID Mapping;

        if (Flags & MM_COPY_MEMORY_PHYSICAL)
        {
            Physical = (ULONG64)SourceAddress.PhysicalAddress.QuadPart + Done;
        }
        else if (!MiTranslateCurrentAddress((ULONG64)(ULONG_PTR)SourceAddress.VirtualAddress + Done,
                                            &Physical, NULL))
        {
            break;
        }

        Chunk = PAGE_SIZE - (SIZE_T)(Physical & (PAGE_SIZE - 1));
        if (Chunk > NumberOfBytes - Done)
            Chunk = NumberOfBytes - Done;

        Mapping = MiArchMapFrame(Physical >> PAGE_SHIFT);
        if (Mapping == NULL)
            break;
        RtlCopyMemory((PUCHAR)TargetAddress + Done,
                      (PUCHAR)Mapping + (Physical & (PAGE_SIZE - 1)), Chunk);
        Done += Chunk;
    }

    *NumberOfBytesTransferred = Done;
    return (Done == NumberOfBytes) ? STATUS_SUCCESS : STATUS_PARTIAL_COPY;
}

NTSTATUS
NTAPI
MmMapUserAddressesToPage(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ PVOID PageAddress)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(NumberOfBytes);
    UNREFERENCED_PARAMETER(PageAddress);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmPrefetchVirtualAddresses(
    _In_ PPREFETCH_VIRTUAL_ADDRESS_LIST ParameterBlock)
{
    UNREFERENCED_PARAMETER(ParameterBlock);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmPrefetchPages(
    _In_ ULONG NumberOfLists,
    _In_ PREAD_LIST *ReadLists)
{
    ULONG i, j;

    for (i = 0; i < NumberOfLists; i++)
    {
        PREAD_LIST List = ReadLists[i];
        PMI_CONTROL_AREA Control;

        if (List->IsImage || List->FileObject->SectionObjectPointer == NULL)
            continue;

        Control = MiReferenceDataControlArea(List->FileObject->SectionObjectPointer);
        if (Control == NULL)
            continue;

        for (j = 0; j < List->NumberOfEntries; j++)
            MiSegmentMakeResident(Control->Segment, (ULONG64)List->List[j].Alignment & ~((ULONG64)PAGE_SIZE - 1),
                                  PAGE_SIZE);

        MiDereferenceControlArea(Control);
    }

    return STATUS_SUCCESS;
}

static EX_PUSH_LOCK MiRotateLock;

static
NTSTATUS
MiRotateCopy(
    _In_ PMDL Destination,
    _In_ PMDL Source,
    _In_ SIZE_T Bytes,
    _In_opt_ PMM_ROTATE_COPY_CALLBACK_FUNCTION CopyFunction,
    _In_opt_ PVOID Context)
{
    PVOID To, From;

    if (CopyFunction != NULL)
        return CopyFunction(Destination, Source, Context);

    To = MmMapLockedPagesSpecifyCache(Destination, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    if (To == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    From = MmMapLockedPagesSpecifyCache(Source, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    if (From == NULL)
    {
        MmUnmapLockedPages(To, Destination);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(To, From, Bytes);
    MmUnmapLockedPages(From, Source);
    MmUnmapLockedPages(To, Destination);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmRotatePhysicalView(
    _In_ PVOID VirtualAddress,
    _Inout_ PSIZE_T NumberOfBytes,
    _In_opt_ PMDLX NewMdl,
    _In_ MM_ROTATE_DIRECTION Direction,
    _In_ PMM_ROTATE_COPY_CALLBACK_FUNCTION CopyFunction,
    _In_opt_ PVOID Context)
{
    ULONG64 Va = (ULONG64)(ULONG_PTR)VirtualAddress;
    PMI_FRAME_NUMBER Mapped = NULL, Regular;
    PMDL Source = NULL, Destination = NULL;
    SIZE_T Bytes = *NumberOfBytes;
    const MI_FRAME_NUMBER *Target = NULL;
    BOOLEAN ToFrameBuffer, Copy;
    PMI_ADDRESS_SPACE Space;
    ULONG LeafFlags = 0;
    NTSTATUS Status;
    ULONG64 Pages;

    *NumberOfBytes = 0;

    if (Va & (PAGE_SIZE - 1))
        return STATUS_INVALID_PARAMETER_1;

    if (Bytes == 0 || (Bytes & (PAGE_SIZE - 1)))
        return STATUS_INVALID_PARAMETER_2;

    if ((LONG)Direction >= (LONG)MmMaximumRotateDirection)
        return STATUS_INVALID_PARAMETER_3;

    if (MI_IS_SYSTEM_VA(VirtualAddress) || MI_PROCESS_OF(PsGetCurrentProcess()) == NULL)
        return STATUS_ACCESS_VIOLATION;

    Space = MiSpaceOfProcess(PsGetCurrentProcess());
    Pages = Bytes >> PAGE_SHIFT;
    Mapped = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Pages * 2 * sizeof(*Mapped), 'oRmM');
    if (Mapped == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Regular = Mapped + Pages;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&MiRotateLock);

    Status = MiRotateQuery(Space, Va, Bytes, Mapped, Regular);
    if (NT_SUCCESS(Status) && (LONG)Direction < 0)
        Status = STATUS_INVALID_PARAMETER_3;

    ToFrameBuffer = (BOOLEAN)(Direction == MmToFrameBuffer || Direction == MmToFrameBufferNoCopy);
    Copy = (BOOLEAN)(Direction == MmToFrameBuffer || Direction == MmToRegularMemory);

    if (NT_SUCCESS(Status) && ToFrameBuffer)
    {
        if (NewMdl == NULL || MmGetMdlByteOffset(NewMdl) != 0 || MmGetMdlByteCount(NewMdl) < Bytes)
        {
            Status = STATUS_INVALID_PARAMETER_3;
        }
        else
        {
            Target = (const MI_FRAME_NUMBER *)MmGetMdlPfnArray(NewMdl);
            LeafFlags = MiFrameIsRam(Target[0]) ? 0 : MI_LEAF_WRITECOMBINE;
        }
    }

    if (NT_SUCCESS(Status) && Copy)
    {
        Source = IoAllocateMdl(NULL, (ULONG)Bytes, FALSE, FALSE, NULL);
        Destination = ToFrameBuffer ? NewMdl : IoAllocateMdl(NULL, (ULONG)Bytes, FALSE, FALSE, NULL);

        if (Source == NULL || Destination == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            RtlCopyMemory(MmGetMdlPfnArray(Source), Mapped, (SIZE_T)Pages * sizeof(*Mapped));
            Source->MdlFlags |= MDL_PAGES_LOCKED;

            if (!ToFrameBuffer)
            {
                RtlCopyMemory(MmGetMdlPfnArray(Destination), Regular, (SIZE_T)Pages * sizeof(*Regular));
                Destination->MdlFlags |= MDL_PAGES_LOCKED;
            }

            Status = MiRotateCopy(Destination, Source, Bytes, CopyFunction, Context);
        }
    }

    if (NT_SUCCESS(Status))
        Status = MiRotateApply(Space, Va, Bytes, Target, LeafFlags);

    ExReleasePushLockExclusive(&MiRotateLock);
    KeLeaveCriticalRegion();

    if (Source != NULL)
        IoFreeMdl(Source);

    if (Destination != NULL && Destination != NewMdl)
        IoFreeMdl(Destination);

    ExFreePoolWithTag(Mapped, 'oRmM');

    if (NT_SUCCESS(Status))
        *NumberOfBytes = Bytes;

    return Status;
}

ULONG
NTAPI
MmDoesFileHaveUserWritableReferences(
    _In_ PSECTION_OBJECT_POINTERS SectionPointer)
{
    PMI_CONTROL_AREA Control = MiReferenceDataControlArea(SectionPointer);
    ULONG Count;

    if (Control == NULL)
        return 0;

    Count = (ULONG)MI_ATOMIC_READ32(&Control->Segment->WritableUserViews);
    MiDereferenceControlArea(Control);
    return Count;
}

MM_SYSTEMSIZE
NTAPI
MmQuerySystemSize(VOID)
{
    if (MmNumberOfPhysicalPages < ((128 * _1MB) >> PAGE_SHIFT))
        return MmSmallSystem;

    if (MmNumberOfPhysicalPages < ((512 * _1MB) >> PAGE_SHIFT))
        return MmMediumSystem;

    return MmLargeSystem;
}

NTSTATUS
NTAPI
MmCreateMirror(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
MmSetBankedSection(
    _In_ HANDLE ProcessHandle,
    _In_ PVOID VirtualAddress,
    _In_ ULONG BankLength,
    _In_ BOOLEAN ReadWriteBank,
    _In_ PVOID BankRoutine,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(BankLength);
    UNREFERENCED_PARAMETER(ReadWriteBank);
    UNREFERENCED_PARAMETER(BankRoutine);
    UNREFERENCED_PARAMETER(Context);
    return STATUS_NOT_IMPLEMENTED;
}
