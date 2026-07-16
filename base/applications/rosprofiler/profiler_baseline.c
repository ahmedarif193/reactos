/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Bounded target rundown using documented Toolhelp APIs
 */

#include "profiler_baseline.h"
#include "profiler_pe.h"

#include <tlhelp32.h>

#define RPERF_SYSTEM_MODULE_INITIAL_BYTES (64 * 1024)
#define RPERF_SYSTEM_MODULE_MAX_BYTES     (16 * 1024 * 1024)
#define RPERF_SYSTEM_MODULE_INFORMATION_CLASS 11
#define RPERF_STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#define RPERF_STATUS_BUFFER_TOO_SMALL     ((NTSTATUS)0xC0000023L)

NTSYSAPI
NTSTATUS
NTAPI
NtQuerySystemInformation(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG InformationLength,
    PULONG ResultLength);

NTSYSAPI
ULONG
NTAPI
RtlNtStatusToDosError(NTSTATUS Status);

typedef struct _RPERF_SYSTEM_MODULE
{
    ULONG Section;
    PVOID MappedBase;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    CHAR FullPathName[256];
} RPERF_SYSTEM_MODULE, *PRPERF_SYSTEM_MODULE;

typedef struct _RPERF_SYSTEM_MODULES
{
    ULONG NumberOfModules;
    RPERF_SYSTEM_MODULE Modules[ANYSIZE_ARRAY];
} RPERF_SYSTEM_MODULES, *PRPERF_SYSTEM_MODULES;

static ULONGLONG
RperfBaselineStableId(ULONG NumericId,
                      const FILETIME *Creation,
                      ULONGLONG Salt)
{
    ULARGE_INTEGER Value;
    ULONGLONG Result;

    Value.LowPart = Creation != NULL ? Creation->dwLowDateTime : 0;
    Value.HighPart = Creation != NULL ? Creation->dwHighDateTime : 0;
    Result = Value.QuadPart ^ ((ULONGLONG)NumericId << 32) ^
             NumericId ^ Salt;
    return Result != 0 ? Result : NumericId;
}

static BOOL
RperfBaselineSequence(ULONGLONG *NextSequence,
                      RPERF_RECORD *Record)
{
    if (*NextSequence == 0 || *NextSequence == (ULONGLONG)-1)
    {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }
    Record->Header.Sequence = (*NextSequence)++;
    return TRUE;
}

static ULONG
RperfBaselineParentProcess(ULONG ProcessId,
                           RPERF_BASELINE_RESULT *Result)
{
    HANDLE Snapshot;
    PROCESSENTRY32W Entry;
    ULONG Parent = 0;

    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
    {
        Result->Partial = TRUE;
        Result->Status = GetLastError();
        return 0;
    }
    ZeroMemory(&Entry, sizeof(Entry));
    Entry.dwSize = sizeof(Entry);
    if (Process32FirstW(Snapshot, &Entry))
    {
        do
        {
            if (Entry.th32ProcessID == ProcessId)
            {
                Parent = Entry.th32ParentProcessID;
                break;
            }
            Entry.dwSize = sizeof(Entry);
        } while (Process32NextW(Snapshot, &Entry));
    }
    CloseHandle(Snapshot);
    return Parent;
}

static ULONGLONG
RperfBaselineProcessKey(ULONG ProcessId,
                        RPERF_BASELINE_RESULT *Result)
{
    HANDLE Process;
    FILETIME Creation, Exit, Kernel, User;
    ULONGLONG Key;

    Process = OpenProcess(PROCESS_QUERY_INFORMATION,
                          FALSE, ProcessId);
    if (Process == NULL)
    {
        Result->Partial = TRUE;
        Result->Status = GetLastError();
        return RperfBaselineStableId(ProcessId, NULL,
                                     0x50524f4345535300ULL);
    }
    if (GetProcessTimes(Process, &Creation, &Exit, &Kernel, &User))
        Key = RperfBaselineStableId(ProcessId, &Creation,
                                    0x50524f4345535300ULL);
    else
    {
        Result->Partial = TRUE;
        Result->Status = GetLastError();
        Key = RperfBaselineStableId(ProcessId, NULL,
                                    0x50524f4345535300ULL);
    }
    CloseHandle(Process);
    return Key;
}

static ULONGLONG
RperfBaselineThreadKey(ULONG ThreadId,
                       RPERF_BASELINE_RESULT *Result)
{
    HANDLE Thread;
    FILETIME Creation, Exit, Kernel, User;
    ULONGLONG Key;

    Thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, ThreadId);
    if (Thread == NULL)
    {
        Result->Partial = TRUE;
        Result->Status = GetLastError();
        return RperfBaselineStableId(ThreadId, NULL,
                                     0x5448524541440000ULL);
    }
    if (GetThreadTimes(Thread, &Creation, &Exit, &Kernel, &User))
        Key = RperfBaselineStableId(ThreadId, &Creation,
                                    0x5448524541440000ULL);
    else
    {
        Result->Partial = TRUE;
        Result->Status = GetLastError();
        Key = RperfBaselineStableId(ThreadId, NULL,
                                    0x5448524541440000ULL);
    }
    CloseHandle(Thread);
    return Key;
}

static BOOL
RperfBaselineAddProcess(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                        RPERF_RECORDING *Recording,
                        ULONGLONG TimestampNs,
                        ULONGLONG *NextSequence,
                        ULONGLONG ProcessKey,
                        RPERF_BASELINE_ID_CALLBACK IdCallback,
                        PVOID IdContext,
                        RPERF_BASELINE_RESULT *Result)
{
    RPERF_RECORD Record;

    ZeroMemory(&Record, sizeof(Record));
    Record.Header.Kind = RperfRecordProcessStart;
    Record.Header.Flags = RPERF_MODEL_RECORD_FLAG_SYNTHETIC;
    Record.Header.TimestampNs = TimestampNs;
    Record.Header.ProcessId = Configuration->ProcessId;
    Record.Header.ProcessKey = ProcessKey;
    Record.Header.Cpu = RPERF_MODEL_ALL_CPUS;
    Record.Data.Lifecycle.ObjectId = ProcessKey;
    Record.Data.Lifecycle.ParentId =
        RperfBaselineParentProcess(Configuration->ProcessId, Result);
    if (!RperfBaselineSequence(NextSequence, &Record) ||
        !RperfRecordingAddRecord(Recording, &Record))
        return FALSE;
    if (IdCallback != NULL &&
        !IdCallback(IdContext, FALSE,
                    Configuration->ProcessId, ProcessKey))
        return FALSE;
    Result->Processes++;
    return TRUE;
}

static BOOL
RperfBaselineAddThreads(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                        RPERF_RECORDING *Recording,
                        ULONGLONG TimestampNs,
                        ULONGLONG *NextSequence,
                        ULONGLONG ProcessKey,
                        RPERF_BASELINE_ID_CALLBACK IdCallback,
                        PVOID IdContext,
                        RPERF_BASELINE_RESULT *Result)
{
    HANDLE Snapshot;
    THREADENTRY32 Entry;

    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
    {
        Result->Partial = TRUE;
        Result->Status = GetLastError();
        return TRUE;
    }
    ZeroMemory(&Entry, sizeof(Entry));
    Entry.dwSize = sizeof(Entry);
    if (Thread32First(Snapshot, &Entry))
    {
        do
        {
            RPERF_RECORD Record;
            ULONGLONG ThreadKey;

            if (Entry.th32OwnerProcessID != Configuration->ProcessId)
            {
                Entry.dwSize = sizeof(Entry);
                continue;
            }
            if (Result->Threads >= Configuration->Limits.MaxThreads)
            {
                Result->Partial = TRUE;
                Result->Status = ERROR_BUFFER_OVERFLOW;
                break;
            }
            ThreadKey = RperfBaselineThreadKey(Entry.th32ThreadID, Result);
            ZeroMemory(&Record, sizeof(Record));
            Record.Header.Kind = RperfRecordThreadStart;
            Record.Header.Flags = RPERF_MODEL_RECORD_FLAG_SYNTHETIC;
            Record.Header.TimestampNs = TimestampNs;
            Record.Header.ProcessId = Configuration->ProcessId;
            Record.Header.ThreadId = Entry.th32ThreadID;
            Record.Header.ProcessKey = ProcessKey;
            Record.Header.ThreadKey = ThreadKey;
            Record.Header.Cpu = RPERF_MODEL_ALL_CPUS;
            Record.Data.Lifecycle.ObjectId = ThreadKey;
            Record.Data.Lifecycle.ParentId = ProcessKey;
            if (!RperfBaselineSequence(NextSequence, &Record) ||
                !RperfRecordingAddRecord(Recording, &Record) ||
                (IdCallback != NULL &&
                 !IdCallback(IdContext, TRUE, Entry.th32ThreadID,
                             ThreadKey)))
            {
                CloseHandle(Snapshot);
                return FALSE;
            }
            Result->Threads++;
            Entry.dwSize = sizeof(Entry);
        } while (Thread32Next(Snapshot, &Entry));
    }
    else
    {
        Result->Partial = TRUE;
        Result->Status = GetLastError();
    }
    CloseHandle(Snapshot);
    return TRUE;
}

static ULONGLONG
RperfBaselineModuleId(const RPERF_RECORDING *Recording,
                      ULONGLONG ProcessKey,
                      ULONGLONG Base,
                      ULONGLONG Size)
{
    ULONGLONG Id = Base ^ (ProcessKey << 1) ^ (Size << 17) ^
                   0x4d4f44554c450000ULL;

    if (Id == 0)
        Id = 1;
    while (RperfRecordingFindModule(Recording, Id) != NULL)
    {
        if (Id == (ULONGLONG)-1)
            return 0;
        Id++;
    }
    return Id;
}

static BOOL
RperfBaselineSystemPath(const CHAR Source[256],
                        PWSTR Path,
                        SIZE_T PathCharacters)
{
    WCHAR Converted[512], WindowsDirectory[MAX_PATH];
    SIZE_T Bytes = 0;
    int Characters;
    UINT WindowsLength;

    while (Bytes < 256 && Source[Bytes] != ANSI_NULL)
        Bytes++;
    if (Bytes == 0 || Bytes == 256)
        return FALSE;
    Characters = MultiByteToWideChar(CP_ACP,
                                     0,
                                     Source,
                                     (int)Bytes,
                                     Converted,
                                     ARRAYSIZE(Converted) - 1);
    if (Characters <= 0)
        return FALSE;
    Converted[Characters] = UNICODE_NULL;
    if (_wcsnicmp(Converted, L"\\SystemRoot\\", 12) == 0)
    {
        WindowsLength = GetWindowsDirectoryW(WindowsDirectory,
                                              ARRAYSIZE(WindowsDirectory));
        if (WindowsLength == 0 ||
            WindowsLength >= ARRAYSIZE(WindowsDirectory) ||
            (SIZE_T)WindowsLength + 1 + wcslen(Converted + 12) >=
                PathCharacters)
        {
            return FALSE;
        }
        CopyMemory(Path,
                   WindowsDirectory,
                   WindowsLength * sizeof(WCHAR));
        Path[WindowsLength++] = L'\\';
        lstrcpyW(Path + WindowsLength, Converted + 12);
        return TRUE;
    }
    if (_wcsnicmp(Converted, L"\\??\\", 4) == 0)
        return wcslen(Converted + 4) < PathCharacters &&
               lstrcpynW(Path, Converted + 4, (int)PathCharacters) != NULL;
    if (wcslen(Converted) >= PathCharacters)
        return FALSE;
    lstrcpyW(Path, Converted);
    return TRUE;
}

BOOL
RperfCaptureSystemModuleBaseline(RPERF_RECORDING *Recording,
                                 ULONGLONG TimestampNs,
                                 ULONGLONG *NextSequence,
                                 RPERF_BASELINE_RESULT *Result)
{
    PRPERF_SYSTEM_MODULES Modules = NULL;
    ULONG Capacity = RPERF_SYSTEM_MODULE_INITIAL_BYTES;
    ULONG Required = 0, MaximumModules, Index;
    NTSTATUS Status;
    BOOL Success = FALSE;

    if (!Recording || !NextSequence || !Result)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (;;)
    {
        Modules = HeapAlloc(GetProcessHeap(), 0, Capacity);
        if (!Modules)
            return FALSE;
        Status = NtQuerySystemInformation(
                                          RPERF_SYSTEM_MODULE_INFORMATION_CLASS,
                                          Modules,
                                          Capacity,
                                          &Required);
        if (Status != RPERF_STATUS_INFO_LENGTH_MISMATCH &&
            Status != RPERF_STATUS_BUFFER_TOO_SMALL)
        {
            break;
        }
        HeapFree(GetProcessHeap(), 0, Modules);
        Modules = NULL;
        if (Capacity == RPERF_SYSTEM_MODULE_MAX_BYTES)
        {
            Result->Partial = TRUE;
            if (Result->Status == ERROR_SUCCESS)
                Result->Status = ERROR_BUFFER_OVERFLOW;
            return TRUE;
        }
        if (Required <= Capacity)
        {
            if (Capacity > RPERF_SYSTEM_MODULE_MAX_BYTES / 2)
                Capacity = RPERF_SYSTEM_MODULE_MAX_BYTES;
            else
                Capacity *= 2;
        }
        else
        {
            Capacity = Required;
        }
        if (Capacity > RPERF_SYSTEM_MODULE_MAX_BYTES)
        {
            Result->Partial = TRUE;
            if (Result->Status == ERROR_SUCCESS)
                Result->Status = ERROR_BUFFER_OVERFLOW;
            return TRUE;
        }
    }
    if (Status < 0)
    {
        Result->Partial = TRUE;
        if (Result->Status == ERROR_SUCCESS)
            Result->Status = RtlNtStatusToDosError(Status);
        Success = TRUE;
        goto Cleanup;
    }
    if (Capacity < FIELD_OFFSET(RPERF_SYSTEM_MODULES, Modules))
    {
        SetLastError(ERROR_BAD_FORMAT);
        goto Cleanup;
    }
    MaximumModules = (Capacity -
        FIELD_OFFSET(RPERF_SYSTEM_MODULES, Modules)) /
        sizeof(Modules->Modules[0]);
    if (Modules->NumberOfModules > MaximumModules)
    {
        SetLastError(ERROR_BAD_FORMAT);
        goto Cleanup;
    }

    for (Index = 0; Index < Modules->NumberOfModules; Index++)
    {
        const RPERF_SYSTEM_MODULE *Source =
            &Modules->Modules[Index];
        RPERF_MODULE Module;
        RPERF_RECORD Record;
        WCHAR Path[1024];
        ULONGLONG Base = (ULONGLONG)(ULONG_PTR)Source->ImageBase;
        ULONGLONG Id;

        if (Recording->ModuleCount >= Recording->Limits.MaxModules)
        {
            Result->Partial = TRUE;
            if (Result->Status == ERROR_SUCCESS)
                Result->Status = ERROR_BUFFER_OVERFLOW;
            break;
        }
        if (!RperfBaselineSystemPath(Source->FullPathName,
                                     Path,
                                     ARRAYSIZE(Path)))
        {
            Result->Partial = TRUE;
            if (Result->Status == ERROR_SUCCESS)
                Result->Status = ERROR_BAD_PATHNAME;
            continue;
        }
        Id = RperfBaselineModuleId(Recording,
                                   0,
                                   Base,
                                   Source->ImageSize);
        if (!Id)
        {
            SetLastError(ERROR_ARITHMETIC_OVERFLOW);
            goto Cleanup;
        }
        ZeroMemory(&Module, sizeof(Module));
        Module.Id = Id;
        Module.Base = Base;
        Module.Size = Source->ImageSize;
        Module.Architecture = RperfNativeArchitecture();
        Module.Path = Path;
        RperfEnrichModuleFromImage(&Module, Path);
        if (!RperfRecordingAddModule(Recording, &Module))
            goto Cleanup;

        ZeroMemory(&Record, sizeof(Record));
        Record.Header.Kind = RperfRecordImageLoad;
        Record.Header.Flags = RPERF_MODEL_RECORD_FLAG_SYNTHETIC;
        Record.Header.TimestampNs = TimestampNs;
        Record.Header.Cpu = RPERF_MODEL_ALL_CPUS;
        Record.Data.Lifecycle.ObjectId = Id;
        Record.Data.Lifecycle.ModuleId = Id;
        Record.Data.Lifecycle.ImageBase = Module.Base;
        Record.Data.Lifecycle.ImageSize = Module.Size;
        if (!RperfBaselineSequence(NextSequence, &Record) ||
            !RperfRecordingAddRecord(Recording, &Record))
        {
            goto Cleanup;
        }
        Result->Modules++;
    }
    Success = TRUE;

Cleanup:
    if (Modules)
        HeapFree(GetProcessHeap(), 0, Modules);
    return Success;
}

static BOOL
RperfBaselineAddModules(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                        RPERF_RECORDING *Recording,
                        ULONGLONG TimestampNs,
                        ULONGLONG *NextSequence,
                        ULONGLONG ProcessKey,
                        RPERF_BASELINE_RESULT *Result)
{
    HANDLE Snapshot;
    MODULEENTRY32W Entry;

    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,
                                        Configuration->ProcessId);
    if (Snapshot == INVALID_HANDLE_VALUE)
    {
        Result->Partial = TRUE;
        Result->Status = GetLastError();
        return TRUE;
    }
    ZeroMemory(&Entry, sizeof(Entry));
    Entry.dwSize = sizeof(Entry);
    if (Module32FirstW(Snapshot, &Entry))
    {
        do
        {
            RPERF_MODULE Module;
            RPERF_RECORD Record;
            ULONGLONG Base = (ULONGLONG)(ULONG_PTR)Entry.modBaseAddr;
            ULONGLONG Id;

            if (Result->Modules >= Configuration->Limits.MaxModules)
            {
                Result->Partial = TRUE;
                Result->Status = ERROR_BUFFER_OVERFLOW;
                break;
            }
            Id = RperfBaselineModuleId(Recording, ProcessKey, Base,
                                       Entry.modBaseSize);
            if (Id == 0)
            {
                CloseHandle(Snapshot);
                SetLastError(ERROR_ARITHMETIC_OVERFLOW);
                return FALSE;
            }
            ZeroMemory(&Module, sizeof(Module));
            Module.Id = Id;
            Module.ProcessKey = ProcessKey;
            Module.Base = Base;
            Module.Size = Entry.modBaseSize;
            Module.Architecture = RperfNativeArchitecture();
            Module.Path = Entry.szExePath;
            RperfEnrichModuleFromImage(&Module, Entry.szExePath);
            if (!RperfRecordingAddModule(Recording, &Module))
            {
                CloseHandle(Snapshot);
                return FALSE;
            }
            ZeroMemory(&Record, sizeof(Record));
            Record.Header.Kind = RperfRecordImageLoad;
            Record.Header.Flags = RPERF_MODEL_RECORD_FLAG_SYNTHETIC;
            Record.Header.TimestampNs = TimestampNs;
            Record.Header.ProcessId = Configuration->ProcessId;
            Record.Header.ProcessKey = ProcessKey;
            Record.Header.Cpu = RPERF_MODEL_ALL_CPUS;
            Record.Data.Lifecycle.ObjectId = Id;
            Record.Data.Lifecycle.ModuleId = Id;
            Record.Data.Lifecycle.ImageBase = Base;
            Record.Data.Lifecycle.ImageSize = Entry.modBaseSize;
            if (!RperfBaselineSequence(NextSequence, &Record) ||
                !RperfRecordingAddRecord(Recording, &Record))
            {
                CloseHandle(Snapshot);
                return FALSE;
            }
            Result->Modules++;
            Entry.dwSize = sizeof(Entry);
        } while (Module32NextW(Snapshot, &Entry));
    }
    else
    {
        Result->Partial = TRUE;
        Result->Status = GetLastError();
    }
    CloseHandle(Snapshot);
    return TRUE;
}

BOOL
RperfCaptureBaseline(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                     RPERF_RECORDING *Recording,
                     ULONGLONG TimestampNs,
                     ULONGLONG *NextSequence,
                     RPERF_BASELINE_ID_CALLBACK IdCallback,
                     PVOID IdContext,
                     RPERF_BASELINE_RESULT *Result)
{
    RPERF_BASELINE_RESULT Local;
    ULONGLONG ProcessKey;

    if (Configuration == NULL || Recording == NULL ||
        NextSequence == NULL || Configuration->ProcessId == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Result == NULL)
        Result = &Local;
    ZeroMemory(Result, sizeof(*Result));
    ProcessKey = RperfBaselineProcessKey(Configuration->ProcessId, Result);
    if (!RperfBaselineAddProcess(Configuration, Recording, TimestampNs,
                                 NextSequence, ProcessKey,
                                 IdCallback, IdContext, Result) ||
        !RperfBaselineAddThreads(Configuration, Recording, TimestampNs,
                                 NextSequence, ProcessKey,
                                 IdCallback, IdContext, Result) ||
        !RperfBaselineAddModules(Configuration, Recording, TimestampNs,
                                 NextSequence, ProcessKey, Result))
        return FALSE;
    return TRUE;
}
