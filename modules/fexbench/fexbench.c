/*
 * fexbench -- CPU / RAM / storage speed, built for ARM64 and x86-64 from the
 * same source so a FEX-emulated run can be compared against a native one.
 *
 * Deliberately plain Win32 and plain C: no intrinsics, no threads, no CRT
 * tricks, so the two binaries differ only in the instruction set the compiler
 * picked.  Build both with modules/fexbench/build-fexbench.sh.
 *
 * Every phase calibrates itself to run for a target wall time before it is
 * measured.  A fixed iteration count is worthless here: the first run of this
 * benchmark finished each phase in one to eighty milliseconds against a 24 MHz
 * counter, so the reported rates were mostly timer granularity.
 *
 * Storage is measured twice on purpose.  The buffered pass is what an
 * application's loading path actually sees, cache and all; the unbuffered pass
 * bypasses the cache and is the only one that says anything about the device.
 * Reporting only the first would have called a 611000 IOPS cache hit "storage".
 *
 * Output goes to the debugger as well as stdout, so it reaches the serial log
 * without passing through a pipe whose block buffering would hide it.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__aarch64__) || defined(_M_ARM64)
#define FEXBENCH_ARCH "arm64"
#else
#define FEXBENCH_ARCH "x86_64"
#endif

#define FEXBENCH_TARGET_SECONDS 1.0
#define FEXBENCH_MEM_BYTES      (64u * 1024u * 1024u)
#define FEXBENCH_FILE_BYTES     (64u * 1024u * 1024u)
#define FEXBENCH_BIG_BLOCK      (1024u * 1024u)
#define FEXBENCH_SMALL_BLOCK    4096u

static double g_TicksPerSecond;

static void BenchPrint(const char *Format, ...)
{
    char Buffer[512];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    va_end(Arguments);

    Buffer[sizeof(Buffer) - 1] = '\0';
    OutputDebugStringA(Buffer);
    fputs(Buffer, stdout);
    fflush(stdout);
}

static double SecondsSince(LARGE_INTEGER Start)
{
    LARGE_INTEGER Now;
    QueryPerformanceCounter(&Now);
    return (double)(Now.QuadPart - Start.QuadPart) / g_TicksPerSecond;
}

/* ---- CPU ---- */

static unsigned long long CpuIntegerChain(unsigned long long Iterations)
{
    unsigned long long Accumulator = 1;
    unsigned long long Index;

    for (Index = 0; Index < Iterations; ++Index)
    {
        Accumulator = Accumulator * 6364136223846793005ULL + 1442695040888963407ULL;
        Accumulator ^= Accumulator >> 33;
    }
    return Accumulator;
}

static unsigned long long CpuBranchChain(unsigned long long Iterations)
{
    unsigned long long State = 12345;
    unsigned long long Taken = 0;
    unsigned long long Index;

    for (Index = 0; Index < Iterations; ++Index)
    {
        State = State * 1103515245ULL + 12345ULL;
        if ((State >> 16) & 1)
            Taken += 3;
        else
            Taken ^= 7;
    }
    return Taken;
}

static void BenchCpu(const char *Name, unsigned long long (*Work)(unsigned long long))
{
    unsigned long long Iterations = 1000000ULL;
    unsigned long long Checksum = 0;
    LARGE_INTEGER Start;
    double Seconds = 0.0;
    int Attempt;

    for (Attempt = 0; Attempt < 24; ++Attempt)
    {
        QueryPerformanceCounter(&Start);
        Checksum = Work(Iterations);
        Seconds = SecondsSince(Start);

        if (Seconds >= FEXBENCH_TARGET_SECONDS)
            break;

        if (Seconds < 0.002)
            Iterations *= 8ULL;
        else
            Iterations = (unsigned long long)((double)Iterations * (FEXBENCH_TARGET_SECONDS / Seconds) * 1.2) + 1ULL;
    }

    BenchPrint("FEXBENCH %s %-13s %10.2f Mops/s  (%.3fs, %llu iters, checksum %08lx)\n",
               FEXBENCH_ARCH, Name,
               (Seconds > 0.0) ? ((double)Iterations / Seconds) / 1e6 : 0.0,
               Seconds,
               (unsigned long long)Iterations,
               (unsigned long)(Checksum & 0xFFFFFFFFULL));
}

/* ---- RAM ---- */

static void BenchMemory(void)
{
    unsigned char *Source;
    unsigned char *Destination;
    LARGE_INTEGER Start;
    double Seconds;
    unsigned long long Passes;
    unsigned long long Sum = 0;
    SIZE_T Offset;

    Source = (unsigned char *)VirtualAlloc(NULL, FEXBENCH_MEM_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Destination = (unsigned char *)VirtualAlloc(NULL, FEXBENCH_MEM_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Source == NULL || Destination == NULL)
    {
        BenchPrint("FEXBENCH %s mem           unavailable (VirtualAlloc failed %lu)\n",
                   FEXBENCH_ARCH, (unsigned long)GetLastError());
        if (Source != NULL) VirtualFree(Source, 0, MEM_RELEASE);
        if (Destination != NULL) VirtualFree(Destination, 0, MEM_RELEASE);
        return;
    }

    memset(Source, 0x5A, FEXBENCH_MEM_BYTES);
    memset(Destination, 0, FEXBENCH_MEM_BYTES);

    QueryPerformanceCounter(&Start);
    Passes = 0;
    do
    {
        memset(Destination, (int)(0xA5 + Passes), FEXBENCH_MEM_BYTES);
        ++Passes;
        Seconds = SecondsSince(Start);
    } while (Seconds < FEXBENCH_TARGET_SECONDS);
    BenchPrint("FEXBENCH %s mem_write     %10.1f MB/s  (%.3fs, %llu passes)\n", FEXBENCH_ARCH,
               (Seconds > 0.0) ? ((double)(Passes * FEXBENCH_MEM_BYTES) / Seconds) / 1048576.0 : 0.0,
               Seconds, (unsigned long long)Passes);

    QueryPerformanceCounter(&Start);
    Passes = 0;
    do
    {
        for (Offset = 0; Offset < FEXBENCH_MEM_BYTES; Offset += 64)
            Sum += Source[Offset];
        ++Passes;
        Seconds = SecondsSince(Start);
    } while (Seconds < FEXBENCH_TARGET_SECONDS);
    BenchPrint("FEXBENCH %s mem_read      %10.1f MB/s  (%.3fs, %llu passes, checksum %08lx)\n", FEXBENCH_ARCH,
               (Seconds > 0.0) ? ((double)(Passes * FEXBENCH_MEM_BYTES) / Seconds) / 1048576.0 : 0.0,
               Seconds, (unsigned long long)Passes, (unsigned long)(Sum & 0xFFFFFFFFULL));

    QueryPerformanceCounter(&Start);
    Passes = 0;
    do
    {
        memcpy(Destination, Source, FEXBENCH_MEM_BYTES);
        ++Passes;
        Seconds = SecondsSince(Start);
    } while (Seconds < FEXBENCH_TARGET_SECONDS);
    BenchPrint("FEXBENCH %s mem_copy      %10.1f MB/s  (%.3fs, %llu passes)\n", FEXBENCH_ARCH,
               (Seconds > 0.0) ? ((double)(Passes * FEXBENCH_MEM_BYTES) / Seconds) / 1048576.0 : 0.0,
               Seconds, (unsigned long long)Passes);

    VirtualFree(Source, 0, MEM_RELEASE);
    VirtualFree(Destination, 0, MEM_RELEASE);
}

/* ---- Storage ---- */

static void BenchStoragePass(const char *Path, unsigned char *Buffer, BOOL Unbuffered)
{
    const char *Label = Unbuffered ? "uncached" : "cached";
    DWORD Flags = FILE_ATTRIBUTE_NORMAL;
    HANDLE File;
    LARGE_INTEGER Start;
    LARGE_INTEGER Seek;
    DWORD Done;
    DWORD Offset;
    double Seconds;
    unsigned long long State = 99991;
    unsigned long Operations;

    if (Unbuffered)
        Flags |= FILE_FLAG_NO_BUFFERING;

    File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, Flags, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        BenchPrint("FEXBENCH %s io_read_%-5s unavailable (CreateFile failed %lu)\n",
                   FEXBENCH_ARCH, Label, (unsigned long)GetLastError());
        return;
    }

    QueryPerformanceCounter(&Start);
    for (Offset = 0; Offset < FEXBENCH_FILE_BYTES; Offset += FEXBENCH_BIG_BLOCK)
    {
        if (!ReadFile(File, Buffer, FEXBENCH_BIG_BLOCK, &Done, NULL) || Done != FEXBENCH_BIG_BLOCK)
            break;
    }
    Seconds = SecondsSince(Start);
    BenchPrint("FEXBENCH %s io_seq_%-6s %10.1f MB/s  (%.3fs, %lu MB)\n", FEXBENCH_ARCH, Label,
               (Seconds > 0.0) ? ((double)Offset / Seconds) / 1048576.0 : 0.0, Seconds,
               (unsigned long)(Offset / 1048576u));

    QueryPerformanceCounter(&Start);
    for (Operations = 0; Operations < 8192; ++Operations)
    {
        State = State * 6364136223846793005ULL + 1442695040888963407ULL;
        Seek.QuadPart = (LONGLONG)((State >> 20) % (FEXBENCH_FILE_BYTES - FEXBENCH_SMALL_BLOCK));
        Seek.QuadPart &= ~(LONGLONG)(FEXBENCH_SMALL_BLOCK - 1);
        if (!SetFilePointerEx(File, Seek, NULL, FILE_BEGIN))
            break;
        if (!ReadFile(File, Buffer, FEXBENCH_SMALL_BLOCK, &Done, NULL) || Done != FEXBENCH_SMALL_BLOCK)
            break;
    }
    Seconds = SecondsSince(Start);
    BenchPrint("FEXBENCH %s io_rand_%-5s %10.0f iops  (%.3fs, %lu x %luB)\n", FEXBENCH_ARCH, Label,
               (Seconds > 0.0) ? ((double)Operations / Seconds) : 0.0, Seconds,
               (unsigned long)Operations, (unsigned long)FEXBENCH_SMALL_BLOCK);

    CloseHandle(File);
}

static void BenchStorage(const char *Directory)
{
    char Path[MAX_PATH];
    unsigned char *Buffer;
    HANDLE File;
    LARGE_INTEGER Start;
    DWORD Done;
    DWORD Offset;
    double Seconds;

    _snprintf(Path, sizeof(Path) - 1, "%s\\fexbench.tmp", Directory);
    Path[sizeof(Path) - 1] = '\0';

    Buffer = (unsigned char *)VirtualAlloc(NULL, FEXBENCH_BIG_BLOCK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Buffer == NULL)
    {
        BenchPrint("FEXBENCH %s io            unavailable (VirtualAlloc failed)\n", FEXBENCH_ARCH);
        return;
    }
    memset(Buffer, 0x33, FEXBENCH_BIG_BLOCK);

    File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        BenchPrint("FEXBENCH %s io            unavailable (CreateFile %s failed %lu)\n",
                   FEXBENCH_ARCH, Path, (unsigned long)GetLastError());
        VirtualFree(Buffer, 0, MEM_RELEASE);
        return;
    }

    QueryPerformanceCounter(&Start);
    for (Offset = 0; Offset < FEXBENCH_FILE_BYTES; Offset += FEXBENCH_BIG_BLOCK)
    {
        if (!WriteFile(File, Buffer, FEXBENCH_BIG_BLOCK, &Done, NULL) || Done != FEXBENCH_BIG_BLOCK)
            break;
    }
    FlushFileBuffers(File);
    Seconds = SecondsSince(Start);
    BenchPrint("FEXBENCH %s io_write_seq  %10.1f MB/s  (%.3fs, %lu MB, flushed)\n", FEXBENCH_ARCH,
               (Seconds > 0.0) ? ((double)Offset / Seconds) / 1048576.0 : 0.0, Seconds,
               (unsigned long)(Offset / 1048576u));
    CloseHandle(File);

    BenchStoragePass(Path, Buffer, FALSE);
    BenchStoragePass(Path, Buffer, TRUE);

    DeleteFileA(Path);
    VirtualFree(Buffer, 0, MEM_RELEASE);
}

int main(int argc, char *argv[])
{
    LARGE_INTEGER Frequency;
    const char *Directory = ".";

    if (argc > 1)
        Directory = argv[1];

    if (!QueryPerformanceFrequency(&Frequency) || Frequency.QuadPart == 0)
    {
        BenchPrint("FEXBENCH %s no performance counter\n", FEXBENCH_ARCH);
        return 1;
    }
    g_TicksPerSecond = (double)Frequency.QuadPart;

    BenchPrint("FEXBENCH_BEGIN %s qpc=%llu Hz dir=%s\n", FEXBENCH_ARCH,
               (unsigned long long)Frequency.QuadPart, Directory);

    BenchCpu("cpu_int", CpuIntegerChain);
    BenchCpu("cpu_branch", CpuBranchChain);
    BenchMemory();
    BenchStorage(Directory);

    BenchPrint("FEXBENCH_END %s\n", FEXBENCH_ARCH);
    return 0;
}
