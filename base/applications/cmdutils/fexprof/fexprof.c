/*
 * PROJECT:     ReactOS FEX/ARM64EC profiler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Sample where an emulated x86-64 process spends its time
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * An x64 process under FEX pays for every exception the emulator takes to
 * implement x86 semantics -- unaligned atomics lowered to ARM64 exclusives,
 * and the write-protected code pages that back self-modifying-code detection.
 * Those are exception dispatches, not I/O and not guest work, so a run that
 * "feels like it is reading from storage" has to be told apart from one that
 * is really faulting.  This samples both sides and prints the deltas:
 *
 *   - per-process file I/O operations and bytes (IO_COUNTERS)
 *   - per-process user and kernel CPU time
 *   - system-wide exception dispatches and alignment fixups
 *   - per-CPU idle/kernel/user/DPC/interrupt time
 *
 * Every number is a kernel counter that already exists; nothing here changes
 * what it measures.  Output goes to the debugger so it reaches the serial log
 * without passing through a pipe, whose block buffering would hide it.
 *
 * Usage: fexprof [-i <ms>] [-n <samples>] [<process-name-substring>]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <ndk/exfuncs.h>
#include <ndk/rtlfuncs.h>

#define FEXPROF_DEFAULT_INTERVAL_MS 1000
#define FEXPROF_MAX_CPUS            64

typedef struct _FEXPROF_PROCESS_SAMPLE
{
    BOOLEAN Found;
    ULONG ProcessId;
    ULONGLONG UserTime100ns;
    ULONGLONG KernelTime100ns;
    ULONGLONG ReadOperations;
    ULONGLONG WriteOperations;
    ULONGLONG OtherOperations;
    ULONGLONG ReadBytes;
    ULONGLONG WriteBytes;
    ULONGLONG OtherBytes;
    ULONGLONG PageFaults;
    ULONGLONG WorkingSet;
} FEXPROF_PROCESS_SAMPLE;

#define FEXPROF_MAX_TRACKED 512
#define FEXPROF_TOP_COUNT    5

typedef struct _FEXPROF_PROCESS_SLOT
{
    ULONG ProcessId;
    ULONGLONG Cpu100ns;
    CHAR Name[32];
} FEXPROF_PROCESS_SLOT;

typedef struct _FEXPROF_SYSTEM_SAMPLE
{
    ULONG CpuCount;
    ULONGLONG Idle100ns;
    ULONGLONG Kernel100ns;
    ULONGLONG User100ns;
    ULONGLONG Dpc100ns;
    ULONGLONG Interrupt100ns;
    ULONGLONG InterruptCount;
    ULONGLONG ExceptionDispatches;
    ULONGLONG AlignmentFixups;
} FEXPROF_SYSTEM_SAMPLE;

static VOID
FexProfPrint(PCSTR Format, ...)
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

static PVOID
FexProfQuery(SYSTEM_INFORMATION_CLASS Class, PULONG ReturnedLength)
{
    NTSTATUS Status;
    ULONG Length = 0x10000;
    PVOID Buffer;

    for (;;)
    {
        Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Length);
        if (Buffer == NULL)
            return NULL;

        Status = NtQuerySystemInformation(Class, Buffer, Length, ReturnedLength);
        if (NT_SUCCESS(Status))
            return Buffer;

        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        if (Status != STATUS_INFO_LENGTH_MISMATCH && Status != STATUS_BUFFER_TOO_SMALL)
            return NULL;

        Length *= 2;
        if (Length > 0x2000000)
            return NULL;
    }
}

static BOOLEAN
FexProfImageNameMatches(PCUNICODE_STRING ImageName, PCSTR Needle)
{
    CHAR Name[256];
    ULONG Index;
    ULONG Count;

    if (ImageName->Buffer == NULL || ImageName->Length == 0)
        return FALSE;

    Count = ImageName->Length / sizeof(WCHAR);
    if (Count >= sizeof(Name))
        Count = sizeof(Name) - 1;

    for (Index = 0; Index < Count; ++Index)
    {
        WCHAR Char = ImageName->Buffer[Index];
        if (Char >= L'A' && Char <= L'Z')
            Char += (L'a' - L'A');
        Name[Index] = (Char < 0x80) ? (CHAR)Char : '?';
    }
    Name[Count] = '\0';

    return (strstr(Name, Needle) != NULL);
}

static VOID
FexProfReportThreads(PSYSTEM_PROCESS_INFORMATION Info)
{
    static PCSTR const WaitReasons[] =
    {
        "Executive", "FreePage", "PageIn", "PoolAllocation", "DelayExecution",
        "Suspended", "UserRequest", "WrExecutive", "WrFreePage", "WrPageIn",
        "WrPoolAllocation", "WrDelayExecution", "WrSuspended", "WrUserRequest",
        "WrEventPair", "WrQueue", "WrLpcReceive", "WrLpcReply", "WrVirtualMemory",
        "WrPageOut", "WrRendezvous", "WrKeyedEvent", "WrTerminated",
        "WrProcessInSwap", "WrCpuRateControl", "WrCalloutStack", "WrKernel",
        "WrResource", "WrPushLock", "WrMutex", "WrQuantumEnd", "WrDispatchInt",
        "WrPreempted", "WrYieldExecution", "WrFastMutex", "WrGuardedMutex",
        "WrRundown"
    };
    static PCSTR const States[] =
    {
        "Initialized", "Ready", "Running", "Standby", "Terminated", "Waiting",
        "Transition", "DeferredReady", "GateWait"
    };
    PSYSTEM_THREAD_INFORMATION Thread = (PSYSTEM_THREAD_INFORMATION)(Info + 1);
    ULONG Index;

    for (Index = 0; Index < Info->NumberOfThreads; ++Index)
    {
        ULONG State = (ULONG)Thread[Index].ThreadState;
        ULONG Reason = (ULONG)Thread[Index].WaitReason;

        FexProfPrint("FEXPROF_THREAD tid=%lu start=%p state=%s wait=%s pri=%ld/%ld switches=%lu user_ms=%lu kernel_ms=%lu\n",
                     (unsigned long)(ULONG_PTR)Thread[Index].ClientId.UniqueThread,
                     Thread[Index].StartAddress,
                     State < RTL_NUMBER_OF(States) ? States[State] : "?",
                     Reason < RTL_NUMBER_OF(WaitReasons) ? WaitReasons[Reason] : "?",
                     (long)Thread[Index].Priority, (long)Thread[Index].BasePriority,
                     (unsigned long)Thread[Index].ContextSwitches,
                     (unsigned long)(Thread[Index].UserTime.QuadPart / 10000),
                     (unsigned long)(Thread[Index].KernelTime.QuadPart / 10000));
    }
}

static VOID
FexProfSampleProcess(PCSTR Needle, FEXPROF_PROCESS_SAMPLE *Sample)
{
    PSYSTEM_PROCESS_INFORMATION Info;
    PVOID Buffer;
    ULONG Length = 0;

    RtlZeroMemory(Sample, sizeof(*Sample));

    Buffer = FexProfQuery(SystemProcessInformation, &Length);
    if (Buffer == NULL)
        return;

    Info = (PSYSTEM_PROCESS_INFORMATION)Buffer;
    for (;;)
    {
        if (FexProfImageNameMatches(&Info->ImageName, Needle))
        {
            Sample->Found = TRUE;
            Sample->ProcessId = (ULONG)(ULONG_PTR)Info->UniqueProcessId;
            Sample->UserTime100ns = (ULONGLONG)Info->UserTime.QuadPart;
            Sample->KernelTime100ns = (ULONGLONG)Info->KernelTime.QuadPart;
            Sample->ReadOperations = (ULONGLONG)Info->ReadOperationCount.QuadPart;
            Sample->WriteOperations = (ULONGLONG)Info->WriteOperationCount.QuadPart;
            Sample->OtherOperations = (ULONGLONG)Info->OtherOperationCount.QuadPart;
            Sample->ReadBytes = (ULONGLONG)Info->ReadTransferCount.QuadPart;
            Sample->WriteBytes = (ULONGLONG)Info->WriteTransferCount.QuadPart;
            Sample->OtherBytes = (ULONGLONG)Info->OtherTransferCount.QuadPart;
            Sample->PageFaults = Info->PageFaultCount;
            Sample->WorkingSet = (ULONGLONG)Info->WorkingSetSize;
            FexProfReportThreads(Info);
            break;
        }

        if (Info->NextEntryOffset == 0)
            break;
        Info = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)Info + Info->NextEntryOffset);
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
}

static ULONGLONG FexProfDelta(ULONGLONG Now, ULONGLONG Before);

static FEXPROF_PROCESS_SLOT g_Previous[FEXPROF_MAX_TRACKED];
static ULONG g_PreviousCount;

static VOID
FexProfCopyImageName(PCUNICODE_STRING ImageName, PCHAR Name, ULONG Size)
{
    ULONG Count;
    ULONG Index;

    Name[0] = '\0';
    if (ImageName->Buffer == NULL || ImageName->Length == 0)
    {
        strncpy(Name, "(idle)", Size - 1);
        Name[Size - 1] = '\0';
        return;
    }

    Count = ImageName->Length / sizeof(WCHAR);
    if (Count >= Size)
        Count = Size - 1;
    for (Index = 0; Index < Count; ++Index)
    {
        WCHAR Char = ImageName->Buffer[Index];
        Name[Index] = (Char < 0x80) ? (CHAR)Char : '?';
    }
    Name[Count] = '\0';
}

static VOID
FexProfReportTop(VOID)
{
    FEXPROF_PROCESS_SLOT Current[FEXPROF_MAX_TRACKED];
    ULONGLONG Delta[FEXPROF_MAX_TRACKED];
    PSYSTEM_PROCESS_INFORMATION Info;
    PVOID Buffer;
    ULONG Length = 0;
    ULONG Count = 0;
    ULONG Index;
    ULONG Inner;
    ULONG Reported;

    Buffer = FexProfQuery(SystemProcessInformation, &Length);
    if (Buffer == NULL)
        return;

    Info = (PSYSTEM_PROCESS_INFORMATION)Buffer;
    for (;;)
    {
        if (Count < FEXPROF_MAX_TRACKED)
        {
            Current[Count].ProcessId = (ULONG)(ULONG_PTR)Info->UniqueProcessId;
            Current[Count].Cpu100ns = (ULONGLONG)Info->UserTime.QuadPart +
                                      (ULONGLONG)Info->KernelTime.QuadPart;
            FexProfCopyImageName(&Info->ImageName, Current[Count].Name,
                                 sizeof(Current[Count].Name));
            ++Count;
        }
        if (Info->NextEntryOffset == 0)
            break;
        Info = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)Info + Info->NextEntryOffset);
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);

    for (Index = 0; Index < Count; ++Index)
    {
        Delta[Index] = 0;
        for (Inner = 0; Inner < g_PreviousCount; ++Inner)
        {
            if (g_Previous[Inner].ProcessId == Current[Index].ProcessId)
            {
                Delta[Index] = FexProfDelta(Current[Index].Cpu100ns,
                                            g_Previous[Inner].Cpu100ns);
                break;
            }
        }
    }

    for (Reported = 0; Reported < FEXPROF_TOP_COUNT; ++Reported)
    {
        ULONG Best = Count;
        for (Index = 0; Index < Count; ++Index)
        {
            if (Delta[Index] == 0)
                continue;
            if (Best == Count || Delta[Index] > Delta[Best])
                Best = Index;
        }
        if (Best == Count)
            break;

        FexProfPrint("FEXPROF_TOP  %-24s pid=%-6lu cpu_ms=%llu\n",
                     Current[Best].Name,
                     (unsigned long)Current[Best].ProcessId,
                     (unsigned long long)(Delta[Best] / 10000ULL));
        Delta[Best] = 0;
    }

    for (Index = 0; Index < Count; ++Index)
        g_Previous[Index] = Current[Index];
    g_PreviousCount = Count;
}

static VOID
FexProfSampleSystem(FEXPROF_SYSTEM_SAMPLE *Sample)
{
    SYSTEM_EXCEPTION_INFORMATION Exceptions;
    PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION Cpus;
    PVOID Buffer;
    ULONG Length = 0;
    ULONG Count;
    ULONG Index;

    RtlZeroMemory(Sample, sizeof(*Sample));

    Buffer = FexProfQuery(SystemProcessorPerformanceInformation, &Length);
    if (Buffer != NULL)
    {
        Cpus = (PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)Buffer;
        Count = Length / sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION);
        if (Count > FEXPROF_MAX_CPUS)
            Count = FEXPROF_MAX_CPUS;

        Sample->CpuCount = Count;
        for (Index = 0; Index < Count; ++Index)
        {
            Sample->Idle100ns += (ULONGLONG)Cpus[Index].IdleTime.QuadPart;
            Sample->Kernel100ns += (ULONGLONG)Cpus[Index].KernelTime.QuadPart;
            Sample->User100ns += (ULONGLONG)Cpus[Index].UserTime.QuadPart;
            Sample->Dpc100ns += (ULONGLONG)Cpus[Index].DpcTime.QuadPart;
            Sample->Interrupt100ns += (ULONGLONG)Cpus[Index].InterruptTime.QuadPart;
            Sample->InterruptCount += Cpus[Index].InterruptCount;
        }
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
    }

    RtlZeroMemory(&Exceptions, sizeof(Exceptions));
    if (NT_SUCCESS(NtQuerySystemInformation(SystemExceptionInformation,
                                            &Exceptions,
                                            sizeof(Exceptions),
                                            NULL)))
    {
        Sample->ExceptionDispatches = Exceptions.ExceptionDispatchCount;
        Sample->AlignmentFixups = Exceptions.AlignmentFixupCount;
    }
}

static ULONGLONG
FexProfDelta(ULONGLONG Now, ULONGLONG Before)
{
    return (Now >= Before) ? (Now - Before) : 0;
}

int __cdecl main(int argc, char *argv[])
{
    FEXPROF_PROCESS_SAMPLE ProcessNow, ProcessBefore;
    FEXPROF_SYSTEM_SAMPLE SystemNow, SystemBefore;
    const char *Needle = "mupen64plus";
    ULONG IntervalMs = FEXPROF_DEFAULT_INTERVAL_MS;
    ULONG Samples = 0;
    ULONG Taken = 0;
    int Index;

    for (Index = 1; Index < argc; ++Index)
    {
        if (strcmp(argv[Index], "-i") == 0 && Index + 1 < argc)
            IntervalMs = (ULONG)strtoul(argv[++Index], NULL, 10);
        else if (strcmp(argv[Index], "-n") == 0 && Index + 1 < argc)
            Samples = (ULONG)strtoul(argv[++Index], NULL, 10);
        else
            Needle = argv[Index];
    }

    if (IntervalMs == 0)
        IntervalMs = FEXPROF_DEFAULT_INTERVAL_MS;

    FexProfPrint("FEXPROF_BEGIN target=%s interval=%lums samples=%lu\n",
                 Needle, (unsigned long)IntervalMs, (unsigned long)Samples);

    FexProfSampleProcess(Needle, &ProcessBefore);
    FexProfSampleSystem(&SystemBefore);

    for (;;)
    {
        ULONGLONG CpuTotal;
        ULONGLONG Busy;

        Sleep(IntervalMs);

        FexProfSampleProcess(Needle, &ProcessNow);
        FexProfSampleSystem(&SystemNow);

        CpuTotal = FexProfDelta(SystemNow.Idle100ns, SystemBefore.Idle100ns) +
                   FexProfDelta(SystemNow.Kernel100ns, SystemBefore.Kernel100ns) +
                   FexProfDelta(SystemNow.User100ns, SystemBefore.User100ns);
        Busy = FexProfDelta(SystemNow.Kernel100ns, SystemBefore.Kernel100ns) +
               FexProfDelta(SystemNow.User100ns, SystemBefore.User100ns);

        FexProfPrint("FEXPROF_SYS busy=%llu%% exc=%llu align=%llu int=%llu dpc_ms=%llu irq_ms=%llu\n",
                     (unsigned long long)(CpuTotal ? (Busy * 100ULL) / CpuTotal : 0ULL),
                     (unsigned long long)FexProfDelta(SystemNow.ExceptionDispatches, SystemBefore.ExceptionDispatches),
                     (unsigned long long)FexProfDelta(SystemNow.AlignmentFixups, SystemBefore.AlignmentFixups),
                     (unsigned long long)FexProfDelta(SystemNow.InterruptCount, SystemBefore.InterruptCount),
                     (unsigned long long)(FexProfDelta(SystemNow.Dpc100ns, SystemBefore.Dpc100ns) / 10000ULL),
                     (unsigned long long)(FexProfDelta(SystemNow.Interrupt100ns, SystemBefore.Interrupt100ns) / 10000ULL));

        if (ProcessNow.Found)
        {
            FexProfPrint("FEXPROF_PROC pid=%lu user_ms=%llu kern_ms=%llu rd=%llu rdKB=%llu wr=%llu wrKB=%llu oth=%llu flt=%llu wsKB=%llu\n",
                         (unsigned long)ProcessNow.ProcessId,
                         (unsigned long long)(FexProfDelta(ProcessNow.UserTime100ns, ProcessBefore.UserTime100ns) / 10000ULL),
                         (unsigned long long)(FexProfDelta(ProcessNow.KernelTime100ns, ProcessBefore.KernelTime100ns) / 10000ULL),
                         (unsigned long long)FexProfDelta(ProcessNow.ReadOperations, ProcessBefore.ReadOperations),
                         (unsigned long long)(FexProfDelta(ProcessNow.ReadBytes, ProcessBefore.ReadBytes) / 1024ULL),
                         (unsigned long long)FexProfDelta(ProcessNow.WriteOperations, ProcessBefore.WriteOperations),
                         (unsigned long long)(FexProfDelta(ProcessNow.WriteBytes, ProcessBefore.WriteBytes) / 1024ULL),
                         (unsigned long long)FexProfDelta(ProcessNow.OtherOperations, ProcessBefore.OtherOperations),
                         (unsigned long long)FexProfDelta(ProcessNow.PageFaults, ProcessBefore.PageFaults),
                         (unsigned long long)(ProcessNow.WorkingSet / 1024ULL));
        }
        else
        {
            FexProfPrint("FEXPROF_PROC absent target=%s\n", Needle);
        }

        FexProfReportTop();

        ProcessBefore = ProcessNow;
        SystemBefore = SystemNow;

        ++Taken;
        if (Samples != 0 && Taken >= Samples)
            break;
    }

    FexProfPrint("FEXPROF_END samples=%lu\n", (unsigned long)Taken);
    return 0;
}
