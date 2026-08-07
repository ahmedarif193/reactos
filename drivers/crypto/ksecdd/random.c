/*
 * PROJECT:     ReactOS Kernel Security Support Provider Interface Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Random number generation and entropy gathering
 * COPYRIGHT:   Copyright 2014-2017 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES *******************************************************************/

#include "ksecdd.h"

#define NDEBUG
#include <debug.h>


/* GLOBALS ********************************************************************/

static FAST_MUTEX KsecRandomLock;
static ULONG KsecRandomKey[8];
static ULONG64 KsecRandomBlockCounter;

static const ULONG KsecRandomSigma[4] = { 0x61707865, 0x3320646E, 0x79622D32, 0x6B206574 };

#define KSEC_ROTL(Value, Count) (((Value) << (Count)) | ((Value) >> (32 - (Count))))

#define KSEC_QUARTER_ROUND(State, A, B, C, D) \
do { \
    State[A] += State[B]; State[D] = KSEC_ROTL(State[D] ^ State[A], 16); \
    State[C] += State[D]; State[B] = KSEC_ROTL(State[B] ^ State[C], 12); \
    State[A] += State[B]; State[D] = KSEC_ROTL(State[D] ^ State[A], 8); \
    State[C] += State[D]; State[B] = KSEC_ROTL(State[B] ^ State[C], 7); \
} while (0)


/* FUNCTIONS ******************************************************************/

static
VOID
KsecRandomCore(
    _In_reads_(16) const ULONG Input[16],
    _Out_writes_(16) ULONG Output[16])
{
    ULONG State[16];
    ULONG i;

    RtlCopyMemory(State, Input, sizeof(State));

    for (i = 0; i < 10; i++)
    {
        KSEC_QUARTER_ROUND(State, 0, 4, 8, 12);
        KSEC_QUARTER_ROUND(State, 1, 5, 9, 13);
        KSEC_QUARTER_ROUND(State, 2, 6, 10, 14);
        KSEC_QUARTER_ROUND(State, 3, 7, 11, 15);
        KSEC_QUARTER_ROUND(State, 0, 5, 10, 15);
        KSEC_QUARTER_ROUND(State, 1, 6, 11, 12);
        KSEC_QUARTER_ROUND(State, 2, 7, 8, 13);
        KSEC_QUARTER_ROUND(State, 3, 4, 9, 14);
    }

    for (i = 0; i < RTL_NUMBER_OF(State); i++)
    {
        Output[i] = State[i] + Input[i];
    }

    RtlSecureZeroMemory(State, sizeof(State));
}

static
VOID
KsecRandomPermutePool(
    _Inout_updates_(16) ULONG Pool[16])
{
    ULONG Mixed[16];

    KsecRandomCore(Pool, Mixed);
    RtlCopyMemory(Pool, Mixed, sizeof(Mixed));
    RtlSecureZeroMemory(Mixed, sizeof(Mixed));
}

static
VOID
KsecRandomAbsorb(
    _Inout_updates_(16) ULONG Pool[16],
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ SIZE_T Length)
{
    const UCHAR *Bytes = Data;
    PUCHAR Rate = (PUCHAR)&Pool[4];
    SIZE_T Offset = 0;
    SIZE_T i;

    for (i = 0; i < Length; i++)
    {
        Rate[Offset] ^= Bytes[i];
        if (++Offset == 32)
        {
            KsecRandomPermutePool(Pool);
            Offset = 0;
        }
    }

    Rate[Offset] ^= 1;
    Rate[31] ^= 0x80;
    KsecRandomPermutePool(Pool);
}

VOID
NTAPI
KsecInitializeRandomSupport(VOID)
{
    KSEC_ENTROPY_DATA EntropyData;
    LARGE_INTEGER PerformanceCounter;
    LARGE_INTEGER PerformanceFrequency;
    LARGE_INTEGER SystemTime;
    LARGE_INTEGER TickCount;
    ULONG_PTR PoolAddress;
    ULONG Pool[16];

    ExInitializeFastMutex(&KsecRandomLock);
    RtlCopyMemory(Pool, KsecRandomSigma, sizeof(KsecRandomSigma));
    RtlZeroMemory(&Pool[4], sizeof(Pool) - sizeof(KsecRandomSigma));

    RtlZeroMemory(&EntropyData, sizeof(EntropyData));
    KsecGatherEntropyData(&EntropyData);
    KsecRandomAbsorb(Pool, &EntropyData, sizeof(EntropyData));

    RtlZeroMemory(&EntropyData, sizeof(EntropyData));
    KsecGatherEntropyData(&EntropyData);
    KsecRandomAbsorb(Pool, &EntropyData, sizeof(EntropyData));

    KeQuerySystemTime(&SystemTime);
    KeQueryTickCount(&TickCount);
    PerformanceCounter = KeQueryPerformanceCounter(&PerformanceFrequency);
    KsecRandomAbsorb(Pool, &SystemTime, sizeof(SystemTime));
    KsecRandomAbsorb(Pool, &TickCount, sizeof(TickCount));
    KsecRandomAbsorb(Pool, &PerformanceCounter, sizeof(PerformanceCounter));
    KsecRandomAbsorb(Pool, &PerformanceFrequency, sizeof(PerformanceFrequency));
    PoolAddress = (ULONG_PTR)Pool;
    KsecRandomAbsorb(Pool, &PoolAddress, sizeof(PoolAddress));

    RtlCopyMemory(KsecRandomKey, &Pool[4], sizeof(KsecRandomKey));
    KsecRandomBlockCounter = (ULONG64)PerformanceCounter.QuadPart ^ (ULONG64)SystemTime.QuadPart;

    RtlSecureZeroMemory(&EntropyData, sizeof(EntropyData));
    RtlSecureZeroMemory(Pool, sizeof(Pool));
}

NTSTATUS
NTAPI
KsecGenRandom(
    PVOID Buffer,
    SIZE_T Length)
{
    LARGE_INTEGER PerformanceCounter;
    ULONG Input[16];
    ULONG Output[16];
    PUCHAR Destination = Buffer;
    SIZE_T Chunk;

    if (Length == 0)
    {
        return STATUS_SUCCESS;
    }

    if (Buffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ExAcquireFastMutex(&KsecRandomLock);

    RtlCopyMemory(Input, KsecRandomSigma, sizeof(KsecRandomSigma));
    RtlCopyMemory(&Input[4], KsecRandomKey, sizeof(KsecRandomKey));

    while (Length > 0)
    {
        PerformanceCounter = KeQueryPerformanceCounter(NULL);
        Input[12] = (ULONG)KsecRandomBlockCounter;
        Input[13] = (ULONG)(KsecRandomBlockCounter >> 32);
        Input[14] = PerformanceCounter.LowPart;
        Input[15] = PerformanceCounter.HighPart;
        KsecRandomBlockCounter++;

        KsecRandomCore(Input, Output);
        Chunk = min(Length, sizeof(Output));
        RtlCopyMemory(Destination, Output, Chunk);
        Destination += Chunk;
        Length -= Chunk;
    }

    PerformanceCounter = KeQueryPerformanceCounter(NULL);
    Input[12] = (ULONG)KsecRandomBlockCounter;
    Input[13] = (ULONG)(KsecRandomBlockCounter >> 32);
    Input[14] = PerformanceCounter.LowPart;
    Input[15] = PerformanceCounter.HighPart;
    KsecRandomBlockCounter++;
    KsecRandomCore(Input, Output);
    RtlCopyMemory(KsecRandomKey, Output, sizeof(KsecRandomKey));

    ExReleaseFastMutex(&KsecRandomLock);

    RtlSecureZeroMemory(Input, sizeof(Input));
    RtlSecureZeroMemory(Output, sizeof(Output));
    return STATUS_SUCCESS;
}

VOID
NTAPI
KsecReadMachineSpecificCounters(
    _Out_ PKSEC_MACHINE_SPECIFIC_COUNTERS MachineSpecificCounters)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    /* Check if RDTSC is available */
    if (ExIsProcessorFeaturePresent(PF_RDTSC_INSTRUCTION_AVAILABLE))
    {
        /* Read the TSC value */
        MachineSpecificCounters->Tsc = __rdtsc();
    }
#if 0 // FIXME: investigate what the requirements are for these
    /* Read the CPU event counter MSRs */
    //MachineSpecificCounters->Ctr0 = __readmsr(0x12);
    //MachineSpecificCounters->Ctr1 = __readmsr(0x13);

    /* Check if this is an MMX capable CPU */
    if (ExIsProcessorFeaturePresent(PF_MMX_INSTRUCTIONS_AVAILABLE))
    {
        /* Read the CPU performance counters 0 and 1 */
        MachineSpecificCounters->Pmc0 = __readpmc(0);
        MachineSpecificCounters->Pmc1 = __readpmc(1);
    }
#endif
#elif defined(_M_ARM)
    /* Read the Cycle Counter Register */
    MachineSpecificCounters->Ccr = _MoveFromCoprocessor(CP15_PMCCNTR);
#elif defined(_M_ARM64) || defined(__aarch64__)
    {
        ULONG64 cntvct;
        __asm__ volatile("mrs %0, CNTVCT_EL0" : "=r"(cntvct));
        *MachineSpecificCounters = (ULONG)cntvct;
    }
#else
    #error Implement me!
#endif
}

/*!
 *  \see http://blogs.msdn.com/b/michael_howard/archive/2005/01/14/353379.aspx (DEAD_LINK)
 */
NTSTATUS
NTAPI
KsecGatherEntropyData(
    PKSEC_ENTROPY_DATA EntropyData)
{
    MD4_CTX Md4Context;
    PTEB Teb;
    PPEB Peb;
    PWSTR String;
    ULONG ReturnLength;
    NTSTATUS Status;

    RtlZeroMemory(EntropyData, sizeof(*EntropyData));

    /* Query some generic values */
    EntropyData->CurrentProcessId = PsGetCurrentProcessId();
    EntropyData->CurrentThreadId = PsGetCurrentThreadId();
    KeQueryTickCount(&EntropyData->TickCount);
    KeQuerySystemTime(&EntropyData->SystemTime);
    EntropyData->PerformanceCounter = KeQueryPerformanceCounter(
                                            &EntropyData->PerformanceFrequency);

    /* Check if we have a TEB/PEB for the process environment */
    Teb = PsGetCurrentThread()->Tcb.Teb;
    if (Teb != NULL)
    {
        Peb = Teb->ProcessEnvironmentBlock;

        /* Initialize the MD4 context */
        MD4Init(&Md4Context);
        _SEH2_TRY
        {
            /* Get the end of the environment */
            String = Peb->ProcessParameters->Environment;
            while (*String)
            {
                String += wcslen(String) + 1;
            }

            /* Update the MD4 context from the environment data */
            MD4Update(&Md4Context,
                      (PUCHAR)Peb->ProcessParameters->Environment,
                      (ULONG)((PUCHAR)String - (PUCHAR)Peb->ProcessParameters->Environment));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Simply ignore the exception */
        }
        _SEH2_END;

        /* Finalize and copy the MD4 hash */
        MD4Final(&Md4Context);
        RtlCopyMemory(&EntropyData->EnvironmentHash, Md4Context.digest, 16);
    }

    /* Read some machine specific hardware counters */
    KsecReadMachineSpecificCounters(&EntropyData->MachineSpecificCounters);

    /* Query processor performance information */
    Status = ZwQuerySystemInformation(SystemProcessorPerformanceInformation,
                                      &EntropyData->SystemProcessorPerformanceInformation,
                                      sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION),
                                      &ReturnLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Query system performance information */
    Status = ZwQuerySystemInformation(SystemPerformanceInformation,
                                      &EntropyData->SystemPerformanceInformation,
                                      sizeof(SYSTEM_PERFORMANCE_INFORMATION),
                                      &ReturnLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Query exception information */
    Status = ZwQuerySystemInformation(SystemExceptionInformation,
                                      &EntropyData->SystemExceptionInformation,
                                      sizeof(SYSTEM_EXCEPTION_INFORMATION),
                                      &ReturnLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Query lookaside information */
    Status = ZwQuerySystemInformation(SystemLookasideInformation,
                                      &EntropyData->SystemLookasideInformation,
                                      sizeof(SYSTEM_LOOKASIDE_INFORMATION),
                                      &ReturnLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Query interrupt information */
    Status = ZwQuerySystemInformation(SystemInterruptInformation,
                                      &EntropyData->SystemInterruptInformation,
                                      sizeof(SYSTEM_INTERRUPT_INFORMATION),
                                      &ReturnLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Query process information */
    Status = ZwQuerySystemInformation(SystemProcessInformation,
                                      &EntropyData->SystemProcessInformation,
                                      sizeof(SYSTEM_PROCESS_INFORMATION),
                                      &ReturnLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return STATUS_SUCCESS;
}
