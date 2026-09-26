/*
 * PROJECT:         ReactOS NT Library
 * FILE:            dll/ntdll/chpewrap.c
 * PURPOSE:         CHPE-wrapped Nt* syscall implementations
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * COPYRIGHT:       Copyright 2023 Alexandre Julliard
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * SVC_WRAP_ emits separate native Nt* and raw Zw* syscall stubs. CHPE
 * processes redirect the Nt* entries to the wrappers in this file, which
 * bracket address-space changes with emulator notifications.
 * Callback ordering and cross-process notifications are adapted from Wine's
 * dlls/ntdll/signal_arm64ec.c.
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64)

typedef struct _CHPE_CROSS_PROCESS_CONNECTION
{
    HANDLE SectionHandle;
    PCROSS_PROCESS_WORK_LIST WorkList;
} CHPE_CROSS_PROCESS_CONNECTION, *PCHPE_CROSS_PROCESS_CONNECTION;

static
BOOLEAN
ChpeIsCurrentProcessHandle(HANDLE ProcessHandle)
{
    return RtlIsCurrentProcess(ProcessHandle);
}

static
VOID
ChpepOpenCrossProcessConnection(HANDLE ProcessHandle, PCHPE_CROSS_PROCESS_CONNECTION Connection)
{
    Connection->SectionHandle = NULL;
    Connection->WorkList = NULL;
    RtlOpenCrossProcessEmulatorWorkConnection(ProcessHandle, &Connection->SectionHandle, (PVOID *)&Connection->WorkList);
}

static
VOID
ChpepCloseCrossProcessConnection(PCHPE_CROSS_PROCESS_CONNECTION Connection)
{
    if (Connection->WorkList)
        ZwUnmapViewOfSection(NtCurrentProcess(), Connection->WorkList);
    if (Connection->SectionHandle)
        NtClose(Connection->SectionHandle);

    Connection->SectionHandle = NULL;
    Connection->WorkList = NULL;
}

static
BOOLEAN
ChpepSendCrossProcessNotification(PCHPE_CROSS_PROCESS_CONNECTION Connection,
                                  ULONG Notification,
                                  PVOID Address,
                                  SIZE_T Size,
                                  ULONG ArgumentCount,
                                  ULONG Argument0,
                                  ULONG Argument1,
                                  ULONG Argument2)
{
    PCROSS_PROCESS_WORK_ENTRY Entry;
    PVOID Unused;

    if (!Connection->WorkList)
        return FALSE;

    Entry = RtlWow64PopCrossProcessWorkFromFreeList(&Connection->WorkList->free_list);
    if (!Entry)
    {
        RtlWow64RequestCrossProcessHeavyFlush(&Connection->WorkList->work_list);
        return FALSE;
    }

    Entry->id = Notification;
    Entry->addr = (ULONGLONG)(ULONG_PTR)Address;
    Entry->size = Size;
    Entry->args[0] = ArgumentCount > 0 ? Argument0 : 0;
    Entry->args[1] = ArgumentCount > 1 ? Argument1 : 0;
    Entry->args[2] = ArgumentCount > 2 ? Argument2 : 0;
    Entry->args[3] = 0;
    RtlWow64PushCrossProcessWorkOntoWorkList(&Connection->WorkList->work_list, Entry, &Unused);
    return TRUE;
}

static
BOOLEAN
ChpepCaptureRange(PVOID *BaseAddress,
                  PSIZE_T RegionSize,
                  PVOID *Base,
                  PSIZE_T Size)
{
    BOOLEAN Captured = TRUE;

    _SEH2_TRY
    {
        *Base = *BaseAddress;
        *Size = *RegionSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Captured = FALSE;
    }
    _SEH2_END;

    return Captured;
}

/*
 * @implemented
 */
static
NTSTATUS
NTAPI
ChpepNtAllocateVirtualMemory(HANDLE ProcessHandle,
                             PVOID *BaseAddress,
                             ULONG_PTR ZeroBits,
                             PSIZE_T RegionSize,
                             ULONG AllocationType,
                             ULONG Protect)
{
    CHPE_CROSS_PROCESS_CONNECTION Connection = {0};
    PVOID CallbackToken;
    NTSTATUS Status;
    BOOLEAN IsCurrent;
    PVOID Base;
    SIZE_T Size;

    CallbackToken = ChpeEnterEmulatorCallback();
    if (!CallbackToken)
        return ZwAllocateVirtualMemory(ProcessHandle, BaseAddress, ZeroBits, RegionSize, AllocationType, Protect);

    if (!ChpepCaptureRange(BaseAddress, RegionSize, &Base, &Size))
    {
        ChpeLeaveEmulatorCallback(CallbackToken);
        return ZwAllocateVirtualMemory(ProcessHandle, BaseAddress, ZeroBits, RegionSize, AllocationType, Protect);
    }

    if (!Base && (AllocationType & MEM_COMMIT))
        AllocationType |= MEM_RESERVE;

    IsCurrent = ChpeIsCurrentProcessHandle(ProcessHandle);
    if (IsCurrent)
        ChpeNotifyMemoryAlloc(Base, Size, AllocationType, Protect, FALSE, 0);
    else
    {
        ChpepOpenCrossProcessConnection(ProcessHandle, &Connection);
        ChpepSendCrossProcessNotification(&Connection, CrossProcessPreVirtualAlloc, Base, Size, 3, AllocationType, Protect, 0);
    }

    Status = ZwAllocateVirtualMemory(ProcessHandle, BaseAddress, ZeroBits, RegionSize, AllocationType, Protect);
    ChpepCaptureRange(BaseAddress, RegionSize, &Base, &Size);

    if (IsCurrent)
        ChpeNotifyMemoryAlloc(Base, Size, AllocationType, Protect, TRUE, Status);
    else
        ChpepSendCrossProcessNotification(&Connection, CrossProcessPostVirtualAlloc, Base, Size, 3, AllocationType, Protect, Status);

    ChpepCloseCrossProcessConnection(&Connection);
    ChpeLeaveEmulatorCallback(CallbackToken);
    return Status;
}

/*
 * @implemented
 */
static
NTSTATUS
NTAPI
ChpepNtAllocateVirtualMemoryEx(HANDLE ProcessHandle,
                               PVOID *BaseAddress,
                               PSIZE_T RegionSize,
                               ULONG AllocationType,
                               ULONG Protect,
                               PMEM_EXTENDED_PARAMETER ExtendedParameters,
                               ULONG ExtendedParameterCount)
{
    CHPE_CROSS_PROCESS_CONNECTION Connection = {0};
    PVOID CallbackToken;
    NTSTATUS Status;
    BOOLEAN IsCurrent;
    PVOID Base;
    SIZE_T Size;
    BOOLEAN EcCode;

    Status = RtlpGetExtendedParameterZeroBits(ExtendedParameters, ExtendedParameterCount, NULL, &EcCode, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    IsCurrent = ChpeIsCurrentProcessHandle(ProcessHandle);
    CallbackToken = ChpeEnterEmulatorCallback();
    if (!CallbackToken)
    {
        Status = ZwAllocateVirtualMemoryEx(ProcessHandle, BaseAddress, RegionSize, AllocationType, Protect, ExtendedParameters, ExtendedParameterCount);
        if (NT_SUCCESS(Status) && IsCurrent && EcCode)
            ChpeMarkEcCodeRange(*BaseAddress, *RegionSize);
        return Status;
    }

    if (!ChpepCaptureRange(BaseAddress, RegionSize, &Base, &Size))
    {
        ChpeLeaveEmulatorCallback(CallbackToken);
        return ZwAllocateVirtualMemoryEx(ProcessHandle, BaseAddress, RegionSize, AllocationType, Protect, ExtendedParameters, ExtendedParameterCount);
    }

    if (!Base && (AllocationType & MEM_COMMIT))
        AllocationType |= MEM_RESERVE;

    if (IsCurrent)
        ChpeNotifyMemoryAlloc(Base, Size, AllocationType, Protect, FALSE, 0);
    else
    {
        ChpepOpenCrossProcessConnection(ProcessHandle, &Connection);
        ChpepSendCrossProcessNotification(&Connection, CrossProcessPreVirtualAlloc, Base, Size, 3, AllocationType, Protect, 0);
    }

    Status = ZwAllocateVirtualMemoryEx(ProcessHandle, BaseAddress, RegionSize, AllocationType, Protect, ExtendedParameters, ExtendedParameterCount);
    ChpepCaptureRange(BaseAddress, RegionSize, &Base, &Size);
    if (NT_SUCCESS(Status) && IsCurrent && EcCode)
        ChpeMarkEcCodeRange(Base, Size);

    if (IsCurrent)
        ChpeNotifyMemoryAlloc(Base, Size, AllocationType, Protect, TRUE, Status);
    else
        ChpepSendCrossProcessNotification(&Connection, CrossProcessPostVirtualAlloc, Base, Size, 3, AllocationType, Protect, Status);

    ChpepCloseCrossProcessConnection(&Connection);
    ChpeLeaveEmulatorCallback(CallbackToken);
    return Status;
}

/*
 * @implemented
 */
static
NTSTATUS
NTAPI
ChpepNtFreeVirtualMemory(HANDLE ProcessHandle,
                         PVOID *BaseAddress,
                         PSIZE_T RegionSize,
                         ULONG FreeType)
{
    CHPE_CROSS_PROCESS_CONNECTION Connection = {0};
    PVOID CallbackToken;
    NTSTATUS Status;
    BOOLEAN IsCurrent;
    PVOID Base;
    SIZE_T Size;

    CallbackToken = ChpeEnterEmulatorCallback();
    if (!CallbackToken)
        return ZwFreeVirtualMemory(ProcessHandle, BaseAddress, RegionSize, FreeType);

    if (!ChpepCaptureRange(BaseAddress, RegionSize, &Base, &Size))
    {
        ChpeLeaveEmulatorCallback(CallbackToken);
        return ZwFreeVirtualMemory(ProcessHandle, BaseAddress, RegionSize, FreeType);
    }

    IsCurrent = ChpeIsCurrentProcessHandle(ProcessHandle);
    if (IsCurrent)
        ChpeNotifyMemoryFree(Base, Size, FreeType, FALSE, 0);
    else
    {
        ChpepOpenCrossProcessConnection(ProcessHandle, &Connection);
        ChpepSendCrossProcessNotification(&Connection, CrossProcessPreVirtualFree, Base, Size, 2, FreeType, 0, 0);
    }

    Status = ZwFreeVirtualMemory(ProcessHandle, BaseAddress, RegionSize, FreeType);
    ChpepCaptureRange(BaseAddress, RegionSize, &Base, &Size);

    if (IsCurrent)
        ChpeNotifyMemoryFree(Base, Size, FreeType, TRUE, Status);
    else
        ChpepSendCrossProcessNotification(&Connection, CrossProcessPostVirtualFree, Base, Size, 2, FreeType, Status, 0);

    ChpepCloseCrossProcessConnection(&Connection);
    ChpeLeaveEmulatorCallback(CallbackToken);
    return Status;
}

/*
 * @implemented
 */
static
NTSTATUS
NTAPI
ChpepNtProtectVirtualMemory(HANDLE ProcessHandle,
                            PVOID *BaseAddress,
                            PSIZE_T RegionSize,
                            ULONG NewProtect,
                            PULONG OldProtect)
{
    CHPE_CROSS_PROCESS_CONNECTION Connection = {0};
    PVOID CallbackToken;
    NTSTATUS Status;
    BOOLEAN IsCurrent;
    PVOID Base;
    SIZE_T Size;

    CallbackToken = ChpeEnterEmulatorCallback();
    if (!CallbackToken)
        return ZwProtectVirtualMemory(ProcessHandle, BaseAddress, RegionSize, NewProtect, OldProtect);

    if (!ChpepCaptureRange(BaseAddress, RegionSize, &Base, &Size))
    {
        ChpeLeaveEmulatorCallback(CallbackToken);
        return ZwProtectVirtualMemory(ProcessHandle, BaseAddress, RegionSize, NewProtect, OldProtect);
    }

    IsCurrent = ChpeIsCurrentProcessHandle(ProcessHandle);
    if (IsCurrent)
        ChpeNotifyMemoryProtect(Base, Size, NewProtect, FALSE, 0);
    else
    {
        ChpepOpenCrossProcessConnection(ProcessHandle, &Connection);
        ChpepSendCrossProcessNotification(&Connection, CrossProcessPreVirtualProtect, Base, Size, 2, NewProtect, 0, 0);
    }

    Status = ZwProtectVirtualMemory(ProcessHandle, BaseAddress, RegionSize, NewProtect, OldProtect);
    ChpepCaptureRange(BaseAddress, RegionSize, &Base, &Size);

    if (IsCurrent)
        ChpeNotifyMemoryProtect(Base, Size, NewProtect, TRUE, Status);
    else
        ChpepSendCrossProcessNotification(&Connection, CrossProcessPostVirtualProtect, Base, Size, 2, NewProtect, Status, 0);

    ChpepCloseCrossProcessConnection(&Connection);
    ChpeLeaveEmulatorCallback(CallbackToken);
    return Status;
}

/*
 * @implemented
 */
static
NTSTATUS
NTAPI
ChpepNtMapViewOfSection(HANDLE SectionHandle,
                        HANDLE ProcessHandle,
                        PVOID *BaseAddress,
                        ULONG_PTR ZeroBits,
                        SIZE_T CommitSize,
                        PLARGE_INTEGER SectionOffset,
                        PSIZE_T ViewSize,
                        SECTION_INHERIT InheritDisposition,
                        ULONG AllocationType,
                        ULONG Protect)
{
    PVOID CallbackToken;
    NTSTATUS Status;
    NTSTATUS NotifyStatus;
    BOOLEAN IsCurrent = ChpeIsCurrentProcessHandle(ProcessHandle);

    Status = ZwMapViewOfSection(SectionHandle, ProcessHandle, BaseAddress, ZeroBits, CommitSize, SectionOffset, ViewSize, InheritDisposition, AllocationType, Protect);

    if (!IsCurrent || !NT_SUCCESS(Status))
        return Status;

    CallbackToken = ChpeEnterEmulatorCallback();
    if (!CallbackToken)
        return Status;

    NotifyStatus = ChpeNotifyMapViewOfSection(NULL, *BaseAddress, NULL, ViewSize ? *ViewSize : 0, AllocationType, Protect);
    if (!NT_SUCCESS(NotifyStatus))
    {
        ChpeNotifyUnmapViewOfSection(*BaseAddress, FALSE, 0);
        Status = ZwUnmapViewOfSection(ProcessHandle, *BaseAddress);
        ChpeNotifyUnmapViewOfSection(*BaseAddress, TRUE, Status);
        Status = NotifyStatus;
    }

    ChpeLeaveEmulatorCallback(CallbackToken);
    return Status;
}

/*
 * @implemented
 */
static
NTSTATUS
NTAPI
ChpepNtUnmapViewOfSection(HANDLE ProcessHandle, PVOID BaseAddress)
{
    PVOID CallbackToken;
    NTSTATUS Status;

    if (!ChpeIsCurrentProcessHandle(ProcessHandle))
        return ZwUnmapViewOfSection(ProcessHandle, BaseAddress);

    CallbackToken = ChpeEnterEmulatorCallback();
    if (!CallbackToken)
        return ZwUnmapViewOfSection(ProcessHandle, BaseAddress);

    ChpeNotifyUnmapViewOfSection(BaseAddress, FALSE, 0);
    Status = ZwUnmapViewOfSection(ProcessHandle, BaseAddress);
    ChpeNotifyUnmapViewOfSection(BaseAddress, TRUE, Status);
    ChpeLeaveEmulatorCallback(CallbackToken);
    return Status;
}

/*
 * @implemented
 */
static
NTSTATUS
NTAPI
ChpepNtFlushInstructionCache(HANDLE ProcessHandle,
                             PVOID BaseAddress,
                             SIZE_T NumberOfBytesToFlush)
{
    CHPE_CROSS_PROCESS_CONNECTION Connection = {0};
    PVOID CallbackToken;
    NTSTATUS Status;
    BOOLEAN IsCurrent;

    Status = ZwFlushInstructionCache(ProcessHandle, BaseAddress, NumberOfBytesToFlush);
    if (!NT_SUCCESS(Status))
        return Status;

    CallbackToken = ChpeEnterEmulatorCallback();
    if (!CallbackToken)
        return Status;

    IsCurrent = ChpeIsCurrentProcessHandle(ProcessHandle);
    if (IsCurrent)
        ChpeFlushInstructionCache(BaseAddress, NumberOfBytesToFlush);
    else
    {
        ChpepOpenCrossProcessConnection(ProcessHandle, &Connection);
        ChpepSendCrossProcessNotification(&Connection, CrossProcessFlushCache, BaseAddress, NumberOfBytesToFlush, 0, 0, 0, 0);
        ChpepCloseCrossProcessConnection(&Connection);
    }

    ChpeLeaveEmulatorCallback(CallbackToken);
    return Status;
}

static
NTSTATUS
NTAPI
ChpepNtReadFile(HANDLE FileHandle,
                HANDLE Event,
                PIO_APC_ROUTINE ApcRoutine,
                PVOID ApcContext,
                PIO_STATUS_BLOCK IoStatusBlock,
                PVOID Buffer,
                ULONG Length,
                PLARGE_INTEGER ByteOffset,
                PULONG Key)
{
    PVOID CallbackToken;
    NTSTATUS Status;

    CallbackToken = ChpeEnterEmulatorCallback();
    if (!CallbackToken)
        return ZwReadFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, ByteOffset, Key);

    ChpeNotifyReadFile(FileHandle, Buffer, Length, FALSE, 0);
    Status = ZwReadFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, ByteOffset, Key);
    ChpeNotifyReadFile(FileHandle, Buffer, Length, TRUE, Status);
    ChpeLeaveEmulatorCallback(CallbackToken);
    return Status;
}

static
NTSTATUS
NTAPI
ChpepNtQuerySystemInformation(SYSTEM_INFORMATION_CLASS SystemInformationClass,
                              PVOID SystemInformation,
                              ULONG SystemInformationLength,
                              PULONG ReturnLength)
{
    NTSTATUS Status;

    Status = ZwQuerySystemInformation(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
    if (NT_SUCCESS(Status) && SystemInformationClass == SystemProcessorInformation)
        ChpeUpdateProcessorInformation(SystemInformation);

    return Status;
}

static
NTSTATUS
NTAPI
ChpepNtTerminateProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus)
{
    PVOID CallbackToken;
    NTSTATUS Status;

    if (ProcessHandle)
        return ZwTerminateProcess(ProcessHandle, ExitStatus);

    CallbackToken = ChpeEnterEmulatorCallback();
    if (!CallbackToken)
        return ZwTerminateProcess(ProcessHandle, ExitStatus);

    ChpeNotifyProcessTermination(ProcessHandle, FALSE, 0);
    Status = ZwTerminateProcess(ProcessHandle, ExitStatus);
    ChpeNotifyProcessTermination(ProcessHandle, TRUE, Status);
    ChpeLeaveEmulatorCallback(CallbackToken);
    return Status;
}

static
NTSTATUS
NTAPI
ChpepNtTerminateThread(HANDLE ThreadHandle, NTSTATUS ExitStatus)
{
    PVOID CallbackToken;
    NTSTATUS Status;

    CallbackToken = ChpeEnterEmulatorCallback();
    if (!CallbackToken)
        return ZwTerminateThread(ThreadHandle, ExitStatus);

    Status = ChpePrepareThreadTermination(ThreadHandle, ExitStatus);
    if (!NT_SUCCESS(Status))
        DPRINT1("CHPE: ThreadTerm failed before NtTerminateThread, Status = 0x%08lx\n", Status);

    Status = ZwTerminateThread(ThreadHandle, ExitStatus);

    ChpeLeaveEmulatorCallback(CallbackToken);
    return Status;
}

/*
 * Keep native NTDLL service entries interceptable in a suspended ARM64 child.
 * A CHPE process installs its notification wrappers before loading the emulator;
 * separate Zw entries remain unpatched for the wrappers and FEX's direct calls.
 */
NTSTATUS
NTAPI
ChpeInitializeSyscallWrappers(VOID)
{
    static const struct
    {
        PVOID Entry;
        PVOID Wrapper;
    } Wrappers[] = {
        {(PVOID)NtAllocateVirtualMemory, (PVOID)ChpepNtAllocateVirtualMemory},
        {(PVOID)NtAllocateVirtualMemoryEx, (PVOID)ChpepNtAllocateVirtualMemoryEx},
        {(PVOID)NtFreeVirtualMemory, (PVOID)ChpepNtFreeVirtualMemory},
        {(PVOID)NtProtectVirtualMemory, (PVOID)ChpepNtProtectVirtualMemory},
        {(PVOID)NtMapViewOfSection, (PVOID)ChpepNtMapViewOfSection},
        {(PVOID)NtUnmapViewOfSection, (PVOID)ChpepNtUnmapViewOfSection},
        {(PVOID)NtFlushInstructionCache, (PVOID)ChpepNtFlushInstructionCache},
        {(PVOID)NtReadFile, (PVOID)ChpepNtReadFile},
        {(PVOID)NtQuerySystemInformation, (PVOID)ChpepNtQuerySystemInformation},
        {(PVOID)NtTerminateProcess, (PVOID)ChpepNtTerminateProcess},
        {(PVOID)NtTerminateThread, (PVOID)ChpepNtTerminateThread},
    };
    struct
    {
        ULONG LoadTarget;
        ULONG BranchTarget;
        ULONG_PTR Target;
    } Patch = {0x58000050, 0xD61F0200, 0}; /* ldr x16, +8; br x16; address */
    PVOID Base;
    SIZE_T Size;
    ULONG Index, OldProtect, IgnoredProtect;
    NTSTATUS Status, FlushStatus;

    C_ASSERT(sizeof(Patch) == 16);
    for (Index = 0; Index < RTL_NUMBER_OF(Wrappers); ++Index)
    {
        Base = Wrappers[Index].Entry;
        Size = sizeof(Patch);
        Status = ZwProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, PAGE_EXECUTE_READWRITE, &OldProtect);
        if (!NT_SUCCESS(Status))
            return Status;

        Patch.Target = (ULONG_PTR)Wrappers[Index].Wrapper;
        RtlCopyMemory(Wrappers[Index].Entry, &Patch, sizeof(Patch));
        FlushStatus = ZwFlushInstructionCache(NtCurrentProcess(), Wrappers[Index].Entry, sizeof(Patch));
        Status = ZwProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, OldProtect, &IgnoredProtect);
        if (!NT_SUCCESS(Status))
            return Status;
        if (!NT_SUCCESS(FlushStatus))
            return FlushStatus;
    }

    return STATUS_SUCCESS;
}

#endif /* _M_ARM64 */
