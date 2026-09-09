/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * RAM-copy measurements, with runtime-selected implementations, correctness
 * checks outside timing, adaptive batches, and no output during measurement.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pseh/pseh2.h>
#include "copy_variants.h"
#include "mesa_tile_variants.h"
#include "platform_diag.h"

static ULONG ArenaBytes = 32u * 1024u * 1024u;
#define RANDOM_SLOTS 32768u
static ULONG ReadOffsets[RANDOM_SLOTS], WriteOffsets[RANDOM_SLOTS];
static BOOL RandomMode;
#define TRIALS 3
#define MAX_ROWS 1600
#define PAD 128
#define MAX_PLACEMENT_OFFSET 4096

typedef void *(__cdecl *COPY_FN)(void *, const void *, size_t);
typedef struct { const char *Name; COPY_FN Copy; ULONG ValidationStatus; } METHOD;
typedef struct
{
    const char *Method, *Mode, *Mapping;
    ULONG Bytes, SourceOffset, DestinationOffset, Slots, Calls, Status;
    ULONGLONG Ticks[TRIALS], Cpu100ns;
    PI_SAMPLE Before, After;
} ROW;
static ROW Rows[MAX_ROWS];
static ULONG RowCount;
static LARGE_INTEGER Frequency;
static volatile BYTE Sink;
static BOOL Serial;
static ULONGLONG TargetTicks;

static void Print(const char *Format, ...)
{
    char Buffer[1024];
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Args);
    va_end(Args);
    Buffer[sizeof(Buffer) - 1] = 0;
    fputs(Buffer, stdout);
    if (Serial)
        OutputDebugStringA(Buffer);
}

static ULONGLONG Now(void)
{
    LARGE_INTEGER Time;
    QueryPerformanceCounter(&Time);
    return Time.QuadPart;
}

static ULONGLONG CpuTime(void)
{
    FILETIME C, E, K, U;
    if (!GetThreadTimes(GetCurrentThread(), &C, &E, &K, &U))
        return 0;
    return (((ULONGLONG)K.dwHighDateTime << 32) | K.dwLowDateTime) +
           (((ULONGLONG)U.dwHighDateTime << 32) | U.dwLowDateTime);
}

static void StoreBarrier(void)
{
#if defined(__aarch64__)
    __asm__ __volatile__("dsb ish" : : : "memory");
#else
    MemoryBarrier();
#endif
}

static __declspec(noinline) void * __cdecl CopyNoop(void *D, const void *S, size_t N)
{
#if defined(__clang__) || defined(__GNUC__)
    __asm__ __volatile__("" : : : "memory");
#endif
    return D;
}

static ULONGLONG Batch(COPY_FN Copy, BYTE *Destination, const BYTE *Source,
                      ULONG Bytes, ULONG Stride, ULONG Slots, ULONG Calls)
{
    ULONG Index, Offset = 0, Span = Slots * Stride;
    ULONGLONG Start = Now();
    if (RandomMode)
    {
        for (Index = 0; Index < Calls; ++Index)
        {
            ULONG Pick = Index & (RANDOM_SLOTS - 1);
            Copy(Destination + WriteOffsets[Pick], Source + ReadOffsets[Pick], Bytes);
        }
    }
    else for (Index = 0; Index < Calls; ++Index)
    {
        Copy(Destination + Offset, Source + Offset, Bytes);
        Offset += Stride;
        if (Offset >= Span)
            Offset = 0;
    }
    StoreBarrier();
    return Now() - Start;
}

static BOOL Validate(COPY_FN Copy, BYTE *Destination, BYTE *Source,
                     ULONG Bytes, ULONG SourceOffset, ULONG DestinationOffset)
{
    ULONG Index;
    BYTE *D = Destination + PAD + DestinationOffset;
    BYTE *S = Source + PAD + SourceOffset;
    /* Poison every destination byte, including red zones, before validation. */
    for (Index = 0; Index < Bytes + 32; ++Index)
        ((volatile BYTE *)D)[(LONG)Index - 16] = 0xa5;
    if (Copy(D, S, Bytes) != D)
        return FALSE;
    for (Index = 0; Index < Bytes; ++Index)
        if (D[Index] != S[Index])
            return FALSE;
    for (Index = 0; Index < 16; ++Index)
        if (D[(LONG)Index - 16] != 0xa5 || D[Bytes + Index] != 0xa5)
            return FALSE;
    return TRUE;
}

static void Measure(const METHOD *Method, const char *Mapping, BOOL Streaming,
                    BYTE *Destination, BYTE *Source, ULONG ArenaBytes,
                    ULONG Bytes, ULONG SourceOffset, ULONG DestinationOffset)
{
    ROW *Row;
    ULONG Stride = (Bytes + 63) & ~63u, Slots, Calls = 1, Trial;
    ULONGLONG Elapsed, CpuStart;
    if (RowCount >= MAX_ROWS)
        return;
    Row = &Rows[RowCount++];
    Row->Method = Method->Name;
    Row->Mode = RandomMode ? "random4k" : Streaming ? "rotating" : "repeated";
    Row->Mapping = Mapping;
    Row->Bytes = Bytes;
    Row->SourceOffset = SourceOffset;
    Row->DestinationOffset = DestinationOffset;
    Slots = Streaming ? ArenaBytes / Stride : 1;
    Row->Slots = Slots;
    _SEH2_TRY
    {
        if (!Validate(Method->Copy, Destination, Source, Bytes, SourceOffset, DestinationOffset))
        {
            Row->Status = ERROR_CRC;
            _SEH2_LEAVE;
        }
        Destination += PAD + DestinationOffset;
        Source += PAD + SourceOffset;
        /* Warm the entire selected working set, with no demand faults timed. */
        Batch(Method->Copy, Destination, Source, Bytes, Stride, Slots, Slots);
        do
        {
            Elapsed = Batch(Method->Copy, Destination, Source, Bytes, Stride, Slots, Calls);
            if (Elapsed >= TargetTicks / 4 || Calls >= (1u << 24))
                break;
            Calls *= 2;
        } while (1);
        if (Elapsed && Elapsed < TargetTicks)
        {
            ULONGLONG Scaled = (ULONGLONG)Calls * TargetTicks / Elapsed;
            Calls = Scaled > (1u << 26) ? (1u << 26) : (ULONG)Scaled;
        }
        Row->Calls = Calls;
        PiSample(&Row->Before);
        for (Trial = 0; Trial < TRIALS; ++Trial)
        {
            CpuStart = CpuTime();
            Row->Ticks[Trial] = Batch(Method->Copy, Destination, Source, Bytes, Stride, Slots, Calls);
            Row->Cpu100ns += CpuTime() - CpuStart;
            Sink ^= Destination[(RandomMode ? WriteOffsets[(Calls - 1) & (RANDOM_SLOTS - 1)] : (Calls - 1) % Slots * Stride) + Bytes - 1];
        }
        PiSample(&Row->After);
        /* Validate complete first/last touched blocks after timing. */
        if (RandomMode)
        {
            /* Destination offsets form a permutation, so each slot has one
             * source throughout the run, even when the offset list wraps. */
            for (Trial = 0; Trial < RANDOM_SLOTS; ++Trial)
                if (memcmp(Destination + WriteOffsets[Trial], Source + ReadOffsets[Trial], Bytes))
                { Row->Status = ERROR_CRC; break; }
        }
        else if (memcmp(Destination, Source, Bytes) ||
                 memcmp(Destination + ((Calls - 1) % Slots) * Stride,
                        Source + ((Calls - 1) % Slots) * Stride, Bytes))
            Row->Status = ERROR_CRC;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Row->Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
}

static void Initialize(BYTE *Memory, ULONG Bytes)
{
    ULONG Index, Random = 0x12345678;
    for (Index = 0; Index < Bytes; ++Index)
    {
        Random = Random * 1664525u + 1013904223u;
        Memory[Index] = (BYTE)(Random >> 24);
    }
}

int main(int argc, char **argv)
{
    static const ULONG Sizes[] = {64, 1024, 8192, 65536, 262144, 1048576, 8294400, 16777216};
    static const ULONG Alignments[][2] = {{0,0}, {1,1}, {0,4}, {1,3}};
    METHOD Methods[20];
    ULONG MethodCount = 0, M, S, A, Mode, Index, Failures = 0;
    HMODULE Ucrt = NULL;
    ULONG SpecialError[2] = {0}, SpecialProtect[2] = {0};
    DWORD_PTR ProcessMask = 0, SystemMask = 0, Pin = 0, PreviousAffinity = 0;
    SYSTEM_INFO Info;
    BYTE *Source, *Destination;
    ULONGLONG ClockStart, ClockCost, HarnessHot, HarnessRotating;
    COPY_FN volatile Empty = CopyNoop;
    ULONG TrialMs = 25, RequestedTrialMs = 0;
    BOOL Special = FALSE, NcOnly = FALSE, StreamOnly = FALSE, PlacementOnly = FALSE, Unaligned = FALSE;
    for (Index = 1; Index < (ULONG)argc; ++Index)
    {
        if (!strcmp(argv[Index], "--serial")) Serial = TRUE;
        else if (!strcmp(argv[Index], "--special")) Special = TRUE;
        else if (!strcmp(argv[Index], "--nc-only")) NcOnly = Special = TRUE;
        else if (!strcmp(argv[Index], "--streaming")) StreamOnly = TRUE;
        else if (!strcmp(argv[Index], "--pi-clocks")) PiRequested = TRUE;
        else if (!strcmp(argv[Index], "--placement")) PlacementOnly = TRUE;
        else if (!strcmp(argv[Index], "--unaligned")) Unaligned = Special = TRUE;
        else if (!strcmp(argv[Index], "--trial-ms") && Index + 1 < (ULONG)argc)
        {
            char *End;
            RequestedTrialMs = strtoul(argv[++Index], &End, 10);
            if (*End || RequestedTrialMs < 10 || RequestedTrialMs > 1000)
            { Print("--trial-ms must be 10..1000\n"); return 1; }
        }
        else { Print("usage: memcopybench [--serial] [--special] [--nc-only] [--streaming] [--pi-clocks] [--placement] [--unaligned] [--trial-ms 10..1000]\n"); return 1; }
    }
    if (StreamOnly || PlacementOnly)
    {
        if (Special || (StreamOnly && PlacementOnly)) { Print("--streaming/--placement cannot be combined with each other or --special/--nc-only\n"); return 1; }
        ArenaBytes = 128u * 1024u * 1024u;
        TrialMs = 250;
    }
    if (RequestedTrialMs) TrialMs = RequestedTrialMs;
    PiOpen();
    ZeroMemory(Methods, sizeof(Methods));
    QueryPerformanceFrequency(&Frequency);
    if (!Frequency.QuadPart)
        return 1;
    TargetTicks = (ULONGLONG)Frequency.QuadPart * TrialMs / 1000;
    GetSystemInfo(&Info);
    if (GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask))
    {
        Pin = (ProcessMask & 4) ? 4 : ProcessMask & (0 - ProcessMask);
        PreviousAffinity = SetThreadAffinityMask(GetCurrentThread(), Pin);
    }
    Methods[MethodCount].Name = "msvcrt_memcpy";
    Methods[MethodCount].Copy = (COPY_FN)GetProcAddress(GetModuleHandleW(L"msvcrt.dll"), "memcpy");
    if (Methods[MethodCount].Copy) ++MethodCount;
    Methods[MethodCount].Name = "msvcrt_memmove";
    Methods[MethodCount].Copy = (COPY_FN)GetProcAddress(GetModuleHandleW(L"msvcrt.dll"), "memmove");
    if (Methods[MethodCount].Copy) ++MethodCount;
    /* Mesa imports its copy functions through the UCRT API set. */
    Ucrt = LoadLibraryW(L"ucrtbase.dll");
    if (Ucrt)
    {
        Methods[MethodCount].Name = "ucrtbase_memcpy";
        Methods[MethodCount].Copy = (COPY_FN)GetProcAddress(Ucrt, "memcpy");
        if (Methods[MethodCount].Copy) ++MethodCount;
        Methods[MethodCount].Name = "ucrtbase_memmove";
        Methods[MethodCount].Copy = (COPY_FN)GetProcAddress(Ucrt, "memmove");
        if (Methods[MethodCount].Copy) ++MethodCount;
    }
    Methods[MethodCount].Name = "ntdll_memmove";
    Methods[MethodCount].Copy = (COPY_FN)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "memmove");
    if (Methods[MethodCount].Copy) ++MethodCount;
    Methods[MethodCount].Name = "ntdll_memcpy";
    Methods[MethodCount].Copy = (COPY_FN)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "memcpy");
    if (Methods[MethodCount].Copy) ++MethodCount;
#ifdef HAVE_ARM64_COPY_VARIANTS
    Methods[MethodCount].Name = "gpr64_normal_ram";
    Methods[MethodCount++].Copy = CopyGpr64;
    Methods[MethodCount].Name = "neon64_normal_ram";
    Methods[MethodCount++].Copy = CopyNeon64;
    Methods[MethodCount].Name = "neon_pipelined";
    Methods[MethodCount++].Copy = CopyNeonPipelined;
    Methods[MethodCount].Name = "neon_interleaved";
    Methods[MethodCount++].Copy = CopyNeonInterleaved;
    Methods[MethodCount].Name = "neon_prefetch256";
    Methods[MethodCount++].Copy = CopyNeonPrefetch256;
    Methods[MethodCount].Name = "neon_prefetch512";
    Methods[MethodCount++].Copy = CopyNeonPrefetch512;
    Methods[MethodCount].Name = "neon_nontemporal";
    Methods[MethodCount++].Copy = CopyNeonNonTemporal;
    Methods[MethodCount].Name = "neon_prefetch256_nt";
    Methods[MethodCount++].Copy = CopyNeonPrefetch256Nt;
    Methods[MethodCount].Name = "neon_ld1_64_normal_ram";
    Methods[MethodCount++].Copy = CopyNeonLd1_64;
    Methods[MethodCount].Name = "neon_ld1_2d_normal_ram";
    Methods[MethodCount++].Copy = CopyNeonLd1_2d;
#endif
#ifdef HAVE_MESA_TILE_VARIANTS
    Methods[MethodCount].Name = "mesa_tile_before_8";
    Methods[MethodCount++].Copy = CopyMesaBefore8;
    Methods[MethodCount].Name = "mesa_tile_after_8";
    Methods[MethodCount++].Copy = CopyMesaAfter8;
    Methods[MethodCount].Name = "mesa_tile_before_16";
    Methods[MethodCount++].Copy = CopyMesaBefore16;
    Methods[MethodCount].Name = "mesa_tile_after_16";
    Methods[MethodCount++].Copy = CopyMesaAfter16;
#endif
    if (Unaligned)
    {
        ULONG Selected = 0;
        for (M = 0; M < MethodCount; ++M)
            if (!strcmp(Methods[M].Name, "ntdll_memcpy") || !strcmp(Methods[M].Name, "ucrtbase_memcpy") ||
                !strcmp(Methods[M].Name, "neon64_normal_ram") || !strcmp(Methods[M].Name, "gpr64_normal_ram"))
                Methods[Selected++] = Methods[M];
        MethodCount = Selected;
    }
    Source = VirtualAlloc(NULL, ArenaBytes + 2 * PAD + MAX_PLACEMENT_OFFSET, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Destination = VirtualAlloc(NULL, ArenaBytes + 2 * PAD + MAX_PLACEMENT_OFFSET, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!Source || !Destination)
    {
        Print("MEMCOPY_ERROR allocation=%lu\n", GetLastError());
        if (Source) VirtualFree(Source, 0, MEM_RELEASE);
        if (Destination) VirtualFree(Destination, 0, MEM_RELEASE);
        return 1;
    }
    Initialize(Source, ArenaBytes + 2 * PAD + MAX_PLACEMENT_OFFSET);
    memset(Destination, 0, ArenaBytes + 2 * PAD + MAX_PLACEMENT_OFFSET);
    ClockStart = Now();
    for (Index = 0; Index < 10000; ++Index) Now();
    ClockCost = Now() - ClockStart;
    HarnessHot = Batch(Empty, Destination, Source, 64, 64, 1, 1000000);
    HarnessRotating = Batch(Empty, Destination, Source, 64, 64, ArenaBytes / 64, 1000000);
    /* Exercise tails and independently misaligned pointers before timing. */
    for (M = 0; M < MethodCount; ++M)
    {
        _SEH2_TRY
        {
            static const ULONG Offsets[] = {0,1,3,4,7,8,15};
            ULONG N, D, Src;
            for (N = 0; N <= 257 && !Methods[M].ValidationStatus; ++N)
                for (D = 0; D < ARRAYSIZE(Offsets); ++D)
                    for (Src = 0; Src < ARRAYSIZE(Offsets); ++Src)
                        if (!Validate(Methods[M].Copy, Destination, Source, N, Offsets[Src], Offsets[D]))
                            Methods[M].ValidationStatus = ERROR_CRC;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Methods[M].ValidationStatus = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (Methods[M].ValidationStatus) ++Failures;
    }
    if (PlacementOnly)
    {
        static const ULONG Offsets[] = {0, 64, 128, 256, 512, 1024, 2048, 4096};
        static const ULONG PlacementSizes[] = {1048576, 16777216};
        for (S = 0; S < ARRAYSIZE(PlacementSizes); ++S)
            for (A = 0; A < ARRAYSIZE(Offsets); ++A)
                for (M = 0; M < MethodCount; ++M)
                {
                    ULONG Pick = ((S + A) & 1) ? MethodCount - 1 - M : M;
                    const char *Name = Methods[Pick].Name;
                    if (strcmp(Name, "ntdll_memcpy") && strcmp(Name, "gpr64_normal_ram")) continue;
                    if (Methods[Pick].ValidationStatus) continue;
                    Measure(&Methods[Pick], "cached_to_cached", TRUE, Destination, Source,
                            ArenaBytes, PlacementSizes[S], 0, Offsets[A]);
                }
    }
    else if (StreamOnly)
    {
        static const ULONG StreamSizes[] = {4096, 1048576, 16777216, 33554432};
        ULONG Seed = 0x456789ab;
        /* Independent page permutations, generated before any timed batch. */
        for (Index = 0; Index < RANDOM_SLOTS; ++Index)
            ReadOffsets[Index] = WriteOffsets[Index] = Index * 4096;
        for (Index = RANDOM_SLOTS - 1; Index; --Index)
        {
            ULONG Pick, Temp;
            Seed = Seed * 1664525u + 1013904223u;
            Pick = Seed % (Index + 1);
            Temp = ReadOffsets[Index]; ReadOffsets[Index] = ReadOffsets[Pick]; ReadOffsets[Pick] = Temp;
            Seed = Seed * 1664525u + 1013904223u;
            Pick = Seed % (Index + 1);
            Temp = WriteOffsets[Index]; WriteOffsets[Index] = WriteOffsets[Pick]; WriteOffsets[Pick] = Temp;
        }
        for (S = 0; S <= ARRAYSIZE(StreamSizes); ++S)
            for (M = 0; M < MethodCount; ++M)
            {
                ULONG Pick = (S & 1) ? MethodCount - 1 - M : M;
                const char *Name = Methods[Pick].Name;
                if (strcmp(Name, "ntdll_memcpy") && strcmp(Name, "ucrtbase_memcpy") &&
                    strcmp(Name, "gpr64_normal_ram") && strcmp(Name, "neon64_normal_ram") &&
                    strcmp(Name, "neon_prefetch256") && strcmp(Name, "neon_prefetch512") &&
                    strcmp(Name, "neon_nontemporal") && strcmp(Name, "neon_prefetch256_nt") &&
                    strcmp(Name, "neon_pipelined") && strcmp(Name, "neon_interleaved")) continue;
                if (Methods[Pick].ValidationStatus) continue;
                RandomMode = S == ARRAYSIZE(StreamSizes);
                Measure(&Methods[Pick], "cached_to_cached", TRUE, Destination, Source,
                        ArenaBytes, RandomMode ? 4096 : StreamSizes[S], 0, 0);
            }
        RandomMode = FALSE;
    }
    else     if (NcOnly)
    {
        for (M = 0; M < MethodCount; ++M)
            if (!Methods[M].ValidationStatus)
                Measure(&Methods[M], "cached_to_cached", FALSE, Destination, Source,
                        ArenaBytes, 1048576, 0, 0);
    }
    else
    /* Alternate method order to reduce fixed thermal/time-order bias. */
    for (Mode = 0; Mode < 2; ++Mode)
        for (S = 0; S < ARRAYSIZE(Sizes); ++S)
            for (A = 0; A < ARRAYSIZE(Alignments); ++A)
                for (M = 0; M < MethodCount; ++M)
                {
                    ULONG Pick = ((S + A + Mode) & 1) ? MethodCount - 1 - M : M;
                    if (Unaligned && Sizes[S] != 65536 && Sizes[S] != 1048576 && Sizes[S] != 8294400 && Sizes[S] != 16777216) continue;
                    if (Methods[Pick].ValidationStatus) continue;
                    Measure(&Methods[Pick], "cached_to_cached", Mode, Destination, Source,
                            ArenaBytes, Sizes[S], Alignments[A][0], Alignments[A][1]);
                }
    if (Special)
    {
        static const DWORD Flags[2] = {PAGE_NOCACHE, PAGE_WRITECOMBINE};
        static const char *Names[2][2] = {{"cached_to_nocache", "nocache_to_cached"},
                                        {"cached_to_writecombine", "writecombine_to_cached"}};
        for (Index = 0; Index < 2; ++Index)
        {
            BYTE *Memory = VirtualAlloc(NULL, 1048576 + 2 * PAD + 16, MEM_RESERVE | MEM_COMMIT,
                                       PAGE_READWRITE | Flags[Index]);
            MEMORY_BASIC_INFORMATION MemoryInfo;
            if (!Memory)
            {
                SpecialError[Index] = GetLastError();
                continue;
            }
            if (VirtualQuery(Memory, &MemoryInfo, sizeof(MemoryInfo)))
                SpecialProtect[Index] = MemoryInfo.Protect;
            _SEH2_TRY
            {
                Initialize(Memory, 1048576 + 2 * PAD + 16);
                for (Mode = 0; Mode < 2; ++Mode)
                    for (A = 0; A < (Unaligned ? ARRAYSIZE(Alignments) : 1); ++A)
                        for (M = 0; M < MethodCount; ++M)
                            Measure(&Methods[M], Names[Index][Mode], FALSE,
                                    Mode ? Destination : Memory, Mode ? Memory : Source,
                                    1048576, 1048576, Alignments[A][0], Alignments[A][1]);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                SpecialError[Index] = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            VirtualFree(Memory, 0, MEM_RELEASE);
        }
    }
    /* All serial/file formatting starts after the measurements finish. */
    Print("MEMCOPY_BEGIN version=2 arch=%u cpus=%lu affinity=%I64u pinned=%u frequency=%I64u "
          "arena_per_buffer=%u trials=%u target_ms=%lu qpc_avg_ns=%.2f\n",
          Info.wProcessorArchitecture, Info.dwNumberOfProcessors, (ULONGLONG)Pin,
          PreviousAffinity != 0, Frequency.QuadPart, ArenaBytes, TRIALS, TrialMs,
          (double)ClockCost * 1e9 / Frequency.QuadPart / 10000);
    Print("MEMCOPY_HARNESS hot_ns=%.2f rotating_ns=%.2f calls=1000000\n",
          (double)HarnessHot * 1e3 / Frequency.QuadPart,
          (double)HarnessRotating * 1e3 / Frequency.QuadPart);
    if (PiRequested)
    {
        static const ULONG Ids[] = {3, 4, 5, 8};
        Print("MEMCOPY_PI open_error=%lu samples=outside_timing\n", PiOpenError);
        for (M = 0; M < ARRAYSIZE(Ids); ++M)
            Print("MEMCOPY_PI_CLOCK id=%lu configured_hz=%lu configured_error=%lu max_hz=%lu max_error=%lu\n",
                  Ids[M], PiConfigured[M].Value, PiConfigured[M].Error, PiMaximum[M].Value, PiMaximum[M].Error);
    }
    for (M = 0; M < MethodCount; ++M)
        Print("MEMCOPY_METHOD name=%s address=%p validation=%08lx\n",
              Methods[M].Name, Methods[M].Copy, Methods[M].ValidationStatus);
    for (Index = 0; Index < RowCount; ++Index)
    {
        const ROW *R = &Rows[Index];
        ULONGLONG Min = R->Ticks[0], Max = R->Ticks[0], Sum = 0, Median;
        double Seconds, Payload;
        for (M = 0; M < TRIALS; ++M)
        {
            if (R->Ticks[M] < Min) Min = R->Ticks[M];
            if (R->Ticks[M] > Max) Max = R->Ticks[M];
            Sum += R->Ticks[M];
        }
        Median = Sum - Min - Max;
        Seconds = (double)Median / Frequency.QuadPart;
        Payload = (double)R->Bytes * R->Calls;
        if (R->Status || !Median) ++Failures;
        Print("MEMCOPY_ROW method=%s map=%s mode=%s bytes=%lu src_off=%lu dst_off=%lu slots=%lu "
              "calls=%lu status=%08lx median_ticks=%I64u min_ticks=%I64u max_ticks=%I64u "
              "MBps=%.2f MiBps=%.2f ns_per_copy=%.2f cpu_100ns=%I64u\n",
              R->Method, R->Mapping, R->Mode, R->Bytes, R->SourceOffset, R->DestinationOffset,
              R->Slots, R->Calls, R->Status, Median, Min, Max,
              Seconds ? Payload / Seconds / 1e6 : 0, Seconds ? Payload / Seconds / 1048576 : 0,
              R->Calls ? Seconds * 1e9 / R->Calls : 0, R->Cpu100ns);
        if (PiRequested)
            for (M = 0; M < 2; ++M)
            {
                const PI_SAMPLE *P = M ? &R->After : &R->Before;
                Print("MEMCOPY_PI_SAMPLE row=%lu phase=%s arm_hz=%lu arm_error=%lu core_hz=%lu core_error=%lu "
                      "sdram_hz=%lu sdram_error=%lu temp_mC=%lu temp_error=%lu\n",
                      Index, M ? "after" : "before", P->Arm.Value, P->Arm.Error, P->Core.Value, P->Core.Error,
                      P->Sdram.Value, P->Sdram.Error, P->Temperature.Value, P->Temperature.Error);
            }
    }
    if (Special)
        for (Index = 0; Index < 2; ++Index)
            Print("MEMCOPY_MAPPING kind=%s allocation_error=%08lx reported_protect=%08lx\n",
                  Index ? "writecombine" : "nocache", SpecialError[Index], SpecialProtect[Index]);
    Print("MEMCOPY_END rows=%lu failures=%lu sink=%u\n", RowCount, Failures, Sink);
    VirtualFree(Source, 0, MEM_RELEASE);
    VirtualFree(Destination, 0, MEM_RELEASE);
    if (PreviousAffinity) SetThreadAffinityMask(GetCurrentThread(), PreviousAffinity);
    if (Ucrt) FreeLibrary(Ucrt);
    if (PiMailbox != INVALID_HANDLE_VALUE) CloseHandle(PiMailbox);
    return Failures ? 1 : 0;
}
