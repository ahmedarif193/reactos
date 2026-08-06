/*
 * PROJECT:     ReactOS diskbench
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Unbuffered disk read/write benchmark
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 *
 * Usage: diskbench <file-on-target-volume> [file-size-MB]
 *
 * The file is created with FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
 * so every transfer goes through the storage stack to the device; the cache
 * manager never serves or absorbs any of it. Results print as one line per
 * phase: DISKBENCH <phase> <block-size> <MB/s> <IOPS>.
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_FILE_MB 64
#define RANDOM_OPS      2048

static ULONG RandomOps = RANDOM_OPS;

static LARGE_INTEGER Frequency;

static double ElapsedSeconds(LARGE_INTEGER Start, LARGE_INTEGER End)
{
    return (double)(End.QuadPart - Start.QuadPart) / (double)Frequency.QuadPart;
}

static ULONG NextRandom(PULONG Seed)
{
    *Seed = *Seed * 1103515245 + 12345;
    return (*Seed >> 8) & 0x7FFFFF;
}

static void Report(const char* Format, ...)
{
    char Line[192];
    va_list Args;

    va_start(Args, Format);
    _vsnprintf(Line, sizeof(Line) - 1, Format, Args);
    va_end(Args);
    Line[sizeof(Line) - 1] = 0;
    /* Both channels: stdout for a console, the debugger for serial logs. */
    printf("%s", Line);
    fflush(stdout);
    OutputDebugStringA(Line);
}

static void ReportPhase(const char* Phase, ULONG BlockSize, ULONGLONG Bytes, ULONG Ops, double Seconds, DWORD TickMs)
{
    double MBs = Seconds > 0.0 ? ((double)Bytes / (1024.0 * 1024.0)) / Seconds : 0.0;
    double Iops = Seconds > 0.0 ? (double)Ops / Seconds : 0.0;

    Report("DISKBENCH %s %lu %.2f %.0f ticks %lu\n", Phase, BlockSize, MBs, Iops, TickMs);
}

static BOOL RunSequential(HANDLE File, BOOL Write, ULONG BlockSize, ULONGLONG FileSize, PUCHAR Buffer, const char* Phase)
{
    LARGE_INTEGER Start, End, Offset;
    ULONGLONG Done = 0;
    ULONG Ops = 0;
    DWORD Transferred;
    DWORD Tick0 = GetTickCount();

    Offset.QuadPart = 0;
    if (SetFilePointerEx(File, Offset, NULL, FILE_BEGIN) == FALSE)
        return FALSE;

    QueryPerformanceCounter(&Start);
    while (Done + BlockSize <= FileSize)
    {
        BOOL Ok = Write ? WriteFile(File, Buffer, BlockSize, &Transferred, NULL)
                        : ReadFile(File, Buffer, BlockSize, &Transferred, NULL);

        if (!Ok || Transferred != BlockSize)
        {
            Report("DISKBENCH %s FAILED error %lu at %I64u\n", Phase, GetLastError(), Done);
            return FALSE;
        }
        Done += BlockSize;
        Ops++;
    }
    QueryPerformanceCounter(&End);

    ReportPhase(Phase, BlockSize, Done, Ops, ElapsedSeconds(Start, End), GetTickCount() - Tick0);
    return TRUE;
}

static BOOL RunRandom(HANDLE File, BOOL Write, ULONG BlockSize, ULONGLONG FileSize, PUCHAR Buffer, const char* Phase)
{
    LARGE_INTEGER Start, End, Offset;
    ULONGLONG Blocks = FileSize / BlockSize;
    ULONG Seed = 0x5EED5EED;
    ULONG Ops;
    DWORD Transferred;
    DWORD Tick0 = GetTickCount();

    QueryPerformanceCounter(&Start);
    for (Ops = 0; Ops < RandomOps; Ops++)
    {
        BOOL Ok;

        Offset.QuadPart = (LONGLONG)(NextRandom(&Seed) % Blocks) * BlockSize;
        if (SetFilePointerEx(File, Offset, NULL, FILE_BEGIN) == FALSE)
            return FALSE;

        Ok = Write ? WriteFile(File, Buffer, BlockSize, &Transferred, NULL)
                   : ReadFile(File, Buffer, BlockSize, &Transferred, NULL);
        if (!Ok || Transferred != BlockSize)
        {
            Report("DISKBENCH %s FAILED error %lu at %I64d\n", Phase, GetLastError(), Offset.QuadPart);
            return FALSE;
        }
    }
    QueryPerformanceCounter(&End);

    ReportPhase(Phase, BlockSize, (ULONGLONG)Ops * BlockSize, Ops, ElapsedSeconds(Start, End), GetTickCount() - Tick0);
    return TRUE;
}

/* Overlapped read-only bench: keeps Depth requests in flight against the
 * raw disk so the queue actually fills; the synchronous scan above only
 * measures per-request latency. */
static void AsyncRawPhase(HANDLE Disk, ULONG DiskNumber, ULONGLONG Limit,
                          ULONG BlockSize, ULONG Depth, BOOL Random,
                          ULONG MaxOps, ULONG MaxMs)
{
    OVERLAPPED Slots[64];
    PUCHAR Region;
    ULONGLONG NextOffset = 0;
    ULONGLONG Bytes = 0;
    ULONG Seed = 0x5EED5EED;
    ULONG Issued = 0;
    ULONG Done = 0;
    ULONG Index;
    DWORD Tick0;
    DWORD Elapsed;
    char Tag[32];
    BOOL Failed = FALSE;

    if (Depth > 64)
        Depth = 64;
    Region = (PUCHAR)VirtualAlloc(NULL, (SIZE_T)Depth * BlockSize, MEM_COMMIT, PAGE_READWRITE);
    if (!Region)
        return;
    memset(Slots, 0, sizeof(Slots));
    for (Index = 0; Index < Depth; Index++)
    {
        Slots[Index].hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (!Slots[Index].hEvent)
        {
            while (Index)
                CloseHandle(Slots[--Index].hEvent);
            VirtualFree(Region, 0, MEM_RELEASE);
            return;
        }
    }

    Tick0 = GetTickCount();
    while (Done < MaxOps && !Failed)
    {
        Index = Done % Depth;
        if (Issued >= Depth)
        {
            DWORD Transferred;

            if (!GetOverlappedResult(Disk, &Slots[Index], &Transferred, TRUE) ||
                Transferred != BlockSize)
            {
                Report("DISKBENCH ARAW %lu FAILED error %lu\n", DiskNumber, GetLastError());
                Failed = TRUE;
                break;
            }
            Bytes += Transferred;
            Done++;
        }
        if (Issued < MaxOps && (GetTickCount() - Tick0) < MaxMs)
        {
            ULONGLONG Offset;
            DWORD Transferred;

            if (Random)
                Offset = (ULONGLONG)(NextRandom(&Seed) % (ULONG)(Limit >> 12)) << 12;
            else
            {
                Offset = NextOffset;
                NextOffset += BlockSize;
                if (NextOffset + BlockSize > Limit)
                    NextOffset = 0;
            }
            Index = Issued % Depth;
            ResetEvent(Slots[Index].hEvent);
            Slots[Index].Offset = (DWORD)Offset;
            Slots[Index].OffsetHigh = (DWORD)(Offset >> 32);
            if (!ReadFile(Disk, Region + (SIZE_T)Index * BlockSize, BlockSize, &Transferred, &Slots[Index]) &&
                GetLastError() != ERROR_IO_PENDING)
            {
                Report("DISKBENCH ARAW %lu FAILED submit error %lu\n", DiskNumber, GetLastError());
                Failed = TRUE;
                break;
            }
            Issued++;
        }
        else if (Issued == Done)
        {
            break;
        }
    }
    while (Done < Issued)
    {
        DWORD Transferred;

        Index = Done % Depth;
        if (GetOverlappedResult(Disk, &Slots[Index], &Transferred, TRUE))
            Bytes += Transferred;
        Done++;
    }
    Elapsed = GetTickCount() - Tick0;

    if (!Failed)
    {
        _snprintf(Tag, sizeof(Tag), "ARAW%s-%lu QD%lu", Random ? "RND" : "SEQ", DiskNumber, Depth);
        Report("DISKBENCH %s %lu ops %lu MB %I64u ticks %lu\n",
               Tag, BlockSize, Done, Bytes >> 20, Elapsed);
    }
    for (Index = 0; Index < Depth; Index++)
        CloseHandle(Slots[Index].hEvent);
    VirtualFree(Region, 0, MEM_RELEASE);
}

static int AsyncRawScan(ULONG Depth)
{
    char Path[32];
    ULONG DiskNumber;

    for (DiskNumber = 0; DiskNumber < 8; DiskNumber++)
    {
        HANDLE Disk;
        GET_LENGTH_INFORMATION LengthInfo;
        DWORD Transferred;
        ULONGLONG Limit;

        _snprintf(Path, sizeof(Path), "\\\\.\\PhysicalDrive%lu", DiskNumber);
        Disk = CreateFileA(Path,
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED,
                           NULL);
        if (Disk == INVALID_HANDLE_VALUE)
            continue;
        if (!DeviceIoControl(Disk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                             &LengthInfo, sizeof(LengthInfo), &Transferred, NULL))
            LengthInfo.Length.QuadPart = 0;
        Limit = (ULONGLONG)LengthInfo.Length.QuadPart;
        Report("DISKBENCH ARAWDISK %lu size %I64u MB\n", DiskNumber, Limit >> 20);
        if (Limit < 64 * 1024 * 1024)
        {
            CloseHandle(Disk);
            continue;
        }
        if (Limit > 1024ULL * 1024 * 1024)
            Limit = 1024ULL * 1024 * 1024;

        /* CrystalDiskMark-style SEQ1M Q8, then deeper small-block phases. */
        AsyncRawPhase(Disk, DiskNumber, Limit, 1024 * 1024, 8, FALSE, 4096, 8000);
        AsyncRawPhase(Disk, DiskNumber, Limit, 128 * 1024, Depth, FALSE, 4096, 8000);
        AsyncRawPhase(Disk, DiskNumber, Limit, 4096, Depth, TRUE, 16384, 8000);
        CloseHandle(Disk);
    }
    Report("DISKBENCH ARAWSCAN DONE\n");
    return 0;
}

/* Read-only bench straight off the disk devices: identifies every disk by
 * size and measures the port driver path with no filesystem in between. */
static int RawScan(PUCHAR Buffer)
{
    char Path[32];
    ULONG DiskNumber;

    for (DiskNumber = 0; DiskNumber < 8; DiskNumber++)
    {
        HANDLE Disk;
        GET_LENGTH_INFORMATION LengthInfo;
        LARGE_INTEGER Start, End, Offset;
        ULONGLONG Limit;
        ULONG Seed = 0x5EED5EED;
        ULONG Ops;
        DWORD Transferred;
        DWORD Tick0;
        BOOL Failed = FALSE;

        _snprintf(Path, sizeof(Path), "\\\\.\\PhysicalDrive%lu", DiskNumber);
        Disk = CreateFileA(Path,
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_NO_BUFFERING,
                           NULL);
        if (Disk == INVALID_HANDLE_VALUE)
            continue;

        if (!DeviceIoControl(Disk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                             &LengthInfo, sizeof(LengthInfo), &Transferred, NULL))
            LengthInfo.Length.QuadPart = 0;
        Report("DISKBENCH RAWDISK %lu size %I64u MB\n",
               DiskNumber, (ULONGLONG)LengthInfo.Length.QuadPart >> 20);

        Limit = (ULONGLONG)LengthInfo.Length.QuadPart;
        if (Limit < 32 * 1024 * 1024)
        {
            CloseHandle(Disk);
            continue;
        }

        Offset.QuadPart = 0;
        SetFilePointerEx(Disk, Offset, NULL, FILE_BEGIN);
        Tick0 = GetTickCount();
        QueryPerformanceCounter(&Start);
        for (Ops = 0; Ops < 16; Ops++)
        {
            if (!ReadFile(Disk, Buffer, 1024 * 1024, &Transferred, NULL) || Transferred != 1024 * 1024)
            {
                Report("DISKBENCH RAWSEQ %lu FAILED error %lu\n", DiskNumber, GetLastError());
                Failed = TRUE;
                break;
            }
        }
        QueryPerformanceCounter(&End);
        if (!Failed)
        {
            char Tag[24];

            _snprintf(Tag, sizeof(Tag), "RAWSEQ-%lu", DiskNumber);
            ReportPhase(Tag, 1024 * 1024, (ULONGLONG)Ops << 20, Ops,
                        ElapsedSeconds(Start, End), GetTickCount() - Tick0);
        }

        if (Limit > 1024ULL * 1024 * 1024)
            Limit = 1024ULL * 1024 * 1024;
        Tick0 = GetTickCount();
        QueryPerformanceCounter(&Start);
        for (Ops = 0; Ops < 256 && !Failed; Ops++)
        {
            Offset.QuadPart = (LONGLONG)(NextRandom(&Seed) % (ULONG)(Limit >> 12)) << 12;
            SetFilePointerEx(Disk, Offset, NULL, FILE_BEGIN);
            if (!ReadFile(Disk, Buffer, 4096, &Transferred, NULL) || Transferred != 4096)
            {
                Report("DISKBENCH RAWRND %lu FAILED error %lu\n", DiskNumber, GetLastError());
                Failed = TRUE;
            }
        }
        QueryPerformanceCounter(&End);
        if (!Failed)
        {
            char Tag[24];

            _snprintf(Tag, sizeof(Tag), "RAWRND-%lu", DiskNumber);
            ReportPhase(Tag, 4096, (ULONGLONG)Ops << 12, Ops,
                        ElapsedSeconds(Start, End), GetTickCount() - Tick0);
        }
        CloseHandle(Disk);
    }
    Report("DISKBENCH RAWSCAN DONE\n");
    return 0;
}

int main(int argc, char* argv[])
{
    HANDLE File;
    PUCHAR Buffer;
    ULONGLONG FileSize;
    ULONG Index;

    if (argc < 2)
    {
        printf("Usage: diskbench <file> [size-MB] [random-ops] | diskbench -raw\n");
        return 1;
    }
    FileSize = (ULONGLONG)(argc >= 3 ? atoi(argv[2]) : DEFAULT_FILE_MB) * 1024 * 1024;
    if (FileSize == 0)
        FileSize = (ULONGLONG)DEFAULT_FILE_MB * 1024 * 1024;
    if (argc >= 4 && atoi(argv[3]) > 0)
        RandomOps = atoi(argv[3]);

    QueryPerformanceFrequency(&Frequency);

    Buffer = (PUCHAR)VirtualAlloc(NULL, 1024 * 1024, MEM_COMMIT, PAGE_READWRITE);
    if (!Buffer)
    {
        Report("DISKBENCH SETUP FAILED alloc\n");
        return 1;
    }
    for (Index = 0; Index < 1024 * 1024; Index++)
        Buffer[Index] = (UCHAR)(Index * 2654435761u >> 24);

    if (strcmp(argv[1], "-raw") == 0)
        return RawScan(Buffer);
    if (strcmp(argv[1], "-araw") == 0)
        return AsyncRawScan(argc >= 3 && atoi(argv[2]) > 0 ? atoi(argv[2]) : 32);

    File = CreateFileA(argv[1],
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       CREATE_ALWAYS,
                       FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
                       NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        DeleteFileA(argv[1]);
        File = CreateFileA(argv[1],
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           NULL,
                           CREATE_ALWAYS,
                           FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
                           NULL);
    }
    if (File == INVALID_HANDLE_VALUE)
    {
        Report("DISKBENCH SETUP FAILED open %lu\n", GetLastError());
        return 1;
    }

    /* Sequential write fills the file; everything after reuses it. */
    if (!RunSequential(File, TRUE, 1024 * 1024, FileSize, Buffer, "SEQWRITE") ||
        !RunSequential(File, FALSE, 1024 * 1024, FileSize, Buffer, "SEQREAD") ||
        !RunSequential(File, TRUE, 64 * 1024, FileSize, Buffer, "SEQWRITE") ||
        !RunSequential(File, FALSE, 64 * 1024, FileSize, Buffer, "SEQREAD") ||
        !RunRandom(File, FALSE, 4096, FileSize, Buffer, "RNDREAD") ||
        !RunRandom(File, TRUE, 4096, FileSize, Buffer, "RNDWRITE"))
    {
        CloseHandle(File);
        return 1;
    }

    CloseHandle(File);
    DeleteFileA(argv[1]);
    Report("DISKBENCH DONE\n");
    return 0;
}
