/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ndk/ntndk.h>
#include <intrin.h>

typedef struct _SMP_TEST SMP_TEST;
typedef struct _SMP_CPU
{
    SMP_TEST *Test;
    HANDLE Thread;
    DWORD Number;
    volatile LONG Completed;
    ULONG Errors;
} SMP_CPU;

struct _SMP_TEST
{
    volatile LONG Generation;
    volatile LONG Stop;
    volatile ULONG *Data;
    ULONG *Code;
    SMP_CPU Cpus[64];
};

static DWORD WINAPI SmpCpu(PVOID Argument)
{
    SMP_CPU *Cpu = Argument;
    SMP_TEST *Test = Cpu->Test;
    LONG Generation = 0, Next;
    ULONG (*Function)(VOID) = (PVOID)Test->Code;
    if (!SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << Cpu->Number))
        ++Cpu->Errors;
    while (!InterlockedCompareExchange(&Test->Stop, 0, 0))
    {
        Next = InterlockedCompareExchange(&Test->Generation, 0, 0);
        if (Next == Generation) { YieldProcessor(); continue; }
        Generation = Next;
        __try
        {
            Test->Data[Cpu->Number] = Generation;
            if (Generation & 1) ++Cpu->Errors; /* read-only must fault */
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            if (!(Generation & 1) || GetExceptionCode() != STATUS_ACCESS_VIOLATION)
                ++Cpu->Errors;
        }
        if (Function() != (ULONG)Generation) ++Cpu->Errors;
        InterlockedExchange(&Cpu->Completed, Generation);
    }
    return Cpu->Errors;
}

ULONG RunSmpTests(VOID)
{
    SMP_TEST *Test;
    DWORD_PTR ProcessMask, SystemMask, Original;
    DWORD Number, First = 0, Count = 0, OldProtect, Started;
    LONG Generation;
    ULONG Errors = 0;
    BOOLEAN Complete;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask))
        return 1;
    while (!(ProcessMask & ((DWORD_PTR)1 << First))) ++First;
    Original = SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << First);
    if (!Original) return 1;
    Test = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Test));
    if (!Test) { SetThreadAffinityMask(GetCurrentThread(), Original); return 1; }
    Test->Data = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Test->Code = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!Test->Data || !Test->Code) { Errors = 1; goto Done; }
    for (Number = 0; Number < 64; ++Number)
    {
        SMP_CPU *Cpu = &Test->Cpus[Number];
        if (!(ProcessMask & ((DWORD_PTR)1 << Number)) || Number == First) continue;
        Cpu->Number = Number;
        Cpu->Test = Test;
        Cpu->Thread = CreateThread(NULL, 0, SmpCpu, Cpu, 0, NULL);
        if (!Cpu->Thread) ++Errors;
        else ++Count;
    }
    DbgPrint("RISCVSMPUSER: BEGIN remote=%lu mask=%Ix\n", Count, ProcessMask);
    for (Generation = 1; Generation <= 64 && Count; ++Generation)
    {
        if (!VirtualProtect((PVOID)Test->Data, 4096,
                            (Generation & 1) ? PAGE_READONLY : PAGE_READWRITE, &OldProtect) ||
            !VirtualProtect(Test->Code, 4096, PAGE_READWRITE, &OldProtect))
        { ++Errors; break; }
        Test->Code[0] = ((ULONG)Generation << 20) | 0x513; /* addi a0, zero, n */
        Test->Code[1] = 0x00008067;                       /* ret */
        if (!VirtualProtect(Test->Code, 4096, PAGE_EXECUTE_READ, &OldProtect) ||
            !FlushInstructionCache(GetCurrentProcess(), Test->Code, 8))
        { ++Errors; break; }
        InterlockedExchange(&Test->Generation, Generation);
        Started = GetTickCount();
        do
        {
            Complete = TRUE;
            for (Number = 0; Number < 64; ++Number)
                if (Test->Cpus[Number].Thread &&
                    InterlockedCompareExchange(&Test->Cpus[Number].Completed, 0, 0) != Generation)
                    Complete = FALSE;
            if (GetTickCount() - Started > 30000)
            {
                DbgPrint("RISCVSMPUSER: FAIL generation=%ld timed out\n", Generation);
                ++Errors;
                goto Stop;
            }
            YieldProcessor();
        } while (!Complete);
    }
Stop:
    InterlockedExchange(&Test->Stop, 1);
    for (Number = 0; Number < 64; ++Number)
    {
        SMP_CPU *Cpu = &Test->Cpus[Number];
        if (!Cpu->Thread) continue;
        /* Keep the worker's data resident until it has actually exited. */
        WaitForSingleObject(Cpu->Thread, INFINITE);
        Errors += Cpu->Errors;
        CloseHandle(Cpu->Thread);
    }
    DbgPrint("RISCVSMPUSER: END remote=%lu generations=%ld failures=%lu\n",
             Count, Generation - 1, Errors);
Done:
    if (Test->Code) VirtualFree(Test->Code, 0, MEM_RELEASE);
    if (Test->Data) VirtualFree((PVOID)Test->Data, 0, MEM_RELEASE);
    HeapFree(GetProcessHeap(), 0, Test);
    SetThreadAffinityMask(GetCurrentThread(), Original);
    return Errors;
}
