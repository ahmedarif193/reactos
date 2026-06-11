#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#ifdef CONFIG_SMP

#define KI_SMP_STRESS_BUGCHECK 0x534D5053UL

typedef struct _KI_SMP_STRESS_WORK
{
    KDPC Dpc;
    volatile LONG Done;
    ULONG Iterations;
    ULONG CpuIndex;
} KI_SMP_STRESS_WORK, *PKI_SMP_STRESS_WORK;

static volatile LONG KiSmpStressIpiCount;
static volatile LONG KiSmpStressDpcCount;
static volatile LONG64 KiSmpStressInterlocked;
static volatile ULONG64 KiSmpStressLocked;
static KSPIN_LOCK KiSmpStressLock;
static KI_SMP_STRESS_WORK KiSmpStressWork[MAXIMUM_PROCESSORS];

static ULONG64 KiSmpStressSeed;

static
ULONG64
KiSmpStressRand(VOID)
{
    ULONG64 x = KiSmpStressSeed;

    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    KiSmpStressSeed = x;
    return x;
}

static
ULONG_PTR
NTAPI
KiSmpStressIpiCallback(
    _In_ ULONG_PTR Argument)
{
    UNREFERENCED_PARAMETER(Argument);

    InterlockedIncrement(&KiSmpStressIpiCount);
    return 0;
}

static
VOID
NTAPI
KiSmpStressDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PKI_SMP_STRESS_WORK Work = DeferredContext;
    ULONG i;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Work->CpuIndex != KeGetCurrentProcessorNumber())
    {
        KeBugCheckEx(KI_SMP_STRESS_BUGCHECK,
                     1,
                     Work->CpuIndex,
                     KeGetCurrentProcessorNumber(),
                     0);
    }

    for (i = 0; i < Work->Iterations; i++)
    {
        KeAcquireSpinLockAtDpcLevel(&KiSmpStressLock);
        KiSmpStressLocked++;
        KeReleaseSpinLockFromDpcLevel(&KiSmpStressLock);

        InterlockedIncrement64(&KiSmpStressInterlocked);
    }

    InterlockedIncrement(&KiSmpStressDpcCount);
    InterlockedExchange(&Work->Done, 1);
}

static
VOID
KiSmpStressSpinWait(
    _In_ volatile LONG *Value,
    _In_ LONG Expected,
    _In_ ULONG_PTR Tag)
{
    ULONG64 Deadline = KeQueryInterruptTime() + 20000000ULL;

    while (*Value != Expected)
    {
        YieldProcessor();
        if (KeQueryInterruptTime() > Deadline)
        {
            KeBugCheckEx(KI_SMP_STRESS_BUGCHECK,
                         2,
                         Tag,
                         (ULONG_PTR)Expected,
                         (ULONG_PTR)*Value);
        }
    }
}

static
VOID
KiSmpStressIpiPhase(
    _In_ ULONG Processors)
{
    ULONG Rounds = (ULONG)(KiSmpStressRand() % 48) + 16;
    ULONG i;
    LONG Expected;

    InterlockedExchange(&KiSmpStressIpiCount, 0);
    for (i = 0; i < Rounds; i++)
    {
        KeIpiGenericCall(KiSmpStressIpiCallback, 0);
    }

    Expected = (LONG)(Rounds * Processors);
    if (KiSmpStressIpiCount != Expected)
    {
        KeBugCheckEx(KI_SMP_STRESS_BUGCHECK,
                     3,
                     (ULONG_PTR)Expected,
                     (ULONG_PTR)KiSmpStressIpiCount,
                     Rounds);
    }
}

static
VOID
KiSmpStressDpcPhase(
    _In_ ULONG Processors)
{
    ULONG Iterations = (ULONG)(KiSmpStressRand() % 1500) + 500;
    ULONG64 ExpectedLocked;
    ULONG Cpu;

    InterlockedExchange(&KiSmpStressDpcCount, 0);
    KiSmpStressLocked = 0;
    InterlockedExchange64(&KiSmpStressInterlocked, 0);

    for (Cpu = 0; Cpu < Processors; Cpu++)
    {
        PKI_SMP_STRESS_WORK Work = &KiSmpStressWork[Cpu];

        Work->Done = 0;
        Work->Iterations = Iterations;
        Work->CpuIndex = Cpu;
        KeInitializeDpc(&Work->Dpc, KiSmpStressDpcRoutine, Work);
        KeSetTargetProcessorDpc(&Work->Dpc, (CCHAR)Cpu);
        KeSetImportanceDpc(&Work->Dpc, HighImportance);

        if (!KeInsertQueueDpc(&Work->Dpc, NULL, NULL))
        {
            KeBugCheckEx(KI_SMP_STRESS_BUGCHECK, 4, Cpu, 0, 0);
        }
    }

    KiSmpStressSpinWait(&KiSmpStressDpcCount, (LONG)Processors, 5);

    ExpectedLocked = (ULONG64)Iterations * Processors;
    if (KiSmpStressLocked != ExpectedLocked)
    {
        KeBugCheckEx(KI_SMP_STRESS_BUGCHECK,
                     6,
                     (ULONG_PTR)ExpectedLocked,
                     (ULONG_PTR)KiSmpStressLocked,
                     Iterations);
    }

    if (KiSmpStressInterlocked != (LONG64)ExpectedLocked)
    {
        KeBugCheckEx(KI_SMP_STRESS_BUGCHECK,
                     7,
                     (ULONG_PTR)ExpectedLocked,
                     (ULONG_PTR)KiSmpStressInterlocked,
                     Iterations);
    }
}

static
VOID
KiSmpStressAffinityPhase(
    _In_ ULONG Processors)
{
    ULONG Cpu;

    for (Cpu = 0; Cpu < Processors; Cpu++)
    {
        KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
        if (KeGetCurrentProcessorNumber() != Cpu)
        {
            KeBugCheckEx(KI_SMP_STRESS_BUGCHECK,
                         8,
                         Cpu,
                         KeGetCurrentProcessorNumber(),
                         0);
        }
    }

    KeRevertToUserAffinityThread();
}

VOID
NTAPI
KiArm64SmpStress(VOID)
{
    ULONG Processors = KeNumberProcessors;
    ULONG64 Start;
    ULONG64 Budget;
    ULONG64 Elapsed;
    ULONG Round = 0;

    if (Processors < 2)
    {
        return;
    }

    KeInitializeSpinLock(&KiSmpStressLock);
    KiSmpStressSeed = KeQueryInterruptTime() | 1;

    Budget = ((KiSmpStressRand() % 6) + 5) * 10000000ULL;
    Start = KeQueryInterruptTime();

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "[arm64][SMPSTRESS] start cpus=%lu budget=%lums\n",
               Processors,
               (ULONG)(Budget / 10000));

    for (;;)
    {
        Elapsed = KeQueryInterruptTime() - Start;
        if (Elapsed >= Budget)
        {
            break;
        }

        switch (KiSmpStressRand() % 3)
        {
            case 0:
                KiSmpStressIpiPhase(Processors);
                break;
            case 1:
                KiSmpStressDpcPhase(Processors);
                break;
            default:
                KiSmpStressAffinityPhase(Processors);
                break;
        }

        Round++;
        if ((Round & 0x3F) == 0)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[arm64][SMPSTRESS] round=%lu elapsed=%lums\n",
                       Round,
                       (ULONG)(Elapsed / 10000));
        }
    }

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "[arm64][SMPSTRESS] PASS rounds=%lu elapsed=%lums ipi=%ld locked=%llu\n",
               Round,
               (ULONG)(Elapsed / 10000),
               (LONG)KiSmpStressIpiCount,
               (ULONG64)KiSmpStressLocked);
}

#else

VOID
NTAPI
KiArm64SmpStress(VOID)
{
}

#endif
