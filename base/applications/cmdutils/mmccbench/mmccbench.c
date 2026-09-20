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

typedef enum
{
    UNIT_OPS,
    UNIT_PAGES,
    UNIT_MBPS
} UNIT;

typedef struct
{
    const char *Name;
    UNIT Unit;
    ULONGLONG Work;
    ULONGLONG Ticks;
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

static void Measure(const char *Name, UNIT Unit, WORKER *Workers, WORKFN Work, ULONG StartIterations)
{
    ULONGLONG TotalWork = 0, Ticks = 0;
    ULONG Iterations = StartIterations;
    ULONG Status;
    ULONG Attempt;

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
    Results[ResultCount].Work = TotalWork;
    Results[ResultCount].Ticks = Ticks;
    Results[ResultCount].Status = Status;
    ResultCount++;
}

static void ReportRow(RESULT *Row)
{
    double Seconds;
    double Value;
    const char *Unit;

    if (Row->Status != 0)
    {
        printf("MMCC %-22s threads=%-2lu ERROR %lu\n", Row->Name, ThreadCount, Row->Status);
        return;
    }

    Seconds = (double)Row->Ticks / (double)Frequency.QuadPart;
    if (Seconds <= 0.0)
        Seconds = 1e-9;

    switch (Row->Unit)
    {
        case UNIT_MBPS:
            Value = ((double)Row->Work / (1024.0 * 1024.0)) / Seconds;
            Unit = "MB/s";
            break;
        case UNIT_PAGES:
            Value = (double)Row->Work / Seconds;
            Unit = "pages/s";
            break;
        default:
            Value = (double)Row->Work / Seconds;
            Unit = "ops/s";
            break;
    }
    printf("MMCC %-22s threads=%-2lu %14.1f %-8s (work=%I64u ms=%I64u)\n",
           Row->Name, ThreadCount, Value, Unit,
           Row->Work, TicksToMs(Row->Ticks));
}

int main(int argc, char *argv[])
{
    WORKER *Workers;
    SYSTEM_INFO SystemInfo;
    char TempPath[MAX_PATH];
    ULONG i, j;
    int Argument;

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
        else
        {
            printf("usage: mmccbench [-t threads] [-f fileMB] [-a arenaMB] [-ms targetMs]\n");
            return 1;
        }
    }

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

    Measure("mm_fault_demand_zero", UNIT_PAGES, Workers, MmFaultDemandZero, 1);
    Measure("mm_commit_decommit", UNIT_OPS, Workers, MmCommitDecommit, 64);
    Measure("mm_reserve_release", UNIT_OPS, Workers, MmReserveRelease, 64);

    for (i = 0; i < ThreadCount; i++)
    {
        if (!WorkerFileOpen(&Workers[i], TRUE))
        {
            printf("MMCC_ERROR file setup %lu\n", Workers[i].Status);
            return 1;
        }
    }

    Measure("mm_section_map_unmap", UNIT_OPS, Workers, MmSectionMapUnmap, 16);
    Measure("cc_write_cached", UNIT_MBPS, Workers, CcWriteCached, 1);
    Measure("cc_read_hot", UNIT_MBPS, Workers, CcReadHot, 1);
    Measure("cc_read_random_hot", UNIT_OPS, Workers, CcReadRandomHot, 256);
    Measure("cc_mapped_read", UNIT_PAGES, Workers, CcMappedRead, 1);

    for (i = 0; i < ResultCount; i++)
        ReportRow(&Results[i]);

    for (i = 0; i < ThreadCount; i++)
    {
        WorkerFileClose(&Workers[i]);
        if (Workers[i].Buffer != NULL)
            VirtualFree(Workers[i].Buffer, 0, MEM_RELEASE);
    }
    free(Workers);

    printf("MMCC_DONE sink=%lu\n", (unsigned long)Sink);
    return 0;
}
