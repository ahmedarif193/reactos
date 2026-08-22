/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Process Snapshotting process-information support
 */

#include <stdarg.h>
#include <string.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winternl.h>

#include "wine/kernelbase.h"

#define PSS_CAPTURE_NONE 0
#define PSS_QUERY_PROCESS_INFORMATION 0
#define PSS_PROCESS_FLAGS_WOW64 0x00000002

typedef struct _PSS_PROCESS_INFORMATION_ROS
{
    DWORD ExitStatus;
    void *PebBaseAddress;
    ULONG_PTR AffinityMask;
    LONG BasePriority;
    DWORD ProcessId;
    DWORD ParentProcessId;
    DWORD Flags;
    FILETIME CreateTime;
    FILETIME ExitTime;
    FILETIME KernelTime;
    FILETIME UserTime;
    DWORD PriorityClass;
    ULONG_PTR PeakVirtualSize;
    ULONG_PTR VirtualSize;
    DWORD PageFaultCount;
    ULONG_PTR PeakWorkingSetSize;
    ULONG_PTR WorkingSetSize;
    ULONG_PTR QuotaPeakPagedPoolUsage;
    ULONG_PTR QuotaPagedPoolUsage;
    ULONG_PTR QuotaPeakNonPagedPoolUsage;
    ULONG_PTR QuotaNonPagedPoolUsage;
    ULONG_PTR PagefileUsage;
    ULONG_PTR PeakPagefileUsage;
    ULONG_PTR PrivateUsage;
    DWORD ExecuteFlags;
    WCHAR ImageFileName[MAX_PATH];
} PSS_PROCESS_INFORMATION_ROS;

#ifdef _WIN64
C_ASSERT(sizeof(PSS_PROCESS_INFORMATION_ROS) == 704);
C_ASSERT(FIELD_OFFSET(PSS_PROCESS_INFORMATION_ROS, ParentProcessId) == 32);
C_ASSERT(FIELD_OFFSET(PSS_PROCESS_INFORMATION_ROS, ImageFileName) == 180);
#endif

typedef struct _PSS_SNAPSHOT_ROS
{
    struct _PSS_SNAPSHOT_ROS *Next;
    DWORD ProcessId;
    PSS_PROCESS_INFORMATION_ROS ProcessInfo;
} PSS_SNAPSHOT_ROS;

static RTL_SRWLOCK PssSnapshotLock = RTL_SRWLOCK_INIT;
static PSS_SNAPSHOT_ROS *PssSnapshots;

static PSS_SNAPSHOT_ROS *
PssFindSnapshot(
    _In_ HANDLE Handle)
{
    PSS_SNAPSHOT_ROS *Snapshot;

    for (Snapshot = PssSnapshots; Snapshot; Snapshot = Snapshot->Next)
        if ((HANDLE)Snapshot == Handle) return Snapshot;
    return NULL;
}

DWORD
WINAPI
PssQuerySnapshot(
    _In_ HANDLE SnapshotHandle,
    _In_ DWORD InformationClass,
    _Out_writes_bytes_(BufferLength) PVOID Buffer,
    _In_ DWORD BufferLength)
{
    PSS_SNAPSHOT_ROS *Snapshot;
    DWORD Error = ERROR_SUCCESS;

    if (InformationClass != PSS_QUERY_PROCESS_INFORMATION)
        return ERROR_INVALID_PARAMETER;
    if (BufferLength < sizeof(PSS_PROCESS_INFORMATION_ROS))
        return ERROR_BAD_LENGTH;

    RtlAcquireSRWLockShared(&PssSnapshotLock);
    Snapshot = PssFindSnapshot(SnapshotHandle);
    if (Snapshot)
        RtlCopyMemory(Buffer, &Snapshot->ProcessInfo, sizeof(Snapshot->ProcessInfo));
    else
        Error = ERROR_INVALID_HANDLE;
    RtlReleaseSRWLockShared(&PssSnapshotLock);
    return Error;
}

DWORD
WINAPI
PssFreeSnapshot(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE SnapshotHandle)
{
    PROCESS_BASIC_INFORMATION BasicInfo;
    PSS_SNAPSHOT_ROS **Link;
    PSS_SNAPSHOT_ROS *Snapshot;
    NTSTATUS Status;

    Status = NtQueryInformationProcess(ProcessHandle, ProcessBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    RtlAcquireSRWLockExclusive(&PssSnapshotLock);
    for (Link = &PssSnapshots; (Snapshot = *Link); Link = &Snapshot->Next)
    {
        if ((HANDLE)Snapshot != SnapshotHandle)
            continue;
        if (Snapshot->ProcessId != (DWORD)BasicInfo.UniqueProcessId)
        {
            RtlReleaseSRWLockExclusive(&PssSnapshotLock);
            return ERROR_INVALID_HANDLE;
        }
        *Link = Snapshot->Next;
        RtlReleaseSRWLockExclusive(&PssSnapshotLock);
        RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Snapshot);
        return ERROR_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&PssSnapshotLock);
    return ERROR_INVALID_HANDLE;
}

DWORD
WINAPI
PssCaptureSnapshot(
    _In_ HANDLE ProcessHandle,
    _In_ DWORD CaptureFlags,
    _In_opt_ DWORD ThreadContextFlags,
    _Out_ HANDLE *SnapshotHandle)
{
    PSS_PROCESS_INFORMATION_ROS *Info;
    PROCESS_BASIC_INFORMATION BasicInfo;
    PSS_SNAPSHOT_ROS *Snapshot;
    KERNEL_USER_TIMES Times;
    VM_COUNTERS_EX Counters;
    ULONG_PTR Wow64Info = 0;
    ULONG ExecuteFlags = 0;
    DWORD ImageLength = MAX_PATH;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ThreadContextFlags);

    if (SnapshotHandle)
        *SnapshotHandle = NULL;
    if (!SnapshotHandle)
        return ERROR_INVALID_PARAMETER;
    if (CaptureFlags != PSS_CAPTURE_NONE)
        return ERROR_NOT_SUPPORTED;

    Status = NtQueryInformationProcess(ProcessHandle, ProcessBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);
    Status = NtQueryInformationProcess(ProcessHandle, ProcessTimes, &Times, sizeof(Times), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);
    Status = NtQueryInformationProcess(ProcessHandle, ProcessVmCounters, &Counters, sizeof(Counters), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    Snapshot = RtlAllocateHeap(NtCurrentTeb()->Peb->ProcessHeap, HEAP_ZERO_MEMORY, sizeof(*Snapshot));
    if (!Snapshot)
        return ERROR_NOT_ENOUGH_MEMORY;

    Info = &Snapshot->ProcessInfo;
    Info->ExitStatus = BasicInfo.ExitStatus;
    Info->PebBaseAddress = BasicInfo.PebBaseAddress;
    Info->AffinityMask = BasicInfo.AffinityMask;
    Info->BasePriority = BasicInfo.BasePriority;
    Info->ProcessId = (DWORD)BasicInfo.UniqueProcessId;
    Info->ParentProcessId = (DWORD)BasicInfo.InheritedFromUniqueProcessId;
    Info->CreateTime.dwLowDateTime = Times.CreateTime.u.LowPart;
    Info->CreateTime.dwHighDateTime = Times.CreateTime.u.HighPart;
    Info->ExitTime.dwLowDateTime = Times.ExitTime.u.LowPart;
    Info->ExitTime.dwHighDateTime = Times.ExitTime.u.HighPart;
    Info->KernelTime.dwLowDateTime = Times.KernelTime.u.LowPart;
    Info->KernelTime.dwHighDateTime = Times.KernelTime.u.HighPart;
    Info->UserTime.dwLowDateTime = Times.UserTime.u.LowPart;
    Info->UserTime.dwHighDateTime = Times.UserTime.u.HighPart;
    Info->PriorityClass = GetPriorityClass(ProcessHandle);
    Info->PeakVirtualSize = Counters.PeakVirtualSize;
    Info->VirtualSize = Counters.VirtualSize;
    Info->PageFaultCount = Counters.PageFaultCount;
    Info->PeakWorkingSetSize = Counters.PeakWorkingSetSize;
    Info->WorkingSetSize = Counters.WorkingSetSize;
    Info->QuotaPeakPagedPoolUsage = Counters.QuotaPeakPagedPoolUsage;
    Info->QuotaPagedPoolUsage = Counters.QuotaPagedPoolUsage;
    Info->QuotaPeakNonPagedPoolUsage = Counters.QuotaPeakNonPagedPoolUsage;
    Info->QuotaNonPagedPoolUsage = Counters.QuotaNonPagedPoolUsage;
    Info->PagefileUsage = Counters.PagefileUsage;
    Info->PeakPagefileUsage = Counters.PeakPagefileUsage;
    Info->PrivateUsage = Counters.PrivateUsage;
    Status = NtQueryInformationProcess(ProcessHandle, ProcessExecuteFlags, &ExecuteFlags, sizeof(ExecuteFlags), NULL);
    if (NT_SUCCESS(Status))
        Info->ExecuteFlags = ExecuteFlags;
    Status = NtQueryInformationProcess(ProcessHandle, ProcessWow64Information, &Wow64Info, sizeof(Wow64Info), NULL);
    if (NT_SUCCESS(Status) && Wow64Info)
        Info->Flags |= PSS_PROCESS_FLAGS_WOW64;
    if (!QueryFullProcessImageNameW(ProcessHandle, 0, Info->ImageFileName, &ImageLength))
    {
        DWORD Error = GetLastError();

        RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Snapshot);
        return Error;
    }

    Snapshot->ProcessId = Info->ProcessId;
    RtlAcquireSRWLockExclusive(&PssSnapshotLock);
    Snapshot->Next = PssSnapshots;
    PssSnapshots = Snapshot;
    RtlReleaseSRWLockExclusive(&PssSnapshotLock);
    *SnapshotHandle = (HANDLE)Snapshot;
    return ERROR_SUCCESS;
}
