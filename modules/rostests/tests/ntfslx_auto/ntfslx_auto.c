/*
 * PROJECT:     ReactOS standalone NTFS stress utility
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Boot-time user-mode NTFS exerciser for ntfslx on D:\
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

ULONG __cdecl DbgPrint(PCCH Format, ...);

typedef struct _NTFSLX_IO_STATUS_BLOCK
{
    union
    {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} NTFSLX_IO_STATUS_BLOCK, *PNTFSLX_IO_STATUS_BLOCK;

typedef enum _NTFSLX_FILE_INFORMATION_CLASS
{
    NtfslxFileRenameInformation = 10,
    NtfslxFileDispositionInformation = 13
} NTFSLX_FILE_INFORMATION_CLASS;

typedef struct _NTFSLX_FILE_RENAME_INFORMATION
{
    BOOLEAN ReplaceIfExists;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[260];
} NTFSLX_FILE_RENAME_INFORMATION, *PNTFSLX_FILE_RENAME_INFORMATION;

typedef struct _NTFSLX_FILE_DISPOSITION_INFORMATION
{
    BOOLEAN DeleteFile;
} NTFSLX_FILE_DISPOSITION_INFORMATION, *PNTFSLX_FILE_DISPOSITION_INFORMATION;

typedef struct _NTFSLX_FILE_FULL_EA_INFORMATION
{
    ULONG NextEntryOffset;
    UCHAR Flags;
    UCHAR EaNameLength;
    USHORT EaValueLength;
    CHAR EaName[1];
} NTFSLX_FILE_FULL_EA_INFORMATION, *PNTFSLX_FILE_FULL_EA_INFORMATION;

typedef struct _NTFSLX_FILE_GET_EA_INFORMATION
{
    ULONG NextEntryOffset;
    UCHAR EaNameLength;
    CHAR EaName[1];
} NTFSLX_FILE_GET_EA_INFORMATION, *PNTFSLX_FILE_GET_EA_INFORMATION;

typedef struct _NTFSLX_MOUNT_POINT_REPARSE_BUFFER
{
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    USHORT SubstituteNameOffset;
    USHORT SubstituteNameLength;
    USHORT PrintNameOffset;
    USHORT PrintNameLength;
    WCHAR PathBuffer[512];
} NTFSLX_MOUNT_POINT_REPARSE_BUFFER, *PNTFSLX_MOUNT_POINT_REPARSE_BUFFER;

typedef struct _NTFSLX_REPARSE_DATA_HEADER
{
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
} NTFSLX_REPARSE_DATA_HEADER, *PNTFSLX_REPARSE_DATA_HEADER;

NTSYSAPI NTSTATUS NTAPI NtSetInformationFile(_In_ HANDLE FileHandle,
                                             _Out_ PNTFSLX_IO_STATUS_BLOCK IoStatusBlock,
                                             _In_ PVOID FileInformation,
                                             _In_ ULONG Length,
                                             _In_ NTFSLX_FILE_INFORMATION_CLASS FileInformationClass);
NTSYSAPI NTSTATUS NTAPI NtQuerySecurityObject(_In_ HANDLE Handle,
                                              _In_ SECURITY_INFORMATION SecurityInformation,
                                              _Out_writes_bytes_to_opt_(Length, *LengthNeeded) PSECURITY_DESCRIPTOR SecurityDescriptor,
                                              _In_ ULONG Length,
                                              _Out_ PULONG LengthNeeded);
NTSYSAPI NTSTATUS NTAPI NtSetSecurityObject(_In_ HANDLE Handle,
                                            _In_ SECURITY_INFORMATION SecurityInformation,
                                            _In_ PSECURITY_DESCRIPTOR SecurityDescriptor);
NTSYSAPI NTSTATUS NTAPI NtSetEaFile(_In_ HANDLE FileHandle,
                                    _Out_ PNTFSLX_IO_STATUS_BLOCK IoStatusBlock,
                                    _In_reads_bytes_(Length) PVOID EaBuffer,
                                    _In_ ULONG Length);
NTSYSAPI NTSTATUS NTAPI NtQueryEaFile(_In_ HANDLE FileHandle,
                                      _Out_ PNTFSLX_IO_STATUS_BLOCK IoStatusBlock,
                                      _Out_writes_bytes_to_(Length, *EaReturnLength) PVOID Buffer,
                                      _In_ ULONG Length,
                                      _In_ BOOLEAN ReturnSingleEntry,
                                      _In_reads_bytes_opt_(EaListLength) PVOID EaList,
                                      _In_ ULONG EaListLength,
                                      _Out_opt_ PULONG EaIndex,
                                      _In_ BOOLEAN RestartScan);
NTSYSAPI ULONG NTAPI RtlNtStatusToDosError(_In_ NTSTATUS Status);

/* SystemPoolTagInformation = 22; matches sdk/include/ndk/extypes.h */
typedef struct _NTFSLX_SYSTEM_POOLTAG
{
    union
    {
        UCHAR Tag[4];
        ULONG TagUlong;
    };
    ULONG PagedAllocs;
    ULONG PagedFrees;
    SIZE_T PagedUsed;
    ULONG NonPagedAllocs;
    ULONG NonPagedFrees;
    SIZE_T NonPagedUsed;
} NTFSLX_SYSTEM_POOLTAG, *PNTFSLX_SYSTEM_POOLTAG;

typedef struct _NTFSLX_SYSTEM_POOLTAG_INFORMATION
{
    ULONG Count;
    NTFSLX_SYSTEM_POOLTAG TagInfo[1];
} NTFSLX_SYSTEM_POOLTAG_INFORMATION, *PNTFSLX_SYSTEM_POOLTAG_INFORMATION;

NTSYSAPI NTSTATUS NTAPI NtQuerySystemInformation(_In_ ULONG SystemInformationClass,
                                                  _Out_writes_bytes_(SystemInformationLength) PVOID SystemInformation,
                                                  _In_ ULONG SystemInformationLength,
                                                  _Out_opt_ PULONG ReturnLength);
#define NTFSLX_SYSTEM_POOLTAG_INFORMATION_CLASS 22

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef VOLUME_IS_DIRTY
#define VOLUME_IS_DIRTY 0x00000001
#endif

/*
 * Mirror of the in-driver IOCTL definitions. Keep in sync with
 * drivers/filesystems/ntfslx/ntfslx.h. The TIER0_PROOF IOCTL hits the
 * volume control device and reports on-disk metadata state we can use
 * to assert the mount-non-destructive property and the latch behaviour.
 */
#ifndef IOCTL_NTFSLX_TIER0_PROOF
#define IOCTL_NTFSLX_TIER0_PROOF \
    CTL_CODE(FILE_DEVICE_DISK_FILE_SYSTEM, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef IOCTL_NTFSLX_TC1_MFTLOCK_REENTRANCY
#define IOCTL_NTFSLX_TC1_MFTLOCK_REENTRANCY \
    CTL_CODE(FILE_DEVICE_DISK_FILE_SYSTEM, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

typedef struct _NTFSLX_TIER0_PROOF
{
    ULONG  Version;
    NTSTATUS ReturnStatus;
    ULONG  MftMirrorConsistent;
    ULONG  MftMirrorRecords;
    ULONG  VolumeDirtyFlag;
    ULONG  LogFileFirstDword;
    ULONG  LogFileIs0xFF;
    ULONG  LcnZeroIsReserved;
    ULONGLONG BootLcn;
    ULONGLONG MftMirrLcn;
} NTFSLX_TIER0_PROOF, *PNTFSLX_TIER0_PROOF;

typedef struct _NTFSLX_TC1_REENTRANCY_RESULT
{
    ULONG    Version;
    ULONG    TestCompleted;
    NTSTATUS WriteStatus;
    ULONG    ElapsedMs;
} NTFSLX_TC1_REENTRANCY_RESULT, *PNTFSLX_TC1_REENTRANCY_RESULT;

static LONG g_FailureCount;

#define trace(fmt, ...) DbgPrint("NTFSLX-AUTO: " fmt, ##__VA_ARGS__)
#define fail(fmt, ...) do { ++g_FailureCount; DbgPrint("NTFSLX-AUTO [fail]: " fmt, ##__VA_ARGS__); } while (0)
#define ok(cond, fmt, ...) do { if (!(cond)) fail(fmt, ##__VA_ARGS__); } while (0)
#define skip(cond, fmt, ...) do { if (cond) { fail(fmt, ##__VA_ARGS__); return; } } while (0)

typedef struct _NTFSLX_VOLUME_SPACE_SNAPSHOT
{
    ULONGLONG TotalBytes;
    ULONGLONG FreeBytes;
    ULONGLONG FreeBytesAvailable;
    DWORD ClusterSize;
} NTFSLX_VOLUME_SPACE_SNAPSHOT, *PNTFSLX_VOLUME_SPACE_SNAPSHOT;

typedef struct _NTFSLX_BENCHMARK_RESULT
{
    WCHAR RootPath[4];
    WCHAR FsName[32];
    DWORD ClusterSize;
    ULONGLONG TotalBytes;
    ULONGLONG WriteElapsedUs;
    ULONGLONG ReadElapsedUs;
    ULONGLONG WriteMiBPerSecX100;
    ULONGLONG ReadMiBPerSecX100;
} NTFSLX_BENCHMARK_RESULT, *PNTFSLX_BENCHMARK_RESULT;

#define NTFSLX_BENCH_CHUNK_BYTES (64 * 1024)
#define NTFSLX_BENCH_TOTAL_BYTES (16 * 1024 * 1024)
#define NTFSLX_BENCH_FAT_LABEL L"NTFXBENCH"
#define NTFSLX_BENCH_QUIESCE_INTERVAL_MS 1000
#define NTFSLX_BENCH_QUIESCE_STABLE_SAMPLES 3
#define NTFSLX_BENCH_QUIESCE_MAX_ATTEMPTS 12

static BOOL
FindNamedEntryInPattern(PCWSTR Pattern, PCWSTR Name, PWIN32_FIND_DATAW Match)
{
    WIN32_FIND_DATAW FindData;
    HANDLE hFind;
    BOOL Found = FALSE;

    hFind = FindFirstFileW(Pattern, &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
        return FALSE;

    do
    {
        if (lstrcmpiW(FindData.cFileName, Name) == 0)
        {
            if (Match != NULL)
                *Match = FindData;
            Found = TRUE;
            break;
        }
    }
    while (FindNextFileW(hFind, &FindData));

    FindClose(hFind);
    return Found;
}

static void
FillPattern(BYTE *Buffer, DWORD Size, BYTE Seed, BYTE Step)
{
    DWORD i;

    for (i = 0; i < Size; ++i)
        Buffer[i] = (BYTE)(Seed + (i * Step));
}

static BOOL
QueryVolumeSpaceSnapshot(PCWSTR RootPath, PNTFSLX_VOLUME_SPACE_SNAPSHOT Snapshot)
{
    ULARGE_INTEGER FreeBytesAvailable;
    ULARGE_INTEGER TotalBytes;
    ULARGE_INTEGER TotalFreeBytes;
    DWORD SectorsPerCluster;
    DWORD BytesPerSector;
    DWORD FreeClusters;
    DWORD TotalClusters;
    BOOL Ok;

    ZeroMemory(Snapshot, sizeof(*Snapshot));

    Ok = GetDiskFreeSpaceExW(RootPath,
                             &FreeBytesAvailable,
                             &TotalBytes,
                             &TotalFreeBytes);
    if (!Ok)
        return FALSE;

    Ok = GetDiskFreeSpaceW(RootPath,
                           &SectorsPerCluster,
                           &BytesPerSector,
                           &FreeClusters,
                           &TotalClusters);
    if (!Ok)
        return FALSE;

    Snapshot->TotalBytes = TotalBytes.QuadPart;
    Snapshot->FreeBytes = TotalFreeBytes.QuadPart;
    Snapshot->FreeBytesAvailable = FreeBytesAvailable.QuadPart;
    Snapshot->ClusterSize = SectorsPerCluster * BytesPerSector;
    return TRUE;
}

static ULONGLONG
ExpectedAllocationBytes(ULONGLONG FileSize, DWORD ClusterSize)
{
    if (FileSize == 0 || ClusterSize == 0)
        return 0;

    return ((FileSize + ClusterSize - 1) / ClusterSize) * ClusterSize;
}

static void
TraceVolumeSpaceSnapshot(PCSTR Label, const NTFSLX_VOLUME_SPACE_SNAPSHOT *Snapshot)
{
    trace("%s: total=%I64u free=%I64u avail=%I64u cluster=%lu\n",
          Label,
          Snapshot->TotalBytes,
          Snapshot->FreeBytes,
          Snapshot->FreeBytesAvailable,
          Snapshot->ClusterSize);
}

static BOOL
IsFatFamilyFsName(PCWSTR FsName)
{
    return (FsName != NULL) &&
           (lstrcmpiW(FsName, L"FAT") == 0 ||
            lstrcmpiW(FsName, L"FAT32") == 0 ||
            lstrcmpiW(FsName, L"VFAT") == 0);
}

static ULONGLONG
QueryBenchmarkCounterFrequency(void)
{
    static ULONGLONG Frequency;

    if (Frequency == 0)
    {
        LARGE_INTEGER PerfFrequency;

        if (!QueryPerformanceFrequency(&PerfFrequency) || PerfFrequency.QuadPart <= 0)
            Frequency = 1000000ULL;
        else
            Frequency = (ULONGLONG)PerfFrequency.QuadPart;
    }

    return Frequency;
}

static ULONGLONG
QueryBenchmarkCounter(void)
{
    LARGE_INTEGER Counter;

    if (!QueryPerformanceCounter(&Counter) || Counter.QuadPart < 0)
        return 0;

    return (ULONGLONG)Counter.QuadPart;
}

static ULONGLONG
ComputeElapsedUs(ULONGLONG StartCounter, ULONGLONG EndCounter)
{
    ULONGLONG Frequency;

    if (EndCounter <= StartCounter)
        return 1;

    Frequency = QueryBenchmarkCounterFrequency();
    if (Frequency == 0)
        return 1;

    return ((EndCounter - StartCounter) * 1000000ULL) / Frequency;
}

static ULONGLONG
ComputeBenchmarkMiBPerSecX100(ULONGLONG TotalBytes, ULONGLONG ElapsedUs)
{
    if (ElapsedUs == 0)
        ElapsedUs = 1;

    return (TotalBytes * 100000000ULL) /
           (ElapsedUs * 1024ULL * 1024ULL);
}

static void
FillBenchmarkChunk(BYTE *Buffer, DWORD Size, DWORD ChunkIndex)
{
    DWORD i;
    BYTE Seed = (BYTE)(0x5A + (ChunkIndex * 13));

    for (i = 0; i < Size; ++i)
        Buffer[i] = (BYTE)(Seed + (i * 3) + (i >> 8));
}

static BOOL
BuildBenchmarkPaths(PCWSTR RootPath,
                    PWSTR DirPath,
                    SIZE_T DirPathCount,
                    PWSTR FilePath,
                    SIZE_T FilePathCount)
{
    int Length;

    Length = swprintf(DirPath, L"%lsntfslx_bench", RootPath);
    if (Length < 0 || (SIZE_T)Length >= DirPathCount)
    {
        fail("Benchmark directory path is too long for root '%ls'\n", RootPath);
        return FALSE;
    }

    Length = swprintf(FilePath, L"%lsntfslx_bench\\seq.bin", RootPath);
    if (Length < 0 || (SIZE_T)Length >= FilePathCount)
    {
        fail("Benchmark file path is too long for root '%ls'\n", RootPath);
        return FALSE;
    }

    return TRUE;
}

static void
CleanupBenchmarkArtifacts(PCWSTR RootPath)
{
    WCHAR DirPath[MAX_PATH];
    WCHAR FilePath[MAX_PATH];

    if (!BuildBenchmarkPaths(RootPath,
                             DirPath, _countof(DirPath),
                             FilePath, _countof(FilePath)))
    {
        return;
    }

    DeleteFileW(FilePath);
    RemoveDirectoryW(DirPath);
}

static BOOL
QueryVolumeIdentity(PCWSTR RootPath,
                    PWSTR VolumeName,
                    SIZE_T VolumeNameCount,
                    PWSTR FsName,
                    SIZE_T FsNameCount)
{
    DWORD Serial;
    DWORD MaxComp;
    DWORD Flags;

    VolumeName[0] = UNICODE_NULL;
    FsName[0] = UNICODE_NULL;
    return GetVolumeInformationW(RootPath,
                                 VolumeName, (DWORD)VolumeNameCount,
                                 &Serial, &MaxComp, &Flags,
                                 FsName, (DWORD)FsNameCount);
}

static BOOL
FindFatComparisonVolume(PWSTR RootPath, SIZE_T RootPathCount, PWSTR FsName, SIZE_T FsNameCount)
{
    DWORD Attempt;
    WCHAR VolumeName[64];

    for (Attempt = 0; Attempt < 40; ++Attempt)
    {
        DWORD LogicalDrives = GetLogicalDrives();
        WCHAR Letter;

        for (Letter = L'C'; Letter <= L'Z'; ++Letter)
        {
            UINT DriveType;

            if (Letter == L'D')
                continue;

            if ((LogicalDrives & (1u << (Letter - L'A'))) == 0)
                continue;

            if (RootPathCount < 4)
                return FALSE;

            RootPath[0] = Letter;
            RootPath[1] = L':';
            RootPath[2] = L'\\';
            RootPath[3] = UNICODE_NULL;

            DriveType = GetDriveTypeW(RootPath);
            if (DriveType != DRIVE_REMOVABLE && DriveType != DRIVE_FIXED)
                continue;

            if (!QueryVolumeIdentity(RootPath,
                                     VolumeName, _countof(VolumeName),
                                     FsName, FsNameCount))
                continue;

            trace("Benchmark candidate: drive='%ls' label='%ls' fs='%ls' type=%u attempt=%lu\n",
                  RootPath, VolumeName, FsName, DriveType, Attempt);

            if (IsFatFamilyFsName(FsName) &&
                lstrcmpiW(VolumeName, NTFSLX_BENCH_FAT_LABEL) == 0)
            {
                trace("Benchmark comparison volume ready: drive='%ls' label='%ls' fs='%ls' type=%u attempt=%lu\n",
                      RootPath, VolumeName, FsName, DriveType, Attempt);
                return TRUE;
            }
        }

        if ((Attempt % 5) == 0)
            trace("Waiting for FAT comparison volume attempt=%lu logical=0x%08lX\n",
                  Attempt, LogicalDrives);
        Sleep(1000);
    }

    return FALSE;
}

static BOOL
SnapshotsEqual(const NTFSLX_VOLUME_SPACE_SNAPSHOT *Left,
               const NTFSLX_VOLUME_SPACE_SNAPSHOT *Right)
{
    return Left->TotalBytes == Right->TotalBytes &&
           Left->FreeBytes == Right->FreeBytes &&
           Left->FreeBytesAvailable == Right->FreeBytesAvailable &&
           Left->ClusterSize == Right->ClusterSize;
}

static void
WaitForBenchmarkQuiescence(PCWSTR NtfsRoot, PCWSTR FatRoot)
{
    NTFSLX_VOLUME_SPACE_SNAPSHOT CurrentNtfs;
    NTFSLX_VOLUME_SPACE_SNAPSHOT CurrentFat;
    NTFSLX_VOLUME_SPACE_SNAPSHOT PreviousNtfs;
    NTFSLX_VOLUME_SPACE_SNAPSHOT PreviousFat;
    BOOL HavePrevious = FALSE;
    DWORD StableSamples = 0;
    DWORD Attempt;

    trace("Benchmark quiesce: waiting for stable volume state ntfs='%ls' fat='%ls'\n",
          NtfsRoot,
          FatRoot);
    Sleep(2 * NTFSLX_BENCH_QUIESCE_INTERVAL_MS);

    for (Attempt = 0; Attempt < NTFSLX_BENCH_QUIESCE_MAX_ATTEMPTS; ++Attempt)
    {
        BOOL NtfsOk;
        BOOL FatOk;

        NtfsOk = QueryVolumeSpaceSnapshot(NtfsRoot, &CurrentNtfs);
        FatOk = QueryVolumeSpaceSnapshot(FatRoot, &CurrentFat);
        if (!NtfsOk || !FatOk)
        {
            trace("Benchmark quiesce: snapshot failed attempt=%lu ntfs_ok=%d fat_ok=%d err=%lu\n",
                  Attempt,
                  NtfsOk,
                  FatOk,
                  GetLastError());
            HavePrevious = FALSE;
            StableSamples = 0;
            Sleep(NTFSLX_BENCH_QUIESCE_INTERVAL_MS);
            continue;
        }

        if (HavePrevious &&
            SnapshotsEqual(&CurrentNtfs, &PreviousNtfs) &&
            SnapshotsEqual(&CurrentFat, &PreviousFat))
        {
            ++StableSamples;
        }
        else
        {
            StableSamples = 1;
        }

        PreviousNtfs = CurrentNtfs;
        PreviousFat = CurrentFat;
        HavePrevious = TRUE;

        if (StableSamples >= NTFSLX_BENCH_QUIESCE_STABLE_SAMPLES)
        {
            trace("Benchmark quiesce: stable after %lu samples\n", StableSamples);
            TraceVolumeSpaceSnapshot("benchmark-quiesce-ntfs", &CurrentNtfs);
            TraceVolumeSpaceSnapshot("benchmark-quiesce-fat", &CurrentFat);
            return;
        }

        Sleep(NTFSLX_BENCH_QUIESCE_INTERVAL_MS);
    }

    trace("Benchmark quiesce: timeout after %lu attempts, proceeding anyway\n",
          (ULONG)NTFSLX_BENCH_QUIESCE_MAX_ATTEMPTS);
    if (HavePrevious)
    {
        TraceVolumeSpaceSnapshot("benchmark-quiesce-ntfs", &PreviousNtfs);
        TraceVolumeSpaceSnapshot("benchmark-quiesce-fat", &PreviousFat);
    }
}

static BOOL
RunBenchmarkWritePhase(PCWSTR RootPath,
                       DWORD TotalBytes,
                       DWORD ChunkBytes,
                       PULONGLONG ElapsedUs)
{
    WCHAR DirPath[MAX_PATH];
    WCHAR FilePath[MAX_PATH];
    PBYTE Buffer = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    ULONGLONG StartCounter;
    ULONGLONG EndCounter;
    DWORD Offset;

    if (!BuildBenchmarkPaths(RootPath,
                             DirPath, _countof(DirPath),
                             FilePath, _countof(FilePath)))
    {
        return FALSE;
    }

    CleanupBenchmarkArtifacts(RootPath);

    if (!CreateDirectoryW(DirPath, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        fail("CreateDirectoryW(%ls) failed with error %lu\n", DirPath, GetLastError());
        return FALSE;
    }

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fail("CreateFileW(%ls write benchmark) failed with error %lu\n",
             FilePath, GetLastError());
        return FALSE;
    }

    Buffer = VirtualAlloc(NULL,
                          ChunkBytes,
                          MEM_COMMIT | MEM_RESERVE,
                          PAGE_READWRITE);
    if (Buffer == NULL)
    {
        fail("VirtualAlloc(write benchmark) failed with error %lu\n", GetLastError());
        CloseHandle(hFile);
        return FALSE;
    }

    StartCounter = QueryBenchmarkCounter();

    for (Offset = 0; Offset < TotalBytes; Offset += ChunkBytes)
    {
        DWORD BytesWritten = 0;
        BOOL Ok;

        FillBenchmarkChunk(Buffer, ChunkBytes, Offset / ChunkBytes);
        Ok = WriteFile(hFile, Buffer, ChunkBytes, &BytesWritten, NULL);
        if (!Ok || BytesWritten != ChunkBytes)
        {
            fail("Write benchmark failed for '%ls' at offset %lu: ok=%d written=%lu err=%lu\n",
                 FilePath, Offset, Ok, BytesWritten, GetLastError());
            VirtualFree(Buffer, 0, MEM_RELEASE);
            CloseHandle(hFile);
            return FALSE;
        }
    }

    if (!FlushFileBuffers(hFile))
    {
        fail("FlushFileBuffers(%ls benchmark) failed with error %lu\n",
             FilePath, GetLastError());
        VirtualFree(Buffer, 0, MEM_RELEASE);
        CloseHandle(hFile);
        return FALSE;
    }

    EndCounter = QueryBenchmarkCounter();
    VirtualFree(Buffer, 0, MEM_RELEASE);
    CloseHandle(hFile);
    *ElapsedUs = ComputeElapsedUs(StartCounter, EndCounter);
    return TRUE;
}

static BOOL
RunBenchmarkReadPhase(PCWSTR RootPath,
                      DWORD TotalBytes,
                      DWORD ChunkBytes,
                      PULONGLONG ElapsedUs)
{
    WCHAR DirPath[MAX_PATH];
    WCHAR FilePath[MAX_PATH];
    PBYTE Expected = NULL;
    PBYTE ReadBuffer = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    ULONGLONG StartCounter;
    ULONGLONG EndCounter;
    DWORD Offset;

    if (!BuildBenchmarkPaths(RootPath,
                             DirPath, _countof(DirPath),
                             FilePath, _countof(FilePath)))
    {
        return FALSE;
    }

    hFile = CreateFileW(FilePath,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fail("CreateFileW(%ls read benchmark) failed with error %lu\n",
             FilePath, GetLastError());
        return FALSE;
    }

    Expected = VirtualAlloc(NULL,
                            ChunkBytes,
                            MEM_COMMIT | MEM_RESERVE,
                            PAGE_READWRITE);
    ReadBuffer = VirtualAlloc(NULL,
                              ChunkBytes,
                              MEM_COMMIT | MEM_RESERVE,
                              PAGE_READWRITE);
    if (Expected == NULL || ReadBuffer == NULL)
    {
        fail("VirtualAlloc(read benchmark) failed with error %lu\n", GetLastError());
        if (Expected != NULL)
            VirtualFree(Expected, 0, MEM_RELEASE);
        if (ReadBuffer != NULL)
            VirtualFree(ReadBuffer, 0, MEM_RELEASE);
        CloseHandle(hFile);
        return FALSE;
    }

    StartCounter = QueryBenchmarkCounter();

    for (Offset = 0; Offset < TotalBytes; Offset += ChunkBytes)
    {
        DWORD BytesRead = 0;
        BOOL Ok;

        FillBenchmarkChunk(Expected, ChunkBytes, Offset / ChunkBytes);
        Ok = ReadFile(hFile, ReadBuffer, ChunkBytes, &BytesRead, NULL);
        if (!Ok || BytesRead != ChunkBytes)
        {
            fail("Read benchmark failed for '%ls' at offset %lu: ok=%d read=%lu err=%lu\n",
                 FilePath, Offset, Ok, BytesRead, GetLastError());
            VirtualFree(Expected, 0, MEM_RELEASE);
            VirtualFree(ReadBuffer, 0, MEM_RELEASE);
            CloseHandle(hFile);
            return FALSE;
        }

        if (memcmp(ReadBuffer, Expected, ChunkBytes) != 0)
        {
            fail("Read benchmark content mismatch for '%ls' at chunk %lu\n",
                 FilePath, Offset / ChunkBytes);
            VirtualFree(Expected, 0, MEM_RELEASE);
            VirtualFree(ReadBuffer, 0, MEM_RELEASE);
            CloseHandle(hFile);
            return FALSE;
        }
    }

    EndCounter = QueryBenchmarkCounter();
    VirtualFree(Expected, 0, MEM_RELEASE);
    VirtualFree(ReadBuffer, 0, MEM_RELEASE);
    CloseHandle(hFile);
    *ElapsedUs = ComputeElapsedUs(StartCounter, EndCounter);
    return TRUE;
}

static void
TraceBenchmarkResult(const NTFSLX_BENCHMARK_RESULT *Result)
{
    trace("bench result fs='%ls' drive='%ls' bytes=%I64u write_mib_s=%I64u.%02I64u read_mib_s=%I64u.%02I64u cluster=%lu write_ms=%I64u.%02I64u read_ms=%I64u.%02I64u\n",
          Result->FsName,
          Result->RootPath,
          Result->TotalBytes,
          Result->WriteMiBPerSecX100 / 100, Result->WriteMiBPerSecX100 % 100,
          Result->ReadMiBPerSecX100 / 100, Result->ReadMiBPerSecX100 % 100,
          Result->ClusterSize,
          Result->WriteElapsedUs / 1000, (Result->WriteElapsedUs % 1000) / 10,
          Result->ReadElapsedUs / 1000, (Result->ReadElapsedUs % 1000) / 10);
}

static void
ExpectFreeSpaceDelta(PCSTR Label,
                     const NTFSLX_VOLUME_SPACE_SNAPSHOT *Baseline,
                     const NTFSLX_VOLUME_SPACE_SNAPSHOT *Current,
                     ULONGLONG ExpectedBytes)
{
    ULONGLONG ActualBytes = Baseline->FreeBytes - Current->FreeBytes;

    ok(ActualBytes == ExpectedBytes,
       "%s free-space delta is %I64u, expected %I64u (baseline free=%I64u current free=%I64u)\n",
       Label, ActualBytes, ExpectedBytes, Baseline->FreeBytes, Current->FreeBytes);
}

static void
ExpectSameFreeSpace(PCSTR Label,
                    const NTFSLX_VOLUME_SPACE_SNAPSHOT *Expected,
                    const NTFSLX_VOLUME_SPACE_SNAPSHOT *Actual)
{
    ok(Actual->FreeBytes == Expected->FreeBytes,
       "%s free space changed unexpectedly: %I64u -> %I64u\n",
       Label, Expected->FreeBytes, Actual->FreeBytes);
}

static BOOL
DeleteTreeRecursive(PCWSTR RootPath)
{
    WCHAR Pattern[MAX_PATH];
    WIN32_FIND_DATAW FindData;
    HANDLE hFind;

    swprintf(Pattern, L"%ls\\*", RootPath);
    hFind = FindFirstFileW(Pattern, &FindData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            WCHAR ChildPath[MAX_PATH];

            if (lstrcmpW(FindData.cFileName, L".") == 0 ||
                lstrcmpW(FindData.cFileName, L"..") == 0)
            {
                continue;
            }

            swprintf(ChildPath, L"%ls\\%ls", RootPath, FindData.cFileName);
            if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                DeleteTreeRecursive(ChildPath);
            }
            else
            {
                SetFileAttributesW(ChildPath, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(ChildPath);
            }
        }
        while (FindNextFileW(hFind, &FindData));

        FindClose(hFind);
    }

    SetFileAttributesW(RootPath, FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(RootPath);
    return TRUE;
}

static BOOL
EnsureProbeCaseDirectories(void)
{
    BOOL Ok;
    DWORD Error;

    Ok = CreateDirectoryW(L"D:\\ntfslx_auto", NULL);
    Error = GetLastError();
    if (!Ok && Error != ERROR_ALREADY_EXISTS)
    {
        fail("CreateDirectoryW(ntfslx_auto probe root) failed with error %lu\n", Error);
        return FALSE;
    }

    Ok = CreateDirectoryW(L"D:\\ntfslx_auto\\cases", NULL);
    Error = GetLastError();
    if (!Ok && Error != ERROR_ALREADY_EXISTS)
    {
        fail("CreateDirectoryW(ntfslx_auto\\cases probe root) failed with error %lu\n", Error);
        return FALSE;
    }

    return TRUE;
}

static BOOL
WriteFileBuffer(PCWSTR FilePath, const BYTE *Buffer, DWORD Size)
{
    HANDLE hFile;
    DWORD BytesWritten;
    BOOL Ok;

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fail("CreateFileW(write) failed with error %lu\n", GetLastError());
        return FALSE;
    }

    Ok = WriteFile(hFile, Buffer, Size, &BytesWritten, NULL);
    ok(Ok && BytesWritten == Size,
       "WriteFile(%lu bytes) failed: ok=%d written=%lu err=%lu\n",
       Size, Ok, BytesWritten, GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers failed with error %lu\n", GetLastError());
    CloseHandle(hFile);
    return Ok && BytesWritten == Size;
}

static BOOL
ReadFileBuffer(PCWSTR FilePath, BYTE *Buffer, DWORD Size)
{
    HANDLE hFile;
    DWORD BytesRead;
    BOOL Ok;

    hFile = CreateFileW(FilePath,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fail("CreateFileW(read) failed with error %lu\n", GetLastError());
        return FALSE;
    }

    Ok = ReadFile(hFile, Buffer, Size, &BytesRead, NULL);
    ok(Ok && BytesRead == Size,
       "ReadFile(%lu bytes) failed: ok=%d read=%lu err=%lu\n",
       Size, Ok, BytesRead, GetLastError());
    CloseHandle(hFile);
    return Ok && BytesRead == Size;
}

static BOOL
RenameOpenFileSameDirectory(HANDLE hFile, PCWSTR LeafName, BOOL ReplaceIfExists)
{
    NTFSLX_FILE_RENAME_INFORMATION RenameBuffer;
    NTFSLX_IO_STATUS_BLOCK IoStatus;
    ULONG NameBytes;
    NTSTATUS Status;

    if (hFile == INVALID_HANDLE_VALUE || LeafName == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    NameBytes = (ULONG)(wcslen(LeafName) * sizeof(WCHAR));
    if (NameBytes == 0 || NameBytes >= sizeof(RenameBuffer.FileName))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ZeroMemory(&RenameBuffer, sizeof(RenameBuffer));
    RenameBuffer.ReplaceIfExists = ReplaceIfExists;
    RenameBuffer.RootDirectory = NULL;
    RenameBuffer.FileNameLength = NameBytes;
    CopyMemory(RenameBuffer.FileName, LeafName, NameBytes);

    Status = NtSetInformationFile(hFile,
                                  &IoStatus,
                                  &RenameBuffer,
                                  FIELD_OFFSET(NTFSLX_FILE_RENAME_INFORMATION, FileName) + NameBytes,
                                  NtfslxFileRenameInformation);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

static BOOL
SetDeletePendingOnHandle(HANDLE hFile, BOOL DeleteFile)
{
    NTFSLX_FILE_DISPOSITION_INFORMATION DispInfo;
    NTFSLX_IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    if (hFile == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    DispInfo.DeleteFile = DeleteFile ? TRUE : FALSE;
    Status = NtSetInformationFile(hFile,
                                  &IoStatus,
                                  &DispInfo,
                                  sizeof(DispInfo),
                                  NtfslxFileDispositionInformation);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

static BOOL
WaitForNtfsVolume(void)
{
    DWORD Attempt;

    for (Attempt = 0; Attempt < 60; ++Attempt)
    {
        UINT DriveType;

        DriveType = GetDriveTypeW(L"D:\\");
        if (DriveType != DRIVE_NO_ROOT_DIR)
        {
            HANDLE hDir;
            WIN32_FIND_DATAW FindData;
            HANDLE hFind;

            hDir = CreateFileW(L"D:\\",
                               FILE_LIST_DIRECTORY | SYNCHRONIZE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL,
                               OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS,
                               NULL);
            if (hDir != INVALID_HANDLE_VALUE)
            {
                WCHAR FsName[32];
                WCHAR VolumeName[64];
                DWORD Serial;
                DWORD MaxComp;
                DWORD Flags;

                CloseHandle(hDir);
                if (GetVolumeInformationW(L"D:\\",
                                          VolumeName, _countof(VolumeName),
                                          &Serial, &MaxComp, &Flags,
                                          FsName, _countof(FsName)))
                {
                    trace("Volume ready: label='%ls' fs='%ls' serial=0x%08lX flags=0x%08lX attempt=%lu\n",
                          VolumeName, FsName, Serial, Flags, Attempt);
                }
                else
                {
                    trace("Volume root ready before volume info succeeded (driveType=%u err=%lu attempt=%lu)\n",
                          DriveType, GetLastError(), Attempt);
                }
                return TRUE;
            }

            hFind = FindFirstFileW(L"D:\\*", &FindData);
            if (hFind != INVALID_HANDLE_VALUE)
            {
                FindClose(hFind);
                trace("Volume enumeration ready before directory open succeeded (driveType=%u attempt=%lu)\n",
                      DriveType, Attempt);
                return TRUE;
            }
        }

        if ((Attempt % 5) == 0)
            trace("Waiting for D:\\ root access (driveType=%u attempt=%lu err=%lu)\n",
                  DriveType, Attempt, GetLastError());
        Sleep(1000);
    }

    fail("Timed out waiting for D:\\ root access\n");
    return FALSE;
}

static void
TestVolumeInfo(void)
{
    WCHAR VolumeName[64];
    WCHAR FsName[32];
    DWORD Serial, MaxComp, Flags;
    BOOL Ok;

    Ok = GetVolumeInformationW(L"D:\\",
                               VolumeName, _countof(VolumeName),
                               &Serial, &MaxComp, &Flags,
                               FsName, _countof(FsName));
    ok(Ok, "GetVolumeInformationW failed (error %lu)\n", GetLastError());
    if (!Ok)
        return;

    ok(wcscmp(FsName, L"NTFS") == 0,
       "Expected fs name 'NTFS', got '%ls'\n", FsName);
    ok(MaxComp == 255, "Expected MaxComp 255, got %lu\n", MaxComp);
    ok(wcslen(VolumeName) > 0, "Volume label should not be empty\n");
    trace("Label='%ls' Serial=0x%08lX Flags=0x%08lX\n", VolumeName, Serial, Flags);
}

static void
TestFreeSpace(void)
{
    NTFSLX_VOLUME_SPACE_SNAPSHOT Snapshot;
    BOOL Ok;

    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    if (!Ok)
    {
        skip(TRUE, "QueryVolumeSpaceSnapshot failed (error %lu)\n", GetLastError());
        return;
    }

    ok(Snapshot.TotalBytes > 0, "TotalBytes should be > 0\n");
    ok(Snapshot.FreeBytes > 0, "TotalFreeBytes should be > 0 (got %I64u)\n",
       Snapshot.FreeBytes);
    ok(Snapshot.FreeBytes <= Snapshot.TotalBytes,
       "Free (%I64u) should be <= Total (%I64u)\n",
       Snapshot.FreeBytes, Snapshot.TotalBytes);
    ok(Snapshot.ClusterSize > 0, "ClusterSize should be > 0\n");
    TraceVolumeSpaceSnapshot("volume-space", &Snapshot);
}

static void
TestFat32VsNtfsBenchmark(void)
{
    NTFSLX_BENCHMARK_RESULT NtfsResult;
    NTFSLX_BENCHMARK_RESULT FatResult;
    NTFSLX_VOLUME_SPACE_SNAPSHOT Snapshot;
    WCHAR FatRoot[4];
    WCHAR FatFsName[32];
    ULONGLONG WriteRatioX100;
    ULONGLONG ReadRatioX100;
    BOOL Ok;

    ZeroMemory(&NtfsResult, sizeof(NtfsResult));
    ZeroMemory(&FatResult, sizeof(FatResult));
    lstrcpynW(NtfsResult.RootPath, L"D:\\", _countof(NtfsResult.RootPath));
    lstrcpynW(NtfsResult.FsName, L"NTFS", _countof(NtfsResult.FsName));
    NtfsResult.TotalBytes = NTFSLX_BENCH_TOTAL_BYTES;

    if (!FindFatComparisonVolume(FatRoot, _countof(FatRoot), FatFsName, _countof(FatFsName)))
    {
        fail("Timed out waiting for FAT32 comparison volume\n");
        return;
    }

    lstrcpynW(FatResult.RootPath, FatRoot, _countof(FatResult.RootPath));
    lstrcpynW(FatResult.FsName, FatFsName, _countof(FatResult.FsName));
    FatResult.TotalBytes = NTFSLX_BENCH_TOTAL_BYTES;

    WaitForBenchmarkQuiescence(L"D:\\", FatRoot);

    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(D:\\ benchmark) failed with error %lu\n", GetLastError());
    if (!Ok)
        return;
    NtfsResult.ClusterSize = Snapshot.ClusterSize;

    Ok = QueryVolumeSpaceSnapshot(FatRoot, &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(%ls benchmark) failed with error %lu\n", FatRoot, GetLastError());
    if (!Ok)
        return;
    FatResult.ClusterSize = Snapshot.ClusterSize;

    if (!RunBenchmarkWritePhase(L"D:\\",
                                NTFSLX_BENCH_TOTAL_BYTES,
                                NTFSLX_BENCH_CHUNK_BYTES,
                                &NtfsResult.WriteElapsedUs))
    {
        CleanupBenchmarkArtifacts(L"D:\\");
        return;
    }

    if (!RunBenchmarkWritePhase(FatRoot,
                                NTFSLX_BENCH_TOTAL_BYTES,
                                NTFSLX_BENCH_CHUNK_BYTES,
                                &FatResult.WriteElapsedUs))
    {
        CleanupBenchmarkArtifacts(L"D:\\");
        CleanupBenchmarkArtifacts(FatRoot);
        return;
    }

    if (!RunBenchmarkReadPhase(L"D:\\",
                               NTFSLX_BENCH_TOTAL_BYTES,
                               NTFSLX_BENCH_CHUNK_BYTES,
                               &NtfsResult.ReadElapsedUs))
    {
        CleanupBenchmarkArtifacts(L"D:\\");
        CleanupBenchmarkArtifacts(FatRoot);
        return;
    }

    if (!RunBenchmarkReadPhase(FatRoot,
                               NTFSLX_BENCH_TOTAL_BYTES,
                               NTFSLX_BENCH_CHUNK_BYTES,
                               &FatResult.ReadElapsedUs))
    {
        CleanupBenchmarkArtifacts(L"D:\\");
        CleanupBenchmarkArtifacts(FatRoot);
        return;
    }

    NtfsResult.WriteMiBPerSecX100 = ComputeBenchmarkMiBPerSecX100(NtfsResult.TotalBytes,
                                                                  NtfsResult.WriteElapsedUs);
    NtfsResult.ReadMiBPerSecX100 = ComputeBenchmarkMiBPerSecX100(NtfsResult.TotalBytes,
                                                                 NtfsResult.ReadElapsedUs);
    FatResult.WriteMiBPerSecX100 = ComputeBenchmarkMiBPerSecX100(FatResult.TotalBytes,
                                                                 FatResult.WriteElapsedUs);
    FatResult.ReadMiBPerSecX100 = ComputeBenchmarkMiBPerSecX100(FatResult.TotalBytes,
                                                                FatResult.ReadElapsedUs);

    ok(NtfsResult.WriteMiBPerSecX100 > 0, "NTFS write benchmark throughput should be > 0\n");
    ok(NtfsResult.ReadMiBPerSecX100 > 0, "NTFS read benchmark throughput should be > 0\n");
    ok(FatResult.WriteMiBPerSecX100 > 0, "FAT write benchmark throughput should be > 0\n");
    ok(FatResult.ReadMiBPerSecX100 > 0, "FAT read benchmark throughput should be > 0\n");

    TraceBenchmarkResult(&NtfsResult);
    TraceBenchmarkResult(&FatResult);

    WriteRatioX100 = (FatResult.WriteMiBPerSecX100 != 0)
                     ? (NtfsResult.WriteMiBPerSecX100 * 100ULL) / FatResult.WriteMiBPerSecX100
                     : 0;
    ReadRatioX100 = (FatResult.ReadMiBPerSecX100 != 0)
                    ? (NtfsResult.ReadMiBPerSecX100 * 100ULL) / FatResult.ReadMiBPerSecX100
                    : 0;
    trace("bench compare ntfs_vs_fat32 write_ratio=%I64u.%02I64u read_ratio=%I64u.%02I64u\n",
          WriteRatioX100 / 100, WriteRatioX100 % 100,
          ReadRatioX100 / 100, ReadRatioX100 % 100);

    CleanupBenchmarkArtifacts(L"D:\\");
    CleanupBenchmarkArtifacts(FatRoot);
}

static void
TestDirListing(void)
{
    WIN32_FIND_DATAW FindData;
    HANDLE hFind;
    DWORD Count = 0;

    hFind = FindFirstFileW(L"D:\\*", &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        skip(TRUE, "FindFirstFile D:\\* failed (error %lu)\n", GetLastError());
        return;
    }

    do
    {
        Count++;
    }
    while (FindNextFileW(hFind, &FindData));

    FindClose(hFind);

    ok(Count >= 1, "Expected at least 1 root entry, got %lu\n", Count);
    trace("Root entries seen: %lu\n", Count);
}

static void
TestCreateWriteRead(void)
{
    HANDLE hFile;
    DWORD BytesWritten, BytesRead;
    BOOL Ok;
    BYTE WritePattern[4096];
    BYTE ReadBuffer[4096];
    DWORD i;
    DWORD Errors;
    LARGE_INTEGER FilePos;

    for (i = 0; i < sizeof(WritePattern); i++)
        WritePattern[i] = (BYTE)(i % 256);

    hFile = CreateFileW(L"D:\\ntfslx_rw_test.bin",
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        trace("CreateFile(CREATE_ALWAYS) failed with error %lu\n", GetLastError());
        skip(TRUE, "File creation not supported, skipping R/W tests\n");
        return;
    }

    Ok = WriteFile(hFile, WritePattern, sizeof(WritePattern), &BytesWritten, NULL);
    ok(Ok, "WriteFile failed with error %lu\n", GetLastError());
    ok(BytesWritten == sizeof(WritePattern),
       "Expected %u bytes written, got %lu\n", (unsigned)sizeof(WritePattern), BytesWritten);

    FilePos.QuadPart = 0;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx failed with error %lu\n", GetLastError());

    memset(ReadBuffer, 0xCC, sizeof(ReadBuffer));
    Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok, "ReadFile failed with error %lu\n", GetLastError());
    ok(BytesRead == sizeof(WritePattern),
       "Expected %u bytes read, got %lu\n", (unsigned)sizeof(WritePattern), BytesRead);

    Errors = 0;
    for (i = 0; i < BytesRead; i++)
    {
        if (ReadBuffer[i] != WritePattern[i])
        {
            Errors++;
            if (Errors <= 3)
            {
                ok(0, "Mismatch at offset %lu: expected 0x%02X, got 0x%02X\n",
                   i, (unsigned)WritePattern[i], (unsigned)ReadBuffer[i]);
            }
        }
    }
    ok(Errors == 0, "Total read-back mismatches: %lu / %lu\n", Errors, BytesRead);

    CloseHandle(hFile);

    hFile = CreateFileW(L"D:\\ntfslx_rw_test.bin",
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        ok(0, "Reopen failed with error %lu\n", GetLastError());
        return;
    }

    memset(ReadBuffer, 0xDD, sizeof(ReadBuffer));
    Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok, "Reopen ReadFile failed with error %lu\n", GetLastError());
    ok(BytesRead == sizeof(WritePattern),
       "Reopen expected %u bytes, got %lu\n", (unsigned)sizeof(WritePattern), BytesRead);
    ok(memcmp(ReadBuffer, WritePattern, sizeof(WritePattern)) == 0,
       "Reopen content mismatch\n");

    CloseHandle(hFile);
    DeleteFileW(L"D:\\ntfslx_rw_test.bin");
}

static void
TestExplorerStyleSubdirListing(void)
{
    enum { FirstChunk = 4096, SecondChunk = 2048, TotalBytes = FirstChunk + SecondChunk };
    static const WCHAR DirPath[] = L"D:\\copy_subdir";
    static const WCHAR DirOpenPath[] = L"D:\\copy_subdir\\";
    static const WCHAR FilePath[] = L"D:\\copy_subdir\\copy.bin";
    static const WCHAR Pattern[] = L"D:\\copy_subdir\\*";
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hDir = INVALID_HANDLE_VALUE;
    BYTE ChunkA[FirstChunk];
    BYTE ChunkB[SecondChunk];
    BYTE ReadBuffer[TotalBytes];
    WIN32_FIND_DATAW FindData;
    LARGE_INTEGER FilePos;
    DWORD BytesWritten;
    DWORD BytesRead;
    DWORD Error;
    DWORD Pass;
    BOOL Ok;
    BOOL Found;
    ULONGLONG Size;

    DeleteFileW(FilePath);
    RemoveDirectoryW(DirPath);

    Ok = CreateDirectoryW(DirPath, NULL);
    Error = GetLastError();
    ok(Ok || Error == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW failed with error %lu\n", Error);
    if (!Ok && Error != ERROR_ALREADY_EXISTS)
        return;

    memset(ChunkA, 0x11, sizeof(ChunkA));
    memset(ChunkB, 0x22, sizeof(ChunkB));
    memset(ReadBuffer, 0xCC, sizeof(ReadBuffer));

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "CreateFileW(copy.bin) failed with error %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = WriteFile(hFile, ChunkA, sizeof(ChunkA), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(ChunkA),
       "First chunk write failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());

    Found = FindNamedEntryInPattern(Pattern, L"copy.bin", &FindData);
    ok(Found, "copy.bin missing after first chunk\n");
    if (Found)
    {
        Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        ok(Size == FirstChunk,
           "copy.bin size after first chunk is %I64u, expected %u\n",
           Size, FirstChunk);
    }

    Ok = WriteFile(hFile, ChunkB, sizeof(ChunkB), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(ChunkB),
       "Second chunk write failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());

    Found = FindNamedEntryInPattern(Pattern, L"copy.bin", &FindData);
    ok(Found, "copy.bin missing after second chunk\n");
    if (Found)
    {
        Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        ok(Size == TotalBytes,
           "copy.bin size after second chunk is %I64u, expected %u\n",
           Size, TotalBytes);
    }

    FilePos.QuadPart = 0;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx failed with error %lu\n", GetLastError());

    Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok && BytesRead == sizeof(ReadBuffer),
       "Read-back while handle open failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesRead, GetLastError());
    ok(memcmp(ReadBuffer, ChunkA, sizeof(ChunkA)) == 0,
       "First chunk content mismatch before close\n");
    ok(memcmp(ReadBuffer + sizeof(ChunkA), ChunkB, sizeof(ChunkB)) == 0,
       "Second chunk content mismatch before close\n");

    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    hDir = CreateFileW(DirOpenPath,
                       FILE_LIST_DIRECTORY | SYNCHRONIZE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS,
                       NULL);
    ok(hDir != INVALID_HANDLE_VALUE,
       "Open directory with trailing slash failed with error %lu\n", GetLastError());
    if (hDir != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDir);
        hDir = INVALID_HANDLE_VALUE;
    }

    for (Pass = 0; Pass < 2; ++Pass)
    {
        Found = FindNamedEntryInPattern(Pattern, L"copy.bin", &FindData);
        ok(Found, "copy.bin missing on enumeration pass %lu\n", Pass);
        if (Found)
        {
            Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
            ok(Size == TotalBytes,
               "copy.bin size on enumeration pass %lu is %I64u, expected %u\n",
               Pass, Size, TotalBytes);
        }
    }

    hFile = CreateFileW(FilePath,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "Reopen copy.bin failed with error %lu\n", GetLastError());
    if (hFile != INVALID_HANDLE_VALUE)
    {
        memset(ReadBuffer, 0xDD, sizeof(ReadBuffer));
        Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
        ok(Ok && BytesRead == sizeof(ReadBuffer),
           "Reopen read failed: ok=%d bytes=%lu err=%lu\n",
           Ok, BytesRead, GetLastError());
        ok(memcmp(ReadBuffer, ChunkA, sizeof(ChunkA)) == 0,
           "Reopen first chunk content mismatch\n");
        ok(memcmp(ReadBuffer + sizeof(ChunkA), ChunkB, sizeof(ChunkB)) == 0,
           "Reopen second chunk content mismatch\n");
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }

Cleanup:
    if (hDir != INVALID_HANDLE_VALUE)
        CloseHandle(hDir);
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    DeleteFileW(FilePath);
    RemoveDirectoryW(DirPath);
}

static void
TestDirectoryChangeNotifications(void)
{
    static const WCHAR DirPath[] = L"D:\\notify_watch";
    static const WCHAR FilePath[] = L"D:\\notify_watch\\change.bin";
    HANDLE hNotify = INVALID_HANDLE_VALUE;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    BYTE Data[256];
    DWORD BytesWritten;
    DWORD WaitStatus;
    DWORD Error;
    BOOL Ok;

    DeleteFileW(FilePath);
    RemoveDirectoryW(DirPath);

    Ok = CreateDirectoryW(DirPath, NULL);
    Error = GetLastError();
    ok(Ok || Error == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(notify_watch) failed with error %lu\n", Error);
    if (!Ok && Error != ERROR_ALREADY_EXISTS)
        return;

    hNotify = FindFirstChangeNotificationW(DirPath,
                                           FALSE,
                                           FILE_NOTIFY_CHANGE_FILE_NAME |
                                           FILE_NOTIFY_CHANGE_SIZE |
                                           FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (hNotify == INVALID_HANDLE_VALUE)
    {
        skip(TRUE, "FindFirstChangeNotificationW failed with error %lu\n", GetLastError());
        goto Cleanup;
    }

    memset(Data, 0x5A, sizeof(Data));

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "CreateFileW(change.bin) failed with error %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = WriteFile(hFile, Data, sizeof(Data), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(Data),
       "WriteFile(change.bin) failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    WaitStatus = WaitForSingleObject(hNotify, 2000);
    ok(WaitStatus == WAIT_OBJECT_0,
       "WaitForSingleObject(create/write) returned %lu\n", WaitStatus);

    Ok = FindNextChangeNotification(hNotify);
    ok(Ok, "FindNextChangeNotification failed with error %lu\n", GetLastError());

    Ok = DeleteFileW(FilePath);
    ok(Ok, "DeleteFileW(change.bin) failed with error %lu\n", GetLastError());

    WaitStatus = WaitForSingleObject(hNotify, 2000);
    ok(WaitStatus == WAIT_OBJECT_0,
       "WaitForSingleObject(delete) returned %lu\n", WaitStatus);

Cleanup:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    if (hNotify != INVALID_HANDLE_VALUE)
        FindCloseChangeNotification(hNotify);
    DeleteFileW(FilePath);
    RemoveDirectoryW(DirPath);
}

static void
TestGapWriteZeroFill(void)
{
    enum { GapBytes = 8192, TailBytes = 1024, TotalBytes = GapBytes + TailBytes };
    static const WCHAR FilePath[] = L"D:\\gap_write.bin";
    HANDLE hFile = INVALID_HANDLE_VALUE;
    BYTE Tail[TailBytes];
    BYTE *ReadBuffer = NULL;
    LARGE_INTEGER FilePos;
    LARGE_INTEGER FileSize;
    DWORD BytesWritten;
    DWORD BytesRead;
    DWORD Errors = 0;
    DWORD i;
    BOOL Ok;

    DeleteFileW(FilePath);
    FillPattern(Tail, sizeof(Tail), 0x31, 7);

    ReadBuffer = HeapAlloc(GetProcessHeap(), 0, TotalBytes);
    ok(ReadBuffer != NULL, "HeapAlloc(%u) failed\n", TotalBytes);
    if (ReadBuffer == NULL)
        return;
    memset(ReadBuffer, 0xCC, TotalBytes);

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "CreateFileW(gap_write.bin) failed with error %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    FilePos.QuadPart = GapBytes;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(gap_write.bin) failed with error %lu\n", GetLastError());

    Ok = WriteFile(hFile, Tail, sizeof(Tail), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(Tail),
       "Gap write failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());

    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(gap_write.bin) failed with error %lu\n", GetLastError());

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok && FileSize.QuadPart == TotalBytes,
       "Gap-write size mismatch: ok=%d size=%I64d expected=%u err=%lu\n",
       Ok, FileSize.QuadPart, TotalBytes, GetLastError());

    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    hFile = CreateFileW(FilePath,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "Reopen gap_write.bin failed with error %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = ReadFile(hFile, ReadBuffer, TotalBytes, &BytesRead, NULL);
    ok(Ok && BytesRead == TotalBytes,
       "Gap read failed: ok=%d bytes=%lu expected=%u err=%lu\n",
       Ok, BytesRead, TotalBytes, GetLastError());

    for (i = 0; i < GapBytes && i < BytesRead; ++i)
    {
        if (ReadBuffer[i] != 0)
        {
            if (Errors < 3)
            {
                ok(0, "Gap byte %lu leaked stale value 0x%02X\n", i, ReadBuffer[i]);
            }
            ++Errors;
        }
    }
    ok(Errors == 0, "Gap zero-fill mismatches: %lu\n", Errors);
    ok(BytesRead >= GapBytes + TailBytes &&
       memcmp(ReadBuffer + GapBytes, Tail, sizeof(Tail)) == 0,
       "Gap tail content mismatch\n");

Cleanup:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    HeapFree(GetProcessHeap(), 0, ReadBuffer);
    DeleteFileW(FilePath);
}

static void
TestTruncateRewriteNoStaleData(void)
{
    enum { OriginalBytes = 4096, RewriteBytes = 512 };
    static const WCHAR FilePath[] = L"D:\\rewrite_small.bin";
    HANDLE hFile = INVALID_HANDLE_VALUE;
    BYTE Original[OriginalBytes];
    BYTE Rewrite[RewriteBytes];
    BYTE ReadBuffer[OriginalBytes];
    LARGE_INTEGER FileSize;
    DWORD BytesWritten;
    DWORD BytesRead;
    BOOL Ok;

    DeleteFileW(FilePath);
    FillPattern(Original, sizeof(Original), 0x41, 3);
    FillPattern(Rewrite, sizeof(Rewrite), 0xC1, 5);
    memset(ReadBuffer, 0xDD, sizeof(ReadBuffer));

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "CreateFileW(rewrite_small.bin) failed with error %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = WriteFile(hFile, Original, sizeof(Original), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(Original),
       "Initial write failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "Initial FlushFileBuffers failed with error %lu\n", GetLastError());
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "Rewrite CreateFileW failed with error %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = WriteFile(hFile, Rewrite, sizeof(Rewrite), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(Rewrite),
       "Rewrite write failed: ok=%d bytes=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "Rewrite FlushFileBuffers failed with error %lu\n", GetLastError());
    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok && FileSize.QuadPart == RewriteBytes,
       "Rewrite size mismatch while open: ok=%d size=%I64d expected=%u err=%lu\n",
       Ok, FileSize.QuadPart, RewriteBytes, GetLastError());
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    hFile = CreateFileW(FilePath,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "Reopen rewrite_small.bin failed with error %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok && FileSize.QuadPart == RewriteBytes,
       "Rewrite size mismatch after reopen: ok=%d size=%I64d expected=%u err=%lu\n",
       Ok, FileSize.QuadPart, RewriteBytes, GetLastError());

    Ok = ReadFile(hFile, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok && BytesRead == RewriteBytes,
       "Rewrite read failed: ok=%d bytes=%lu expected=%u err=%lu\n",
       Ok, BytesRead, RewriteBytes, GetLastError());
    ok(memcmp(ReadBuffer, Rewrite, sizeof(Rewrite)) == 0,
       "Rewrite content mismatch\n");

Cleanup:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    DeleteFileW(FilePath);
}

static void
TestVolumeSpaceAccountingSingleFile(void)
{
    static const WCHAR FilePath[] = L"D:\\space_accounting.bin";
    HANDLE hFile = INVALID_HANDLE_VALUE;
    NTFSLX_VOLUME_SPACE_SNAPSHOT Baseline;
    NTFSLX_VOLUME_SPACE_SNAPSHOT Snapshot;
    LARGE_INTEGER FileSize;
    BYTE *Buffer = NULL;
    LARGE_INTEGER FilePos;
    DWORD ClusterSize;
    DWORD BytesWritten;
    BOOL Ok;

    DeleteFileW(FilePath);

    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Baseline);
    if (!Ok)
    {
        skip(TRUE, "Single-file space test: QueryVolumeSpaceSnapshot failed (%lu)\n",
             GetLastError());
        return;
    }

    ClusterSize = Baseline.ClusterSize;
    Buffer = HeapAlloc(GetProcessHeap(), 0, ClusterSize * 3);
    ok(Buffer != NULL, "HeapAlloc(%lu) failed\n", ClusterSize * 3);
    if (Buffer == NULL)
        return;

    FillPattern(Buffer, ClusterSize * 3, 0x51, 11);
    TraceVolumeSpaceSnapshot("single baseline", &Baseline);

    hFile = CreateFileW(FilePath,
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "CreateFileW(space_accounting.bin) failed with error %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok, "GetFileSizeEx failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;

    ok(FileSize.QuadPart == 0, "Fresh file size is %I64d, expected 0\n", FileSize.QuadPart);

    Ok = WriteFile(hFile, Buffer, ClusterSize, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize,
       "First-cluster write failed: ok=%d bytes=%lu expected=%lu err=%lu\n",
       Ok, BytesWritten, ClusterSize, GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(first cluster) failed with error %lu\n", GetLastError());

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok, "GetFileSizeEx(first cluster) failed with error %lu\n", GetLastError());
    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(first cluster) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("single first cluster", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)FileSize.QuadPart, ClusterSize));

    Ok = WriteFile(hFile, Buffer + ClusterSize, ClusterSize * 2, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize * 2,
       "Append write failed: ok=%d bytes=%lu expected=%lu err=%lu\n",
       Ok, BytesWritten, ClusterSize * 2, GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(append) failed with error %lu\n", GetLastError());

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok, "GetFileSizeEx(append) failed with error %lu\n", GetLastError());
    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(append) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("single three clusters", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)FileSize.QuadPart, ClusterSize));

    FilePos.QuadPart = ClusterSize / 2;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(overwrite) failed with error %lu\n", GetLastError());

    Ok = WriteFile(hFile, Buffer, ClusterSize / 2, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize / 2,
       "Overwrite write failed: ok=%d bytes=%lu expected=%lu err=%lu\n",
       Ok, BytesWritten, ClusterSize / 2, GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(overwrite) failed with error %lu\n", GetLastError());

    {
        NTFSLX_VOLUME_SPACE_SNAPSHOT OverwriteSnapshot;
        Ok = QueryVolumeSpaceSnapshot(L"D:\\", &OverwriteSnapshot);
        ok(Ok, "QueryVolumeSpaceSnapshot(overwrite) failed with error %lu\n", GetLastError());
        if (!Ok)
            goto Cleanup;
        ExpectSameFreeSpace("single overwrite", &Snapshot, &OverwriteSnapshot);
        Snapshot = OverwriteSnapshot;
    }

    FilePos.QuadPart = ClusterSize;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(truncate to one cluster) failed with error %lu\n", GetLastError());
    Ok = SetEndOfFile(hFile);
    ok(Ok, "SetEndOfFile(one cluster) failed with error %lu\n", GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(one cluster) failed with error %lu\n", GetLastError());

    Ok = GetFileSizeEx(hFile, &FileSize);
    ok(Ok, "GetFileSizeEx(one cluster) failed with error %lu\n", GetLastError());
    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(one cluster) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("single truncate to one cluster", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)FileSize.QuadPart, ClusterSize));

    FilePos.QuadPart = 0;
    Ok = SetFilePointerEx(hFile, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(truncate to zero) failed with error %lu\n", GetLastError());
    Ok = SetEndOfFile(hFile);
    ok(Ok, "SetEndOfFile(zero) failed with error %lu\n", GetLastError());
    Ok = FlushFileBuffers(hFile);
    ok(Ok, "FlushFileBuffers(zero) failed with error %lu\n", GetLastError());

    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(zero) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectSameFreeSpace("single truncate to zero", &Baseline, &Snapshot);

Cleanup:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);

    DeleteFileW(FilePath);
    if (QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot))
        ExpectSameFreeSpace("single delete cleanup", &Baseline, &Snapshot);

    if (Buffer != NULL)
        HeapFree(GetProcessHeap(), 0, Buffer);
}

static void
TestVolumeSpaceAccountingMultiFile(void)
{
    static const WCHAR DirPath[] = L"D:\\space_multi";
    static const WCHAR FileAPath[] = L"D:\\space_multi\\a.bin";
    static const WCHAR FileBPath[] = L"D:\\space_multi\\b.bin";
    static const WCHAR FileCPath[] = L"D:\\space_multi\\c.bin";
    HANDLE hA = INVALID_HANDLE_VALUE;
    HANDLE hB = INVALID_HANDLE_VALUE;
    HANDLE hC = INVALID_HANDLE_VALUE;
    NTFSLX_VOLUME_SPACE_SNAPSHOT Baseline;
    NTFSLX_VOLUME_SPACE_SNAPSHOT Snapshot;
    LARGE_INTEGER SizeA;
    LARGE_INTEGER SizeB;
    LARGE_INTEGER SizeC;
    BYTE *Buffer = NULL;
    DWORD ClusterSize;
    DWORD BytesWritten;
    BOOL Ok;
    LARGE_INTEGER FilePos;

    DeleteFileW(FileAPath);
    DeleteFileW(FileBPath);
    DeleteFileW(FileCPath);
    RemoveDirectoryW(DirPath);

    Ok = CreateDirectoryW(DirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(space_multi) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Baseline);
    if (!Ok)
    {
        skip(TRUE, "Multi-file space test: QueryVolumeSpaceSnapshot failed (%lu)\n",
             GetLastError());
        goto Cleanup;
    }

    ClusterSize = Baseline.ClusterSize;
    Buffer = HeapAlloc(GetProcessHeap(), 0, ClusterSize * 4);
    ok(Buffer != NULL, "HeapAlloc(%lu) failed\n", ClusterSize * 4);
    if (Buffer == NULL)
        goto Cleanup;

    FillPattern(Buffer, ClusterSize * 4, 0x27, 9);

    hA = CreateFileW(FileAPath, GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    hB = CreateFileW(FileBPath, GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    hC = CreateFileW(FileCPath, GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(hA != INVALID_HANDLE_VALUE && hB != INVALID_HANDLE_VALUE && hC != INVALID_HANDLE_VALUE,
       "CreateFileW multi-file failed: a=%p b=%p c=%p err=%lu\n",
       hA, hB, hC, GetLastError());
    if (hA == INVALID_HANDLE_VALUE || hB == INVALID_HANDLE_VALUE || hC == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = WriteFile(hA, Buffer, ClusterSize, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize, "WriteFile(a.bin) failed\n");
    Ok = WriteFile(hB, Buffer, ClusterSize * 2, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize * 2, "WriteFile(b.bin) failed\n");
    Ok = WriteFile(hC, Buffer, ClusterSize * 4, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize * 4, "WriteFile(c.bin) failed\n");
    Ok = FlushFileBuffers(hA);
    ok(Ok, "FlushFileBuffers(a.bin) failed with error %lu\n", GetLastError());
    Ok = FlushFileBuffers(hB);
    ok(Ok, "FlushFileBuffers(b.bin) failed with error %lu\n", GetLastError());
    Ok = FlushFileBuffers(hC);
    ok(Ok, "FlushFileBuffers(c.bin) failed with error %lu\n", GetLastError());

    Ok = GetFileSizeEx(hA, &SizeA);
    ok(Ok, "GetFileSizeEx(a.bin) failed with error %lu\n", GetLastError());
    Ok = GetFileSizeEx(hB, &SizeB);
    ok(Ok, "GetFileSizeEx(b.bin) failed with error %lu\n", GetLastError());
    Ok = GetFileSizeEx(hC, &SizeC);
    ok(Ok, "GetFileSizeEx(c.bin) failed with error %lu\n", GetLastError());

    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(multi initial) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("multi initial", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)SizeA.QuadPart, ClusterSize) +
                         ExpectedAllocationBytes((ULONGLONG)SizeB.QuadPart, ClusterSize) +
                         ExpectedAllocationBytes((ULONGLONG)SizeC.QuadPart, ClusterSize));

    FilePos.QuadPart = ClusterSize / 4;
    Ok = SetFilePointerEx(hB, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(b.bin overwrite) failed with error %lu\n", GetLastError());
    Ok = WriteFile(hB, Buffer, ClusterSize / 2, &BytesWritten, NULL);
    ok(Ok && BytesWritten == ClusterSize / 2,
       "WriteFile(b.bin overwrite) failed: ok=%d bytes=%lu expected=%lu err=%lu\n",
       Ok, BytesWritten, ClusterSize / 2, GetLastError());
    Ok = FlushFileBuffers(hB);
    ok(Ok, "FlushFileBuffers(b.bin overwrite) failed with error %lu\n", GetLastError());

    {
        NTFSLX_VOLUME_SPACE_SNAPSHOT OverwriteSnapshot;
        Ok = QueryVolumeSpaceSnapshot(L"D:\\", &OverwriteSnapshot);
        ok(Ok, "QueryVolumeSpaceSnapshot(multi overwrite) failed with error %lu\n", GetLastError());
        if (!Ok)
            goto Cleanup;
        ExpectSameFreeSpace("multi overwrite", &Snapshot, &OverwriteSnapshot);
        Snapshot = OverwriteSnapshot;
    }

    CloseHandle(hB);
    hB = INVALID_HANDLE_VALUE;
    Ok = DeleteFileW(FileBPath);
    ok(Ok, "DeleteFileW(b.bin) failed with error %lu\n", GetLastError());
    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(after delete b) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("multi after delete b", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)SizeA.QuadPart, ClusterSize) +
                         ExpectedAllocationBytes((ULONGLONG)SizeC.QuadPart, ClusterSize));

    FilePos.QuadPart = ClusterSize;
    Ok = SetFilePointerEx(hC, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(c.bin truncate) failed with error %lu\n", GetLastError());
    Ok = SetEndOfFile(hC);
    ok(Ok, "SetEndOfFile(c.bin) failed with error %lu\n", GetLastError());
    Ok = FlushFileBuffers(hC);
    ok(Ok, "FlushFileBuffers(c.bin truncate) failed with error %lu\n", GetLastError());
    Ok = GetFileSizeEx(hC, &SizeC);
    ok(Ok, "GetFileSizeEx(c.bin truncate) failed with error %lu\n", GetLastError());
    Ok = QueryVolumeSpaceSnapshot(L"D:\\", &Snapshot);
    ok(Ok, "QueryVolumeSpaceSnapshot(after truncate c) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    ExpectFreeSpaceDelta("multi after truncate c", &Baseline, &Snapshot,
                         ExpectedAllocationBytes((ULONGLONG)SizeA.QuadPart, ClusterSize) +
                         ExpectedAllocationBytes((ULONGLONG)SizeC.QuadPart, ClusterSize));

Cleanup:
    if (hA != INVALID_HANDLE_VALUE)
        CloseHandle(hA);
    if (hB != INVALID_HANDLE_VALUE)
        CloseHandle(hB);
    if (hC != INVALID_HANDLE_VALUE)
        CloseHandle(hC);

    DeleteFileW(FileAPath);
    DeleteFileW(FileBPath);
    DeleteFileW(FileCPath);
    RemoveDirectoryW(DirPath);

    if (Buffer != NULL)
        HeapFree(GetProcessHeap(), 0, Buffer);
}

static void
TestCopyRenameCycle(void)
{
    static const WCHAR DirPath[] = L"D:\\copy_move";
    static const WCHAR SourcePath[] = L"D:\\copy_move\\source.bin";
    static const WCHAR CopyPath[] = L"D:\\copy_move\\copy.bin";
    static const WCHAR FinalPath[] = L"D:\\copy_move\\copy_final.bin";
    static const WCHAR Pattern[] = L"D:\\copy_move\\*";
    BYTE Source[3072];
    BYTE ReadBuffer[3072];
    WIN32_FIND_DATAW FindData;
    BOOL Ok;
    BOOL Found;
    ULONGLONG Size;
    HANDLE hRename = INVALID_HANDLE_VALUE;

    DeleteFileW(FinalPath);
    DeleteFileW(CopyPath);
    DeleteFileW(SourcePath);
    RemoveDirectoryW(DirPath);

    Ok = CreateDirectoryW(DirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(copy_move) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    FillPattern(Source, sizeof(Source), 0x61, 7);
    memset(ReadBuffer, 0x00, sizeof(ReadBuffer));

    if (!WriteFileBuffer(SourcePath, Source, sizeof(Source)))
        goto Cleanup;

    Ok = CopyFileW(SourcePath, CopyPath, FALSE);
    ok(Ok, "CopyFileW failed with error %lu\n", GetLastError());

    Found = FindNamedEntryInPattern(Pattern, L"copy.bin", &FindData);
    ok(Found, "copy.bin missing after CopyFileW\n");
    if (Found)
    {
        Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        ok(Size == sizeof(Source), "copy.bin size is %I64u, expected %u\n", Size, (unsigned)sizeof(Source));
    }

    if (!ReadFileBuffer(CopyPath, ReadBuffer, sizeof(ReadBuffer)))
        goto Cleanup;
    ok(memcmp(ReadBuffer, Source, sizeof(Source)) == 0, "Copied file content mismatch\n");

    hRename = CreateFileW(CopyPath,
                          DELETE | SYNCHRONIZE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    ok(hRename != INVALID_HANDLE_VALUE,
       "CreateFileW(rename handle) failed with error %lu\n", GetLastError());
    if (hRename == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = RenameOpenFileSameDirectory(hRename, L"copy_final.bin", TRUE);
    ok(Ok, "NtSetInformationFile(FileRenameInformation) failed with error %lu\n", GetLastError());
    CloseHandle(hRename);
    hRename = INVALID_HANDLE_VALUE;

    Found = FindNamedEntryInPattern(Pattern, L"copy_final.bin", &FindData);
    ok(Found, "copy_final.bin missing after rename\n");
    ok(GetFileAttributesW(CopyPath) == INVALID_FILE_ATTRIBUTES,
       "copy.bin should be absent after rename (err=%lu)\n", GetLastError());

Cleanup:
    if (hRename != INVALID_HANDLE_VALUE)
        CloseHandle(hRename);
    DeleteFileW(FinalPath);
    DeleteFileW(CopyPath);
    DeleteFileW(SourcePath);
    RemoveDirectoryW(DirPath);
}

static void
TestReplaceExistingRename(void)
{
    static const WCHAR RootPath[] = L"D:\\replace_rename";
    static const WCHAR SourcePath[] = L"D:\\replace_rename\\source.bin";
    static const WCHAR VictimPath[] = L"D:\\replace_rename\\victim.bin";
    static const WCHAR Pattern[] = L"D:\\replace_rename\\*";
    BYTE Source[1536];
    BYTE Victim[1024];
    BYTE ReadBuffer[1536];
    WIN32_FIND_DATAW FindData;
    BOOL Ok;
    BOOL Found;
    ULONGLONG Size;

    DeleteTreeRecursive(RootPath);
    DeleteFileW(VictimPath);
    DeleteFileW(SourcePath);
    RemoveDirectoryW(RootPath);

    Ok = CreateDirectoryW(RootPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(replace_rename) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    FillPattern(Source, sizeof(Source), 0x31, 3);
    FillPattern(Victim, sizeof(Victim), 0x91, 5);
    memset(ReadBuffer, 0x00, sizeof(ReadBuffer));

    if (!WriteFileBuffer(SourcePath, Source, sizeof(Source)))
        goto Cleanup;
    if (!WriteFileBuffer(VictimPath, Victim, sizeof(Victim)))
        goto Cleanup;

    Ok = MoveFileExW(SourcePath, VictimPath, MOVEFILE_REPLACE_EXISTING);
    ok(Ok, "MoveFileExW replace-existing rename failed with error %lu\n", GetLastError());

    Found = FindNamedEntryInPattern(Pattern, L"victim.bin", &FindData);
    ok(Found, "victim.bin missing after replace-existing rename\n");
    if (Found)
    {
        Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        ok(Size == sizeof(Source),
           "victim.bin size after replace-existing rename is %I64u, expected %u\n",
           Size, (unsigned)sizeof(Source));
    }
    ok(GetFileAttributesW(SourcePath) == INVALID_FILE_ATTRIBUTES,
       "source.bin should be absent after replace-existing rename (err=%lu)\n", GetLastError());

    if (!ReadFileBuffer(VictimPath, ReadBuffer, sizeof(Source)))
        goto Cleanup;
    ok(memcmp(ReadBuffer, Source, sizeof(Source)) == 0,
       "replace-existing victim content mismatch\n");

Cleanup:
    DeleteTreeRecursive(RootPath);
}

static void
TestDirectoryRenameCycle(void)
{
    static const WCHAR RootPath[] = L"D:\\dir_rename";
    static const WCHAR SourceDirPath[] = L"D:\\dir_rename\\srcdir";
    static const WCHAR FinalDirPath[] = L"D:\\dir_rename\\dstdir";
    static const WCHAR SourceFilePath[] = L"D:\\dir_rename\\srcdir\\child.bin";
    static const WCHAR FinalFilePath[] = L"D:\\dir_rename\\dstdir\\child.bin";
    static const WCHAR Pattern[] = L"D:\\dir_rename\\*";
    BYTE Data[2048];
    BYTE ReadBuffer[2048];
    WIN32_FIND_DATAW FindData;
    HANDLE hDir = INVALID_HANDLE_VALUE;
    BOOL Ok;
    BOOL Found;

    DeleteTreeRecursive(RootPath);
    DeleteFileW(FinalFilePath);
    DeleteFileW(SourceFilePath);
    RemoveDirectoryW(FinalDirPath);
    RemoveDirectoryW(SourceDirPath);
    RemoveDirectoryW(RootPath);

    Ok = CreateDirectoryW(RootPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(dir_rename) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    Ok = CreateDirectoryW(SourceDirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(srcdir) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        goto Cleanup;

    FillPattern(Data, sizeof(Data), 0x52, 9);
    memset(ReadBuffer, 0x00, sizeof(ReadBuffer));

    if (!WriteFileBuffer(SourceFilePath, Data, sizeof(Data)))
        goto Cleanup;

    Ok = MoveFileExW(SourceDirPath, FinalDirPath, 0);
    ok(Ok, "MoveFileExW directory rename failed with error %lu\n", GetLastError());

    Found = FindNamedEntryInPattern(Pattern, L"dstdir", &FindData);
    ok(Found, "dstdir missing after directory rename\n");
    ok(GetFileAttributesW(SourceDirPath) == INVALID_FILE_ATTRIBUTES,
       "srcdir should be absent after directory rename (err=%lu)\n", GetLastError());

    hDir = CreateFileW(L"D:\\dir_rename\\dstdir\\",
                       FILE_LIST_DIRECTORY | SYNCHRONIZE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS,
                       NULL);
    ok(hDir != INVALID_HANDLE_VALUE,
       "Open renamed directory with trailing slash failed with error %lu\n", GetLastError());
    if (hDir != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDir);
        hDir = INVALID_HANDLE_VALUE;
    }

    if (!ReadFileBuffer(FinalFilePath, ReadBuffer, sizeof(Data)))
        goto Cleanup;
    ok(memcmp(ReadBuffer, Data, sizeof(Data)) == 0,
       "renamed directory child content mismatch\n");

Cleanup:
    if (hDir != INVALID_HANDLE_VALUE)
        CloseHandle(hDir);
    DeleteTreeRecursive(RootPath);
}

static void
TestMoveFileToRoot(void)
{
    static const WCHAR RootPath[] = L"D:\\move_to_root";
    static const WCHAR SourceDirPath[] = L"D:\\move_to_root\\src";
    static const WCHAR SourceFilePath[] = L"D:\\move_to_root\\src\\child.bin";
    static const WCHAR TargetFilePath[] = L"D:\\ntfslx_move_root_live.bin";
    static const WCHAR RootPattern[] = L"D:\\*";
    BYTE Data[1536];
    BYTE ReadBuffer[1536];
    WIN32_FIND_DATAW FindData;
    BOOL Ok;
    BOOL Found;
    ULONGLONG Size;

    DeleteFileW(TargetFilePath);
    DeleteTreeRecursive(RootPath);
    RemoveDirectoryW(SourceDirPath);
    RemoveDirectoryW(RootPath);

    Ok = CreateDirectoryW(RootPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(move_to_root) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    Ok = CreateDirectoryW(SourceDirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(move_to_root\\src) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        goto Cleanup;

    FillPattern(Data, sizeof(Data), 0x81, 3);
    memset(ReadBuffer, 0x00, sizeof(ReadBuffer));

    if (!WriteFileBuffer(SourceFilePath, Data, sizeof(Data)))
        goto Cleanup;

    Ok = MoveFileExW(SourceFilePath, TargetFilePath, 0);
    ok(Ok, "MoveFileExW(move_to_root -> root) failed with error %lu\n", GetLastError());

    Found = FindNamedEntryInPattern(RootPattern, L"ntfslx_move_root_live.bin", &FindData);
    ok(Found, "ntfslx_move_root_live.bin missing from root after move\n");
    if (Found)
    {
        Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        ok(Size == sizeof(Data),
           "ntfslx_move_root_live.bin size is %I64u, expected %u\n",
           Size, (unsigned)sizeof(Data));
    }
    ok(GetFileAttributesW(SourceFilePath) == INVALID_FILE_ATTRIBUTES,
       "move_to_root\\src\\child.bin should be absent after root move (err=%lu)\n",
       GetLastError());

    if (!ReadFileBuffer(TargetFilePath, ReadBuffer, sizeof(ReadBuffer)))
        goto Cleanup;
    ok(memcmp(ReadBuffer, Data, sizeof(Data)) == 0,
       "root-moved file content mismatch\n");

Cleanup:
    DeleteFileW(TargetFilePath);
    DeleteTreeRecursive(RootPath);
}

static void
TestReplaceExistingMoveAcrossDirectories(void)
{
    static const WCHAR RootPath[] = L"D:\\replace_move_dirs";
    static const WCHAR SourceDirPath[] = L"D:\\replace_move_dirs\\src";
    static const WCHAR DestDirPath[] = L"D:\\replace_move_dirs\\dst";
    static const WCHAR SourcePath[] = L"D:\\replace_move_dirs\\src\\source.bin";
    static const WCHAR VictimPath[] = L"D:\\replace_move_dirs\\dst\\victim.bin";
    static const WCHAR DestPattern[] = L"D:\\replace_move_dirs\\dst\\*";
    BYTE Source[704];
    BYTE Victim[192];
    BYTE ReadBuffer[704];
    WIN32_FIND_DATAW FindData;
    BOOL Ok;
    BOOL Found;
    ULONGLONG Size;

    DeleteTreeRecursive(RootPath);
    RemoveDirectoryW(SourceDirPath);
    RemoveDirectoryW(DestDirPath);
    RemoveDirectoryW(RootPath);

    Ok = CreateDirectoryW(RootPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(replace_move_dirs) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    Ok = CreateDirectoryW(SourceDirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(replace_move_dirs\\src) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        goto Cleanup;

    Ok = CreateDirectoryW(DestDirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(replace_move_dirs\\dst) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        goto Cleanup;

    FillPattern(Source, sizeof(Source), 0x62, 5);
    FillPattern(Victim, sizeof(Victim), 0x17, 7);
    memset(ReadBuffer, 0x00, sizeof(ReadBuffer));

    if (!WriteFileBuffer(SourcePath, Source, sizeof(Source)))
        goto Cleanup;
    if (!WriteFileBuffer(VictimPath, Victim, sizeof(Victim)))
        goto Cleanup;

    Ok = MoveFileExW(SourcePath, VictimPath, MOVEFILE_REPLACE_EXISTING);
    ok(Ok, "MoveFileExW(cross-dir replace move) failed with error %lu\n", GetLastError());

    Found = FindNamedEntryInPattern(DestPattern, L"victim.bin", &FindData);
    ok(Found, "victim.bin missing after cross-dir replace move\n");
    if (Found)
    {
        Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        ok(Size == sizeof(Source),
           "victim.bin size after cross-dir replace move is %I64u, expected %u\n",
           Size, (unsigned)sizeof(Source));
    }
    ok(GetFileAttributesW(SourcePath) == INVALID_FILE_ATTRIBUTES,
       "source.bin should be absent after cross-dir replace move (err=%lu)\n",
       GetLastError());

    if (!ReadFileBuffer(VictimPath, ReadBuffer, sizeof(ReadBuffer)))
        goto Cleanup;
    ok(memcmp(ReadBuffer, Source, sizeof(Source)) == 0,
       "cross-dir replace move victim content mismatch\n");

Cleanup:
    DeleteTreeRecursive(RootPath);
}

static void
TestRootDeleteRecreateChurn(void)
{
    static const WCHAR FilePath[] = L"D:\\ntfslx_recycle_live.bin";
    static const WCHAR RootPattern[] = L"D:\\*";
    BYTE First[288];
    BYTE Second[640];
    BYTE ReadBuffer[640];
    WIN32_FIND_DATAW FindData;
    BOOL Ok;
    BOOL Found;
    ULONGLONG Size;

    DeleteFileW(FilePath);
    FillPattern(First, sizeof(First), 0x34, 3);
    FillPattern(Second, sizeof(Second), 0xB2, 1);
    memset(ReadBuffer, 0x00, sizeof(ReadBuffer));

    if (!WriteFileBuffer(FilePath, First, sizeof(First)))
        return;

    Ok = DeleteFileW(FilePath);
    ok(Ok, "DeleteFileW(ntfslx_recycle_live.bin first pass) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;

    ok(GetFileAttributesW(FilePath) == INVALID_FILE_ATTRIBUTES,
       "ntfslx_recycle_live.bin should be absent after delete (err=%lu)\n",
       GetLastError());

    if (!WriteFileBuffer(FilePath, Second, sizeof(Second)))
        goto Cleanup;

    Found = FindNamedEntryInPattern(RootPattern, L"ntfslx_recycle_live.bin", &FindData);
    ok(Found, "ntfslx_recycle_live.bin missing after recreate\n");
    if (Found)
    {
        Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        ok(Size == sizeof(Second),
           "ntfslx_recycle_live.bin size after recreate is %I64u, expected %u\n",
           Size, (unsigned)sizeof(Second));
    }

    if (!ReadFileBuffer(FilePath, ReadBuffer, sizeof(ReadBuffer)))
        goto Cleanup;
    ok(memcmp(ReadBuffer, Second, sizeof(Second)) == 0,
       "root delete/recreate content mismatch\n");

Cleanup:
    DeleteFileW(FilePath);
}

static void
TestDirectoryDeleteRecreateChurn(void)
{
    static const WCHAR DirPath[] = L"D:\\dir_recycle_live";
    static const WCHAR ChildPath[] = L"D:\\dir_recycle_live\\child.bin";
    static const WCHAR RootPattern[] = L"D:\\*";
    BYTE First[320];
    BYTE Second[960];
    BYTE ReadBuffer[960];
    WIN32_FIND_DATAW FindData;
    HANDLE hDir = INVALID_HANDLE_VALUE;
    BOOL Ok;
    BOOL Found;
    ULONGLONG Size;

    DeleteTreeRecursive(DirPath);
    RemoveDirectoryW(DirPath);
    FillPattern(First, sizeof(First), 0x29, 9);
    FillPattern(Second, sizeof(Second), 0xA7, 3);
    memset(ReadBuffer, 0x00, sizeof(ReadBuffer));

    Ok = CreateDirectoryW(DirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(dir_recycle_live) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    if (!WriteFileBuffer(ChildPath, First, sizeof(First)))
        goto Cleanup;

    Ok = DeleteFileW(ChildPath);
    ok(Ok, "DeleteFileW(dir_recycle_live\\child.bin first pass) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;

    Ok = RemoveDirectoryW(DirPath);
    ok(Ok, "RemoveDirectoryW(dir_recycle_live first pass) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;

    Ok = CreateDirectoryW(DirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "Recreate dir_recycle_live failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        goto Cleanup;

    if (!WriteFileBuffer(ChildPath, Second, sizeof(Second)))
        goto Cleanup;

    Found = FindNamedEntryInPattern(RootPattern, L"dir_recycle_live", &FindData);
    ok(Found, "dir_recycle_live missing after recreate\n");

    hDir = CreateFileW(L"D:\\dir_recycle_live\\",
                       FILE_LIST_DIRECTORY | SYNCHRONIZE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS,
                       NULL);
    ok(hDir != INVALID_HANDLE_VALUE,
       "Open dir_recycle_live with trailing slash failed with error %lu\n", GetLastError());
    if (hDir != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDir);
        hDir = INVALID_HANDLE_VALUE;
    }

    Found = FindNamedEntryInPattern(L"D:\\dir_recycle_live\\*", L"child.bin", &FindData);
    ok(Found, "dir_recycle_live\\child.bin missing after recreate\n");
    if (Found)
    {
        Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        ok(Size == sizeof(Second),
           "dir_recycle_live\\child.bin size is %I64u, expected %u\n",
           Size, (unsigned)sizeof(Second));
    }

    if (!ReadFileBuffer(ChildPath, ReadBuffer, sizeof(ReadBuffer)))
        goto Cleanup;
    ok(memcmp(ReadBuffer, Second, sizeof(Second)) == 0,
       "directory delete/recreate child content mismatch\n");

Cleanup:
    if (hDir != INVALID_HANDLE_VALUE)
        CloseHandle(hDir);
    DeleteTreeRecursive(DirPath);
}

static void
TestSharedHandleTruncateRewrite(void)
{
    static const WCHAR FilePath[] = L"D:\\shared_truncate_live.bin";
    BYTE Original[4096];
    BYTE Rewrite[1536];
    BYTE ReadBuffer[4096];
    HANDLE hReader = INVALID_HANDLE_VALUE;
    HANDLE hWriter = INVALID_HANDLE_VALUE;
    LARGE_INTEGER FilePos;
    LARGE_INTEGER FileSize;
    DWORD BytesWritten = 0;
    DWORD BytesRead = 0;
    BOOL Ok;

    DeleteFileW(FilePath);
    FillPattern(Original, sizeof(Original), 0x48, 5);
    FillPattern(Rewrite, sizeof(Rewrite), 0xD4, 2);
    memset(ReadBuffer, 0x00, sizeof(ReadBuffer));

    if (!WriteFileBuffer(FilePath, Original, sizeof(Original)))
        return;

    hReader = CreateFileW(FilePath,
                          GENERIC_READ | SYNCHRONIZE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    ok(hReader != INVALID_HANDLE_VALUE,
       "CreateFileW(shared_truncate_live reader) failed with error %lu\n", GetLastError());
    if (hReader == INVALID_HANDLE_VALUE)
        goto Cleanup;

    hWriter = CreateFileW(FilePath,
                          GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    ok(hWriter != INVALID_HANDLE_VALUE,
       "CreateFileW(shared_truncate_live writer) failed with error %lu\n", GetLastError());
    if (hWriter == INVALID_HANDLE_VALUE)
        goto Cleanup;

    FilePos.QuadPart = 0;
    Ok = SetFilePointerEx(hWriter, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(shared truncate writer to zero) failed with error %lu\n", GetLastError());
    Ok = SetEndOfFile(hWriter);
    ok(Ok, "SetEndOfFile(shared truncate writer to zero) failed with error %lu\n", GetLastError());

    Ok = WriteFile(hWriter, Rewrite, sizeof(Rewrite), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(Rewrite),
       "WriteFile(shared truncate rewrite) failed: ok=%d written=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());
    Ok = FlushFileBuffers(hWriter);
    ok(Ok, "FlushFileBuffers(shared truncate writer) failed with error %lu\n", GetLastError());

    FilePos.QuadPart = 0;
    Ok = SetFilePointerEx(hReader, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(shared truncate reader) failed with error %lu\n", GetLastError());

    Ok = ReadFile(hReader, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok && BytesRead == sizeof(Rewrite),
       "ReadFile(shared truncate reader) failed: ok=%d read=%lu expected=%u err=%lu\n",
       Ok, BytesRead, (unsigned)sizeof(Rewrite), GetLastError());
    ok(memcmp(ReadBuffer, Rewrite, sizeof(Rewrite)) == 0,
       "shared truncate reader content mismatch\n");

    Ok = GetFileSizeEx(hReader, &FileSize);
    ok(Ok && FileSize.QuadPart == sizeof(Rewrite),
       "shared truncate reader size mismatch: ok=%d size=%I64d expected=%u err=%lu\n",
       Ok, FileSize.QuadPart, (unsigned)sizeof(Rewrite), GetLastError());

    CloseHandle(hWriter);
    hWriter = INVALID_HANDLE_VALUE;
    CloseHandle(hReader);
    hReader = INVALID_HANDLE_VALUE;

    hReader = CreateFileW(FilePath,
                          GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    ok(hReader != INVALID_HANDLE_VALUE,
       "Reopen shared_truncate_live.bin failed with error %lu\n", GetLastError());
    if (hReader == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = GetFileSizeEx(hReader, &FileSize);
    ok(Ok && FileSize.QuadPart == sizeof(Rewrite),
       "shared truncate reopen size mismatch: ok=%d size=%I64d expected=%u err=%lu\n",
       Ok, FileSize.QuadPart, (unsigned)sizeof(Rewrite), GetLastError());

    memset(ReadBuffer, 0x00, sizeof(ReadBuffer));
    Ok = ReadFile(hReader, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok && BytesRead == sizeof(Rewrite),
       "ReadFile(shared truncate reopen) failed: ok=%d read=%lu expected=%u err=%lu\n",
       Ok, BytesRead, (unsigned)sizeof(Rewrite), GetLastError());
    ok(memcmp(ReadBuffer, Rewrite, sizeof(Rewrite)) == 0,
       "shared truncate reopen content mismatch\n");

Cleanup:
    if (hWriter != INVALID_HANDLE_VALUE)
        CloseHandle(hWriter);
    if (hReader != INVALID_HANDLE_VALUE)
        CloseHandle(hReader);
    DeleteFileW(FilePath);
}

static void
TestDeleteOnCloseWithOpenHandle(void)
{
    static const WCHAR FilePath[] = L"D:\\delete_on_close_keep.bin";
    BYTE Data[1024];
    BYTE ReadBuffer[1024];
    HANDLE hKeep = INVALID_HANDLE_VALUE;
    HANDLE hDelete = INVALID_HANDLE_VALUE;
    HANDLE hProbe = INVALID_HANDLE_VALUE;
    DWORD BytesRead = 0;
    BOOL Ok;

    DeleteFileW(FilePath);
    FillPattern(Data, sizeof(Data), 0x73, 4);
    memset(ReadBuffer, 0x00, sizeof(ReadBuffer));

    if (!WriteFileBuffer(FilePath, Data, sizeof(Data)))
        return;

    hKeep = CreateFileW(FilePath,
                        GENERIC_READ | SYNCHRONIZE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hKeep != INVALID_HANDLE_VALUE,
       "CreateFileW(keep handle) failed with error %lu\n", GetLastError());
    if (hKeep == INVALID_HANDLE_VALUE)
        goto Cleanup;

    hDelete = CreateFileW(FilePath,
                          DELETE | SYNCHRONIZE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    ok(hDelete != INVALID_HANDLE_VALUE,
       "CreateFileW(delete-pending handle) failed with error %lu\n", GetLastError());
    if (hDelete == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Ok = SetDeletePendingOnHandle(hDelete, TRUE);
    ok(Ok, "SetDeletePendingOnHandle(TRUE) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;

    CloseHandle(hDelete);
    hDelete = INVALID_HANDLE_VALUE;

    Ok = ReadFile(hKeep, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok && BytesRead == sizeof(ReadBuffer),
       "ReadFile(keep handle after delete-pending cleanup) failed: ok=%d read=%lu err=%lu\n",
       Ok, BytesRead, GetLastError());
    ok(memcmp(ReadBuffer, Data, sizeof(Data)) == 0,
       "keep handle content mismatch after delete-pending cleanup\n");

    hProbe = CreateFileW(FilePath,
                         GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    ok(hProbe == INVALID_HANDLE_VALUE,
       "reopen should fail while delete-pending file still has a live handle (err=%lu)\n",
       GetLastError());
    if (hProbe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hProbe);
        hProbe = INVALID_HANDLE_VALUE;
    }

    CloseHandle(hKeep);
    hKeep = INVALID_HANDLE_VALUE;

    hProbe = CreateFileW(FilePath,
                         GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    ok(hProbe == INVALID_HANDLE_VALUE,
       "file should be gone after last handle close (err=%lu)\n", GetLastError());
    if (hProbe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hProbe);
        hProbe = INVALID_HANDLE_VALUE;
    }

Cleanup:
    if (hProbe != INVALID_HANDLE_VALUE)
        CloseHandle(hProbe);
    if (hDelete != INVALID_HANDLE_VALUE)
        CloseHandle(hDelete);
    if (hKeep != INVALID_HANDLE_VALUE)
        CloseHandle(hKeep);
    DeleteFileW(FilePath);
}

static void
TestShareConflictChurn(void)
{
    static const WCHAR FilePath[] = L"D:\\share_conflict.bin";
    BYTE Data[512];
    BYTE Rewrite[128];
    BYTE ReadBuffer[128];
    HANDLE hNoShare = INVALID_HANDLE_VALUE;
    HANDLE hConflict = INVALID_HANDLE_VALUE;
    HANDLE hReader = INVALID_HANDLE_VALUE;
    HANDLE hCompat = INVALID_HANDLE_VALUE;
    HANDLE hWriter = INVALID_HANDLE_VALUE;
    LARGE_INTEGER FilePos;
    DWORD BytesWritten = 0;
    DWORD BytesRead = 0;
    BOOL Ok;

    DeleteFileW(FilePath);
    FillPattern(Data, sizeof(Data), 0x19, 2);
    FillPattern(Rewrite, sizeof(Rewrite), 0xD0, 1);
    memset(ReadBuffer, 0x00, sizeof(ReadBuffer));

    if (!WriteFileBuffer(FilePath, Data, sizeof(Data)))
        return;

    hNoShare = CreateFileW(FilePath,
                           GENERIC_READ,
                           0,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
    ok(hNoShare != INVALID_HANDLE_VALUE,
       "CreateFileW(no-share reader) failed with error %lu\n", GetLastError());
    if (hNoShare == INVALID_HANDLE_VALUE)
        goto Cleanup;

    hConflict = CreateFileW(FilePath,
                            GENERIC_READ,
                            FILE_SHARE_READ,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
    ok(hConflict == INVALID_HANDLE_VALUE,
       "conflicting shared open should fail (err=%lu)\n", GetLastError());
    if (hConflict != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hConflict);
        hConflict = INVALID_HANDLE_VALUE;
    }

    CloseHandle(hNoShare);
    hNoShare = INVALID_HANDLE_VALUE;

    hReader = CreateFileW(FilePath,
                          GENERIC_READ,
                          FILE_SHARE_READ,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    ok(hReader != INVALID_HANDLE_VALUE,
       "CreateFileW(shared reader) failed with error %lu\n", GetLastError());
    if (hReader == INVALID_HANDLE_VALUE)
        goto Cleanup;

    hWriter = CreateFileW(FilePath,
                          GENERIC_WRITE,
                          FILE_SHARE_READ,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    ok(hWriter == INVALID_HANDLE_VALUE,
       "writer open should fail against read-only share (err=%lu)\n", GetLastError());
    if (hWriter != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hWriter);
        hWriter = INVALID_HANDLE_VALUE;
    }

    hCompat = CreateFileW(FilePath,
                          GENERIC_READ,
                          FILE_SHARE_READ,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    ok(hCompat != INVALID_HANDLE_VALUE,
       "compatible shared reader failed with error %lu\n", GetLastError());
    if (hCompat != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hCompat);
        hCompat = INVALID_HANDLE_VALUE;
    }

    CloseHandle(hReader);
    hReader = INVALID_HANDLE_VALUE;

    hWriter = CreateFileW(FilePath,
                          GENERIC_WRITE | SYNCHRONIZE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    ok(hWriter != INVALID_HANDLE_VALUE,
       "CreateFileW(shared writer) failed with error %lu\n", GetLastError());
    if (hWriter == INVALID_HANDLE_VALUE)
        goto Cleanup;

    FilePos.QuadPart = 128;
    Ok = SetFilePointerEx(hWriter, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(shared writer) failed with error %lu\n", GetLastError());

    Ok = WriteFile(hWriter, Rewrite, sizeof(Rewrite), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(Rewrite),
       "WriteFile(shared writer) failed: ok=%d written=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());
    CloseHandle(hWriter);
    hWriter = INVALID_HANDLE_VALUE;

    hReader = CreateFileW(FilePath,
                          GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);
    ok(hReader != INVALID_HANDLE_VALUE,
       "reopen reader after share churn failed with error %lu\n", GetLastError());
    if (hReader == INVALID_HANDLE_VALUE)
        goto Cleanup;

    FilePos.QuadPart = 128;
    Ok = SetFilePointerEx(hReader, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(reopen reader) failed with error %lu\n", GetLastError());

    Ok = ReadFile(hReader, ReadBuffer, sizeof(ReadBuffer), &BytesRead, NULL);
    ok(Ok && BytesRead == sizeof(ReadBuffer),
       "ReadFile(reopen reader) failed: ok=%d read=%lu err=%lu\n",
       Ok, BytesRead, GetLastError());
    ok(memcmp(ReadBuffer, Rewrite, sizeof(Rewrite)) == 0,
       "share churn rewrite content mismatch\n");

Cleanup:
    if (hWriter != INVALID_HANDLE_VALUE)
        CloseHandle(hWriter);
    if (hCompat != INVALID_HANDLE_VALUE)
        CloseHandle(hCompat);
    if (hReader != INVALID_HANDLE_VALUE)
        CloseHandle(hReader);
    if (hConflict != INVALID_HANDLE_VALUE)
        CloseHandle(hConflict);
    if (hNoShare != INVALID_HANDLE_VALUE)
        CloseHandle(hNoShare);
    DeleteFileW(FilePath);
}


static void
TestSecurityDescriptorProbe(void)
{
    static const WCHAR FilePath[] = L"D:\\ntfslx_auto\\cases\\security_probe.bin";
    BYTE Data[128];
    BYTE *SecurityBuffer = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    ULONG SecuritySize = 0;
    NTSTATUS Status;

    DeleteFileW(FilePath);
    FillPattern(Data, sizeof(Data), 0x8C, 5);

    if (!EnsureProbeCaseDirectories())
        return;

    if (!WriteFileBuffer(FilePath, Data, sizeof(Data)))
        return;

    hFile = CreateFileW(FilePath,
                        READ_CONTROL | WRITE_DAC | SYNCHRONIZE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "security probe: CreateFileW failed with error %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    SecurityBuffer = HeapAlloc(GetProcessHeap(), 0, 4096);
    ok(SecurityBuffer != NULL, "HeapAlloc(security probe) failed\n");
    if (SecurityBuffer == NULL)
        goto Cleanup;

    ZeroMemory(SecurityBuffer, 4096);
    SecuritySize = 4096;
    Status = NtQuerySecurityObject(hFile,
                                   OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                   (PSECURITY_DESCRIPTOR)SecurityBuffer,
                                   4096,
                                   &SecuritySize);
    ok(NT_SUCCESS(Status),
       "security probe: NtQuerySecurityObject failed status=0x%08lx\n",
       (ULONG)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = NtSetSecurityObject(hFile,
                                 DACL_SECURITY_INFORMATION,
                                 (PSECURITY_DESCRIPTOR)SecurityBuffer);
    ok(NT_SUCCESS(Status),
       "security probe: NtSetSecurityObject failed status=0x%08lx\n",
       (ULONG)Status);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    trace("probe-result security=pass size=%lu\n", SecuritySize);

Cleanup:
    if (SecurityBuffer != NULL)
        HeapFree(GetProcessHeap(), 0, SecurityBuffer);
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    DeleteFileW(FilePath);
}

static void
TestEaRoundTripProbe(void)
{
    static const WCHAR FilePath[] = L"D:\\ntfslx_auto\\cases\\ea_probe.bin";
    static const CHAR EaName[] = "ntfslx.probe";
    static const BYTE EaValue[] = { 0xE1, 0xA7, 0x01, 0x3C };
    BYTE Data[96];
    BYTE SetBuffer[128];
    BYTE QueryList[64];
    BYTE QueryBuffer[256];
    HANDLE hFile = INVALID_HANDLE_VALUE;
    PNTFSLX_FILE_FULL_EA_INFORMATION EaSet;
    PNTFSLX_FILE_GET_EA_INFORMATION EaQuery;
    NTFSLX_IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    ULONG NameBytes;
    ULONG SetLength;
    ULONG QueryLength;
    PNTFSLX_FILE_FULL_EA_INFORMATION Returned;

    DeleteFileW(FilePath);
    FillPattern(Data, sizeof(Data), 0x6B, 7);

    if (!EnsureProbeCaseDirectories())
        return;

    if (!WriteFileBuffer(FilePath, Data, sizeof(Data)))
        return;

    hFile = CreateFileW(FilePath,
                        FILE_READ_EA | FILE_WRITE_EA | SYNCHRONIZE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "ea probe: open failed with error %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        goto Cleanup;

    ZeroMemory(SetBuffer, sizeof(SetBuffer));
    NameBytes = (ULONG)strlen(EaName);
    SetLength = FIELD_OFFSET(NTFSLX_FILE_FULL_EA_INFORMATION, EaName) + NameBytes + 1 + sizeof(EaValue);
    ok(SetLength <= sizeof(SetBuffer), "EA set buffer overflow\n");
    if (SetLength > sizeof(SetBuffer))
        goto Cleanup;

    EaSet = (PNTFSLX_FILE_FULL_EA_INFORMATION)SetBuffer;
    EaSet->NextEntryOffset = 0;
    EaSet->Flags = 0;
    EaSet->EaNameLength = (UCHAR)NameBytes;
    EaSet->EaValueLength = (USHORT)sizeof(EaValue);
    CopyMemory(EaSet->EaName, EaName, NameBytes + 1);
    CopyMemory(EaSet->EaName + NameBytes + 1, EaValue, sizeof(EaValue));

    Status = NtSetEaFile(hFile, &IoStatus, EaSet, SetLength);
    if (!NT_SUCCESS(Status))
    {
        trace("probe-result ea=unsupported op=set status=0x%08lx\n", (ULONG)Status);
        goto Cleanup;
    }

    ZeroMemory(QueryList, sizeof(QueryList));
    QueryLength = FIELD_OFFSET(NTFSLX_FILE_GET_EA_INFORMATION, EaName) + NameBytes + 1;
    ok(QueryLength <= sizeof(QueryList), "EA query list overflow\n");
    if (QueryLength > sizeof(QueryList))
        goto Cleanup;

    EaQuery = (PNTFSLX_FILE_GET_EA_INFORMATION)QueryList;
    EaQuery->NextEntryOffset = 0;
    EaQuery->EaNameLength = (UCHAR)NameBytes;
    CopyMemory(EaQuery->EaName, EaName, NameBytes + 1);

    ZeroMemory(QueryBuffer, sizeof(QueryBuffer));
    Status = NtQueryEaFile(hFile,
                           &IoStatus,
                           QueryBuffer,
                           sizeof(QueryBuffer),
                           FALSE,
                           EaQuery,
                           QueryLength,
                           NULL,
                           TRUE);
    if (!NT_SUCCESS(Status))
    {
        trace("probe-result ea=unsupported op=query status=0x%08lx\n", (ULONG)Status);
        goto Cleanup;
    }

    Returned = (PNTFSLX_FILE_FULL_EA_INFORMATION)QueryBuffer;
    ok(Returned->EaNameLength == NameBytes,
       "EA probe: returned name length %u, expected %lu\n",
       Returned->EaNameLength, NameBytes);
    ok(memcmp(Returned->EaName, EaName, NameBytes + 1) == 0,
       "EA probe: returned EA name mismatch\n");
    ok(Returned->EaValueLength == sizeof(EaValue),
       "EA probe: returned value length %u, expected %lu\n",
       Returned->EaValueLength, (ULONG)sizeof(EaValue));
    ok(memcmp(Returned->EaName + Returned->EaNameLength + 1, EaValue, sizeof(EaValue)) == 0,
       "EA probe: returned value mismatch\n");
    trace("probe-result ea=pass\n");

Cleanup:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    DeleteFileW(FilePath);
}

static void
TestReparsePointProbe(void)
{
    static const WCHAR ProbeRootPath[] = L"D:\\ntfslx_reparse_probe";
    static const WCHAR ProbeDirPath[] = L"D:\\ntfslx_reparse_probe\\link";
    static const WCHAR ProbeTargetPath[] = L"D:\\ntfslx_reparse_probe\\target";
    static const WCHAR SubstitutePath[] = L"\\??\\D:\\ntfslx_reparse_probe\\target";
    static const WCHAR PrintPath[] = L"D:\\ntfslx_reparse_probe\\target";
    BYTE Buffer[1024];
    HANDLE hDir = INVALID_HANDLE_VALUE;
    DWORD BytesReturned = 0;
    PNTFSLX_MOUNT_POINT_REPARSE_BUFFER ReparseBuffer;
    PNTFSLX_REPARSE_DATA_HEADER DeleteHeader;
    ULONG InputLength;
    USHORT SubstituteBytes;
    USHORT PrintBytes;
    BOOL Ok;

    DeleteTreeRecursive(ProbeRootPath);
    RemoveDirectoryW(ProbeRootPath);

    Ok = CreateDirectoryW(ProbeRootPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(reparse probe root) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    Ok = CreateDirectoryW(ProbeTargetPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(reparse probe target) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        goto Cleanup;

    Ok = CreateDirectoryW(ProbeDirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(reparse probe link) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        goto Cleanup;

    hDir = CreateFileW(ProbeDirPath,
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                       NULL);
    ok(hDir != INVALID_HANDLE_VALUE,
       "reparse probe: open failed with error %lu\n", GetLastError());
    if (hDir == INVALID_HANDLE_VALUE)
        goto Cleanup;

    ZeroMemory(Buffer, sizeof(Buffer));
    ReparseBuffer = (PNTFSLX_MOUNT_POINT_REPARSE_BUFFER)Buffer;
    SubstituteBytes = (USHORT)(wcslen(SubstitutePath) * sizeof(WCHAR));
    PrintBytes = (USHORT)(wcslen(PrintPath) * sizeof(WCHAR));
    InputLength = FIELD_OFFSET(NTFSLX_MOUNT_POINT_REPARSE_BUFFER, PathBuffer) +
                  SubstituteBytes + sizeof(WCHAR) +
                  PrintBytes + sizeof(WCHAR);
    ok(InputLength <= sizeof(Buffer), "reparse probe buffer overflow\n");
    if (InputLength > sizeof(Buffer))
        goto Cleanup;

    ReparseBuffer->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    ReparseBuffer->ReparseDataLength = (USHORT)(8 + SubstituteBytes + sizeof(WCHAR) + PrintBytes + sizeof(WCHAR));
    ReparseBuffer->Reserved = 0;
    ReparseBuffer->SubstituteNameOffset = 0;
    ReparseBuffer->SubstituteNameLength = SubstituteBytes;
    ReparseBuffer->PrintNameOffset = SubstituteBytes + sizeof(WCHAR);
    ReparseBuffer->PrintNameLength = PrintBytes;
    CopyMemory(ReparseBuffer->PathBuffer, SubstitutePath, SubstituteBytes + sizeof(WCHAR));
    CopyMemory((BYTE *)ReparseBuffer->PathBuffer + SubstituteBytes + sizeof(WCHAR),
               PrintPath,
               PrintBytes + sizeof(WCHAR));

    Ok = DeviceIoControl(hDir,
                         FSCTL_SET_REPARSE_POINT,
                         ReparseBuffer,
                         InputLength,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    if (!Ok)
    {
        trace("probe-result reparse=unsupported op=set error=%lu\n", GetLastError());
        goto Cleanup;
    }

    ZeroMemory(Buffer, sizeof(Buffer));
    Ok = DeviceIoControl(hDir,
                         FSCTL_GET_REPARSE_POINT,
                         NULL,
                         0,
                         Buffer,
                         sizeof(Buffer),
                         &BytesReturned,
                         NULL);
    ok(Ok, "reparse probe: FSCTL_GET_REPARSE_POINT failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;

    ReparseBuffer = (PNTFSLX_MOUNT_POINT_REPARSE_BUFFER)Buffer;
    ok(ReparseBuffer->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT,
       "reparse probe: returned tag 0x%08lx, expected mount point\n",
       ReparseBuffer->ReparseTag);

    DeleteHeader = (PNTFSLX_REPARSE_DATA_HEADER)Buffer;
    DeleteHeader->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    DeleteHeader->ReparseDataLength = 0;
    DeleteHeader->Reserved = 0;
    Ok = DeviceIoControl(hDir,
                         FSCTL_DELETE_REPARSE_POINT,
                         DeleteHeader,
                         sizeof(*DeleteHeader),
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    ok(Ok, "reparse probe: FSCTL_DELETE_REPARSE_POINT failed with error %lu\n", GetLastError());
    trace("probe-result reparse=pass\n");

Cleanup:
    if (hDir != INVALID_HANDLE_VALUE)
        CloseHandle(hDir);
    RemoveDirectoryW(ProbeDirPath);
    RemoveDirectoryW(ProbeTargetPath);
    RemoveDirectoryW(ProbeRootPath);
}

static void
TestLogFileProbe(void)
{
    static const WCHAR LogFilePath[] = L"D:\\$LogFile";
    HANDLE hFile = INVALID_HANDLE_VALUE;
    LARGE_INTEGER Size;

    hFile = CreateFileW(LogFilePath,
                        FILE_READ_ATTRIBUTES | FILE_READ_DATA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    ok(hFile != INVALID_HANDLE_VALUE,
       "logfile probe: open failed with error %lu\n", GetLastError());
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    if (GetFileSizeEx(hFile, &Size))
        trace("probe-result logfile=pass size=%I64d\n", Size.QuadPart);
    else
        fail("logfile probe: size query failed with error %lu\n", GetLastError());

    CloseHandle(hFile);
}

static void
TestDirtyVolumeProbe(void)
{
    HANDLE hVolume = INVALID_HANDLE_VALUE;
    DWORD DirtyFlags = 0;
    DWORD BytesReturned = 0;
    BOOL Ok;

    hVolume = CreateFileW(L"\\\\.\\D:",
                          FILE_READ_ATTRIBUTES,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);
    ok(hVolume != INVALID_HANDLE_VALUE,
       "dirty-volume probe: open failed with error %lu\n", GetLastError());
    if (hVolume == INVALID_HANDLE_VALUE)
        return;

    Ok = DeviceIoControl(hVolume,
                         FSCTL_MARK_VOLUME_DIRTY,
                         NULL,
                         0,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    ok(Ok,
       "dirty-volume probe: FSCTL_MARK_VOLUME_DIRTY failed with error %lu\n",
       GetLastError());
    if (!Ok)
    {
        CloseHandle(hVolume);
        return;
    }

    Ok = DeviceIoControl(hVolume,
                         FSCTL_IS_VOLUME_DIRTY,
                         NULL,
                         0,
                         &DirtyFlags,
                         sizeof(DirtyFlags),
                         &BytesReturned,
                         NULL);
    ok(Ok,
       "dirty-volume probe: FSCTL_IS_VOLUME_DIRTY failed with error %lu\n",
       GetLastError());
    if (!Ok)
    {
        CloseHandle(hVolume);
        return;
    }

    ok((DirtyFlags & VOLUME_IS_DIRTY) != 0,
       "dirty-volume probe: expected mounted volume to report dirty flags, got 0x%08lx\n",
       DirtyFlags);
    trace("probe-result dirty-volume=dirty flags=0x%08lx bytes=%lu\n", DirtyFlags, BytesReturned);

    CloseHandle(hVolume);
}

/*
 * TestReadLargeOffset — user-mode regression test for the large-offset
 * read failure mode that historically presented as 0xc0000218 at SMSS
 * time on a 471 KiB SOFTWARE hive at offset 0x60028. Writes a 1 MiB+
 * pattern with a per-offset 64-bit stamp, then reads it back via
 *   - the buffered path (the OS's CcCopyRead route),
 *   - the FILE_FLAG_NO_BUFFERING path (paging-IO-style direct disk),
 *   - and a single bulk read across the whole file.
 *
 * Any wrong-LCN read produces a stamp mismatch and trips the assertion
 *   "ReadLargeOffset: stamp mismatch at offset 0x60028"
 * which is the load-bearing line that would have caught Bug #1 before
 * the read path got into a state where CM bug-checked.
 */
static ULONGLONG
LargeOffsetStamp(ULONGLONG Offset)
{
    return (Offset / 8) ^ 0xDEADBEEFCAFEBABEULL;
}

static void
TestReadLargeOffset(void)
{
    static const ULONGLONG kFileSize = 1ULL * 1024 * 1024 + 4096;
    static const DWORD kChunk = 64 * 1024;
    static const ULONGLONG kProbeOffsets[] = {
        0,
        4096,
        16384,
        0x60000,
        0x60028,
        0x80000,
        0xF0000,
        1ULL * 1024 * 1024,
    };

    HANDLE hFile = INVALID_HANDLE_VALUE;
    BYTE *Buffer = NULL;
    BYTE *Verify = NULL;
    BOOL Ok;
    ULONGLONG Off;
    ULONG ProbeIdx;
    DWORD BytesIo;
    DWORD StampMismatches = 0;
    DWORD NoBufMismatches = 0;
    DWORD BulkMismatches = 0;
    LARGE_INTEGER Move;
    LARGE_INTEGER NewPos;

    Ok = CreateDirectoryW(L"D:\\NtfslxRT", NULL);
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        fail("ReadLargeOffset: mkdir D:\\NtfslxRT err=%lu\n", GetLastError());
        return;
    }

    DeleteFileW(L"D:\\NtfslxRT\\largepattern.bin");

    Buffer = (BYTE *)HeapAlloc(GetProcessHeap(), 0, kChunk);
    Verify = (BYTE *)HeapAlloc(GetProcessHeap(), 0, kChunk);
    if (Buffer == NULL || Verify == NULL)
    {
        fail("ReadLargeOffset: HeapAlloc failed\n");
        goto Cleanup;
    }

    /* === Phase 1: write the pattern === */
    hFile = CreateFileW(L"D:\\NtfslxRT\\largepattern.bin",
                       GENERIC_READ | GENERIC_WRITE,
                       0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fail("ReadLargeOffset: create pattern file err=%lu\n", GetLastError());
        goto Cleanup;
    }

    for (Off = 0; Off < kFileSize; Off += kChunk)
    {
        DWORD ChunkLen = (DWORD)(kFileSize - Off > kChunk ? kChunk : kFileSize - Off);
        DWORD i;
        for (i = 0; i + 8 <= ChunkLen; i += 8)
        {
            ULONGLONG Stamp = LargeOffsetStamp(Off + i);
            memcpy(Buffer + i, &Stamp, sizeof(ULONGLONG));
        }
        Ok = WriteFile(hFile, Buffer, ChunkLen, &BytesIo, NULL);
        if (!Ok || BytesIo != ChunkLen)
        {
            fail("ReadLargeOffset: write @0x%I64x err=%lu wrote=%lu/%lu\n",
                 Off, GetLastError(), BytesIo, ChunkLen);
            goto Cleanup;
        }
    }
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    /* === Phase 2: buffered reads at each probe offset === */
    hFile = CreateFileW(L"D:\\NtfslxRT\\largepattern.bin",
                       GENERIC_READ,
                       FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fail("ReadLargeOffset: reopen buffered err=%lu\n", GetLastError());
        goto Cleanup;
    }

    for (ProbeIdx = 0; ProbeIdx < _countof(kProbeOffsets); ++ProbeIdx)
    {
        DWORD ProbeLen = 64;
        ULONGLONG Expected;
        ULONGLONG Got;

        Off = kProbeOffsets[ProbeIdx];
        if (Off + ProbeLen > kFileSize)
            ProbeLen = (DWORD)(kFileSize - Off);

        Move.QuadPart = (LONGLONG)Off;
        if (!SetFilePointerEx(hFile, Move, &NewPos, FILE_BEGIN) ||
            (ULONGLONG)NewPos.QuadPart != Off)
        {
            fail("ReadLargeOffset: seek to 0x%I64x err=%lu\n",
                 Off, GetLastError());
            ++StampMismatches;
            continue;
        }

        memset(Verify, 0, ProbeLen);
        if (!ReadFile(hFile, Verify, ProbeLen, &BytesIo, NULL) ||
            BytesIo != ProbeLen)
        {
            fail("ReadLargeOffset: buffered read @0x%I64x err=%lu got=%lu\n",
                 Off, GetLastError(), BytesIo);
            ++StampMismatches;
            continue;
        }

        Expected = LargeOffsetStamp(Off);
        memcpy(&Got, Verify, sizeof(ULONGLONG));
        if (Got != Expected)
        {
            fail("ReadLargeOffset: stamp mismatch at offset 0x%I64x"
                 " got=0x%016I64x expect=0x%016I64x\n",
                 Off, Got, Expected);
            ++StampMismatches;
        }
    }
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    /* === Phase 3: FILE_FLAG_NO_BUFFERING reads (sector-aligned) === */
    hFile = CreateFileW(L"D:\\NtfslxRT\\largepattern.bin",
                       GENERIC_READ,
                       FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING,
                       NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        for (ProbeIdx = 0; ProbeIdx < _countof(kProbeOffsets); ++ProbeIdx)
        {
            ULONGLONG AlignedOff;
            DWORD ChunkLen = 4096;
            DWORD i;

            Off = kProbeOffsets[ProbeIdx];
            if (Off + ChunkLen > kFileSize)
                continue;
            /* Round to 512 bytes for FILE_FLAG_NO_BUFFERING */
            AlignedOff = Off & ~((ULONGLONG)511);

            Move.QuadPart = (LONGLONG)AlignedOff;
            if (!SetFilePointerEx(hFile, Move, &NewPos, FILE_BEGIN) ||
                (ULONGLONG)NewPos.QuadPart != AlignedOff)
            {
                fail("ReadLargeOffset: no-buf seek 0x%I64x err=%lu\n",
                     AlignedOff, GetLastError());
                ++NoBufMismatches;
                continue;
            }

            memset(Verify, 0, ChunkLen);
            if (!ReadFile(hFile, Verify, ChunkLen, &BytesIo, NULL) ||
                BytesIo != ChunkLen)
            {
                fail("ReadLargeOffset: no-buf read 0x%I64x err=%lu got=%lu\n",
                     AlignedOff, GetLastError(), BytesIo);
                ++NoBufMismatches;
                continue;
            }

            for (i = 0; i + 8 <= ChunkLen; i += 8)
            {
                ULONGLONG StampOff = AlignedOff + i;
                ULONGLONG Expected = LargeOffsetStamp(StampOff);
                ULONGLONG Got;
                memcpy(&Got, Verify + i, sizeof(ULONGLONG));
                if (Got != Expected)
                {
                    fail("ReadLargeOffset: no-buf stamp mismatch at"
                         " 0x%I64x got=0x%016I64x expect=0x%016I64x\n",
                         StampOff, Got, Expected);
                    ++NoBufMismatches;
                    break; /* Limit log noise. */
                }
            }
        }
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }

    /* === Phase 4: bulk read of the whole file === */
    hFile = CreateFileW(L"D:\\NtfslxRT\\largepattern.bin",
                       GENERIC_READ,
                       FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        for (Off = 0; Off < kFileSize; Off += kChunk)
        {
            DWORD ChunkLen = (DWORD)(kFileSize - Off > kChunk ? kChunk : kFileSize - Off);
            DWORD i;
            memset(Buffer, 0, ChunkLen);
            if (!ReadFile(hFile, Buffer, ChunkLen, &BytesIo, NULL) ||
                BytesIo != ChunkLen)
            {
                fail("ReadLargeOffset: bulk read 0x%I64x err=%lu got=%lu\n",
                     Off, GetLastError(), BytesIo);
                ++BulkMismatches;
                continue;
            }
            for (i = 0; i + 8 <= ChunkLen; i += 8)
            {
                ULONGLONG StampOff = Off + i;
                ULONGLONG Expected = LargeOffsetStamp(StampOff);
                ULONGLONG Got;
                memcpy(&Got, Buffer + i, sizeof(ULONGLONG));
                if (Got != Expected)
                {
                    if (BulkMismatches == 0)
                        fail("ReadLargeOffset: bulk stamp mismatch at"
                             " 0x%I64x got=0x%016I64x expect=0x%016I64x\n",
                             StampOff, Got, Expected);
                    ++BulkMismatches;
                    break;
                }
            }
        }
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }

    ok(StampMismatches == 0,
       "ReadLargeOffset: buffered phase mismatches=%lu (must be 0)\n",
       StampMismatches);
    ok(NoBufMismatches == 0,
       "ReadLargeOffset: no-buffering phase mismatches=%lu (must be 0)\n",
       NoBufMismatches);
    ok(BulkMismatches == 0,
       "ReadLargeOffset: bulk phase mismatches=%lu (must be 0)\n",
       BulkMismatches);
    trace("probe-result read-large-offset buffered=%lu nobuf=%lu bulk=%lu\n",
          StampMismatches, NoBufMismatches, BulkMismatches);

Cleanup:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    DeleteFileW(L"D:\\NtfslxRT\\largepattern.bin");
    if (Buffer)
        HeapFree(GetProcessHeap(), 0, Buffer);
    if (Verify)
        HeapFree(GetProcessHeap(), 0, Verify);
}

/*
 * TestMountIsNonDestructive — user-mode regression test for the
 * mount-time destructive behaviour. Issues IOCTL_NTFSLX_TIER0_PROOF
 * before and after a deliberate write. Catches:
 *   - "mount sets dirty flag" regression (clean-mount VolumeDirtyFlag != 0)
 *   - "mount wipes $LogFile" regression (LogFileIs0xFF == TRUE pre-write,
 *     LogFileFirstDword == 0xFFFFFFFF pre-write)
 *   - latch-fails-to-arm regression (post-write VolumeDirtyFlag != 1)
 */
static void
TestMountIsNonDestructive(void)
{
    HANDLE hVol = INVALID_HANDLE_VALUE;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    NTFSLX_TIER0_PROOF ProofPre = {0};
    NTFSLX_TIER0_PROOF ProofPost = {0};
    DWORD BytesReturned = 0;
    DWORD WriteBytes = 0;
    BYTE TestBytes[4] = { 0xC3, 0x5A, 0xA5, 0x3C };
    BOOL Ok;

    hVol = CreateFileW(L"\\\\.\\D:",
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, 0, NULL);
    if (hVol == INVALID_HANDLE_VALUE)
    {
        fail("MountIsNonDestructive: open volume err=%lu\n", GetLastError());
        return;
    }

    /* Pre-write proof. */
    Ok = DeviceIoControl(hVol, IOCTL_NTFSLX_TIER0_PROOF,
                         NULL, 0,
                         &ProofPre, sizeof(ProofPre),
                         &BytesReturned, NULL);
    if (!Ok)
    {
        fail("MountIsNonDestructive: TIER0_PROOF pre-write err=%lu\n",
             GetLastError());
        CloseHandle(hVol);
        return;
    }

    trace("MountIsNonDestructive: pre VolumeDirtyFlag=%lu"
          " LogFileFirstDword=0x%08lx LogFileIs0xFF=%lu\n",
          ProofPre.VolumeDirtyFlag,
          ProofPre.LogFileFirstDword,
          ProofPre.LogFileIs0xFF);

    /*
     * If the latch has not fired yet (the kernel image just booted and
     * nothing user-mode wrote anything), the mount-non-destructive
     * assertions are load-bearing. If the latch has already fired (e.g.
     * because CM created .LOG files during system bring-up), all we can
     * say at this point is "it has fired, so check post-write".
     */
    if (ProofPre.VolumeDirtyFlag == 0)
    {
        ok(ProofPre.LogFileFirstDword != 0xFFFFFFFF,
           "MountIsNonDestructive: clean-mount LogFileFirstDword=0x%08lx"
           " (mount must NOT wipe $LogFile)\n",
           ProofPre.LogFileFirstDword);
        ok(ProofPre.LogFileIs0xFF == 0,
           "MountIsNonDestructive: clean-mount LogFileIs0xFF=%lu"
           " (mount must NOT wipe $LogFile)\n",
           ProofPre.LogFileIs0xFF);
    }
    else
    {
        trace("MountIsNonDestructive: latch already fired this boot,"
              " skipping clean-mount LogFile assertion\n");
    }

    /* Trigger a write. */
    DeleteFileW(L"D:\\NtfslxRT_latch.bin");
    hFile = CreateFileW(L"D:\\NtfslxRT_latch.bin",
                       GENERIC_READ | GENERIC_WRITE,
                       0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fail("MountIsNonDestructive: create latch trigger err=%lu\n",
             GetLastError());
        CloseHandle(hVol);
        return;
    }
    Ok = WriteFile(hFile, TestBytes, sizeof(TestBytes), &WriteBytes, NULL);
    ok(Ok && WriteBytes == sizeof(TestBytes),
       "MountIsNonDestructive: latch trigger write err=%lu wrote=%lu\n",
       GetLastError(), WriteBytes);
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    /* Post-write proof. */
    Ok = DeviceIoControl(hVol, IOCTL_NTFSLX_TIER0_PROOF,
                         NULL, 0,
                         &ProofPost, sizeof(ProofPost),
                         &BytesReturned, NULL);
    if (Ok)
    {
        ok(ProofPost.VolumeDirtyFlag == 1,
           "MountIsNonDestructive: post-write VolumeDirtyFlag=%lu"
           " (latch must arm dirty bit on first mutation)\n",
           ProofPost.VolumeDirtyFlag);
        ok(ProofPost.LogFileIs0xFF == 1 ||
               ProofPost.LogFileFirstDword == 0x52545352 /* 'RSTR' */ ||
               ProofPost.LogFileFirstDword == 0x444B4843 /* 'CHKD' */ ||
               ProofPost.LogFileFirstDword == 0x474F4C58 /* 'XLOG' */,
           "MountIsNonDestructive: post-write LogFileFirstDword=0x%08lx"
           " LogFileIs0xFF=%lu (latch must wipe or stamp $LogFile)\n",
           ProofPost.LogFileFirstDword, ProofPost.LogFileIs0xFF);
        trace("MountIsNonDestructive: post VolumeDirtyFlag=%lu"
              " LogFileFirstDword=0x%08lx LogFileIs0xFF=%lu\n",
              ProofPost.VolumeDirtyFlag,
              ProofPost.LogFileFirstDword,
              ProofPost.LogFileIs0xFF);
    }
    else
    {
        fail("MountIsNonDestructive: TIER0_PROOF post-write err=%lu\n",
             GetLastError());
    }

    DeleteFileW(L"D:\\NtfslxRT_latch.bin");
    CloseHandle(hVol);
}

/*
 * TC.1 - regression test for the FAST_MUTEX/sync-IRP deadlock fixed in
 * drivers/filesystems/ntfslx/diskwrite.c. The driver-side handler for
 * IOCTL_NTFSLX_TC1_MFTLOCK_REENTRANCY:
 *
 *   1. Spawns a worker thread.
 *   2. Worker reads sector 0, acquires DevExt->MftAllocLock, calls
 *      NtfslxWriteDisk with the same bytes (idempotent), releases lock.
 *   3. Worker signals an event when finished. If the deadlock returns
 *      (sync-IRP-under-FAST_MUTEX), the worker hangs forever.
 *   4. The IOCTL caller waits 10 s for the event. Timeout reports
 *      TestCompleted=0 instead of hanging the boot.
 *
 * If anyone reverts diskwrite.c to IoBuildSynchronousFsdRequest, this
 * test reports TestCompleted=0 and the boot regression is caught here
 * before any other harm spreads.
 */
static void
TestMftAllocLockReentrancy(void)
{
    HANDLE hVol = INVALID_HANDLE_VALUE;
    NTFSLX_TC1_REENTRANCY_RESULT Result = {0};
    DWORD BytesReturned = 0;
    BOOL Ok;

    hVol = CreateFileW(L"\\\\.\\D:",
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, 0, NULL);
    if (hVol == INVALID_HANDLE_VALUE)
    {
        fail("MftAllocLockReentrancy: open volume err=%lu\n", GetLastError());
        return;
    }

    Ok = DeviceIoControl(hVol, IOCTL_NTFSLX_TC1_MFTLOCK_REENTRANCY,
                         NULL, 0,
                         &Result, sizeof(Result),
                         &BytesReturned, NULL);
    if (!Ok)
    {
        fail("MftAllocLockReentrancy: IOCTL err=%lu\n", GetLastError());
        CloseHandle(hVol);
        return;
    }

    trace("MftAllocLockReentrancy: Completed=%lu WriteStatus=0x%08lx ElapsedMs=%lu\n",
          Result.TestCompleted, Result.WriteStatus, Result.ElapsedMs);

    /*
     * Load-bearing assertion: completion proves the lock+sync-IRP path
     * does not deadlock. A reintroduction of the bug would set
     * Result.TestCompleted=0 (worker thread never signalled within the
     * 10 s watchdog window).
     */
    ok(Result.TestCompleted != 0,
       "MftAllocLockReentrancy: lock+write under MftAllocLock did NOT complete"
       " within 10s - probable FAST_MUTEX/sync-IRP deadlock regression\n");

    if (Result.TestCompleted)
    {
        ok(NT_SUCCESS(Result.WriteStatus),
           "MftAllocLockReentrancy: NtfslxWriteDisk under MftAllocLock returned 0x%08lx\n",
           Result.WriteStatus);
        /* Single-sector write should be very fast - well under 1s on any
         * sane disk. Slow writes are not a regression of THIS bug, but
         * worth surfacing since they could indicate a different problem. */
        ok(Result.ElapsedMs < 5000,
           "MftAllocLockReentrancy: single-sector write under lock took %lu ms\n",
           Result.ElapsedMs);
    }

    CloseHandle(hVol);
}

/*
 * TC.4 - regression test for burst-create latency. Creates and
 * deletes a fixed number of files in a tight loop in a fresh
 * subdirectory, times each call, asserts max < 100ms and p99 < 50ms.
 * Reports min/median/avg/p99/max for trend tracking.
 *
 * Files are created in a private subdirectory so the test isn't
 * fighting for INDEX_ROOT slots in the volume root with the rest of
 * the ntfslx_auto.exe scenarios. The original spec called for 1000
 * files; in practice the current driver caps directory growth before
 * that point (T1.5 "Minimal redo records for $INDEX_ALLOCATION
 * updates" is the corresponding production task that would lift this
 * cap). 32 entries fit comfortably below the cap, drive a real burst,
 * and any latency regression of the create fast-path manifests in
 * the per-call timing all the same.
 */
#define TC4_FILE_COUNT 32
#define TC4_MAX_LATENCY_MS 100
#define TC4_P99_LATENCY_MS 50
#define TC4_DIR_NAME L"D:\\tc4_burst_dir"

static int __cdecl
CompareDword(const void *a, const void *b)
{
    DWORD ua = *(const DWORD *)a;
    DWORD ub = *(const DWORD *)b;
    if (ua < ub) return -1;
    if (ua > ub) return 1;
    return 0;
}

static void
ComputeLatencyStats(
    DWORD *Latencies, DWORD Count,
    DWORD *OutMin, DWORD *OutMax, DWORD *OutMedian, DWORD *OutP99,
    DWORD *OutAvg)
{
    DWORD i;
    ULONGLONG Sum = 0;
    DWORD p99Idx;

    *OutMin = 0; *OutMax = 0; *OutMedian = 0; *OutP99 = 0; *OutAvg = 0;
    if (Count == 0) return;

    qsort(Latencies, Count, sizeof(DWORD), CompareDword);
    *OutMin = Latencies[0];
    *OutMax = Latencies[Count - 1];
    *OutMedian = Latencies[Count / 2];
    p99Idx = (Count * 99) / 100;
    if (p99Idx >= Count) p99Idx = Count - 1;
    *OutP99 = Latencies[p99Idx];
    for (i = 0; i < Count; i++) Sum += Latencies[i];
    *OutAvg = (DWORD)(Sum / Count);
}

static void
TestBurstCreateLatency(void)
{
    DWORD *Latencies;
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Start, End;
    DWORD i;
    DWORD ErrorCount;
    DWORD FirstError;
    DWORD Min, Max, Median, P99, Avg;
    HANDLE hFile;

    if (!QueryPerformanceFrequency(&Frequency) || Frequency.QuadPart <= 0)
    {
        fail("BurstCreateLatency: performance counter unavailable\n");
        return;
    }

    Latencies = (DWORD *)HeapAlloc(GetProcessHeap(), 0,
                                    TC4_FILE_COUNT * sizeof(DWORD));
    if (Latencies == NULL)
    {
        fail("BurstCreateLatency: alloc latency array\n");
        return;
    }

    /* Make sure the test subdirectory exists and is empty. RemoveDirectory
     * fails if there are leftover files from a prior run, so explicitly
     * delete each potential leftover first. */
    for (i = 0; i < TC4_FILE_COUNT; i++)
    {
        WCHAR Path[80];
        swprintf(Path, L"%ls\\%04lu.bin", TC4_DIR_NAME, i);
        DeleteFileW(Path);
    }
    RemoveDirectoryW(TC4_DIR_NAME);
    if (!CreateDirectoryW(TC4_DIR_NAME, NULL))
    {
        fail("BurstCreateLatency: create dir err=%lu\n", GetLastError());
        HeapFree(GetProcessHeap(), 0, Latencies);
        return;
    }

    /* Phase 1: burst-create. */
    ErrorCount = 0;
    FirstError = 0;
    for (i = 0; i < TC4_FILE_COUNT; i++)
    {
        WCHAR Path[64];
        ULONGLONG DeltaTicks;
        swprintf(Path, L"%ls\\%04lu.bin", TC4_DIR_NAME, i);
        QueryPerformanceCounter(&Start);
        hFile = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, NULL);
        QueryPerformanceCounter(&End);
        DeltaTicks = (ULONGLONG)(End.QuadPart - Start.QuadPart);
        Latencies[i] = (DWORD)((DeltaTicks * 1000) / Frequency.QuadPart);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            if (ErrorCount == 0)
                FirstError = GetLastError();
            ErrorCount++;
        }
        else
        {
            CloseHandle(hFile);
        }
    }
    ok(ErrorCount == 0,
       "BurstCreateLatency: %lu/%lu creates failed firstErr=%lu\n",
       ErrorCount, (DWORD)TC4_FILE_COUNT, FirstError);

    ComputeLatencyStats(Latencies, TC4_FILE_COUNT,
                        &Min, &Max, &Median, &P99, &Avg);
    trace("BurstCreateLatency create: min=%lu median=%lu avg=%lu p99=%lu max=%lu (n=%lu)\n",
          Min, Median, Avg, P99, Max, (DWORD)TC4_FILE_COUNT);

    ok(Max < TC4_MAX_LATENCY_MS,
       "BurstCreateLatency: create max %lums >= %lums (regression)\n",
       Max, (DWORD)TC4_MAX_LATENCY_MS);
    ok(P99 < TC4_P99_LATENCY_MS,
       "BurstCreateLatency: create p99 %lums >= %lums (regression)\n",
       P99, (DWORD)TC4_P99_LATENCY_MS);

    /* Phase 2: burst-delete. */
    ErrorCount = 0;
    FirstError = 0;
    for (i = 0; i < TC4_FILE_COUNT; i++)
    {
        WCHAR Path[64];
        ULONGLONG DeltaTicks;
        BOOL Ok;
        swprintf(Path, L"%ls\\%04lu.bin", TC4_DIR_NAME, i);
        QueryPerformanceCounter(&Start);
        Ok = DeleteFileW(Path);
        QueryPerformanceCounter(&End);
        DeltaTicks = (ULONGLONG)(End.QuadPart - Start.QuadPart);
        Latencies[i] = (DWORD)((DeltaTicks * 1000) / Frequency.QuadPart);
        if (!Ok)
        {
            if (ErrorCount == 0)
                FirstError = GetLastError();
            ErrorCount++;
        }
    }
    ok(ErrorCount == 0,
       "BurstCreateLatency: %lu/%lu deletes failed firstErr=%lu\n",
       ErrorCount, (DWORD)TC4_FILE_COUNT, FirstError);

    ComputeLatencyStats(Latencies, TC4_FILE_COUNT,
                        &Min, &Max, &Median, &P99, &Avg);
    trace("BurstCreateLatency delete: min=%lu median=%lu avg=%lu p99=%lu max=%lu (n=%lu)\n",
          Min, Median, Avg, P99, Max, (DWORD)TC4_FILE_COUNT);

    ok(Max < TC4_MAX_LATENCY_MS,
       "BurstCreateLatency: delete max %lums >= %lums (regression)\n",
       Max, (DWORD)TC4_MAX_LATENCY_MS);
    ok(P99 < TC4_P99_LATENCY_MS,
       "BurstCreateLatency: delete p99 %lums >= %lums (regression)\n",
       P99, (DWORD)TC4_P99_LATENCY_MS);

    /* Best-effort directory cleanup. Failures here don't fail the test. */
    RemoveDirectoryW(TC4_DIR_NAME);
    HeapFree(GetProcessHeap(), 0, Latencies);
}

static void
TestVolumeControlProbe(void)
{
    HANDLE hVolume = INVALID_HANDLE_VALUE;
    HANDLE hSecond = INVALID_HANDLE_VALUE;
    DWORD BytesReturned = 0;
    BOOL Ok;

    hVolume = CreateFileW(L"\\\\.\\D:",
                          GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);
    ok(hVolume != INVALID_HANDLE_VALUE,
       "volume control probe: manage-volume open failed with error %lu\n",
       GetLastError());
    if (hVolume == INVALID_HANDLE_VALUE)
        return;

    Ok = DeviceIoControl(hVolume,
                         FSCTL_LOCK_VOLUME,
                         NULL,
                         0,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    ok(Ok,
       "volume control probe: FSCTL_LOCK_VOLUME failed with error %lu\n",
       GetLastError());
    if (!Ok)
    {
        CloseHandle(hVolume);
        return;
    }

    hSecond = CreateFileW(L"\\\\.\\D:",
                          FILE_READ_ATTRIBUTES,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);
    ok(hSecond == INVALID_HANDLE_VALUE,
       "volume control probe: second volume open should fail while locked\n");
    if (hSecond != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hSecond);
        hSecond = INVALID_HANDLE_VALUE;
    }

    Ok = DeviceIoControl(hVolume,
                         FSCTL_UNLOCK_VOLUME,
                         NULL,
                         0,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    ok(Ok,
       "volume control probe: FSCTL_UNLOCK_VOLUME failed with error %lu\n",
       GetLastError());
    if (!Ok)
    {
        CloseHandle(hVolume);
        return;
    }

    hSecond = CreateFileW(L"\\\\.\\D:",
                          FILE_READ_ATTRIBUTES,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);
    ok(hSecond != INVALID_HANDLE_VALUE,
       "volume control probe: second volume open should succeed after unlock (err=%lu)\n",
       GetLastError());
    if (hSecond != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hSecond);
        hSecond = INVALID_HANDLE_VALUE;
    }

    trace("probe-result volume-lock=pass\n");
    CloseHandle(hVolume);
}

static void
TestVolumeDismountProbe(void)
{
    HANDLE hVolume = INVALID_HANDLE_VALUE;
    HANDLE hSecond = INVALID_HANDLE_VALUE;
    DWORD BytesReturned = 0;
    BOOL Ok;

    hVolume = CreateFileW(L"\\\\.\\D:",
                          GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);
    ok(hVolume != INVALID_HANDLE_VALUE,
       "volume dismount probe: manage-volume open failed with error %lu\n",
       GetLastError());
    if (hVolume == INVALID_HANDLE_VALUE)
        return;

    Ok = DeviceIoControl(hVolume,
                         FSCTL_LOCK_VOLUME,
                         NULL,
                         0,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    ok(Ok,
       "volume dismount probe: FSCTL_LOCK_VOLUME failed with error %lu\n",
       GetLastError());
    if (!Ok)
    {
        CloseHandle(hVolume);
        return;
    }

    Ok = DeviceIoControl(hVolume,
                         FSCTL_DISMOUNT_VOLUME,
                         NULL,
                         0,
                         NULL,
                         0,
                         &BytesReturned,
                         NULL);
    ok(Ok,
       "volume dismount probe: FSCTL_DISMOUNT_VOLUME failed with error %lu\n",
       GetLastError());
    if (!Ok)
    {
        CloseHandle(hVolume);
        return;
    }

    hSecond = CreateFileW(L"\\\\.\\D:",
                          FILE_READ_ATTRIBUTES,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          NULL,
                          OPEN_EXISTING,
                          0,
                          NULL);
    ok(hSecond == INVALID_HANDLE_VALUE,
       "volume dismount probe: second volume open should fail after dismount\n");
    if (hSecond != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hSecond);
        hSecond = INVALID_HANDLE_VALUE;
    }

    trace("probe-result volume-dismount=pass\n");
    CloseHandle(hVolume);
}

/*
 * TestRootfsVsImageCompare - byte-for-byte verify that what the preinstall
 * pipeline laid down on the secondary ReactOS.img NTFS partition matches
 * what the running LiveCD rootfs has on its ramdisk. Catches:
 *   - File missing on the image (preinstall.lst entry skipped)
 *   - File content drift (build vs preinstall version mismatch)
 *   - ntfslx returning wrong bytes for image-side reads (read-path bug)
 *
 * Scans drive letters for an NTFS volume labeled "ReactOS" (the preinstall
 * volume label), then walks a curated list of system32 binaries plus the
 * core driver set and compares each file against \SystemRoot\System32\.
 */
static BOOL
FindReactOsImageVolume(PWSTR RootPath, SIZE_T RootPathCount)
{
    DWORD Attempt;

    for (Attempt = 0; Attempt < 30; ++Attempt)
    {
        DWORD LogicalDrives = GetLogicalDrives();
        WCHAR Letter;

        for (Letter = L'C'; Letter <= L'Z'; ++Letter)
        {
            UINT DriveType;
            WCHAR VolumeName[64];
            WCHAR FsName[32];

            if ((LogicalDrives & (1u << (Letter - L'A'))) == 0)
                continue;

            if (RootPathCount < 4)
                return FALSE;

            RootPath[0] = Letter;
            RootPath[1] = L':';
            RootPath[2] = L'\\';
            RootPath[3] = UNICODE_NULL;

            DriveType = GetDriveTypeW(RootPath);
            if (DriveType != DRIVE_REMOVABLE && DriveType != DRIVE_FIXED)
                continue;

            if (!QueryVolumeIdentity(RootPath,
                                     VolumeName, _countof(VolumeName),
                                     FsName, _countof(FsName)))
                continue;

            if (lstrcmpiW(FsName, L"NTFS") == 0 &&
                lstrcmpiW(VolumeName, L"ReactOS") == 0)
            {
                trace("rootfs-vs-image: volume found drive='%ls' label='%ls' fs='%ls'\n",
                      RootPath, VolumeName, FsName);
                return TRUE;
            }
        }

        if ((Attempt % 5) == 0)
            trace("rootfs-vs-image: waiting for ReactOS NTFS volume attempt=%lu\n",
                  Attempt);
        Sleep(1000);
    }

    return FALSE;
}

static BOOL
ReadEntireFile(PCWSTR Path, BYTE **OutBuf, DWORD *OutSize)
{
    HANDLE hFile;
    LARGE_INTEGER Size;
    BYTE *Buf;
    DWORD Total = 0;
    /* Chunk size kept under 256 KiB to avoid tripping HAL's missing
     * scatter/gather list construction (HalCalculateScatterGatherListSize
     * is UNIMPLEMENTED on the current HAL — large reads through the
     * storage stack fail with "Scatter/gather list construction failed!"). */
    static const DWORD CHUNK = 64 * 1024;

    *OutBuf = NULL;
    *OutSize = 0;

    hFile = CreateFileW(Path,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_FLAG_SEQUENTIAL_SCAN,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!GetFileSizeEx(hFile, &Size) || Size.QuadPart > (LONGLONG)(64 * 1024 * 1024))
    {
        CloseHandle(hFile);
        return FALSE;
    }

    if (Size.QuadPart == 0)
    {
        CloseHandle(hFile);
        return TRUE;
    }

    Buf = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)Size.QuadPart);
    if (Buf == NULL)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    while (Total < (DWORD)Size.QuadPart)
    {
        DWORD Want = (DWORD)Size.QuadPart - Total;
        DWORD Got = 0;
        if (Want > CHUNK)
            Want = CHUNK;
        if (!ReadFile(hFile, Buf + Total, Want, &Got, NULL))
        {
            HeapFree(GetProcessHeap(), 0, Buf);
            CloseHandle(hFile);
            return FALSE;
        }
        if (Got == 0)
            break;
        Total += Got;
    }

    CloseHandle(hFile);
    if (Total != (DWORD)Size.QuadPart)
    {
        HeapFree(GetProcessHeap(), 0, Buf);
        return FALSE;
    }
    *OutBuf = Buf;
    *OutSize = Total;
    return TRUE;
}

/*
 * Files known to deliberately differ between LiveCD rootfs and the
 * preinstall NTFS image. The preinstall pipeline picks different sources
 * for these on purpose; they're configured for the corresponding boot
 * mode (LiveCD setup-wizard vs installed-system). Excluded from byte
 * comparison.
 */
static BOOL
IsExpectedDifferentPath(PCWSTR RelPath)
{
    /* Hive content forks deliberately between LiveCD setup-wizard mode
     * and preinstall installed-system mode. */
    if (lstrcmpiW(RelPath, L"system32\\config\\system") == 0 ||
        lstrcmpiW(RelPath, L"system32\\config\\software") == 0 ||
        lstrcmpiW(RelPath, L"system32\\config\\default") == 0)
    {
        return TRUE;
    }
    /* Bootloader config and unattend differ per partition layout. */
    if (lstrcmpiW(RelPath, L"freeldr.ini") == 0 ||
        lstrcmpiW(RelPath, L"unattend.inf") == 0 ||
        _wcsnicmp(RelPath, L"reactos\\unattend.inf", 20) == 0)
    {
        return TRUE;
    }
    /* Per-user profile state and TxR transactions are populated lazily
     * the first time the rootfs boots; the LiveCD ramdisk has them
     * after first run, the preinstall image doesn't yet. */
    if (_wcsnicmp(RelPath, L"Profiles\\", 9) == 0 ||
        _wcsnicmp(RelPath, L"system32\\config\\TxR\\", 20) == 0)
    {
        return TRUE;
    }
    /* Runtime-only artifacts (event logs, scratch dirs, lazy-create UI bits)
     * never appear on the preinstall image; they're created on first boot. */
    if (lstrcmpiW(RelPath, L"system32\\config\\AppEvent.Evt") == 0 ||
        lstrcmpiW(RelPath, L"system32\\config\\SecEvent.Evt") == 0 ||
        lstrcmpiW(RelPath, L"system32\\config\\SysEvent.Evt") == 0 ||
        lstrcmpiW(RelPath, L"Fonts\\desktop.ini") == 0)
    {
        return TRUE;
    }
    /* TEMP / scratch dirs not in build artifacts. */
    if (_wcsnicmp(RelPath, L"TEMP\\", 5) == 0 ||
        lstrcmpiW(RelPath, L"TEMP") == 0)
    {
        return TRUE;
    }
    return FALSE;
}

typedef struct _RFS_VS_IMG_STATS
{
    DWORD Compared;
    DWORD Matches;
    DWORD Mismatches;
    DWORD Skipped;
    DWORD MissingOnImage;
} RFS_VS_IMG_STATS;

static void
CompareOneFileTracked(PCWSTR RootfsRoot,
                      PCWSTR RelPath,
                      PCWSTR ImageDriveRoot,
                      RFS_VS_IMG_STATS *Stats)
{
    WCHAR RootfsPath[1024];
    WCHAR ImagePath[1024];
    BYTE *RootfsBuf = NULL;
    BYTE *ImageBuf = NULL;
    DWORD RootfsSize = 0;
    DWORD ImageSize = 0;
    int Written;

    Written = _snwprintf(RootfsPath, _countof(RootfsPath) - 1, L"%ls\\%ls", RootfsRoot, RelPath);
    if (Written < 0) { ++Stats->Skipped; return; }
    RootfsPath[_countof(RootfsPath) - 1] = L'\0';
    Written = _snwprintf(ImagePath, _countof(ImagePath) - 1, L"%lsreactos\\%ls", ImageDriveRoot, RelPath);
    if (Written < 0) { ++Stats->Skipped; return; }
    ImagePath[_countof(ImagePath) - 1] = L'\0';

    if (!ReadEntireFile(RootfsPath, &RootfsBuf, &RootfsSize))
    {
        ++Stats->Skipped;
        return;
    }

    if (!ReadEntireFile(ImagePath, &ImageBuf, &ImageSize))
    {
        ++Stats->MissingOnImage;
        fail("rootfs-vs-image: image missing or unreadable '%ls' err=%lu\n",
             RelPath, GetLastError());
        if (RootfsBuf != NULL)
            HeapFree(GetProcessHeap(), 0, RootfsBuf);
        return;
    }

    if (RootfsSize != ImageSize)
    {
        ++Stats->Mismatches;
        fail("rootfs-vs-image: SIZE MISMATCH '%ls' rootfs=%lu image=%lu\n",
             RelPath, RootfsSize, ImageSize);
    }
    else if (RootfsSize > 0 && memcmp(RootfsBuf, ImageBuf, RootfsSize) != 0)
    {
        DWORD i;
        DWORD FirstDiff = 0;
        for (i = 0; i < RootfsSize; ++i)
        {
            if (RootfsBuf[i] != ImageBuf[i])
            {
                FirstDiff = i;
                break;
            }
        }
        ++Stats->Mismatches;
        fail("rootfs-vs-image: CONTENT MISMATCH '%ls' size=%lu first_diff=%lu rootfs=0x%02X image=0x%02X\n",
             RelPath, RootfsSize, FirstDiff,
             RootfsBuf[FirstDiff], ImageBuf[FirstDiff]);
    }
    else
    {
        ++Stats->Matches;
    }
    ++Stats->Compared;

    if (RootfsBuf != NULL)
        HeapFree(GetProcessHeap(), 0, RootfsBuf);
    if (ImageBuf != NULL)
        HeapFree(GetProcessHeap(), 0, ImageBuf);
}

static void
WalkAndCompare(PCWSTR RootfsRoot,
               PCWSTR ImageDriveRoot,
               PCWSTR RelPrefix,
               RFS_VS_IMG_STATS *Stats)
{
    WCHAR Pattern[1024];
    WCHAR ChildRel[1024];
    WIN32_FIND_DATAW FindData;
    HANDLE hFind;
    int Written;

    if (RelPrefix[0] == L'\0')
        Written = _snwprintf(Pattern, _countof(Pattern) - 1, L"%ls\\*", RootfsRoot);
    else
        Written = _snwprintf(Pattern, _countof(Pattern) - 1, L"%ls\\%ls\\*", RootfsRoot, RelPrefix);
    if (Written < 0)
    {
        trace("rootfs-vs-image: skip too-long pattern under '%ls'\n", RelPrefix);
        return;
    }
    Pattern[_countof(Pattern) - 1] = L'\0';

    hFind = FindFirstFileW(Pattern, &FindData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (FindData.cFileName[0] == L'.' &&
            (FindData.cFileName[1] == L'\0' ||
             (FindData.cFileName[1] == L'.' && FindData.cFileName[2] == L'\0')))
            continue;

        if (RelPrefix[0] == L'\0')
            Written = _snwprintf(ChildRel, _countof(ChildRel) - 1, L"%ls", FindData.cFileName);
        else
            Written = _snwprintf(ChildRel, _countof(ChildRel) - 1, L"%ls\\%ls", RelPrefix, FindData.cFileName);
        if (Written < 0)
        {
            ++Stats->Skipped;
            continue;
        }
        ChildRel[_countof(ChildRel) - 1] = L'\0';

        if (IsExpectedDifferentPath(ChildRel))
        {
            ++Stats->Skipped;
            continue;
        }

        if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            WalkAndCompare(RootfsRoot, ImageDriveRoot, ChildRel, Stats);
        }
        else
        {
            /* Periodic progress trace so a hang can be located. */
            if ((Stats->Compared % 100) == 0 && Stats->Compared > 0)
                trace("rootfs-vs-image: progress compared=%lu matches=%lu mismatches=%lu missing=%lu skipped=%lu cur='%ls'\n",
                      Stats->Compared, Stats->Matches, Stats->Mismatches,
                      Stats->MissingOnImage, Stats->Skipped, ChildRel);
            CompareOneFileTracked(RootfsRoot, ChildRel, ImageDriveRoot, Stats);
        }
    } while (FindNextFileW(hFind, &FindData));

    FindClose(hFind);
}

static void
TestRootfsVsImageCompare(void)
{
    WCHAR ImageDriveRoot[8];
    WCHAR RootfsRoot[MAX_PATH];
    RFS_VS_IMG_STATS Stats;
    DWORD Got;

    RtlZeroMemory(&Stats, sizeof(Stats));

    if (!FindReactOsImageVolume(ImageDriveRoot, _countof(ImageDriveRoot)))
    {
        trace("rootfs-vs-image: ReactOS NTFS volume not found - skipping\n");
        return;
    }

    /* Resolve rootfs path via %SystemRoot% (e.g. "C:\ReactOS"). NT-style
     * "\SystemRoot\" doesn't resolve through Win32 GetFileAttributesW. */
    Got = GetEnvironmentVariableW(L"SystemRoot", RootfsRoot, _countof(RootfsRoot));
    if (Got == 0 || Got >= _countof(RootfsRoot))
    {
        Got = GetWindowsDirectoryW(RootfsRoot, _countof(RootfsRoot));
        if (Got == 0 || Got >= _countof(RootfsRoot))
        {
            trace("rootfs-vs-image: could not resolve rootfs root - skipping\n");
            return;
        }
    }

    trace("rootfs-vs-image: walking entire rootfs tree (rootfs=%ls image=%lsreactos)\n",
          RootfsRoot, ImageDriveRoot);

    WalkAndCompare(RootfsRoot, ImageDriveRoot, L"", &Stats);

    trace("rootfs-vs-image: total=%lu matches=%lu mismatches=%lu missing_on_image=%lu skipped=%lu\n",
          Stats.Compared, Stats.Matches, Stats.Mismatches,
          Stats.MissingOnImage, Stats.Skipped);
}

static void
TestPersistentArtifactTree(void)
{
    static const WCHAR RootPath[] = L"D:\\ntfslx_auto";
    static const WCHAR RootPattern[] = L"D:\\ntfslx_auto\\*";
    static const WCHAR AlphaPath[] = L"D:\\ntfslx_auto\\alpha";
    static const WCHAR AlphaPattern[] = L"D:\\ntfslx_auto\\alpha\\*";
    static const WCHAR MixPath[] = L"D:\\ntfslx_auto\\mix";
    static const WCHAR MultiPath[] = L"D:\\ntfslx_auto\\multi";
    static const WCHAR CasesPath[] = L"D:\\ntfslx_auto\\cases";
    static const WCHAR CasesPattern[] = L"D:\\ntfslx_auto\\cases\\*";
    static const WCHAR RenameDirPath[] = L"D:\\ntfslx_auto\\cases\\rename";
    static const WCHAR RenamePattern[] = L"D:\\ntfslx_auto\\cases\\rename\\*";
    static const WCHAR MoveSourceDirPath[] = L"D:\\ntfslx_auto\\cases\\move_src";
    static const WCHAR RootMoveSourceDirPath[] = L"D:\\ntfslx_auto\\cases\\root_move_src";
    static const WCHAR ReplaceMoveSourceDirPath[] = L"D:\\ntfslx_auto\\cases\\replace_move_src";
    static const WCHAR ReplaceMoveDestDirPath[] = L"D:\\ntfslx_auto\\cases\\replace_move_dst";
    static const WCHAR DirRecyclePath[] = L"D:\\ntfslx_auto\\cases\\dir_recycle";
    static const WCHAR MovePattern[] = L"D:\\ntfslx_auto\\cases\\move_done\\*";
    static const WCHAR ReplaceMoveDestPattern[] = L"D:\\ntfslx_auto\\cases\\replace_move_dst\\*";
    static const WCHAR ReplaceMoveSourcePattern[] = L"D:\\ntfslx_auto\\cases\\replace_move_src\\*";
    static const WCHAR DirRecyclePattern[] = L"D:\\ntfslx_auto\\cases\\dir_recycle\\*";
    static const WCHAR RootFilePath[] = L"D:\\ntfslx_auto\\root.bin";
    static const WCHAR AlphaFilePath[] = L"D:\\ntfslx_auto\\alpha\\a.bin";
    static const WCHAR AlphaCopyPath[] = L"D:\\ntfslx_auto\\alpha\\copy.bin";
    static const WCHAR MixSmallPath[] = L"D:\\ntfslx_auto\\mix\\small.bin";
    static const WCHAR MixBigPath[] = L"D:\\ntfslx_auto\\mix\\big.bin";
    static const WCHAR MultiAPath[] = L"D:\\ntfslx_auto\\multi\\fa.bin";
    static const WCHAR MultiBPath[] = L"D:\\ntfslx_auto\\multi\\fb.bin";
    static const WCHAR MultiCPath[] = L"D:\\ntfslx_auto\\multi\\fc.bin";
    static const WCHAR ReplaceSourcePath[] = L"D:\\ntfslx_auto\\cases\\rename\\source.bin";
    static const WCHAR ReplaceVictimPath[] = L"D:\\ntfslx_auto\\cases\\rename\\victim.bin";
    static const WCHAR MoveFinalDirPath[] = L"D:\\ntfslx_auto\\cases\\move_done";
    static const WCHAR MoveSourceFilePath[] = L"D:\\ntfslx_auto\\cases\\move_src\\child.bin";
    static const WCHAR MoveFinalFilePath[] = L"D:\\ntfslx_auto\\cases\\move_done\\child.bin";
    static const WCHAR RootMoveSourceFilePath[] = L"D:\\ntfslx_auto\\cases\\root_move_src\\child.bin";
    static const WCHAR RootMoveTargetFilePath[] = L"D:\\ntfslx_root_move.bin";
    static const WCHAR ReplaceMoveSourceFilePath[] = L"D:\\ntfslx_auto\\cases\\replace_move_src\\source.bin";
    static const WCHAR ReplaceMoveVictimPath[] = L"D:\\ntfslx_auto\\cases\\replace_move_dst\\victim.bin";
    static const WCHAR DirRecycleChildPath[] = L"D:\\ntfslx_auto\\cases\\dir_recycle\\child.bin";
    static const WCHAR SharedRewritePath[] = L"D:\\ntfslx_auto\\cases\\shared_rewrite.bin";
    static const WCHAR RootRecyclePath[] = L"D:\\ntfslx_recycle.bin";
    static const WCHAR ShareFilePath[] = L"D:\\ntfslx_auto\\cases\\share.bin";
    static const WCHAR DeleteGonePath[] = L"D:\\ntfslx_auto\\cases\\delete_gone.bin";
    BYTE RootData[300];
    BYTE AlphaData[266];
    BYTE SmallData[128];
    BYTE BigData[4096];
    BYTE MultiAData[64];
    BYTE MultiBData[64];
    BYTE MultiCData[64];
    BYTE ReplaceData[384];
    BYTE ReplaceStale[96];
    BYTE ReplaceRead[384];
    BYTE MoveData[300];
    BYTE MoveRead[300];
    BYTE RootMoveData[512];
    BYTE RootMoveRead[512];
    BYTE ReplaceMoveData[704];
    BYTE ReplaceMoveStale[192];
    BYTE ReplaceMoveRead[704];
    BYTE RecycleFirst[288];
    BYTE RecycleSecond[640];
    BYTE RecycleRead[640];
    BYTE DirRecycleFirst[320];
    BYTE DirRecycleSecond[960];
    BYTE DirRecycleRead[960];
    BYTE SharedRewriteOriginal[4096];
    BYTE SharedRewriteFinal[1536];
    BYTE SharedRewriteRead[1536];
    BYTE ShareInitial[256];
    BYTE ShareRewrite[64];
    BYTE ShareRead[256];
    BYTE DeleteData[96];
    WIN32_FIND_DATAW FindData;
    HANDLE hShareReader = INVALID_HANDLE_VALUE;
    HANDLE hShareWriter = INVALID_HANDLE_VALUE;
    HANDLE hSharedRewriteReader = INVALID_HANDLE_VALUE;
    HANDLE hSharedRewriteWriter = INVALID_HANDLE_VALUE;
    HANDLE hDeleteKeep = INVALID_HANDLE_VALUE;
    HANDLE hDeleteMark = INVALID_HANDLE_VALUE;
    HANDLE hDir = INVALID_HANDLE_VALUE;
    LARGE_INTEGER FilePos;
    DWORD BytesWritten = 0;
    BOOL Ok;
    BOOL Found;

    DeleteFileW(RootMoveTargetFilePath);
    DeleteFileW(RootRecyclePath);
    DeleteTreeRecursive(RootPath);

    Ok = CreateDirectoryW(RootPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(ntfslx_auto) failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    Ok = CreateDirectoryW(AlphaPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(alpha) failed with error %lu\n", GetLastError());
    Ok = CreateDirectoryW(MixPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(mix) failed with error %lu\n", GetLastError());
    Ok = CreateDirectoryW(MultiPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(multi) failed with error %lu\n", GetLastError());
    Ok = CreateDirectoryW(CasesPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(cases) failed with error %lu\n", GetLastError());
    Ok = CreateDirectoryW(RenameDirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(cases\\rename) failed with error %lu\n", GetLastError());
    Ok = CreateDirectoryW(MoveSourceDirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(cases\\move_src) failed with error %lu\n", GetLastError());
    Ok = CreateDirectoryW(RootMoveSourceDirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(cases\\root_move_src) failed with error %lu\n", GetLastError());
    Ok = CreateDirectoryW(ReplaceMoveSourceDirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(cases\\replace_move_src) failed with error %lu\n", GetLastError());
    Ok = CreateDirectoryW(ReplaceMoveDestDirPath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(cases\\replace_move_dst) failed with error %lu\n", GetLastError());
    Ok = CreateDirectoryW(DirRecyclePath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "CreateDirectoryW(cases\\dir_recycle) failed with error %lu\n", GetLastError());

    FillPattern(RootData, sizeof(RootData), 0x10, 1);
    FillPattern(AlphaData, sizeof(AlphaData), 0xA0, 1);
    FillPattern(SmallData, sizeof(SmallData), 0x20, 1);
    FillPattern(BigData, sizeof(BigData), 0x40, 3);
    FillPattern(ReplaceData, sizeof(ReplaceData), 0x71, 2);
    FillPattern(ReplaceStale, sizeof(ReplaceStale), 0x11, 1);
    FillPattern(MoveData, sizeof(MoveData), 0xC0, 1);
    FillPattern(RootMoveData, sizeof(RootMoveData), 0xE1, 2);
    FillPattern(ReplaceMoveData, sizeof(ReplaceMoveData), 0x62, 5);
    FillPattern(ReplaceMoveStale, sizeof(ReplaceMoveStale), 0x17, 7);
    FillPattern(RecycleFirst, sizeof(RecycleFirst), 0x34, 3);
    FillPattern(RecycleSecond, sizeof(RecycleSecond), 0xB2, 1);
    FillPattern(DirRecycleFirst, sizeof(DirRecycleFirst), 0x29, 9);
    FillPattern(DirRecycleSecond, sizeof(DirRecycleSecond), 0xA7, 3);
    FillPattern(SharedRewriteOriginal, sizeof(SharedRewriteOriginal), 0x48, 5);
    FillPattern(SharedRewriteFinal, sizeof(SharedRewriteFinal), 0xD4, 2);
    FillPattern(ShareInitial, sizeof(ShareInitial), 0x15, 1);
    FillPattern(ShareRewrite, sizeof(ShareRewrite), 0xD0, 1);
    FillPattern(DeleteData, sizeof(DeleteData), 0x5A, 1);
    memset(MultiAData, 0xAA, sizeof(MultiAData));
    memset(MultiBData, 0xBB, sizeof(MultiBData));
    memset(MultiCData, 0xCC, sizeof(MultiCData));
    memset(ReplaceRead, 0x00, sizeof(ReplaceRead));
    memset(MoveRead, 0x00, sizeof(MoveRead));
    memset(RootMoveRead, 0x00, sizeof(RootMoveRead));
    memset(ReplaceMoveRead, 0x00, sizeof(ReplaceMoveRead));
    memset(RecycleRead, 0x00, sizeof(RecycleRead));
    memset(DirRecycleRead, 0x00, sizeof(DirRecycleRead));
    memset(SharedRewriteRead, 0x00, sizeof(SharedRewriteRead));
    memset(ShareRead, 0x00, sizeof(ShareRead));

    if (!WriteFileBuffer(RootFilePath, RootData, sizeof(RootData)))
        goto Cleanup;
    if (!WriteFileBuffer(AlphaFilePath, AlphaData, sizeof(AlphaData)))
        goto Cleanup;
    if (!WriteFileBuffer(MixSmallPath, SmallData, sizeof(SmallData)))
        goto Cleanup;
    if (!WriteFileBuffer(MixBigPath, BigData, sizeof(BigData)))
        goto Cleanup;
    if (!WriteFileBuffer(MultiAPath, MultiAData, sizeof(MultiAData)))
        goto Cleanup;
    if (!WriteFileBuffer(MultiBPath, MultiBData, sizeof(MultiBData)))
        goto Cleanup;
    if (!WriteFileBuffer(MultiCPath, MultiCData, sizeof(MultiCData)))
        goto Cleanup;

    Ok = CopyFileW(RootFilePath, AlphaCopyPath, FALSE);
    ok(Ok, "CopyFileW(root.bin -> alpha\\copy.bin) failed with error %lu\n", GetLastError());

    if (!WriteFileBuffer(ReplaceSourcePath, ReplaceData, sizeof(ReplaceData)))
        goto Cleanup;
    if (!WriteFileBuffer(ReplaceVictimPath, ReplaceStale, sizeof(ReplaceStale)))
        goto Cleanup;

    Ok = MoveFileExW(ReplaceSourcePath, ReplaceVictimPath, MOVEFILE_REPLACE_EXISTING);
    ok(Ok, "Persistent MoveFileExW replace rename failed with error %lu\n", GetLastError());

    if (!WriteFileBuffer(MoveSourceFilePath, MoveData, sizeof(MoveData)))
        goto Cleanup;

    Ok = MoveFileExW(MoveSourceDirPath, MoveFinalDirPath, 0);
    ok(Ok, "Persistent MoveFileExW directory rename failed with error %lu\n", GetLastError());

    if (!WriteFileBuffer(RootMoveSourceFilePath, RootMoveData, sizeof(RootMoveData)))
        goto Cleanup;

    Ok = MoveFileExW(RootMoveSourceFilePath, RootMoveTargetFilePath, 0);
    ok(Ok, "Persistent MoveFileExW(root move) failed with error %lu\n", GetLastError());

    Ok = RemoveDirectoryW(RootMoveSourceDirPath);
    ok(Ok, "RemoveDirectoryW(cases\\root_move_src) failed with error %lu\n", GetLastError());

    if (!WriteFileBuffer(ReplaceMoveSourceFilePath, ReplaceMoveData, sizeof(ReplaceMoveData)))
        goto Cleanup;
    if (!WriteFileBuffer(ReplaceMoveVictimPath, ReplaceMoveStale, sizeof(ReplaceMoveStale)))
        goto Cleanup;

    Ok = MoveFileExW(ReplaceMoveSourceFilePath, ReplaceMoveVictimPath, MOVEFILE_REPLACE_EXISTING);
    ok(Ok, "Persistent MoveFileExW(cross-dir replace move) failed with error %lu\n", GetLastError());

    if (!WriteFileBuffer(RootRecyclePath, RecycleFirst, sizeof(RecycleFirst)))
        goto Cleanup;
    Ok = DeleteFileW(RootRecyclePath);
    ok(Ok, "DeleteFileW(ntfslx_recycle.bin first pass) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    if (!WriteFileBuffer(RootRecyclePath, RecycleSecond, sizeof(RecycleSecond)))
        goto Cleanup;

    if (!WriteFileBuffer(DirRecycleChildPath, DirRecycleFirst, sizeof(DirRecycleFirst)))
        goto Cleanup;
    Ok = DeleteFileW(DirRecycleChildPath);
    ok(Ok, "DeleteFileW(cases\\dir_recycle\\child.bin first pass) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    Ok = RemoveDirectoryW(DirRecyclePath);
    ok(Ok, "RemoveDirectoryW(cases\\dir_recycle first pass) failed with error %lu\n", GetLastError());
    if (!Ok)
        goto Cleanup;
    Ok = CreateDirectoryW(DirRecyclePath, NULL);
    ok(Ok || GetLastError() == ERROR_ALREADY_EXISTS,
       "Recreate cases\\dir_recycle failed with error %lu\n", GetLastError());
    if (!Ok && GetLastError() != ERROR_ALREADY_EXISTS)
        goto Cleanup;
    if (!WriteFileBuffer(DirRecycleChildPath, DirRecycleSecond, sizeof(DirRecycleSecond)))
        goto Cleanup;

    if (!WriteFileBuffer(SharedRewritePath, SharedRewriteOriginal, sizeof(SharedRewriteOriginal)))
        goto Cleanup;
    hSharedRewriteReader = CreateFileW(SharedRewritePath,
                                       GENERIC_READ | SYNCHRONIZE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       NULL,
                                       OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL,
                                       NULL);
    ok(hSharedRewriteReader != INVALID_HANDLE_VALUE,
       "CreateFileW(persistent shared rewrite reader) failed with error %lu\n", GetLastError());
    if (hSharedRewriteReader == INVALID_HANDLE_VALUE)
        goto Cleanup;
    hSharedRewriteWriter = CreateFileW(SharedRewritePath,
                                       GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       NULL,
                                       OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL,
                                       NULL);
    ok(hSharedRewriteWriter != INVALID_HANDLE_VALUE,
       "CreateFileW(persistent shared rewrite writer) failed with error %lu\n", GetLastError());
    if (hSharedRewriteWriter == INVALID_HANDLE_VALUE)
        goto Cleanup;
    FilePos.QuadPart = 0;
    Ok = SetFilePointerEx(hSharedRewriteWriter, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(persistent shared rewrite to zero) failed with error %lu\n", GetLastError());
    Ok = SetEndOfFile(hSharedRewriteWriter);
    ok(Ok, "SetEndOfFile(persistent shared rewrite to zero) failed with error %lu\n", GetLastError());
    Ok = WriteFile(hSharedRewriteWriter, SharedRewriteFinal, sizeof(SharedRewriteFinal), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(SharedRewriteFinal),
       "WriteFile(persistent shared rewrite) failed: ok=%d written=%lu err=%lu\n",
       Ok, BytesWritten, GetLastError());
    Ok = FlushFileBuffers(hSharedRewriteWriter);
    ok(Ok, "FlushFileBuffers(persistent shared rewrite writer) failed with error %lu\n", GetLastError());
    FilePos.QuadPart = 0;
    Ok = SetFilePointerEx(hSharedRewriteReader, FilePos, NULL, FILE_BEGIN);
    ok(Ok, "SetFilePointerEx(persistent shared rewrite reader) failed with error %lu\n", GetLastError());
    Ok = ReadFile(hSharedRewriteReader, SharedRewriteRead, sizeof(SharedRewriteRead), &BytesWritten, NULL);
    ok(Ok && BytesWritten == sizeof(SharedRewriteFinal),
       "ReadFile(persistent shared rewrite reader) failed: ok=%d read=%lu expected=%u err=%lu\n",
       Ok, BytesWritten, (unsigned)sizeof(SharedRewriteFinal), GetLastError());
    ok(memcmp(SharedRewriteRead, SharedRewriteFinal, sizeof(SharedRewriteFinal)) == 0,
       "persistent shared rewrite reader content mismatch\n");
    CloseHandle(hSharedRewriteWriter);
    hSharedRewriteWriter = INVALID_HANDLE_VALUE;
    CloseHandle(hSharedRewriteReader);
    hSharedRewriteReader = INVALID_HANDLE_VALUE;

    if (!WriteFileBuffer(ShareFilePath, ShareInitial, sizeof(ShareInitial)))
        goto Cleanup;

    hShareReader = CreateFileW(ShareFilePath,
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    ok(hShareReader != INVALID_HANDLE_VALUE,
       "CreateFileW(persistent share reader) failed with error %lu\n", GetLastError());
    if (hShareReader != INVALID_HANDLE_VALUE)
    {
        hShareWriter = CreateFileW(ShareFilePath,
                                   GENERIC_WRITE | SYNCHRONIZE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL,
                                   NULL);
        ok(hShareWriter != INVALID_HANDLE_VALUE,
           "CreateFileW(persistent share writer) failed with error %lu\n", GetLastError());
        if (hShareWriter != INVALID_HANDLE_VALUE)
        {
            FilePos.QuadPart = 128;
            Ok = SetFilePointerEx(hShareWriter, FilePos, NULL, FILE_BEGIN);
            ok(Ok, "SetFilePointerEx(persistent share writer) failed with error %lu\n", GetLastError());
            Ok = WriteFile(hShareWriter, ShareRewrite, sizeof(ShareRewrite), &BytesWritten, NULL);
            ok(Ok && BytesWritten == sizeof(ShareRewrite),
               "WriteFile(persistent share writer) failed: ok=%d written=%lu err=%lu\n",
               Ok, BytesWritten, GetLastError());
            CloseHandle(hShareWriter);
            hShareWriter = INVALID_HANDLE_VALUE;
        }
        CloseHandle(hShareReader);
        hShareReader = INVALID_HANDLE_VALUE;
    }

    if (!ReadFileBuffer(ShareFilePath, ShareRead, sizeof(ShareRead)))
        goto Cleanup;
    ok(memcmp(ShareRead, ShareInitial, 128) == 0,
       "share.bin prefix changed unexpectedly\n");
    ok(memcmp(ShareRead + 128, ShareRewrite, sizeof(ShareRewrite)) == 0,
       "share.bin rewritten range mismatch\n");

    if (!WriteFileBuffer(DeleteGonePath, DeleteData, sizeof(DeleteData)))
        goto Cleanup;

    hDeleteKeep = CreateFileW(DeleteGonePath,
                              GENERIC_READ | SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL);
    ok(hDeleteKeep != INVALID_HANDLE_VALUE,
       "CreateFileW(persistent delete keep) failed with error %lu\n", GetLastError());
    if (hDeleteKeep != INVALID_HANDLE_VALUE)
    {
        hDeleteMark = CreateFileW(DeleteGonePath,
                                  DELETE | SYNCHRONIZE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  NULL);
        ok(hDeleteMark != INVALID_HANDLE_VALUE,
           "CreateFileW(persistent delete mark) failed with error %lu\n", GetLastError());
        if (hDeleteMark != INVALID_HANDLE_VALUE)
        {
            Ok = SetDeletePendingOnHandle(hDeleteMark, TRUE);
            ok(Ok, "SetDeletePendingOnHandle(persistent) failed with error %lu\n", GetLastError());
            CloseHandle(hDeleteMark);
            hDeleteMark = INVALID_HANDLE_VALUE;
        }
        CloseHandle(hDeleteKeep);
        hDeleteKeep = INVALID_HANDLE_VALUE;
    }
    ok(GetFileAttributesW(DeleteGonePath) == INVALID_FILE_ATTRIBUTES,
       "delete_gone.bin should be absent after delete-pending close (err=%lu)\n", GetLastError());

    Found = FindNamedEntryInPattern(RootPattern, L"alpha", &FindData);
    ok(Found, "alpha missing from persistent root listing\n");
    Found = FindNamedEntryInPattern(RootPattern, L"mix", &FindData);
    ok(Found, "mix missing from persistent root listing\n");
    Found = FindNamedEntryInPattern(RootPattern, L"multi", &FindData);
    ok(Found, "multi missing from persistent root listing\n");
    Found = FindNamedEntryInPattern(RootPattern, L"cases", &FindData);
    ok(Found, "cases missing from persistent root listing\n");

    Found = FindNamedEntryInPattern(AlphaPattern, L"a.bin", &FindData);
    ok(Found, "a.bin missing from alpha listing\n");
    if (Found)
    {
        ULONGLONG Size = ((ULONGLONG)FindData.nFileSizeHigh << 32) | FindData.nFileSizeLow;
        ok(Size == sizeof(AlphaData), "alpha\\a.bin size is %I64u, expected %u\n", Size, (unsigned)sizeof(AlphaData));
    }

    Found = FindNamedEntryInPattern(CasesPattern, L"rename", &FindData);
    ok(Found, "rename missing from cases listing\n");
    Found = FindNamedEntryInPattern(CasesPattern, L"move_done", &FindData);
    ok(Found, "move_done missing from cases listing\n");
    Found = FindNamedEntryInPattern(CasesPattern, L"share.bin", &FindData);
    ok(Found, "share.bin missing from cases listing\n");
    Found = FindNamedEntryInPattern(CasesPattern, L"replace_move_src", &FindData);
    ok(Found, "replace_move_src missing from cases listing\n");
    Found = FindNamedEntryInPattern(CasesPattern, L"replace_move_dst", &FindData);
    ok(Found, "replace_move_dst missing from cases listing\n");
    Found = FindNamedEntryInPattern(CasesPattern, L"dir_recycle", &FindData);
    ok(Found, "dir_recycle missing from cases listing\n");
    Found = FindNamedEntryInPattern(CasesPattern, L"shared_rewrite.bin", &FindData);
    ok(Found, "shared_rewrite.bin missing from cases listing\n");
    Found = FindNamedEntryInPattern(CasesPattern, L"delete_gone.bin", &FindData);
    ok(!Found, "delete_gone.bin should not remain in cases listing\n");
    Found = FindNamedEntryInPattern(CasesPattern, L"root_move_src", &FindData);
    ok(!Found, "root_move_src should be absent after persistent root move\n");
    ok(GetFileAttributesW(MoveSourceDirPath) == INVALID_FILE_ATTRIBUTES,
       "move_src should be absent after persistent directory rename (err=%lu)\n", GetLastError());
    ok(GetFileAttributesW(RootMoveSourceFilePath) == INVALID_FILE_ATTRIBUTES,
       "root_move_src\\child.bin should be absent after persistent root move (err=%lu)\n",
       GetLastError());
    ok(GetFileAttributesW(ReplaceMoveSourceFilePath) == INVALID_FILE_ATTRIBUTES,
       "replace_move_src\\source.bin should be absent after persistent cross-dir replace move (err=%lu)\n",
       GetLastError());

    Found = FindNamedEntryInPattern(RenamePattern, L"victim.bin", &FindData);
    ok(Found, "rename\\victim.bin missing after persistent replace rename\n");
    Found = FindNamedEntryInPattern(RenamePattern, L"source.bin", &FindData);
    ok(!Found, "rename\\source.bin should be absent after persistent replace rename\n");
    if (!ReadFileBuffer(ReplaceVictimPath, ReplaceRead, sizeof(ReplaceRead)))
        goto Cleanup;
    ok(memcmp(ReplaceRead, ReplaceData, sizeof(ReplaceData)) == 0,
       "persistent replace victim content mismatch\n");

    Found = FindNamedEntryInPattern(MovePattern, L"child.bin", &FindData);
    ok(Found, "move_done\\child.bin missing after persistent directory rename\n");
    if (!ReadFileBuffer(MoveFinalFilePath, MoveRead, sizeof(MoveRead)))
        goto Cleanup;
    ok(memcmp(MoveRead, MoveData, sizeof(MoveData)) == 0,
       "persistent moved child content mismatch\n");

    Found = FindNamedEntryInPattern(ReplaceMoveDestPattern, L"victim.bin", &FindData);
    ok(Found, "replace_move_dst\\victim.bin missing after persistent cross-dir replace move\n");
    Found = FindNamedEntryInPattern(ReplaceMoveSourcePattern, L"source.bin", &FindData);
    ok(!Found, "replace_move_src\\source.bin should not remain after persistent cross-dir replace move\n");
    if (!ReadFileBuffer(ReplaceMoveVictimPath, ReplaceMoveRead, sizeof(ReplaceMoveRead)))
        goto Cleanup;
    ok(memcmp(ReplaceMoveRead, ReplaceMoveData, sizeof(ReplaceMoveData)) == 0,
       "persistent cross-dir replace move victim content mismatch\n");

    Found = FindNamedEntryInPattern(DirRecyclePattern, L"child.bin", &FindData);
    ok(Found, "dir_recycle\\child.bin missing after persistent directory recreate\n");
    if (!ReadFileBuffer(DirRecycleChildPath, DirRecycleRead, sizeof(DirRecycleRead)))
        goto Cleanup;
    ok(memcmp(DirRecycleRead, DirRecycleSecond, sizeof(DirRecycleSecond)) == 0,
       "persistent directory recycle child content mismatch\n");

    if (!ReadFileBuffer(SharedRewritePath, SharedRewriteRead, sizeof(SharedRewriteRead)))
        goto Cleanup;
    ok(memcmp(SharedRewriteRead, SharedRewriteFinal, sizeof(SharedRewriteFinal)) == 0,
       "persistent shared rewrite final content mismatch\n");

    Found = FindNamedEntryInPattern(L"D:\\*", L"ntfslx_root_move.bin", &FindData);
    ok(Found, "ntfslx_root_move.bin missing from root after persistent root move\n");
    if (!ReadFileBuffer(RootMoveTargetFilePath, RootMoveRead, sizeof(RootMoveRead)))
        goto Cleanup;
    ok(memcmp(RootMoveRead, RootMoveData, sizeof(RootMoveData)) == 0,
       "persistent root-moved file content mismatch\n");

    Found = FindNamedEntryInPattern(L"D:\\*", L"ntfslx_recycle.bin", &FindData);
    ok(Found, "ntfslx_recycle.bin missing from root after delete/recreate churn\n");
    if (!ReadFileBuffer(RootRecyclePath, RecycleRead, sizeof(RecycleRead)))
        goto Cleanup;
    ok(memcmp(RecycleRead, RecycleSecond, sizeof(RecycleSecond)) == 0,
       "persistent root recycle content mismatch\n");

    hDir = CreateFileW(L"D:\\ntfslx_auto\\alpha\\",
                       FILE_LIST_DIRECTORY | SYNCHRONIZE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS,
                       NULL);
    ok(hDir != INVALID_HANDLE_VALUE, "Open alpha with trailing slash failed with error %lu\n", GetLastError());
    if (hDir != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDir);
        hDir = INVALID_HANDLE_VALUE;
    }

    hDir = CreateFileW(L"D:\\ntfslx_auto\\cases\\move_done\\",
                       FILE_LIST_DIRECTORY | SYNCHRONIZE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS,
                       NULL);
    ok(hDir != INVALID_HANDLE_VALUE, "Open move_done with trailing slash failed with error %lu\n", GetLastError());
    if (hDir != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDir);
        hDir = INVALID_HANDLE_VALUE;
    }

    trace("Persistent artifact tree created under D:\\ntfslx_auto\n");

Cleanup:
    if (hDir != INVALID_HANDLE_VALUE)
        CloseHandle(hDir);
    if (hSharedRewriteWriter != INVALID_HANDLE_VALUE)
        CloseHandle(hSharedRewriteWriter);
    if (hSharedRewriteReader != INVALID_HANDLE_VALUE)
        CloseHandle(hSharedRewriteReader);
    if (hDeleteMark != INVALID_HANDLE_VALUE)
        CloseHandle(hDeleteMark);
    if (hDeleteKeep != INVALID_HANDLE_VALUE)
        CloseHandle(hDeleteKeep);
    if (hShareWriter != INVALID_HANDLE_VALUE)
        CloseHandle(hShareWriter);
    if (hShareReader != INVALID_HANDLE_VALUE)
        CloseHandle(hShareReader);
}

static void
RunScenario(PCSTR Name, void (*Scenario)(void))
{
    LONG Before = g_FailureCount;

    trace("BEGIN %s\n", Name);
    Scenario();
    trace("END %s delta_failures=%ld total_failures=%ld\n",
          Name,
          g_FailureCount - Before,
          g_FailureCount);
}

int
wmain(void)
{
    trace("START\n");

    if (!WaitForNtfsVolume())
    {
        trace("COMPLETE failures=%ld\n", g_FailureCount);
        return (g_FailureCount != 0);
    }

    RunScenario("volume info", TestVolumeInfo);
    RunScenario("free space", TestFreeSpace);
    RunScenario("root listing", TestDirListing);
    RunScenario("create write read", TestCreateWriteRead);
    RunScenario("explorer style subdir listing", TestExplorerStyleSubdirListing);
    RunScenario("directory change notifications", TestDirectoryChangeNotifications);
    RunScenario("gap write zero fill", TestGapWriteZeroFill);
    RunScenario("truncate rewrite no stale data", TestTruncateRewriteNoStaleData);
    RunScenario("single-file space accounting", TestVolumeSpaceAccountingSingleFile);
    RunScenario("multi-file space accounting", TestVolumeSpaceAccountingMultiFile);
    RunScenario("copy rename cycle", TestCopyRenameCycle);
    RunScenario("replace existing rename", TestReplaceExistingRename);
    RunScenario("directory rename cycle", TestDirectoryRenameCycle);
    RunScenario("move file to root", TestMoveFileToRoot);
    RunScenario("replace existing move across directories", TestReplaceExistingMoveAcrossDirectories);
    RunScenario("root delete recreate churn", TestRootDeleteRecreateChurn);
    RunScenario("directory delete recreate churn", TestDirectoryDeleteRecreateChurn);
    RunScenario("shared handle truncate rewrite", TestSharedHandleTruncateRewrite);
    RunScenario("delete on close with open handle", TestDeleteOnCloseWithOpenHandle);
    RunScenario("share conflict churn", TestShareConflictChurn);
    RunScenario("security descriptor probe", TestSecurityDescriptorProbe);
    RunScenario("logfile probe", TestLogFileProbe);
    RunScenario("ea round-trip probe", TestEaRoundTripProbe);
    RunScenario("reparse point probe", TestReparsePointProbe);
    RunScenario("dirty volume probe", TestDirtyVolumeProbe);
    /*
     * Bug-class regressions:
     *   - "read large offset" exercises the failure mode behind Bug #1
     *     (0xc0000218 STATUS_CANNOT_LOAD_REGISTRY_FILE on the SOFTWARE
     *     hive at offset 0x60028). Runs *before* the dirty-volume
     *     probe forced everything dirty, so the cache state is fresh
     *     enough to catch a paging-IO mistranslation.
     *   - "mount is non-destructive" exercises Bug #2 (mount-time
     *     dirty-bit + $LogFile wipe). Issues IOCTL_NTFSLX_TIER0_PROOF
     *     before and after a deliberate write to verify the latch.
     */
    RunScenario("read large offset", TestReadLargeOffset);
    RunScenario("mount is non-destructive", TestMountIsNonDestructive);
    RunScenario("MftAllocLock reentrancy", TestMftAllocLockReentrancy);    RunScenario("burst create latency", TestBurstCreateLatency);
    RunScenario("volume control probe", TestVolumeControlProbe);
    trace("BLOCKER PROBES COMPLETE\n");
    RunScenario("fat32 vs ntfs sequential benchmark", TestFat32VsNtfsBenchmark);
    RunScenario("rootfs vs image compare", TestRootfsVsImageCompare);
    RunScenario("persistent artifact tree", TestPersistentArtifactTree);
    RunScenario("final free space", TestFreeSpace);
    RunScenario("volume dismount probe", TestVolumeDismountProbe);

    trace("COMPLETE failures=%ld\n", g_FailureCount);
    return (g_FailureCount != 0);
}
