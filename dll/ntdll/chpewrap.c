/*
 * PROJECT:         ReactOS NT Library
 * FILE:            dll/ntdll/chpewrap.c
 * PURPOSE:         CHPE-wrapped Nt* syscall implementations
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * COPYRIGHT:       Copyright 2023 Alexandre Julliard
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * SVC_WRAP_ emits only the raw Zw* syscall stubs for the selected ARM64
 * services.  This file provides the corresponding Nt* entry points and
 * brackets address-space changes with CHPE emulator notifications.
 * Callback ordering and cross-process notifications are adapted from Wine's
 * dlls/ntdll/signal_arm64ec.c.
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64)

static
BOOLEAN
ChpeIsCurrentProcessHandle(HANDLE ProcessHandle)
{
    PROCESS_BASIC_INFORMATION ProcessInfo;
    NTSTATUS Status;

    if (ProcessHandle == NtCurrentProcess())
        return TRUE;

    Status = ZwQueryInformationProcess(ProcessHandle, ProcessBasicInformation, &ProcessInfo, sizeof(ProcessInfo), NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;

    return ProcessInfo.UniqueProcessId == (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtAllocateVirtualMemory(HANDLE ProcessHandle,
                        PVOID *BaseAddress,
                        ULONG_PTR ZeroBits,
                        PSIZE_T RegionSize,
                        ULONG AllocationType,
                        ULONG Protect)
{
    NTSTATUS Status;
    BOOLEAN Notify = ChpeIsCurrentProcessHandle(ProcessHandle);

    if (Notify)
        ChpeNotifyMemoryAlloc(*BaseAddress, *RegionSize, AllocationType, Protect, FALSE, 0);

    Status = ZwAllocateVirtualMemory(ProcessHandle, BaseAddress, ZeroBits, RegionSize, AllocationType, Protect);

    if (Notify)
        ChpeNotifyMemoryAlloc(*BaseAddress, *RegionSize, AllocationType, Protect, TRUE, Status);

    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtFreeVirtualMemory(HANDLE ProcessHandle,
                    PVOID *BaseAddress,
                    PSIZE_T RegionSize,
                    ULONG FreeType)
{
    NTSTATUS Status;
    BOOLEAN Notify = ChpeIsCurrentProcessHandle(ProcessHandle);

    if (Notify)
        ChpeNotifyMemoryFree(*BaseAddress, *RegionSize, FreeType, FALSE, 0);

    Status = ZwFreeVirtualMemory(ProcessHandle, BaseAddress, RegionSize, FreeType);

    if (Notify)
        ChpeNotifyMemoryFree(*BaseAddress, *RegionSize, FreeType, TRUE, Status);

    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtProtectVirtualMemory(HANDLE ProcessHandle,
                       PVOID *BaseAddress,
                       PSIZE_T RegionSize,
                       ULONG NewProtect,
                       PULONG OldProtect)
{
    NTSTATUS Status;
    BOOLEAN Notify = ChpeIsCurrentProcessHandle(ProcessHandle);

    if (Notify)
        ChpeNotifyMemoryProtect(*BaseAddress, *RegionSize, NewProtect, FALSE, 0);

    Status = ZwProtectVirtualMemory(ProcessHandle, BaseAddress, RegionSize, NewProtect, OldProtect);

    if (Notify)
        ChpeNotifyMemoryProtect(*BaseAddress, *RegionSize, NewProtect, TRUE, Status);

    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtMapViewOfSection(HANDLE SectionHandle,
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
    NTSTATUS Status;
    BOOLEAN Notify = ChpeIsCurrentProcessHandle(ProcessHandle);

    Status = ZwMapViewOfSection(SectionHandle, ProcessHandle, BaseAddress, ZeroBits, CommitSize, SectionOffset, ViewSize, InheritDisposition, AllocationType, Protect);

    if (Notify && NT_SUCCESS(Status))
    {
        ChpeNotifyMapViewOfSection(NULL, *BaseAddress, NULL,
                                   ViewSize ? *ViewSize : 0,
                                   AllocationType, Protect);
    }

    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtUnmapViewOfSection(HANDLE ProcessHandle, PVOID BaseAddress)
{
    NTSTATUS Status;
    BOOLEAN Notify = ChpeIsCurrentProcessHandle(ProcessHandle);

    if (Notify)
        ChpeNotifyUnmapViewOfSection(BaseAddress, FALSE, 0);

    Status = ZwUnmapViewOfSection(ProcessHandle, BaseAddress);

    if (Notify)
        ChpeNotifyUnmapViewOfSection(BaseAddress, TRUE, Status);

    return Status;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtFlushInstructionCache(HANDLE ProcessHandle,
                        PVOID BaseAddress,
                        SIZE_T NumberOfBytesToFlush)
{
    NTSTATUS Status;
    BOOLEAN Notify = ChpeIsCurrentProcessHandle(ProcessHandle);

    Status = ZwFlushInstructionCache(ProcessHandle, BaseAddress, NumberOfBytesToFlush);

    if (Notify && NT_SUCCESS(Status))
    {
        ChpeFlushInstructionCache(BaseAddress, NumberOfBytesToFlush);
    }

    return Status;
}

NTSTATUS
NTAPI
NtReadFile(HANDLE FileHandle,
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

NTSTATUS
NTAPI
NtQuerySystemInformation(SYSTEM_INFORMATION_CLASS SystemInformationClass,
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

NTSTATUS
NTAPI
NtTerminateProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus)
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

NTSTATUS
NTAPI
NtTerminateThread(HANDLE ThreadHandle, NTSTATUS ExitStatus)
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

#endif /* _M_ARM64 */
