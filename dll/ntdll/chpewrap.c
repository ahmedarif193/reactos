/*
 * PROJECT:         ReactOS NT Library
 * FILE:            dll/ntdll/chpewrap.c
 * PURPOSE:         CHPE-wrapped Nt* syscall implementations
 *
 * SVC_WRAP_ emits only the raw Zw* syscall stubs for the selected ARM64
 * services.  This file provides the corresponding Nt* entry points and
 * brackets address-space changes with CHPE emulator notifications.
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64)

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

    ChpeNotifyMemoryAlloc(*BaseAddress, *RegionSize, AllocationType, Protect, FALSE, 0);

    Status = ZwAllocateVirtualMemory(ProcessHandle, BaseAddress, ZeroBits,
                                     RegionSize, AllocationType, Protect);

    ChpeNotifyMemoryAlloc(*BaseAddress, *RegionSize, AllocationType,
                          Protect, TRUE, Status);

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

    ChpeNotifyMemoryFree(*BaseAddress, *RegionSize, FreeType, FALSE, 0);

    Status = ZwFreeVirtualMemory(ProcessHandle, BaseAddress, RegionSize, FreeType);

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

    ChpeNotifyMemoryProtect(*BaseAddress, *RegionSize, NewProtect, FALSE, 0);

    Status = ZwProtectVirtualMemory(ProcessHandle, BaseAddress, RegionSize,
                                    NewProtect, OldProtect);

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

    Status = ZwMapViewOfSection(SectionHandle, ProcessHandle, BaseAddress,
                                ZeroBits, CommitSize, SectionOffset, ViewSize,
                                InheritDisposition, AllocationType, Protect);

    if (NT_SUCCESS(Status))
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

    ChpeNotifyUnmapViewOfSection(BaseAddress, FALSE, 0);

    Status = ZwUnmapViewOfSection(ProcessHandle, BaseAddress);

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

    Status = ZwFlushInstructionCache(ProcessHandle, BaseAddress, NumberOfBytesToFlush);

    if (NT_SUCCESS(Status))
    {
        ChpeFlushInstructionCache(BaseAddress, NumberOfBytesToFlush);
    }

    return Status;
}

#endif /* _M_ARM64 */
