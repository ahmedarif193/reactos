/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_THREADS 64
#define DEFAULT_ARENA_MB 32
#define DEFAULT_FILE_MB 16
#define DEFAULT_TARGET_MS 1000
#define RANDOM_SLOTS 4096
#define RANDOM_BLOCK 4096
#define GENERAL_FILE_MB 128
#define GENERAL_COPY_MB 32
#define GENERAL_WRITE_MB 64
#define GENERAL_ALLOC_MB 64
#define GENERAL_CHUNK (1024u * 1024u)
#define GENERAL_SMALL_FILES 64
#define GENERAL_SMALL_BYTES 4096u
#define GENERAL_MAP_WRITES 1024

typedef enum
{
    UNIT_OPS,
    UNIT_PAGES,
    UNIT_MBPS
} UNIT;

#define TRIALS 3

typedef struct
{
    const char *Name;
    UNIT Unit;
    double Rate[TRIALS];
    ULONG Trials;
    ULONG Status;
} RESULT;

typedef struct _WORKER WORKER;
typedef ULONGLONG (*WORKFN)(WORKER *Worker, ULONG Iterations);

struct _WORKER
{
    HANDLE Thread;
    ULONG Index;
    ULONG Iterations;
    WORKFN Work;
    ULONGLONG Done;
    ULONG Status;
    HANDLE Start;
    PUCHAR Arena;
    SIZE_T ArenaBytes;
    char Path[MAX_PATH];
    HANDLE File;
    PUCHAR Buffer;
    SIZE_T BufferBytes;
    ULONG Offsets[RANDOM_SLOTS];
    char BigPath[MAX_PATH];
    char CopyPath[MAX_PATH];
    char DirPath[MAX_PATH];
    HANDLE BigFile;
    PUCHAR Chunk;
    ULONG Seed;
};

static LARGE_INTEGER Frequency;
static ULONG ArenaBytes = DEFAULT_ARENA_MB * 1024u * 1024u;
static ULONG FileBytes = DEFAULT_FILE_MB * 1024u * 1024u;
static ULONG TargetMs = DEFAULT_TARGET_MS;
static ULONG ThreadCount = 1;
static ULONG PageSize = 4096;
static RESULT Results[16];
static ULONG ResultCount;
static volatile ULONG Sink;
static char SelfPath[MAX_PATH];
static ULONG GenFileMb = GENERAL_FILE_MB;
static ULONG GenCopyMb = GENERAL_COPY_MB;
static ULONG GenWriteMb = GENERAL_WRITE_MB;
static ULONG GenAllocMb = GENERAL_ALLOC_MB;
static BOOL Profile;

static ULONGLONG NowTicks(void)
{
    LARGE_INTEGER Counter;
    QueryPerformanceCounter(&Counter);
    return (ULONGLONG)Counter.QuadPart;
}

static ULONGLONG TicksToMs(ULONGLONG Ticks)
{
    return (Ticks * 1000ull) / (ULONGLONG)Frequency.QuadPart;
}

static ULONGLONG MmFaultDemandZero(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Pages = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        PUCHAR Base;
        SIZE_T Offset;

        Base = (PUCHAR)VirtualAlloc(NULL, Worker->ArenaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (Base == NULL)
        {
            Worker->Status = GetLastError();
            return Pages;
        }

        for (Offset = 0; Offset < Worker->ArenaBytes; Offset += PageSize)
        {
            Base[Offset] = (UCHAR)Offset;
        }
        Pages += Worker->ArenaBytes / PageSize;
        Sink += Base[0];
        VirtualFree(Base, 0, MEM_RELEASE);
    }
    return Pages;
}

static ULONGLONG MmCommitDecommit(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    ULONG i;
    PUCHAR Base;

    Base = (PUCHAR)VirtualAlloc(NULL, Worker->ArenaBytes, MEM_RESERVE, PAGE_NOACCESS);
    if (Base == NULL)
    {
        Worker->Status = GetLastError();
        return 0;
    }

    for (i = 0; i < Iterations; i++)
    {
        SIZE_T Offset = ((SIZE_T)i * 64u * 1024u) % Worker->ArenaBytes;
        PVOID Chunk;

        Chunk = VirtualAlloc(Base + Offset, 64u * 1024u, MEM_COMMIT, PAGE_READWRITE);
        if (Chunk == NULL)
        {
            Worker->Status = GetLastError();
            break;
        }
        *(volatile UCHAR *)Chunk = 1;
        if (!VirtualFree(Chunk, 64u * 1024u, MEM_DECOMMIT))
        {
            Worker->Status = GetLastError();
            break;
        }
        Ops++;
    }

    VirtualFree(Base, 0, MEM_RELEASE);
    return Ops;
}

static ULONGLONG MmReserveRelease(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        PVOID Base = VirtualAlloc(NULL, 1024u * 1024u, MEM_RESERVE, PAGE_NOACCESS);
        if (Base == NULL)
        {
            Worker->Status = GetLastError();
            break;
        }
        if (!VirtualFree(Base, 0, MEM_RELEASE))
        {
            Worker->Status = GetLastError();
            break;
        }
        Ops++;
    }
    return Ops;
}

static ULONGLONG MmSectionMapUnmap(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        HANDLE Mapping;
        PUCHAR View;

        Mapping = CreateFileMappingA(Worker->File, NULL, PAGE_READONLY, 0, 0, NULL);
        if (Mapping == NULL)
        {
            Worker->Status = GetLastError();
            break;
        }
        View = (PUCHAR)MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 64u * 1024u);
        if (View == NULL)
        {
            Worker->Status = GetLastError();
            CloseHandle(Mapping);
            break;
        }
        Sink += View[0];
        UnmapViewOfFile(View);
        CloseHandle(Mapping);
        Ops++;
    }
    return Ops;
}

static ULONGLONG CcWriteCached(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Bytes = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Zero;
        SIZE_T Offset;

        Zero.QuadPart = 0;
        if (!SetFilePointerEx(Worker->File, Zero, NULL, FILE_BEGIN))
        {
            Worker->Status = GetLastError();
            break;
        }
        for (Offset = 0; Offset < FileBytes; Offset += Worker->BufferBytes)
        {
            DWORD Written = 0;
            if (!WriteFile(Worker->File, Worker->Buffer, (DWORD)Worker->BufferBytes, &Written, NULL) ||
                Written != Worker->BufferBytes)
            {
                Worker->Status = GetLastError();
                return Bytes;
            }
            Bytes += Written;
        }
    }
    return Bytes;
}

static ULONGLONG CcReadHot(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Bytes = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Zero;
        SIZE_T Offset;

        Zero.QuadPart = 0;
        if (!SetFilePointerEx(Worker->File, Zero, NULL, FILE_BEGIN))
        {
            Worker->Status = GetLastError();
            break;
        }
        for (Offset = 0; Offset < FileBytes; Offset += Worker->BufferBytes)
        {
            DWORD Read = 0;
            if (!ReadFile(Worker->File, Worker->Buffer, (DWORD)Worker->BufferBytes, &Read, NULL) || Read == 0)
            {
                Worker->Status = GetLastError();
                return Bytes;
            }
            Bytes += Read;
        }
        Sink += Worker->Buffer[0];
    }
    return Bytes;
}

static ULONGLONG CcReadRandomHot(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Position;
        DWORD Read = 0;

        Position.QuadPart = Worker->Offsets[i % RANDOM_SLOTS];
        if (!SetFilePointerEx(Worker->File, Position, NULL, FILE_BEGIN))
        {
            Worker->Status = GetLastError();
            break;
        }
        if (!ReadFile(Worker->File, Worker->Buffer, RANDOM_BLOCK, &Read, NULL) || Read == 0)
        {
            Worker->Status = GetLastError();
            break;
        }
        Sink += Worker->Buffer[0];
        Ops++;
    }
    return Ops;
}

static ULONGLONG CcMappedRead(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Pages = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        HANDLE Mapping;
        PUCHAR View;
        SIZE_T Offset;

        Mapping = CreateFileMappingA(Worker->File, NULL, PAGE_READONLY, 0, 0, NULL);
        if (Mapping == NULL)
        {
            Worker->Status = GetLastError();
            break;
        }
        View = (PUCHAR)MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
        if (View == NULL)
        {
            Worker->Status = GetLastError();
            CloseHandle(Mapping);
            break;
        }
        for (Offset = 0; Offset < FileBytes; Offset += PageSize)
        {
            Sink += View[Offset];
            Pages++;
        }
        UnmapViewOfFile(View);
        CloseHandle(Mapping);
    }
    return Pages;
}

static void Fail(WORKER *Worker)
{
    DWORD Error = GetLastError();

    Worker->Status = Error ? Error : ERROR_GEN_FAILURE;
}

static ULONG NextRandom(WORKER *Worker)
{
    Worker->Seed = Worker->Seed * 1664525u + 1013904223u;
    return Worker->Seed >> 8;
}

static BOOL GenWriteChunks(WORKER *Worker, HANDLE File, ULONG Megabytes)
{
    ULONG i;

    for (i = 0; i < Megabytes; i++)
    {
        DWORD Written = 0;

        if (!WriteFile(File, Worker->Chunk, GENERAL_CHUNK, &Written, NULL) || Written != GENERAL_CHUNK)
        {
            Fail(Worker);
            return FALSE;
        }
    }
    return TRUE;
}

static ULONGLONG GenAllocFillFree(WORKER *Worker, ULONG Iterations)
{
    SIZE_T Bytes = (SIZE_T)GenAllocMb * GENERAL_CHUNK;
    ULONGLONG Done = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        PUCHAR Base = (PUCHAR)VirtualAlloc(NULL, Bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        if (Base == NULL)
        {
            Fail(Worker);
            break;
        }
        memset(Base, (int)i, Bytes);
        Sink += Base[Bytes - 1];
        VirtualFree(Base, 0, MEM_RELEASE);
        Done += Bytes;
    }
    return Done;
}

static ULONGLONG GenFileSeqWrite(WORKER *Worker, ULONG Iterations)
{
    char Path[MAX_PATH];
    ULONGLONG Done = 0;
    ULONG i;

    sprintf(Path, "%s.w", Worker->BigPath);
    for (i = 0; i < Iterations; i++)
    {
        HANDLE File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        BOOL Ok;

        if (File == INVALID_HANDLE_VALUE)
        {
            Fail(Worker);
            break;
        }
        Ok = GenWriteChunks(Worker, File, GenWriteMb);
        if (Ok && !FlushFileBuffers(File))
        {
            Fail(Worker);
            Ok = FALSE;
        }
        CloseHandle(File);
        DeleteFileA(Path);
        if (!Ok)
            break;
        Done += (ULONGLONG)GenWriteMb * GENERAL_CHUNK;
    }
    return Done;
}

static ULONGLONG GenFileSeqRead(WORKER *Worker, ULONG Iterations)
{
    ULONGLONG Done = 0;
    ULONG i, j;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Zero;

        Zero.QuadPart = 0;
        if (!SetFilePointerEx(Worker->BigFile, Zero, NULL, FILE_BEGIN))
        {
            Fail(Worker);
            break;
        }
        for (j = 0; j < GenFileMb; j++)
        {
            DWORD Read = 0;

            if (!ReadFile(Worker->BigFile, Worker->Chunk, GENERAL_CHUNK, &Read, NULL) || Read != GENERAL_CHUNK)
            {
                Fail(Worker);
                return Done;
            }
            Done += Read;
        }
        Sink += Worker->Chunk[0];
    }
    return Done;
}

static ULONGLONG GenFileRandomRead(WORKER *Worker, ULONG Iterations)
{
    ULONG Blocks = GenFileMb * (GENERAL_CHUNK / RANDOM_BLOCK);
    ULONGLONG Ops = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        LARGE_INTEGER Position;
        DWORD Read = 0;

        Position.QuadPart = (LONGLONG)(NextRandom(Worker) % Blocks) * RANDOM_BLOCK;
        if (!SetFilePointerEx(Worker->BigFile, Position, NULL, FILE_BEGIN) ||
            !ReadFile(Worker->BigFile, Worker->Chunk, RANDOM_BLOCK, &Read, NULL) || Read != RANDOM_BLOCK)
        {
            Fail(Worker);
            break;
        }
        Sink += Worker->Chunk[0];
        Ops++;
    }
    return Ops;
}

static ULONGLONG GenMapSeqRead(WORKER *Worker, ULONG Iterations)
{
    SIZE_T Bytes = (SIZE_T)GenFileMb * GENERAL_CHUNK;
    ULONGLONG Pages = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        HANDLE Mapping = CreateFileMappingA(Worker->BigFile, NULL, PAGE_READONLY, 0, 0, NULL);
        PUCHAR View;
        SIZE_T Offset;

        if (Mapping == NULL)
        {
            Fail(Worker);
            break;
        }
        View = (PUCHAR)MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
        if (View == NULL)
        {
            Fail(Worker);
            CloseHandle(Mapping);
            break;
        }
        for (Offset = 0; Offset < Bytes; Offset += PageSize)
            Sink += View[Offset];
        Pages += Bytes / PageSize;
        UnmapViewOfFile(View);
        CloseHandle(Mapping);
    }
    return Pages;
}

static ULONGLONG GenMapRandomWrite(WORKER *Worker, ULONG Iterations)
{
    ULONG Pages = (ULONG)(((SIZE_T)GenFileMb * GENERAL_CHUNK) / PageSize);
    ULONGLONG Done = 0;
    ULONG i, j;

    for (i = 0; i < Iterations; i++)
    {
        HANDLE Mapping = CreateFileMappingA(Worker->BigFile, NULL, PAGE_READWRITE, 0, 0, NULL);
        PUCHAR View;
        BOOL Ok;

        if (Mapping == NULL)
        {
            Fail(Worker);
            break;
        }
        View = (PUCHAR)MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, 0);
        if (View == NULL)
        {
            Fail(Worker);
            CloseHandle(Mapping);
            break;
        }
        for (j = 0; j < GENERAL_MAP_WRITES; j++)
            *(volatile ULONG *)(View + (SIZE_T)(NextRandom(Worker) % Pages) * PageSize) = i + j;
        Ok = FlushViewOfFile(View, 0);
        if (!Ok)
            Fail(Worker);
        UnmapViewOfFile(View);
        CloseHandle(Mapping);
        if (!Ok)
            break;
        Done += GENERAL_MAP_WRITES;
    }
    return Done;
}

static ULONGLONG GenSmallFiles(WORKER *Worker, ULONG Iterations)
{
    char Path[MAX_PATH];
    ULONGLONG Files = 0;
    ULONG i, j;

    for (i = 0; i < Iterations; i++)
    {
        for (j = 0; j < GENERAL_SMALL_FILES; j++)
        {
            HANDLE File;
            DWORD Written = 0;

            sprintf(Path, "%s\\f%lu.tmp", Worker->DirPath, (unsigned long)j);
            File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (File == INVALID_HANDLE_VALUE)
            {
                Fail(Worker);
                return Files;
            }
            if (!WriteFile(File, Worker->Chunk, GENERAL_SMALL_BYTES, &Written, NULL) ||
                Written != GENERAL_SMALL_BYTES)
            {
                Fail(Worker);
                CloseHandle(File);
                return Files;
            }
            CloseHandle(File);
        }
        for (j = 0; j < GENERAL_SMALL_FILES; j++)
        {
            sprintf(Path, "%s\\f%lu.tmp", Worker->DirPath, (unsigned long)j);
            if (!DeleteFileA(Path))
            {
                Fail(Worker);
                return Files;
            }
        }
        Files += GENERAL_SMALL_FILES;
    }
    return Files;
}

static ULONGLONG GenFileCopy(WORKER *Worker, ULONG Iterations)
{
    char Path[MAX_PATH];
    ULONGLONG Done = 0;
    ULONG i;

    sprintf(Path, "%s.copy", Worker->CopyPath);
    for (i = 0; i < Iterations; i++)
    {
        if (!CopyFileA(Worker->CopyPath, Path, FALSE))
        {
            Fail(Worker);
            break;
        }
        DeleteFileA(Path);
        Done += (ULONGLONG)GenCopyMb * GENERAL_CHUNK;
    }
    return Done;
}

static ULONGLONG GenProcessSpawn(WORKER *Worker, ULONG Iterations)
{
    char CommandLine[MAX_PATH + 16];
    ULONGLONG Done = 0;
    ULONG i;

    for (i = 0; i < Iterations; i++)
    {
        STARTUPINFOA StartupInfo;
        PROCESS_INFORMATION ProcessInfo;
        DWORD ExitCode = 1;

        sprintf(CommandLine, "\"%s\" -noop", SelfPath);
        ZeroMemory(&StartupInfo, sizeof(StartupInfo));
        StartupInfo.cb = sizeof(StartupInfo);
        if (!CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
        {
            Fail(Worker);
            break;
        }
        WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
        GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);
        if (ExitCode != 0)
        {
            Worker->Status = ERROR_GEN_FAILURE;
            break;
        }
        Done++;
    }
    return Done;
}

static DWORD WINAPI WorkerMain(LPVOID Context)
{
    WORKER *Worker = (WORKER *)Context;

    WaitForSingleObject(Worker->Start, INFINITE);
    Worker->Done = Worker->Work(Worker, Worker->Iterations);
    return 0;
}

static BOOL WorkerFileOpen(WORKER *Worker, BOOL Prefill)
{
    DWORD Written;
    SIZE_T Offset;

    Worker->File = CreateFileA(Worker->Path,
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    if (Worker->File == INVALID_HANDLE_VALUE)
    {
        Worker->Status = GetLastError();
        Worker->File = NULL;
        return FALSE;
    }

    if (!Prefill)
        return TRUE;

    for (Offset = 0; Offset < FileBytes; Offset += Worker->BufferBytes)
    {
        if (!WriteFile(Worker->File, Worker->Buffer, (DWORD)Worker->BufferBytes, &Written, NULL))
        {
            Worker->Status = GetLastError();
            return FALSE;
        }
    }
    FlushFileBuffers(Worker->File);
    return TRUE;
}

static void WorkerFileClose(WORKER *Worker)
{
    if (Worker->File != NULL)
    {
        CloseHandle(Worker->File);
        Worker->File = NULL;
    }
    DeleteFileA(Worker->Path);
}

static ULONG RunPass(WORKER *Workers, WORKFN Work, ULONG Iterations, ULONGLONG *TotalWork, ULONGLONG *Ticks)
{
    HANDLE Handles[MAX_THREADS];
    HANDLE Start;
    ULONGLONG Begin, End;
    ULONG i;
    ULONG Status = 0;

    Start = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Start == NULL)
        return GetLastError();

    for (i = 0; i < ThreadCount; i++)
    {
        Workers[i].Work = Work;
        Workers[i].Iterations = Iterations;
        Workers[i].Done = 0;
        Workers[i].Start = Start;
        Workers[i].Thread = CreateThread(NULL, 0, WorkerMain, &Workers[i], 0, NULL);
        if (Workers[i].Thread == NULL)
        {
            CloseHandle(Start);
            return GetLastError();
        }
        Handles[i] = Workers[i].Thread;
    }

    Begin = NowTicks();
    SetEvent(Start);
    WaitForMultipleObjects(ThreadCount, Handles, TRUE, INFINITE);
    End = NowTicks();

    *TotalWork = 0;
    for (i = 0; i < ThreadCount; i++)
    {
        *TotalWork += Workers[i].Done;
        if (Workers[i].Status != 0 && Status == 0)
            Status = Workers[i].Status;
        CloseHandle(Workers[i].Thread);
        Workers[i].Thread = NULL;
    }
    *Ticks = End - Begin;
    CloseHandle(Start);
    return Status;
}

static void ReportRow(RESULT *Row);

static int CompareDouble(const void *A, const void *B)
{
    double L = *(const double *)A, R = *(const double *)B;
    return (L > R) - (L < R);
}

static double RateOf(UNIT Unit, ULONGLONG Work, ULONGLONG Ticks)
{
    double Seconds = (double)Ticks / (double)Frequency.QuadPart;

    if (Seconds <= 0.0)
        Seconds = 1e-9;
    if (Unit == UNIT_MBPS)
        return ((double)Work / (1024.0 * 1024.0)) / Seconds;
    return (double)Work / Seconds;
}

static void Measure(const char *Name, UNIT Unit, WORKER *Workers, WORKFN Work, ULONG StartIterations)
{
    ULONGLONG TotalWork = 0, Ticks = 0;
    ULONG Iterations = StartIterations;
    ULONG Status;
    ULONG Attempt, Trial;

    Status = RunPass(Workers, Work, 1, &TotalWork, &Ticks);

    for (Attempt = 0; Attempt < 12 && Status == 0; Attempt++)
    {
        Status = RunPass(Workers, Work, Iterations, &TotalWork, &Ticks);
        if (Status != 0)
            break;
        if (TicksToMs(Ticks) >= TargetMs)
            break;
        if (TicksToMs(Ticks) < 20)
            Iterations *= 8;
        else
            Iterations = (ULONG)((ULONGLONG)Iterations * TargetMs / (TicksToMs(Ticks) + 1) + 1);
    }

    Results[ResultCount].Name = Name;
    Results[ResultCount].Unit = Unit;
    Results[ResultCount].Trials = 0;
    Results[ResultCount].Status = Status;

    if (Profile)
    {
        char Message[128];

        sprintf(Message, "MMCC_PROFILE %s threads=%lu\n", Name, ThreadCount);
        OutputDebugStringA(Message);
    }

    for (Trial = 0; Trial < TRIALS && Status == 0; Trial++)
    {
        Status = RunPass(Workers, Work, Iterations, &TotalWork, &Ticks);
        if (Status != 0)
        {
            Results[ResultCount].Status = Status;
            break;
        }
        Results[ResultCount].Rate[Results[ResultCount].Trials++] = RateOf(Unit, TotalWork, Ticks);
    }

    if (Results[ResultCount].Trials > 1)
    {
        qsort(Results[ResultCount].Rate, Results[ResultCount].Trials,
              sizeof(double), CompareDouble);
    }
    ReportRow(&Results[ResultCount]);
    fflush(stdout);
    ResultCount++;
}

static void ReportRow(RESULT *Row)
{
    const char *Unit;
    double Median, Low, High, Spread;

    if (Row->Status != 0 || Row->Trials == 0)
    {
        printf("MMCC %-22s threads=%-2lu ERROR %lu\n", Row->Name, ThreadCount, Row->Status);
        return;
    }

    switch (Row->Unit)
    {
        case UNIT_MBPS:  Unit = "MB/s"; break;
        case UNIT_PAGES: Unit = "pages/s"; break;
        default:         Unit = "ops/s"; break;
    }

    Median = Row->Rate[Row->Trials / 2];
    Low = Row->Rate[0];
    High = Row->Rate[Row->Trials - 1];
    Spread = (Low > 0.0) ? (High / Low) : 0.0;

    printf("MMCC %-22s threads=%-2lu %14.1f %-8s (n=%lu min=%.1f max=%.1f spread=%.2fx)\n",
           Row->Name, ThreadCount, Median, Unit,
           (unsigned long)Row->Trials, Low, High, Spread);
}

static BOOL GeneralOpen(WORKER *Worker, const char *TempPath)
{
    HANDLE File;

    Worker->Seed = 0x9E3779B9u * (Worker->Index + 1);
    Worker->Chunk = (PUCHAR)VirtualAlloc(NULL, GENERAL_CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Worker->Chunk == NULL)
    {
        Fail(Worker);
        return FALSE;
    }
    memset(Worker->Chunk, (int)(Worker->Index + 1), GENERAL_CHUNK);

    sprintf(Worker->BigPath, "%smmccg%lu.dat", TempPath, (unsigned long)Worker->Index);
    sprintf(Worker->CopyPath, "%smmccc%lu.dat", TempPath, (unsigned long)Worker->Index);
    sprintf(Worker->DirPath, "%smmccd%lu", TempPath, (unsigned long)Worker->Index);
    if (!CreateDirectoryA(Worker->DirPath, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        Fail(Worker);
        return FALSE;
    }

    File = CreateFileA(Worker->CopyPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        Fail(Worker);
        return FALSE;
    }
    if (!GenWriteChunks(Worker, File, GenCopyMb))
    {
        CloseHandle(File);
        return FALSE;
    }
    CloseHandle(File);

    Worker->BigFile = CreateFileA(Worker->BigPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Worker->BigFile == INVALID_HANDLE_VALUE)
    {
        Worker->BigFile = NULL;
        Fail(Worker);
        return FALSE;
    }
    if (!GenWriteChunks(Worker, Worker->BigFile, GenFileMb))
        return FALSE;
    FlushFileBuffers(Worker->BigFile);
    return TRUE;
}

static void GeneralClose(WORKER *Worker)
{
    char Path[MAX_PATH];
    ULONG j;

    if (Worker->BigFile != NULL)
    {
        CloseHandle(Worker->BigFile);
        Worker->BigFile = NULL;
    }
    if (Worker->DirPath[0] != '\0')
    {
        DeleteFileA(Worker->BigPath);
        DeleteFileA(Worker->CopyPath);
        for (j = 0; j < GENERAL_SMALL_FILES; j++)
        {
            sprintf(Path, "%s\\f%lu.tmp", Worker->DirPath, (unsigned long)j);
            DeleteFileA(Path);
        }
        RemoveDirectoryA(Worker->DirPath);
    }
    if (Worker->Chunk != NULL)
    {
        VirtualFree(Worker->Chunk, 0, MEM_RELEASE);
        Worker->Chunk = NULL;
    }
}

static const struct
{
    const char *Name;
    UNIT Unit;
    WORKFN Work;
    ULONG Start;
} GeneralTests[] =
{
    { "gen_alloc_fill_free", UNIT_MBPS, GenAllocFillFree, 1 },
    { "gen_file_seq_write", UNIT_MBPS, GenFileSeqWrite, 1 },
    { "gen_file_seq_read", UNIT_MBPS, GenFileSeqRead, 1 },
    { "gen_file_rand_read", UNIT_OPS, GenFileRandomRead, 256 },
    { "gen_map_seq_read", UNIT_PAGES, GenMapSeqRead, 1 },
    { "gen_map_rand_write", UNIT_PAGES, GenMapRandomWrite, 1 },
    { "gen_small_files", UNIT_OPS, GenSmallFiles, 1 },
    { "gen_file_copy", UNIT_MBPS, GenFileCopy, 1 },
    { "gen_process_spawn", UNIT_OPS, GenProcessSpawn, 1 },
};

static ULONG GeneralShare(ULONG Megabytes)
{
    ULONG Share = Megabytes / ThreadCount;

    return Share ? Share : 1;
}

static BOOL GenCheckFile(WORKER *Worker, const char *Path, ULONG Blocks, BOOL Distinct)
{
    HANDLE File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER Size;
    ULONG b, k;

    if (File == INVALID_HANDLE_VALUE)
    {
        printf("MMCC_VERIFY FAIL open %s %lu\n", Path, GetLastError());
        return FALSE;
    }
    if (!GetFileSizeEx(File, &Size) || Size.QuadPart != (LONGLONG)Blocks * Worker->BufferBytes)
    {
        printf("MMCC_VERIFY FAIL size %s %I64d\n", Path, Size.QuadPart);
        CloseHandle(File);
        return FALSE;
    }
    for (b = 0; b < Blocks; b++)
    {
        UCHAR Expect = Distinct ? (UCHAR)(b * 7 + 1) : (UCHAR)(Worker->Index + 1);
        DWORD Read = 0;

        if (!ReadFile(File, Worker->Buffer, (DWORD)Worker->BufferBytes, &Read, NULL) || Read != Worker->BufferBytes)
        {
            printf("MMCC_VERIFY FAIL read %s block %lu %lu\n", Path, b, GetLastError());
            CloseHandle(File);
            return FALSE;
        }
        for (k = 0; k < Worker->BufferBytes; k++)
        {
            if (Worker->Buffer[k] != Expect)
            {
                printf("MMCC_VERIFY FAIL data %s offset %lu got %u want %u\n", Path,
                       (unsigned long)(b * Worker->BufferBytes + k), Worker->Buffer[k], Expect);
                CloseHandle(File);
                return FALSE;
            }
        }
    }
    CloseHandle(File);
    return TRUE;
}

static BOOL GenVerifyRename(WORKER *Worker)
{
    char OldPath[MAX_PATH];
    char NewPath[MAX_PATH];
    HANDLE First = INVALID_HANDLE_VALUE;
    HANDLE Second = INVALID_HANDLE_VALUE;
    HANDLE Renamed = INVALID_HANDLE_VALUE;
    DWORD Sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    DWORD Written;
    LARGE_INTEGER Position;
    const char *Stage = "create";
    BOOL Ok = FALSE;

    sprintf(OldPath, "%s.rename-old", Worker->BigPath);
    sprintf(NewPath, "%s.rename-new", Worker->BigPath);
    First = CreateFileA(OldPath, GENERIC_READ | GENERIC_WRITE, Sharing, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (First == INVALID_HANDLE_VALUE)
        goto Done;
    Stage = "initial write";
    memset(Worker->Buffer, 1, Worker->BufferBytes);
    if (!WriteFile(First, Worker->Buffer, (DWORD)Worker->BufferBytes, &Written, NULL) ||
        Written != Worker->BufferBytes)
        goto Done;
    Stage = "second open";
    Second = CreateFileA(OldPath, GENERIC_READ | GENERIC_WRITE, Sharing, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Second == INVALID_HANDLE_VALUE)
        goto Done;
    Stage = "pending size";
    if (!GetFileSizeEx(Second, &Position) || Position.QuadPart != Worker->BufferBytes)
        goto Done;
    Stage = "rename";
    if (!MoveFileExA(OldPath, NewPath, MOVEFILE_REPLACE_EXISTING))
        goto Done;
    Stage = "renamed open";
    Renamed = CreateFileA(NewPath, GENERIC_READ | GENERIC_WRITE, Sharing, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Renamed == INVALID_HANDLE_VALUE)
        goto Done;
    Stage = "old handle extension";
    Position.QuadPart = Worker->BufferBytes;
    memset(Worker->Buffer, 8, Worker->BufferBytes);
    if (!SetFilePointerEx(Second, Position, NULL, FILE_BEGIN) ||
        !WriteFile(Second, Worker->Buffer, (DWORD)Worker->BufferBytes, &Written, NULL) ||
        Written != Worker->BufferBytes)
        goto Done;
    CloseHandle(First);
    First = INVALID_HANDLE_VALUE;
    CloseHandle(Second);
    Second = INVALID_HANDLE_VALUE;
    Stage = "flush through renamed handle";
    if (!FlushFileBuffers(Renamed))
        goto Done;
    Stage = "shared final size";
    if (!GetFileSizeEx(Renamed, &Position) || Position.QuadPart != 2 * Worker->BufferBytes)
        goto Done;
    CloseHandle(Renamed);
    Renamed = INVALID_HANDLE_VALUE;
    Stage = "renamed data";
    Ok = GenCheckFile(Worker, NewPath, 2, TRUE);

Done:
    if (!Ok)
        printf("MMCC_VERIFY FAIL rename %s %lu\n", Stage, GetLastError());
    if (First != INVALID_HANDLE_VALUE)
        CloseHandle(First);
    if (Second != INVALID_HANDLE_VALUE)
        CloseHandle(Second);
    if (Renamed != INVALID_HANDLE_VALUE)
        CloseHandle(Renamed);
    DeleteFileA(OldPath);
    DeleteFileA(NewPath);
    return Ok;
}

static BOOL GeneralVerify(WORKER *Worker)
{
    char Path[MAX_PATH];
    ULONG Blocks = 256;
    ULONG b;
    HANDLE File;
    BOOL Ok;

    sprintf(Path, "%s.v", Worker->BigPath);
    File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        printf("MMCC_VERIFY FAIL create %s %lu\n", Path, GetLastError());
        return FALSE;
    }
    for (b = 0; b < Blocks; b++)
    {
        DWORD Written = 0;

        memset(Worker->Buffer, (int)(UCHAR)(b * 7 + 1), Worker->BufferBytes);
        if (!WriteFile(File, Worker->Buffer, (DWORD)Worker->BufferBytes, &Written, NULL) ||
            Written != Worker->BufferBytes)
        {
            printf("MMCC_VERIFY FAIL write %s block %lu %lu\n", Path, b, GetLastError());
            CloseHandle(File);
            return FALSE;
        }
    }
    CloseHandle(File);
    Ok = GenCheckFile(Worker, Path, Blocks, TRUE);
    DeleteFileA(Path);
    if (!Ok)
        return FALSE;

    sprintf(Path, "%s.vcopy", Worker->CopyPath);
    if (!CopyFileA(Worker->CopyPath, Path, FALSE))
    {
        printf("MMCC_VERIFY FAIL copy %s %lu\n", Path, GetLastError());
        return FALSE;
    }
    Ok = GenCheckFile(Worker, Path, (ULONG)(((ULONGLONG)GenCopyMb * GENERAL_CHUNK) / Worker->BufferBytes), FALSE);
    DeleteFileA(Path);
    if (!Ok || !GenVerifyRename(Worker))
        return FALSE;
    {
        WORKER Small = *Worker;

        Small.BufferBytes = 4096;
        return GenVerifyRename(&Small);
    }
}

static int RunGeneralSuite(WORKER *Workers, const char *TempPath, const char *Only)
{
    ULONG i;
    int Result = 0;

    GenFileMb = GeneralShare(GENERAL_FILE_MB);
    GenCopyMb = GeneralShare(GENERAL_COPY_MB);
    GenWriteMb = GeneralShare(GENERAL_WRITE_MB);
    GenAllocMb = GeneralShare(GENERAL_ALLOC_MB);

    for (i = 0; i < ThreadCount; i++)
    {
        if (!GeneralOpen(&Workers[i], TempPath))
        {
            printf("MMCC_ERROR general setup %lu\n", Workers[i].Status);
            Result = 1;
            break;
        }
    }

    if (Result == 0)
    {
        for (i = 0; i < sizeof(GeneralTests) / sizeof(GeneralTests[0]); i++)
        {
            if (Only == NULL || strstr(GeneralTests[i].Name, Only) != NULL)
                Measure(GeneralTests[i].Name, GeneralTests[i].Unit, Workers, GeneralTests[i].Work, GeneralTests[i].Start);
        }
    }

    if (Result == 0)
    {
        for (i = 0; i < ThreadCount; i++)
        {
            if (!GeneralVerify(&Workers[i]))
            {
                Result = 1;
                break;
            }
        }
        printf("MMCC_VERIFY %s\n", Result == 0 ? "ok" : "failed");
    }

    for (i = 0; i < ThreadCount; i++)
        GeneralClose(&Workers[i]);
    return Result;
}

static int RunClassicSuite(WORKER *Workers, const char *Only)
{
    ULONG i;

    if (Only == NULL || strcmp(Only, "mm") == 0)
        Measure("mm_fault_demand_zero", UNIT_PAGES, Workers, MmFaultDemandZero, 1);
    if (Only == NULL || strcmp(Only, "mm") == 0 || strcmp(Only, "commit") == 0)
        Measure("mm_commit_decommit", UNIT_OPS, Workers, MmCommitDecommit, 64);
    if (Only == NULL || strcmp(Only, "mm") == 0)
        Measure("mm_reserve_release", UNIT_OPS, Workers, MmReserveRelease, 64);

    for (i = 0; i < ThreadCount; i++)
    {
        if (!WorkerFileOpen(&Workers[i], TRUE))
        {
            printf("MMCC_ERROR file setup %lu\n", Workers[i].Status);
            return 1;
        }
    }

    if (Only == NULL || strcmp(Only, "mm") == 0)
        Measure("mm_section_map_unmap", UNIT_OPS, Workers, MmSectionMapUnmap, 16);
    if (Only == NULL || strcmp(Only, "cc") == 0)
    {
        Measure("cc_write_cached", UNIT_MBPS, Workers, CcWriteCached, 1);
        Measure("cc_read_hot", UNIT_MBPS, Workers, CcReadHot, 1);
        Measure("cc_read_random_hot", UNIT_OPS, Workers, CcReadRandomHot, 256);
        Measure("cc_mapped_read", UNIT_PAGES, Workers, CcMappedRead, 1);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    WORKER *Workers;
    SYSTEM_INFO SystemInfo;
    char TempPath[MAX_PATH];
    ULONG i, j;
    int Argument;
    int Result;
    const char *Only = NULL;
    const char *Suite = NULL;

    if (argc > 1 && strcmp(argv[1], "-noop") == 0)
        return 0;

    for (Argument = 1; Argument < argc; Argument++)
    {
        if (strcmp(argv[Argument], "-t") == 0 && Argument + 1 < argc)
            ThreadCount = strtoul(argv[++Argument], NULL, 0);
        else if (strcmp(argv[Argument], "-f") == 0 && Argument + 1 < argc)
            FileBytes = strtoul(argv[++Argument], NULL, 0) * 1024u * 1024u;
        else if (strcmp(argv[Argument], "-a") == 0 && Argument + 1 < argc)
            ArenaBytes = strtoul(argv[++Argument], NULL, 0) * 1024u * 1024u;
        else if (strcmp(argv[Argument], "-ms") == 0 && Argument + 1 < argc)
            TargetMs = strtoul(argv[++Argument], NULL, 0);
        else if (strcmp(argv[Argument], "-only") == 0 && Argument + 1 < argc)
            Only = argv[++Argument];
        else if (strcmp(argv[Argument], "-suite") == 0 && Argument + 1 < argc)
            Suite = argv[++Argument];
        else if (strcmp(argv[Argument], "-profile") == 0)
            Profile = TRUE;
        else
        {
            printf("usage: mmccbench [-t threads] [-f fileMB] [-a arenaMB] [-ms targetMs] [-only cc|mm|commit] "
                   "[-suite general] [-profile]\n");
            return 1;
        }
    }

    if (Suite == NULL && Only != NULL && strcmp(Only, "cc") != 0 && strcmp(Only, "mm") != 0 && strcmp(Only, "commit") != 0)
    {
        printf("MMCC_ERROR invalid benchmark group %s\n", Only);
        return 1;
    }

    if (Suite != NULL && strcmp(Suite, "general") != 0)
    {
        printf("MMCC_ERROR invalid suite %s\n", Suite);
        return 1;
    }

    if (GetModuleFileNameA(NULL, SelfPath, sizeof(SelfPath)) == 0)
        SelfPath[0] = '\0';

    if (ThreadCount == 0 || ThreadCount > MAX_THREADS)
        ThreadCount = 1;

    GetSystemInfo(&SystemInfo);
    PageSize = SystemInfo.dwPageSize ? SystemInfo.dwPageSize : 4096;
    QueryPerformanceFrequency(&Frequency);
    if (Frequency.QuadPart == 0)
    {
        printf("MMCC_ERROR no performance counter\n");
        return 1;
    }

    if (GetTempPathA(sizeof(TempPath), TempPath) == 0)
        strcpy(TempPath, ".\\");

    Workers = (WORKER *)calloc(ThreadCount, sizeof(WORKER));
    if (Workers == NULL)
    {
        printf("MMCC_ERROR out of memory\n");
        return 1;
    }

    for (i = 0; i < ThreadCount; i++)
    {
        Workers[i].Index = i;
        Workers[i].ArenaBytes = ArenaBytes / ThreadCount;
        if (Workers[i].ArenaBytes < 1024u * 1024u)
            Workers[i].ArenaBytes = 1024u * 1024u;
        Workers[i].BufferBytes = 64u * 1024u;
        Workers[i].Buffer = (PUCHAR)VirtualAlloc(NULL, Workers[i].BufferBytes,
                                                 MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (Workers[i].Buffer == NULL)
        {
            printf("MMCC_ERROR buffer alloc %lu\n", GetLastError());
            return 1;
        }
        memset(Workers[i].Buffer, (int)(i + 1), Workers[i].BufferBytes);
        sprintf(Workers[i].Path, "%smmcc%lu.tmp", TempPath, (unsigned long)i);
        for (j = 0; j < RANDOM_SLOTS; j++)
        {
            Workers[i].Offsets[j] = (ULONG)(((j * 2654435761u) + i) % (FileBytes / RANDOM_BLOCK)) * RANDOM_BLOCK;
        }
    }

    printf("MMCC_BEGIN cpus=%lu threads=%lu page=%lu arenaMB=%lu fileMB=%lu targetMs=%lu\n",
           (unsigned long)SystemInfo.dwNumberOfProcessors, (unsigned long)ThreadCount,
           (unsigned long)PageSize, (unsigned long)(ArenaBytes / 1024u / 1024u),
           (unsigned long)(FileBytes / 1024u / 1024u), (unsigned long)TargetMs);

    if (Only != NULL)
        printf("MMCC_FILTER group=%s\n", Only);
    if (Suite != NULL)
        printf("MMCC_SUITE %s\n", Suite);

    Result = (Suite != NULL) ? RunGeneralSuite(Workers, TempPath, Only) : RunClassicSuite(Workers, Only);

    for (i = 0; i < ThreadCount; i++)
    {
        WorkerFileClose(&Workers[i]);
        if (Workers[i].Buffer != NULL)
            VirtualFree(Workers[i].Buffer, 0, MEM_RELEASE);
    }
    free(Workers);

    printf("MMCC_DONE sink=%lu\n", (unsigned long)Sink);
    return Result;
}
