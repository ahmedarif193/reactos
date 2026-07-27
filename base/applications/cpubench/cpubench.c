/*
 * PROJECT:     ReactOS CPU benchmark
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Native Dhrystone 2.1 + FP throughput, single-core and SMP scaling
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define VAX_DHRY_PER_SEC 1757
#define FP_FLOPS_PER_ITER 11
#define MAX_CPUS 64
#define SMP_TARGET_SECONDS 15U

static volatile long gDhrySink;
static volatile double gFpSink;

static void emit(const char *Format, ...)
{
    char Buffer[512];
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Args);
    va_end(Args);
    Buffer[sizeof(Buffer) - 1] = 0;
    fputs(Buffer, stdout);
    fflush(stdout);
    OutputDebugStringA(Buffer);
}

typedef enum { Ident_1, Ident_2, Ident_3, Ident_4, Ident_5 } Enumeration;
typedef int One_Thirty;
typedef int One_Fifty;
typedef char Capital_Letter;
typedef int Boolean;
typedef char Str_30[31];
typedef int Arr_1_Dim[50];
typedef int Arr_2_Dim[50][50];

typedef struct record {
    struct record *Ptr_Comp;
    Enumeration Discr;
    union {
        struct { Enumeration Enum_Comp; int Int_Comp; char Str_Comp[31]; } var_1;
        struct { Enumeration E_Comp_2; char Str_2_Comp[31]; } var_2;
        struct { char Ch_1_Comp; char Ch_2_Comp; } var_3;
    } variant;
} Rec_Type, *Rec_Pointer;

typedef struct {
    Rec_Pointer Ptr_Glob;
    Rec_Pointer Next_Ptr_Glob;
    int Int_Glob;
    Boolean Bool_Glob;
    char Ch_1_Glob;
    char Ch_2_Glob;
    Arr_1_Dim Arr_1_Glob;
    Arr_2_Dim Arr_2_Glob;
    Rec_Type Rec1;
    Rec_Type Rec2;
} DHRY_CTX;

static Boolean Func_3(Enumeration Enum_Par_Val)
{
    Enumeration Enum_Loc = Enum_Par_Val;
    return (Enum_Loc == Ident_3);
}

static void Proc_7(One_Fifty Int_1_Par_Val, One_Fifty Int_2_Par_Val, One_Fifty *Int_Par_Ref)
{
    One_Fifty Int_Loc = Int_1_Par_Val + 2;
    *Int_Par_Ref = Int_2_Par_Val + Int_Loc;
}

static void Proc_6(DHRY_CTX *C, Enumeration Enum_Val_Par, Enumeration *Enum_Ref_Par)
{
    *Enum_Ref_Par = Enum_Val_Par;
    if (!Func_3(Enum_Val_Par))
        *Enum_Ref_Par = Ident_4;
    switch (Enum_Val_Par)
    {
        case Ident_1: *Enum_Ref_Par = Ident_1; break;
        case Ident_2: *Enum_Ref_Par = (C->Int_Glob > 100) ? Ident_1 : Ident_4; break;
        case Ident_3: *Enum_Ref_Par = Ident_2; break;
        case Ident_4: break;
        case Ident_5: *Enum_Ref_Par = Ident_3; break;
    }
}

static Enumeration Func_1(DHRY_CTX *C, Capital_Letter Ch_1_Par_Val, Capital_Letter Ch_2_Par_Val)
{
    Capital_Letter Ch_1_Loc = Ch_1_Par_Val;
    Capital_Letter Ch_2_Loc = Ch_1_Loc;
    if (Ch_2_Loc != Ch_2_Par_Val)
        return Ident_1;
    C->Ch_1_Glob = Ch_1_Loc;
    return Ident_2;
}

static Boolean Func_2(DHRY_CTX *C, Str_30 Str_1_Par_Ref, Str_30 Str_2_Par_Ref)
{
    One_Thirty Int_Loc = 2;
    Capital_Letter Ch_Loc = 'A';
    while (Int_Loc <= 2)
    {
        if (Func_1(C, Str_1_Par_Ref[Int_Loc], Str_2_Par_Ref[Int_Loc + 1]) == Ident_1)
        {
            Ch_Loc = 'A';
            Int_Loc += 1;
        }
    }
    if (Ch_Loc >= 'W' && Ch_Loc < 'Z')
        Int_Loc = 7;
    if (Ch_Loc == 'R')
        return TRUE;
    if (strcmp(Str_1_Par_Ref, Str_2_Par_Ref) > 0)
    {
        Int_Loc += 7;
        C->Int_Glob = Int_Loc;
        return TRUE;
    }
    return FALSE;
}

static void Proc_8(Arr_1_Dim Arr_1_Par_Ref, Arr_2_Dim Arr_2_Par_Ref,
                   int Int_1_Par_Val, int Int_2_Par_Val, DHRY_CTX *C)
{
    One_Fifty Int_Index;
    One_Fifty Int_Loc = Int_1_Par_Val + 5;
    Arr_1_Par_Ref[Int_Loc] = Int_2_Par_Val;
    Arr_1_Par_Ref[Int_Loc + 1] = Arr_1_Par_Ref[Int_Loc];
    Arr_1_Par_Ref[Int_Loc + 30] = Int_Loc;
    for (Int_Index = Int_Loc; Int_Index <= Int_Loc + 1; ++Int_Index)
        Arr_2_Par_Ref[Int_Loc][Int_Index] = Int_Loc;
    Arr_2_Par_Ref[Int_Loc][Int_Loc - 1] += 1;
    Arr_2_Par_Ref[Int_Loc + 20][Int_Loc] = Arr_1_Par_Ref[Int_Loc];
    C->Int_Glob = 5;
}

static void Proc_3(DHRY_CTX *C, Rec_Pointer *Ptr_Ref_Par)
{
    if (C->Ptr_Glob != NULL)
        *Ptr_Ref_Par = C->Ptr_Glob->Ptr_Comp;
    Proc_7(10, C->Int_Glob, &C->Ptr_Glob->variant.var_1.Int_Comp);
}

static void Proc_5(DHRY_CTX *C)
{
    C->Ch_1_Glob = 'A';
    C->Bool_Glob = FALSE;
}

static void Proc_4(DHRY_CTX *C)
{
    Boolean Bool_Loc = (C->Ch_1_Glob == 'A');
    C->Bool_Glob = Bool_Loc | C->Bool_Glob;
    C->Ch_2_Glob = 'B';
}

static void Proc_2(DHRY_CTX *C, One_Fifty *Int_Par_Ref)
{
    One_Fifty Int_Loc = *Int_Par_Ref + 10;
    Enumeration Enum_Loc = Ident_2;
    do
    {
        if (C->Ch_1_Glob == 'A')
        {
            Int_Loc -= 1;
            *Int_Par_Ref = Int_Loc - C->Int_Glob;
            Enum_Loc = Ident_1;
        }
    } while (Enum_Loc != Ident_1);
}

static void Proc_1(DHRY_CTX *C, Rec_Pointer Ptr_Val_Par)
{
    Rec_Pointer Next_Record = Ptr_Val_Par->Ptr_Comp;
    *Ptr_Val_Par->Ptr_Comp = *C->Ptr_Glob;
    Ptr_Val_Par->variant.var_1.Int_Comp = 5;
    Next_Record->variant.var_1.Int_Comp = Ptr_Val_Par->variant.var_1.Int_Comp;
    Next_Record->Ptr_Comp = Ptr_Val_Par->Ptr_Comp;
    Proc_3(C, &Next_Record->Ptr_Comp);
    if (Next_Record->Discr == Ident_1)
    {
        Next_Record->variant.var_1.Int_Comp = 6;
        Proc_6(C, Ptr_Val_Par->variant.var_1.Enum_Comp, &Next_Record->variant.var_1.Enum_Comp);
        Next_Record->Ptr_Comp = C->Ptr_Glob->Ptr_Comp;
        Proc_7(Next_Record->variant.var_1.Int_Comp, 10, &Next_Record->variant.var_1.Int_Comp);
    }
    else
    {
        *Ptr_Val_Par = *Ptr_Val_Par->Ptr_Comp;
    }
}

static LONGLONG DhryRun(unsigned long Number_Of_Runs)
{
    DHRY_CTX C;
    int Int_1_Loc, Int_2_Loc, Int_3_Loc;
    char Ch_Index;
    Enumeration Enum_Loc;
    Str_30 Str_1_Loc, Str_2_Loc;
    unsigned long Run_Index;
    LARGE_INTEGER T0, T1;

    Int_3_Loc = 0;
    Enum_Loc = Ident_2;
    memset(&C, 0, sizeof(C));
    C.Next_Ptr_Glob = &C.Rec2;
    C.Ptr_Glob = &C.Rec1;
    C.Ptr_Glob->Ptr_Comp = C.Next_Ptr_Glob;
    C.Ptr_Glob->Discr = Ident_1;
    C.Ptr_Glob->variant.var_1.Enum_Comp = Ident_3;
    C.Ptr_Glob->variant.var_1.Int_Comp = 40;
    strcpy(C.Ptr_Glob->variant.var_1.Str_Comp, "DHRYSTONE PROGRAM, SOME STRING");
    strcpy(Str_1_Loc, "DHRYSTONE PROGRAM, 1'ST STRING");
    C.Arr_2_Glob[8][7] = 10;

    QueryPerformanceCounter(&T0);
    for (Run_Index = 1; Run_Index <= Number_Of_Runs; ++Run_Index)
    {
        Proc_5(&C);
        Proc_4(&C);
        Int_1_Loc = 2;
        Int_2_Loc = 3;
        strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING");
        Enum_Loc = Ident_2;
        C.Bool_Glob = !Func_2(&C, Str_1_Loc, Str_2_Loc);
        while (Int_1_Loc < Int_2_Loc)
        {
            Int_3_Loc = 5 * Int_1_Loc - Int_2_Loc;
            Proc_7(Int_1_Loc, Int_2_Loc, &Int_3_Loc);
            Int_1_Loc += 1;
        }
        Proc_8(C.Arr_1_Glob, C.Arr_2_Glob, Int_1_Loc, Int_3_Loc, &C);
        Proc_1(&C, C.Ptr_Glob);
        for (Ch_Index = 'A'; Ch_Index <= C.Ch_2_Glob; ++Ch_Index)
        {
            if (Enum_Loc == Func_1(&C, Ch_Index, 'C'))
            {
                Proc_6(&C, Ident_1, &Enum_Loc);
                strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 3'RD STRING");
                Int_2_Loc = Run_Index;
                C.Int_Glob = Run_Index;
            }
        }
        Int_2_Loc = Int_2_Loc * Int_1_Loc;
        Int_1_Loc = Int_2_Loc / Int_3_Loc;
        Int_2_Loc = 7 * (Int_2_Loc - Int_3_Loc) - Int_1_Loc;
        Proc_2(&C, &Int_1_Loc);
    }
    QueryPerformanceCounter(&T1);

    gDhrySink += C.Int_Glob + Int_1_Loc + Int_2_Loc + (int)Enum_Loc;
    return T1.QuadPart - T0.QuadPart;
}

static LONGLONG FpRun(unsigned long Iters)
{
    LARGE_INTEGER T0, T1;
    unsigned long i;
    double a = 1.000001, b = 0.500001, c = 0.250001, s = 0.0;

    QueryPerformanceCounter(&T0);
    for (i = 0; i < Iters; ++i)
    {
        s = s + a * b;
        a = a * 1.0000001 - c;
        b = b + 0.0000001;
        s = s - c * b;
        c = c * 0.9999999 + a;
        s = s + a - b;
    }
    QueryPerformanceCounter(&T1);

    gFpSink += s + a + b + c;
    return T1.QuadPart - T0.QuadPart;
}

typedef struct {
    int Cpu;
    int Kind;
    unsigned long BatchCount;
    ULONGLONG TotalCount;
    LONGLONG Ticks;
} WORK;

static DWORD WINAPI Worker(LPVOID Param)
{
    WORK *W = (WORK *)Param;
    ULONGLONG Remaining = W->TotalCount;

    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << W->Cpu);
    W->Ticks = 0;
    while (Remaining != 0)
    {
        unsigned long Count = (Remaining > W->BatchCount) ? W->BatchCount : (unsigned long)Remaining;

        if (W->Kind == 0)
            W->Ticks += DhryRun(Count);
        else
            W->Ticks += FpRun(Count);
        Remaining -= Count;
    }
    return 0;
}

static ULONGLONG RateFromTicks(ULONGLONG Count, unsigned long Unit, LONGLONG Freq, LONGLONG Ticks)
{
    if (Ticks <= 0)
        return 0;

    return (ULONGLONG)(((double)Count * (double)Unit * (double)Freq) / (double)Ticks);
}

static unsigned long Calibrate(int Kind, LONGLONG Freq)
{
    unsigned long Count = 200000;
    LONGLONG Ticks;
    LONGLONG Target = (Freq * 3) / 2;
    LONGLONG Floor = Freq / 5;

    for (;;)
    {
        Ticks = (Kind == 0) ? DhryRun(Count) : FpRun(Count);
        if (Ticks >= Floor || Count >= 100000000UL)
            break;
        Count *= 4;
    }
    if (Ticks > 0)
        Count = (unsigned long)(((ULONGLONG)Count * (ULONGLONG)Target) / (ULONGLONG)Ticks);
    if (Count < 100000UL)
        Count = 100000UL;
    if (Count > 300000000UL)
        Count = 300000000UL;
    return Count;
}

static void RunBench(const char *Name, int Kind, unsigned long Unit, LONGLONG Freq, unsigned NumCpus)
{
    unsigned long Count = Calibrate(Kind, Freq);
    LONGLONG Ticks;
    ULONGLONG SmpCount;
    ULONGLONG Single, AggWall, AggSum, ScalingX100, EffX100;
    WORK W[MAX_CPUS];
    HANDLE Th[MAX_CPUS];
    LARGE_INTEGER WallA, WallB;
    ULONGLONG WallBeginMs, WallEndMs;
    DWORD_PTR PreviousAffinity;
    unsigned i;

    PreviousAffinity = SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1);
    Ticks = (Kind == 0) ? DhryRun(Count) : FpRun(Count);
    Single = RateFromTicks(Count, Unit, Freq, Ticks);

    emit("[cpubench] %s: cores=%u runs/core=%lu qpc=%I64u Hz\n", Name, NumCpus, Count, (ULONGLONG)Freq);
    if (Kind == 0)
        emit("[cpubench] %s single-core: %I64u ops/s = %I64u.%02I64u VAX-MIPS\n",
             Name, Single, Single / VAX_DHRY_PER_SEC, ((Single % VAX_DHRY_PER_SEC) * 100) / VAX_DHRY_PER_SEC);
    else
        emit("[cpubench] %s single-core: %I64u MFLOPS\n", Name, Single / 1000000ULL);

    if (NumCpus < 2)
    {
        emit("[cpubench] %s sustained single-worker control\n", Name);
    }

    SmpCount = (Ticks > 0) ?
        (ULONGLONG)(((double)Count * (double)Freq * SMP_TARGET_SECONDS) / (double)Ticks) : Count;
    if (SmpCount < Count)
        SmpCount = Count;
    emit("[cpubench] %s SMP target: %u seconds, runs/core=%I64u\n",
         Name, SMP_TARGET_SECONDS, SmpCount);

    for (i = 0; i < NumCpus; ++i)
    {
        W[i].Cpu = i;
        W[i].Kind = Kind;
        W[i].BatchCount = Count;
        W[i].TotalCount = SmpCount;
        W[i].Ticks = 0;
    }
    QueryPerformanceCounter(&WallA);
    WallBeginMs = GetTickCount64();
    emit("[cpubench] %s SMP_BEGIN cores=%u target_s=%u qpc=%I64d tick_ms=%I64u\n", Name, NumCpus, SMP_TARGET_SECONDS, WallA.QuadPart, WallBeginMs);
    for (i = 0; i < NumCpus; ++i)
        Th[i] = CreateThread(NULL, 0, Worker, &W[i], 0, NULL);
    WaitForMultipleObjects(NumCpus, Th, TRUE, INFINITE);
    QueryPerformanceCounter(&WallB);
    WallEndMs = GetTickCount64();
    emit("[cpubench] %s SMP_END cores=%u qpc=%I64d tick_ms=%I64u elapsed_ms=%I64u\n", Name, NumCpus, WallB.QuadPart, WallEndMs, (ULONGLONG)((WallB.QuadPart - WallA.QuadPart) * 1000 / Freq));
    if (PreviousAffinity != 0)
        SetThreadAffinityMask(GetCurrentThread(), PreviousAffinity);

    AggSum = 0;
    for (i = 0; i < NumCpus; ++i)
    {
        if (W[i].Ticks > 0)
            AggSum += RateFromTicks(SmpCount, Unit, Freq, W[i].Ticks);
        if (Th[i])
            CloseHandle(Th[i]);
    }
    {
        LONGLONG WallTicks = WallB.QuadPart - WallA.QuadPart;
        AggWall = RateFromTicks(SmpCount * NumCpus, Unit, Freq, WallTicks);
    }
    ScalingX100 = (Single > 0) ? (AggWall * 100ULL) / Single : 0;
    EffX100 = ScalingX100 / NumCpus;

    if (Kind == 0)
        emit("[cpubench] %s all-core(%u): %I64u ops/s = %I64u.%02I64u VAX-MIPS  scaling x%I64u.%02I64u eff %I64u%%\n",
             Name, NumCpus, AggWall, AggWall / VAX_DHRY_PER_SEC, ((AggWall % VAX_DHRY_PER_SEC) * 100) / VAX_DHRY_PER_SEC,
             ScalingX100 / 100, ScalingX100 % 100, EffX100);
    else
        emit("[cpubench] %s all-core(%u): %I64u MFLOPS  scaling x%I64u.%02I64u eff %I64u%%\n",
             Name, NumCpus, AggWall / 1000000ULL, ScalingX100 / 100, ScalingX100 % 100, EffX100);
    emit("[cpubench] %s ideal-sum(%u): %I64u ops/s (per-thread self-timed)\n", Name, NumCpus, AggSum);
}

#define STR_BENCH_LEN 4096
#define STR_BENCH_ITERS 200000

static volatile size_t gStrSink;
static char gStrBuf[STR_BENCH_LEN + 1];

static size_t RefStrlen(const char *s)
{
    const volatile char *p = (const volatile char *)s;
    while (*p) ++p;
    return (size_t)(p - (const volatile char *)s);
}

static size_t (* volatile gLibStrlen)(const char *) = strlen;
static size_t (* volatile gRefStrlen)(const char *) = RefStrlen;

static void RunStringBench(LONGLONG Freq)
{
    unsigned long i;
    LARGE_INTEGER T0, T1;
    LONGLONG LibTicks, RefTicks;
    ULONGLONG LibBps, RefBps, Bytes;

    for (i = 0; i < STR_BENCH_LEN; ++i)
        gStrBuf[i] = (char)('a' + (i & 31));
    gStrBuf[STR_BENCH_LEN] = '\0';

    QueryPerformanceCounter(&T0);
    for (i = 0; i < STR_BENCH_ITERS; ++i)
        gStrSink += gLibStrlen(gStrBuf);
    QueryPerformanceCounter(&T1);
    LibTicks = T1.QuadPart - T0.QuadPart;

    QueryPerformanceCounter(&T0);
    for (i = 0; i < STR_BENCH_ITERS; ++i)
        gStrSink += gRefStrlen(gStrBuf);
    QueryPerformanceCounter(&T1);
    RefTicks = T1.QuadPart - T0.QuadPart;

    Bytes = (ULONGLONG)STR_BENCH_ITERS * STR_BENCH_LEN;
    LibBps = (LibTicks > 0) ? (Bytes * (ULONGLONG)Freq) / (ULONGLONG)LibTicks : 0;
    RefBps = (RefTicks > 0) ? (Bytes * (ULONGLONG)Freq) / (ULONGLONG)RefTicks : 0;

    emit("[cpubench] strlen library(msvcrt): %I64u MB/s\n", LibBps / 1000000ULL);
    emit("[cpubench] strlen ref C byte-loop: %I64u MB/s\n", RefBps / 1000000ULL);
    if (RefBps > 0)
        emit("[cpubench] strlen lib/refC speedup: x%I64u.%02I64u\n",
             LibBps / RefBps, ((LibBps % RefBps) * 100ULL) / RefBps);
}

int main(int argc, char **argv)
{
    SYSTEM_INFO Si;
    LARGE_INTEGER Freq;
    unsigned NumCpus;
    unsigned long RequestedCpus;
    char *End;

    GetSystemInfo(&Si);
    NumCpus = Si.dwNumberOfProcessors;
    if (NumCpus > MAX_CPUS)
        NumCpus = MAX_CPUS;
    if (argc > 2)
    {
        emit("Usage: cpubench [logical-processors]\n");
        return 2;
    }
    if (argc == 2)
    {
        RequestedCpus = strtoul(argv[1], &End, 10);
        if (argv[1][0] == '\0' || *End != '\0' || RequestedCpus == 0 || RequestedCpus > NumCpus)
        {
            emit("[cpubench] logical-processors must be between 1 and %u\n", NumCpus);
            emit("Usage: cpubench [logical-processors]\n");
            return 2;
        }
        NumCpus = (unsigned)RequestedCpus;
    }
    if (!QueryPerformanceFrequency(&Freq) || Freq.QuadPart == 0)
    {
        emit("[cpubench] no QueryPerformanceFrequency -- abort\n");
        return 1;
    }

    emit("[cpubench] ===== ReactOS CPU benchmark =====\n");
    emit("[cpubench] baseline RPi5/Linux: Dhrystone ~20570 VAX-MIPS (8.57 DMIPS/MHz @2.4GHz), ~x4.0 on 4 cores\n");
    RunBench("Dhrystone", 0, 1, Freq.QuadPart, NumCpus);
    RunBench("FP", 1, FP_FLOPS_PER_ITER, Freq.QuadPart, NumCpus);
    RunStringBench(Freq.QuadPart);
    emit("[cpubench] ===== done =====\n");
    return 0;
}
