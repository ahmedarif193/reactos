/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntprocess.c
 * PURPOSE:     NT process address-space integration
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>

SIZE_T MmLargeStackSize = KERNEL_LARGE_STACK_SIZE;
SIZE_T MmMinimumStackCommitInBytes;
SIZE_T MmProcessCommit;
SIZE_T MmHeapSegmentReserve = 1 * _1MB;
SIZE_T MmHeapSegmentCommit = 2 * PAGE_SIZE;
SIZE_T MmHeapDeCommitTotalFreeThreshold = 64 * _1KB;
SIZE_T MmHeapDeCommitFreeBlockThreshold = PAGE_SIZE;
ULONG MmCritsectTimeoutSeconds = 150;
LARGE_INTEGER MmCriticalSectionTimeout;

static ULONG MiRotatingUniprocessorNumber;

PVOID
NTAPI
MmCreateKernelStack(
    _In_ BOOLEAN GuiStack,
    _In_ UCHAR Node)
{
    ULONG Reserve = (ULONG)((GuiStack ? MmLargeStackSize : KERNEL_STACK_SIZE) >> PAGE_SHIFT);
    ULONG Commit = (ULONG)((GuiStack ? KERNEL_LARGE_STACK_COMMIT : KERNEL_STACK_SIZE) >> PAGE_SHIFT);
    ULONG Attempts = 0;
    NTSTATUS Status;
    ULONG64 Top;

    UNREFERENCED_PARAMETER(Node);

    do
    {
        Status = MiCreateKernelStack(&MiSystem, Reserve, Commit, &Top);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    return NT_SUCCESS(Status) ? (PVOID)(ULONG_PTR)Top : NULL;
}

VOID
NTAPI
MmDeleteKernelStack(
    _In_ PVOID StackBase,
    _In_ BOOLEAN GuiStack)
{
    MiDeleteKernelStack(&MiSystem, (ULONG64)(ULONG_PTR)StackBase,
                        (ULONG)((GuiStack ? MmLargeStackSize : KERNEL_STACK_SIZE) >> PAGE_SHIFT));
}

NTSTATUS
NTAPI
MmGrowKernelStackEx(
    _In_ PVOID StackPointer,
    _In_ ULONG GrowSize)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG64 NewLimit = ((ULONG64)(ULONG_PTR)StackPointer - GrowSize) & ~((ULONG64)PAGE_SIZE - 1);
    NTSTATUS Status;

    if (NewLimit >= (ULONG64)Thread->StackLimit)
        return STATUS_SUCCESS;

    Status = MiGrowKernelStack(&MiSystem, (ULONG64)(ULONG_PTR)Thread->StackBase,
                               (ULONG)(MmLargeStackSize >> PAGE_SHIFT), NewLimit);
    if (NT_SUCCESS(Status))
        Thread->StackLimit = (ULONG_PTR)NewLimit;

    return Status;
}

NTSTATUS
NTAPI
MmGrowKernelStack(
    _In_ PVOID StackPointer)
{
    return MmGrowKernelStackEx(StackPointer, KERNEL_LARGE_STACK_COMMIT);
}

SIZE_T
NTAPI
MmQueryProcessCommitCharge(
    _In_ PEPROCESS Process)
{
    PMI_PROCESS Native = MI_PROCESS_OF(Process);

    if (Native == NULL)
        return Process->CommitCharge;

    return (SIZE_T)MI_ATOMIC_READ64(&Native->Space.CommittedPages);
}

BOOLEAN
MiChargeProcessCommit(
    _In_ PVOID Owner,
    _In_ LONG64 Pages)
{
    PEPROCESS Process = Owner;

    if (Process->Job == NULL)
        return TRUE;

    Process->CommitCharge = MmQueryProcessCommitCharge(Process);
    return NT_SUCCESS(PsChargeJobCommitment(Process, (SIZE_T)Pages));
}

VOID
MiReturnProcessCommit(
    _In_ PVOID Owner,
    _In_ LONG64 Pages)
{
    PEPROCESS Process = Owner;

    if (Process->Job != NULL)
        PsReturnJobCommitment(Process, (SIZE_T)Pages);
}

BOOLEAN
NTAPI
MmCreateProcessAddressSpace(
    _In_ ULONG MinWs,
    _In_ PEPROCESS Process,
    _Out_ PULONG_PTR DirectoryTableBase)
{
    PMI_PROCESS Native;
    ULONG Attempts = 0;
    NTSTATUS Status;

    ASSERT(Process->Vm.Instance.VmWorkingSetList == NULL);

    Native = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Native), 'rPmM');
    if (Native == NULL)
        return FALSE;

    do
    {
        Status = MiProcessCreate(&MiSystem, &MiProcessManager, Native);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Native, 'rPmM');
        return FALSE;
    }

    if (MinWs != 0)
        Native->WorkingSetMinimum = MinWs;

    Process->Vm.Instance.VmWorkingSetList = (PVOID)Native;
    Process->Vm.Instance.MinimumWorkingSetSize = (ULONG)Native->WorkingSetMinimum;
    Process->Vm.Instance.MaximumWorkingSetSize = (ULONG)Native->WorkingSetMaximum;
    Process->AddressSpaceInitialized = 1;
    Native->Space.CommitOwner = Process;

    DirectoryTableBase[0] = (ULONG_PTR)Native->Space.RootFrame << PAGE_SHIFT;
    DirectoryTableBase[1] = (ULONG_PTR)MiSystem.SystemSpace.RootFrame << PAGE_SHIFT;
    MiSessionAddProcess(Process);
    return TRUE;
}

NTSTATUS
NTAPI
MmInitializeHandBuiltProcess(
    _In_ PEPROCESS Process,
    _Out_ PULONG_PTR DirectoryTableBase)
{
    PEPROCESS Current = PsGetCurrentProcess();

    DirectoryTableBase[0] = KPROCESS_DTB0(&Current->Pcb);
    DirectoryTableBase[1] = KPROCESS_DTB1(&Current->Pcb);

    ExInitializePushLock(&Process->AddressCreationLock);
    Process->Vm.Instance.VmWorkingSetList = Current->Vm.Instance.VmWorkingSetList;
    Process->HasAddressSpace = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmInitializeHandBuiltProcess2(
    _In_ PEPROCESS Process)
{
    UNREFERENCED_PARAMETER(Process);
    return STATUS_SUCCESS;
}

static
VOID
MiSelectBottomUpBase(
    _In_ PEPROCESS Process,
    _In_ PMI_SECTION_OBJECT Section)
{
    PMI_ADDRESS_SPACE Space = MiSpaceOfProcess(Process);
    PMI_CONTROL_AREA Control = Section->Control;
    ULONG DllCharacteristics = Control->ImageInformation.DllCharacteristics;
    ULONG64 Floor = Space->LowestVa;
    ULONG64 Slots = MI_NT_BOTTOM_UP_SLOTS;
    ULONG Seed;

    if (!Control->Image || !(DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE))
    {
        Process->MitigationFlagsValues.StackRandomizationDisabled = 1;
        return;
    }

    if (Control->Image64 && (DllCharacteristics & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA))
    {
        Floor = MI_NT_HIGH_ENTROPY_FLOOR;
        Slots = (MI_NT_HIGH_ENTROPY_LIMIT - MI_NT_HIGH_ENTROPY_FLOOR) / MI_ALLOCATION_GRANULARITY;
    }

    if (Floor < Space->LowestVa || Floor + Slots * MI_ALLOCATION_GRANULARITY > Space->HighestVa)
    {
        Process->MitigationFlagsValues.StackRandomizationDisabled = 1;
        return;
    }

    if (Floor == MI_NT_HIGH_ENTROPY_FLOOR)
        Process->MitigationFlagsValues.HighEntropyASLREnabled = 1;

    Seed = KeQueryPerformanceCounter(NULL).LowPart ^ (ULONG)KeQueryInterruptTime() ^ (ULONG)(ULONG_PTR)Process;
    Space->BottomUpVa = Floor + (RtlRandomEx(&Seed) % Slots) * MI_ALLOCATION_GRANULARITY;

    if (Control->Image64 && MI_NT_TOP_DOWN_CEILING <= Space->HighestVa)
    {
        Space->TopDownVa = MI_NT_TOP_DOWN_CEILING -
                           (RtlRandomEx(&Seed) % MI_NT_BOTTOM_UP_SLOTS) * MI_ALLOCATION_GRANULARITY;
    }
}

NTSTATUS
NTAPI
MmInitializeProcessAddressSpace(
    _In_ PEPROCESS Process,
    _In_opt_ PEPROCESS ProcessClone,
    _In_opt_ PVOID Section,
    _Inout_ PULONG Flags,
    _In_opt_ POBJECT_NAME_INFORMATION *AuditName)
{
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T ViewSize = 0;
    PVOID ImageBase = NULL;

    UNREFERENCED_PARAMETER(Flags);

    ASSERT(KPROCESS_DTB0(&Process->Pcb) != 0);
    ASSERT(Process->AddressSpaceInitialized <= 1);

    Process->AddressSpaceInitialized = 2;
    ExInitializePushLock(&Process->AddressCreationLock);

    if (ProcessClone != NULL)
    {
        PMI_PROCESS Source;
        PMI_PROCESS Target = MI_PROCESS_OF(Process);
        KAPC_STATE ApcState;

        if (!ExAcquireRundownProtection(&ProcessClone->RundownProtect))
            return STATUS_PROCESS_IS_TERMINATING;
        Source = MI_PROCESS_OF(ProcessClone);
        if (Source == NULL || Target == NULL)
        {
            Status = STATUS_PROCESS_IS_TERMINATING;
            goto CloneDone;
        }
#ifdef _WIN64
        if (ProcessClone->WoW64Process != NULL)
        {
            Status = STATUS_NOT_SUPPORTED;
            goto CloneDone;
        }
#endif
        if (AuditName != NULL && ProcessClone->SectionObject != NULL)
        {
            Status = SeInitializeProcessAuditName(MmGetFileObjectForSection(ProcessClone->SectionObject),
                                                  FALSE, AuditName);
            if (!NT_SUCCESS(Status))
                goto CloneDone;
        }
        MiCleanAddressSpace(&Target->Space);
        Target->Space.BottomUpVa = Source->Space.BottomUpVa;
        Target->Space.TopDownVa = Source->Space.TopDownVa;
        Status = MiCloneAddressSpace(&Source->Space, &Target->Space);
        if (NT_SUCCESS(Status))
        {
            PMI_VAD_NODE Node;

            RtlCopyMemory(Process->ImageFileName, ProcessClone->ImageFileName, sizeof(Process->ImageFileName));
            Process->SectionBaseAddress = ProcessClone->SectionBaseAddress;
            Process->Peb = ProcessClone->Peb;
            Target->Peb = (ULONG64)(ULONG_PTR)Process->Peb;
            MI_RW_ACQUIRE_SHARED(&Target->Space.Lock);
            for (Node = MiVadFirst(&Target->Space.VadRoot); Node != NULL; Node = MiVadNext(Node))
                Process->VirtualSize += (SIZE_T)((Node->EndingVpn - Node->StartingVpn + 1) << PAGE_SHIFT);
            MI_RW_RELEASE_SHARED(&Target->Space.Lock);
            Process->PeakVirtualSize = Process->VirtualSize;
            if (Process->Peb != NULL)
            {
                KeStackAttachProcess(&Process->Pcb, &ApcState);
                _SEH2_TRY
                {
                    Process->Peb->InheritedAddressSpace = TRUE;
                    Process->Peb->BeingDebugged = (BOOLEAN)(Process->DebugPort != NULL);
                    Process->Peb->Mutant = (HANDLE)-1;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                KeUnstackDetachProcess(&ApcState);
            }
        }
CloneDone:
        ExReleaseRundownProtection(&ProcessClone->RundownProtect);
        return Status;
    }

    if (Section == NULL)
        return STATUS_SUCCESS;

    KeAttachProcess(&Process->Pcb);

    {
        PFILE_OBJECT FileObject = MmGetFileObjectForSection(Section);
        UNICODE_STRING FileName = FileObject->FileName;
        PWCHAR Source = (PWCHAR)((PCHAR)FileName.Buffer + FileName.Length);
        PCHAR Destination = (PCHAR)Process->ImageFileName;
        USHORT Length = 0;

        if (FileName.Buffer != NULL)
        {
            while (Source > FileName.Buffer)
            {
                if (*--Source == OBJ_NAME_PATH_SEPARATOR)
                {
                    Source++;
                    break;
                }

                Length++;
            }
        }

        Length = min(Length, sizeof(Process->ImageFileName) - 1);
        while (Length--)
            *Destination++ = (UCHAR)*Source++;
        *Destination = ANSI_NULL;

        if (AuditName != NULL)
            Status = SeInitializeProcessAuditName(FileObject, FALSE, AuditName);
    }

    if (NT_SUCCESS(Status))
    {
        MiSelectBottomUpBase(Process, Section);
        Status = MmMapViewOfSection(Section, Process, &ImageBase, 0, 0, NULL, &ViewSize, ViewShare, MEM_COMMIT,
                                    PAGE_READWRITE);
        Process->SectionBaseAddress = ImageBase;
    }

    KeDetachProcess();
    return Status;
}

VOID
NTAPI
MmCleanProcessAddressSpace(
    _In_ PEPROCESS Process)
{
    PMI_PROCESS Native = MI_PROCESS_OF(Process);

    if (Native == NULL || Process == PsInitialSystemProcess || Native == MI_PROCESS_OF(PsInitialSystemProcess))
        return;

    MiProcessQueryCounters(Native, &(MI_PROCESS_COUNTERS){0});
    MiSecureRangePurgeProcess(Process);
    MiCleanAddressSpace(&Native->Space);
    Process->Vm.Instance.WorkingSetSize = 0;
    Process->VirtualSize = 0;
    Process->CommitCharge = 0;
}

VOID
NTAPI
MmDeleteProcessAddressSpace(
    _In_ PEPROCESS Process)
{
    PMI_PROCESS Native = MI_PROCESS_OF(Process);

    if (Native == NULL || Native == MI_PROCESS_OF(PsInitialSystemProcess))
        return;

    MiSessionRemoveProcess(Process);
    Process->Vm.Instance.VmWorkingSetList = NULL;
    MiProcessDelete(&MiProcessManager, Native);
    ExFreePoolWithTag(Native, 'rPmM');

    KPROCESS_DTB0(&Process->Pcb) = 0;
    KPROCESS_DTB1(&Process->Pcb) = 0;
    Process->HasAddressSpace = FALSE;
}

static
NTSTATUS
MiCreatePebOrTeb(
    _In_ PEPROCESS Process,
    _In_ SIZE_T Size,
    _In_ ULONG_PTR HighestAddress,
    _Out_ PULONG_PTR Base)
{
    ULONG64 Address = 0;
    ULONG64 RegionSize = ROUND_TO_PAGES(Size);
    ULONG Attempts = 0;
    NTSTATUS Status;

    do
    {
        Status = MiAllocateVirtualMemoryEx(MiSpaceOfProcess(Process), &Address, &RegionSize,
                                           MI_MEM_RESERVE | MI_MEM_COMMIT, MI_PROT_READWRITE, HighestAddress);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    *Base = (ULONG_PTR)Address;
    return Status;
}

NTSTATUS
NTAPI
MmCreatePeb(
    _In_ PEPROCESS Process,
    _In_ PINITIAL_PEB InitialPeb,
    _Out_ PPEB *BasePeb)
{
    PPEB Peb = NULL;
    LARGE_INTEGER SectionOffset;
    SIZE_T ViewSize = 0;
    PVOID TableBase = NULL;
    PIMAGE_NT_HEADERS32 NtHeaders = NULL;
    NTSTATUS Status;

    SectionOffset.QuadPart = 0;
    *BasePeb = NULL;

    KeAttachProcess(&Process->Pcb);

    Status = MmMapViewOfSection(ExpNlsSectionPointer, Process, &TableBase, 0, 0, &SectionOffset, &ViewSize,
                                ViewShare, 0, PAGE_READONLY);
    if (NT_SUCCESS(Status))
        Status = MiCreatePebOrTeb(Process, sizeof(PEB), (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS, (PULONG_PTR)&Peb);

    if (!NT_SUCCESS(Status))
    {
        KeDetachProcess();
        return Status;
    }

    _SEH2_TRY
    {
        RtlZeroMemory(Peb, sizeof(PEB));
        Peb->ImageBaseAddress = Process->SectionBaseAddress;
        Peb->InheritedAddressSpace = InitialPeb->InheritedAddressSpace;
        Peb->Mutant = InitialPeb->Mutant;
        Peb->ImageUsesLargePages = InitialPeb->ImageUsesLargePages;
        Peb->AnsiCodePageData = (PCHAR)TableBase + ExpAnsiCodePageDataOffset;
        Peb->OemCodePageData = (PCHAR)TableBase + ExpOemCodePageDataOffset;
        Peb->UnicodeCaseTableData = (PCHAR)TableBase + ExpUnicodeCaseTableDataOffset;
        Peb->OSMajorVersion = NtMajorVersion;
        Peb->OSMinorVersion = NtMinorVersion;
        Peb->OSBuildNumber = (USHORT)(NtBuildNumber & 0xFFFF);
        Peb->OSPlatformId = VER_PLATFORM_WIN32_NT;
        Peb->OSCSDVersion = (USHORT)CmNtCSDVersion;
        Peb->NumberOfProcessors = KeNumberProcessors;
        Peb->ImageProcessAffinityMask = 0;
        Peb->BeingDebugged = (BOOLEAN)(Process->DebugPort != NULL);
        Peb->NtGlobalFlag = NtGlobalFlag;
        Peb->HeapSegmentReserve = MmHeapSegmentReserve;
        Peb->HeapSegmentCommit = MmHeapSegmentCommit;
        Peb->HeapDeCommitTotalFreeThreshold = MmHeapDeCommitTotalFreeThreshold;
        Peb->HeapDeCommitFreeBlockThreshold = MmHeapDeCommitFreeBlockThreshold;
        Peb->CriticalSectionTimeout = MmCriticalSectionTimeout;
        Peb->MinimumStackCommit = MmMinimumStackCommitInBytes;
        Peb->MaximumNumberOfHeaps = (PAGE_SIZE - sizeof(PEB)) / sizeof(PVOID);
        Peb->ProcessHeaps = (PVOID *)(Peb + 1);

        if (Process->Session != NULL)
            Peb->SessionId = MmGetSessionId(Process);

        NtHeaders = (PIMAGE_NT_HEADERS32)RtlImageNtHeader(Peb->ImageBaseAddress);
        if (NtHeaders != NULL)
        {
            USHORT Magic = NtHeaders->OptionalHeader.Magic;
            USHORT CsdVersion = 0;
            ULONG_PTR AffinityMask = 0;
            ULONG Win32VersionValue;
            ULONG ConfigSize;
            PVOID Config = RtlImageDirectoryEntryToData(Peb->ImageBaseAddress, TRUE,
                                                        IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &ConfigSize);

            if (Config != NULL && Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            {
                ProbeForRead(Config, sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32), sizeof(ULONG));
                CsdVersion = ((PIMAGE_LOAD_CONFIG_DIRECTORY32)Config)->CSDVersion;
                AffinityMask = ((PIMAGE_LOAD_CONFIG_DIRECTORY32)Config)->ProcessAffinityMask;
            }
            else if (Config != NULL)
            {
                ProbeForRead(Config, sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64), sizeof(ULONG));
                CsdVersion = ((PIMAGE_LOAD_CONFIG_DIRECTORY64)Config)->CSDVersion;
                AffinityMask = (ULONG_PTR)((PIMAGE_LOAD_CONFIG_DIRECTORY64)Config)->ProcessAffinityMask;
            }

            if (Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            {
                Peb->ImageSubsystem = NtHeaders->OptionalHeader.Subsystem;
                Peb->ImageSubsystemMajorVersion = NtHeaders->OptionalHeader.MajorSubsystemVersion;
                Peb->ImageSubsystemMinorVersion = NtHeaders->OptionalHeader.MinorSubsystemVersion;
                Win32VersionValue = NtHeaders->OptionalHeader.Win32VersionValue;
            }
            else
            {
                PIMAGE_NT_HEADERS64 NtHeaders64 = (PIMAGE_NT_HEADERS64)NtHeaders;

                Peb->ImageSubsystem = NtHeaders64->OptionalHeader.Subsystem;
                Peb->ImageSubsystemMajorVersion = NtHeaders64->OptionalHeader.MajorSubsystemVersion;
                Peb->ImageSubsystemMinorVersion = NtHeaders64->OptionalHeader.MinorSubsystemVersion;
                Win32VersionValue = NtHeaders64->OptionalHeader.Win32VersionValue;
            }

            if (Win32VersionValue != 0)
            {
                Peb->OSMajorVersion = Win32VersionValue & 0xFF;
                Peb->OSMinorVersion = (Win32VersionValue >> 8) & 0xFF;
                Peb->OSBuildNumber = (Win32VersionValue >> 16) & 0x3FFF;
                Peb->OSPlatformId = (Win32VersionValue >> 30) ^ 2;

                if (CsdVersion != 0)
                    Peb->OSCSDVersion = CsdVersion;
            }

            if (AffinityMask != 0)
                Peb->ImageProcessAffinityMask = AffinityMask;

            if (NtHeaders->FileHeader.Characteristics & IMAGE_FILE_UP_SYSTEM_ONLY)
            {
                Peb->ImageProcessAffinityMask = AFFINITY_MASK(MiRotatingUniprocessorNumber);
                MiRotatingUniprocessorNumber = (MiRotatingUniprocessorNumber + 1) % KeNumberProcessors;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = (NtHeaders != NULL) ? STATUS_INVALID_IMAGE_PROTECT : _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    KeDetachProcess();

    if (NT_SUCCESS(Status))
        *BasePeb = Peb;

    return Status;
}

#ifdef _WIN64
NTSTATUS
NTAPI
MmCreatePeb32(
    _In_ PEPROCESS Process,
    _In_ PINITIAL_PEB InitialPeb,
    _In_ PSECTION_IMAGE_INFORMATION ImageInformation,
    _Out_ struct _PEB32 **BasePeb)
{
    PEB32 *Peb = NULL;
    LARGE_INTEGER SectionOffset;
    SIZE_T ViewSize = 0;
    PVOID TableBase = NULL;
    NTSTATUS Status;

    *BasePeb = NULL;
    SectionOffset.QuadPart = 0;

    KeAttachProcess(&Process->Pcb);

    Status = MmMapViewOfSection(ExpNlsSectionPointer, Process, &TableBase, MAXULONG, 0, &SectionOffset, &ViewSize,
                                ViewShare, MEM_TOP_DOWN, PAGE_READONLY);
    if (NT_SUCCESS(Status))
        Status = MiCreatePebOrTeb(Process, PAGE_SIZE, MM_HIGHEST_USER_ADDRESS_WOW64, (PULONG_PTR)&Peb);

    if (!NT_SUCCESS(Status))
    {
        KeDetachProcess();
        return Status;
    }

    _SEH2_TRY
    {
        WOW64INFO *Wow64Info;

        RtlZeroMemory(Peb, PAGE_SIZE);
        Peb->InheritedAddressSpace = InitialPeb->InheritedAddressSpace;
        Peb->BeingDebugged = (BOOLEAN)(Process->DebugPort != NULL);
        Peb->BitField = InitialPeb->BitField;
        Peb->Mutant = HandleToUlong(InitialPeb->Mutant);
        Peb->ImageBaseAddress = PtrToUlong(Process->SectionBaseAddress);
        Peb->AnsiCodePageData = PtrToUlong((PCHAR)TableBase + ExpAnsiCodePageDataOffset);
        Peb->OemCodePageData = PtrToUlong((PCHAR)TableBase + ExpOemCodePageDataOffset);
        Peb->UnicodeCaseTableData = PtrToUlong((PCHAR)TableBase + ExpUnicodeCaseTableDataOffset);
        Process->Peb->AnsiCodePageData = UlongToPtr(Peb->AnsiCodePageData);
        Process->Peb->OemCodePageData = UlongToPtr(Peb->OemCodePageData);
        Process->Peb->UnicodeCaseTableData = UlongToPtr(Peb->UnicodeCaseTableData);
        Peb->OSMajorVersion = NtMajorVersion;
        Peb->OSMinorVersion = NtMinorVersion;
        Peb->OSBuildNumber = (USHORT)(NtBuildNumber & 0xFFFF);
        Peb->OSPlatformId = VER_PLATFORM_WIN32_NT;
        Peb->OSCSDVersion = (USHORT)CmNtCSDVersion;
        Peb->NumberOfProcessors = KeNumberProcessors;
        Peb->ImageProcessAffinityMask = (ULONG)Process->Peb->ImageProcessAffinityMask;
        Peb->NtGlobalFlag = NtGlobalFlag;
        Peb->HeapSegmentReserve = (ULONG)MmHeapSegmentReserve;
        Peb->HeapSegmentCommit = (ULONG)MmHeapSegmentCommit;
        Peb->HeapDeCommitTotalFreeThreshold = (ULONG)MmHeapDeCommitTotalFreeThreshold;
        Peb->HeapDeCommitFreeBlockThreshold = (ULONG)MmHeapDeCommitFreeBlockThreshold;
        Peb->CriticalSectionTimeout = MmCriticalSectionTimeout;
        Peb->MinimumStackCommit = (ULONG)MmMinimumStackCommitInBytes;
        Peb->ImageSubsystem = ImageInformation->SubSystemType;
        Peb->ImageSubsystemMajorVersion = ImageInformation->SubSystemMajorVersion;
        Peb->ImageSubsystemMinorVersion = ImageInformation->SubSystemMinorVersion;

        if (Process->Session != NULL)
            Peb->SessionId = MmGetSessionId(Process);

        Wow64Info = (WOW64INFO *)(Peb + 1);
        Wow64Info->NativeSystemPageSize = PAGE_SIZE;
        Wow64Info->NativeMachineType = IMAGE_FILE_MACHINE_NATIVE;
        Wow64Info->EmulatedMachineType = ImageInformation->Machine;

        Peb->MaximumNumberOfHeaps = (PAGE_SIZE - sizeof(PEB32) - sizeof(WOW64INFO)) / sizeof(ULONG);
        Peb->ProcessHeaps = PtrToUlong(Wow64Info + 1);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    KeDetachProcess();

    if (NT_SUCCESS(Status))
        *BasePeb = Peb;

    return Status;
}
#endif

NTSTATUS
NTAPI
MmCreateTeb(
    _In_ PEPROCESS Process,
    _In_ PCLIENT_ID ClientId,
    _In_ PINITIAL_TEB InitialTeb,
    _In_opt_ PINITIAL_TEB Wow64InitialTeb,
    _Out_ PTEB *BaseTeb)
{
    ULONG_PTR HighestAddress = (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS;
    ULONG TebSize = sizeof(TEB);
    NTSTATUS Status;
    PTEB Teb;
#ifdef _WIN64
    ULONG Teb32Offset = ROUND_TO_PAGES(sizeof(TEB));

    if (Process->WoW64Process != NULL)
    {
        TebSize = Teb32Offset + ROUND_TO_PAGES(sizeof(TEB32));
        HighestAddress = MM_HIGHEST_USER_ADDRESS_WOW64;
    }
#else
    UNREFERENCED_PARAMETER(Wow64InitialTeb);
#endif

    *BaseTeb = NULL;

    KeAttachProcess(&Process->Pcb);

    Status = MiCreatePebOrTeb(Process, TebSize, HighestAddress, (PULONG_PTR)&Teb);
    if (!NT_SUCCESS(Status))
    {
        KeDetachProcess();
        return Status;
    }

    _SEH2_TRY
    {
        RtlZeroMemory(Teb, TebSize);
        Teb->NtTib.ExceptionList = NULL;
        Teb->NtTib.Self = (PNT_TIB)Teb;
        Teb->NtTib.Version = 30 << 8;
        Teb->ClientId = *ClientId;
        Teb->RealClientId = *ClientId;
        Teb->ProcessEnvironmentBlock = Process->Peb;
        Teb->CurrentLocale = PsDefaultThreadLocaleId;

        if (InitialTeb->PreviousStackBase == NULL && InitialTeb->PreviousStackLimit == NULL)
        {
            Teb->NtTib.StackBase = InitialTeb->StackBase;
            Teb->NtTib.StackLimit = InitialTeb->StackLimit;
            Teb->DeallocationStack = InitialTeb->AllocatedStackBase;
        }
        else
        {
            Teb->NtTib.StackBase = InitialTeb->PreviousStackBase;
            Teb->NtTib.StackLimit = InitialTeb->PreviousStackLimit;
        }

        Teb->StaticUnicodeString.MaximumLength = sizeof(Teb->StaticUnicodeBuffer);
        Teb->StaticUnicodeString.Buffer = Teb->StaticUnicodeBuffer;

#ifdef _WIN64
        if (Process->WoW64Process != NULL)
        {
            TEB32 *Teb32 = (TEB32 *)((PUCHAR)Teb + Teb32Offset);

            Teb->NtTib.ExceptionList = (PVOID)Teb32;
            Teb->WowTebOffset = Teb32Offset;
            Teb->TlsSlots[WOW64_TLS_CPURESERVED] = InitialTeb->StackBase;
            Teb32->NtTib.ExceptionList = MAXULONG;

            if (Wow64InitialTeb != NULL)
            {
                Teb32->NtTib.StackBase = PtrToUlong(Wow64InitialTeb->StackBase);
                Teb32->NtTib.StackLimit = PtrToUlong(Wow64InitialTeb->StackLimit);
                Teb32->DeallocationStack = PtrToUlong(Wow64InitialTeb->AllocatedStackBase);
            }

            Teb32->NtTib.Self = PtrToUlong(Teb32);
            Teb32->NtTib.Version = 30 << 8;
            Teb32->ClientId.UniqueProcess = HandleToUlong(ClientId->UniqueProcess);
            Teb32->ClientId.UniqueThread = HandleToUlong(ClientId->UniqueThread);
            Teb32->RealClientId = Teb32->ClientId;
            Teb32->ProcessEnvironmentBlock = PtrToUlong(Process->WoW64Process->Peb);
            Teb32->CurrentLocale = PsDefaultThreadLocaleId;
            Teb32->StaticUnicodeString.MaximumLength = sizeof(Teb32->StaticUnicodeBuffer);
            Teb32->StaticUnicodeString.Buffer = PtrToUlong(Teb32->StaticUnicodeBuffer);
            Teb32->GdiBatchCount = PtrToUlong(Teb);
            Teb32->WowTebOffset = -(LONG)Teb32Offset;
        }
#endif
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    KeDetachProcess();
    *BaseTeb = Teb;
    return Status;
}

VOID
NTAPI
MmDeleteTeb(
    _In_ PEPROCESS Process,
    _In_ PTEB Teb)
{
    PMI_PROCESS Native = MI_PROCESS_OF(Process);

    if (Native != NULL)
        MiProcessDeleteTeb(Native, (ULONG64)(ULONG_PTR)Teb);
}

NTSTATUS
NTAPI
MmSetMemoryPriorityProcess(
    _In_ PEPROCESS Process,
    _In_ UCHAR MemoryPriority)
{
    UCHAR Old = (UCHAR)Process->Vm.Instance.Flags.MemoryPriority;

    Process->Vm.Instance.Flags.MemoryPriority = MemoryPriority;
    return Old;
}

NTSTATUS
NTAPI
MmAdjustWorkingSetSize(
    _In_ SIZE_T WorkingSetMinimumInBytes,
    _In_ SIZE_T WorkingSetMaximumInBytes,
    _In_ ULONG SystemCache,
    _In_ BOOLEAN IncreaseOkay)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PMI_PROCESS Native = MI_PROCESS_OF(Process);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(IncreaseOkay);

    if (SystemCache)
        return STATUS_SUCCESS;

    if (WorkingSetMinimumInBytes == (SIZE_T)-1 && WorkingSetMaximumInBytes == (SIZE_T)-1)
    {
        MiProcessEmptyWorkingSet(Native);
        return STATUS_SUCCESS;
    }

    if (WorkingSetMinimumInBytes == 0)
        WorkingSetMinimumInBytes = (SIZE_T)Native->WorkingSetMinimum << PAGE_SHIFT;

    if (WorkingSetMaximumInBytes == 0)
        WorkingSetMaximumInBytes = (SIZE_T)Native->WorkingSetMaximum << PAGE_SHIFT;

    if (WorkingSetMinimumInBytes > WorkingSetMaximumInBytes)
        return STATUS_BAD_WORKING_SET_LIMIT;

    Status = MiProcessSetWorkingSetLimits(Native, BYTES_TO_PAGES(WorkingSetMinimumInBytes),
                                          BYTES_TO_PAGES(WorkingSetMaximumInBytes),
                                          (BOOLEAN)Process->Vm.Instance.Flags.MaximumWorkingSetHard);
    if (!NT_SUCCESS(Status))
        return STATUS_BAD_WORKING_SET_LIMIT;

    Process->Vm.Instance.MinimumWorkingSetSize = (ULONG)Native->WorkingSetMinimum;
    Process->Vm.Instance.MaximumWorkingSetSize = (ULONG)Native->WorkingSetMaximum;
    return STATUS_SUCCESS;
}
