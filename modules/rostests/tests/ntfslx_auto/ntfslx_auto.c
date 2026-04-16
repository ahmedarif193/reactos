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

NTSYSAPI NTSTATUS NTAPI NtSetInformationFile(_In_ HANDLE FileHandle,
                                             _Out_ PNTFSLX_IO_STATUS_BLOCK IoStatusBlock,
                                             _In_ PVOID FileInformation,
                                             _In_ ULONG Length,
                                             _In_ NTFSLX_FILE_INFORMATION_CLASS FileInformationClass);
NTSYSAPI ULONG NTAPI RtlNtStatusToDosError(_In_ NTSTATUS Status);

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

static LONG g_FailureCount;

#define trace(fmt, ...) DbgPrint("NTFSLX-AUTO: " fmt, ##__VA_ARGS__)
#define fail(fmt, ...) do { ++g_FailureCount; DbgPrint("NTFSLX-AUTO [fail]: " fmt, ##__VA_ARGS__); } while (0)
#define ok(cond, fmt, ...) do { if (!(cond)) fail(fmt, ##__VA_ARGS__); } while (0)
#define skip(cond, fmt, ...) do { if (cond) { DbgPrint("NTFSLX-AUTO [skip]: " fmt, ##__VA_ARGS__); return; } } while (0)

typedef struct _NTFSLX_VOLUME_SPACE_SNAPSHOT
{
    ULONGLONG TotalBytes;
    ULONGLONG FreeBytes;
    ULONGLONG FreeBytesAvailable;
    DWORD ClusterSize;
} NTFSLX_VOLUME_SPACE_SNAPSHOT, *PNTFSLX_VOLUME_SPACE_SNAPSHOT;

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
    if (!Ok)
    {
        skip(TRUE, "GetVolumeInformationW failed (error %lu)\n", GetLastError());
        return;
    }

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
    static const WCHAR MovePattern[] = L"D:\\ntfslx_auto\\cases\\move_done\\*";
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
    BYTE ShareInitial[256];
    BYTE ShareRewrite[64];
    BYTE ShareRead[256];
    BYTE DeleteData[96];
    WIN32_FIND_DATAW FindData;
    HANDLE hShareReader = INVALID_HANDLE_VALUE;
    HANDLE hShareWriter = INVALID_HANDLE_VALUE;
    HANDLE hDeleteKeep = INVALID_HANDLE_VALUE;
    HANDLE hDeleteMark = INVALID_HANDLE_VALUE;
    HANDLE hDir = INVALID_HANDLE_VALUE;
    LARGE_INTEGER FilePos;
    DWORD BytesWritten = 0;
    BOOL Ok;
    BOOL Found;

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

    FillPattern(RootData, sizeof(RootData), 0x10, 1);
    FillPattern(AlphaData, sizeof(AlphaData), 0xA0, 1);
    FillPattern(SmallData, sizeof(SmallData), 0x20, 1);
    FillPattern(BigData, sizeof(BigData), 0x40, 3);
    FillPattern(ReplaceData, sizeof(ReplaceData), 0x71, 2);
    FillPattern(ReplaceStale, sizeof(ReplaceStale), 0x11, 1);
    FillPattern(MoveData, sizeof(MoveData), 0xC0, 1);
    FillPattern(ShareInitial, sizeof(ShareInitial), 0x15, 1);
    FillPattern(ShareRewrite, sizeof(ShareRewrite), 0xD0, 1);
    FillPattern(DeleteData, sizeof(DeleteData), 0x5A, 1);
    memset(MultiAData, 0xAA, sizeof(MultiAData));
    memset(MultiBData, 0xBB, sizeof(MultiBData));
    memset(MultiCData, 0xCC, sizeof(MultiCData));
    memset(ReplaceRead, 0x00, sizeof(ReplaceRead));
    memset(MoveRead, 0x00, sizeof(MoveRead));
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
    Found = FindNamedEntryInPattern(CasesPattern, L"delete_gone.bin", &FindData);
    ok(!Found, "delete_gone.bin should not remain in cases listing\n");
    ok(GetFileAttributesW(MoveSourceDirPath) == INVALID_FILE_ATTRIBUTES,
       "move_src should be absent after persistent directory rename (err=%lu)\n", GetLastError());

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
    RunScenario("delete on close with open handle", TestDeleteOnCloseWithOpenHandle);
    RunScenario("share conflict churn", TestShareConflictChurn);
    RunScenario("persistent artifact tree", TestPersistentArtifactTree);
    RunScenario("final free space", TestFreeSpace);

    trace("COMPLETE failures=%ld\n", g_FailureCount);
    return (g_FailureCount != 0);
}
