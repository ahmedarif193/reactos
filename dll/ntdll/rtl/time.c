/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     NTDLL time query wrappers
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64) && (defined(__GNUC__) || defined(__clang__))
#define RTL_ARM64_QPC_BYPASS_CONFIGURATION 0x0001

static
BOOLEAN
RtlpQueryArm64PerformanceCounter(PLARGE_INTEGER Counter)
{
    ULONGLONG Bias, Value;
    USHORT Configuration;

    Configuration = *(volatile USHORT *)&SharedUserData->QpcData;
    if (Configuration != RTL_ARM64_QPC_BYPASS_CONFIGURATION)
        return FALSE;

    __asm__ __volatile__("dmb ishld" ::: "memory");
    Bias = SharedUserData->QpcBias;
    __asm__ __volatile__("isb\n\tmrs %0, cntpct_el0" : "=r"(Value) :: "memory");
    __asm__ __volatile__("dmb ishld" ::: "memory");

    if (Configuration != *(volatile USHORT *)&SharedUserData->QpcData)
        return FALSE;

    Counter->QuadPart = (LONGLONG)(Value + Bias);
    return TRUE;
}

static
BOOLEAN
RtlpQueryArm64PerformanceFrequency(PLARGE_INTEGER Frequency)
{
    ULONGLONG Value;
    USHORT Configuration;

    Configuration = *(volatile USHORT *)&SharedUserData->QpcData;
    if (Configuration != RTL_ARM64_QPC_BYPASS_CONFIGURATION)
        return FALSE;

    __asm__ __volatile__("dmb ishld" ::: "memory");
    Value = *(volatile ULONGLONG *)&SharedUserData->QpcFrequency;
    __asm__ __volatile__("dmb ishld" ::: "memory");
    if ((Configuration != *(volatile USHORT *)&SharedUserData->QpcData) || !Value)
        return FALSE;

    Frequency->QuadPart = (LONGLONG)Value;
    return TRUE;
}
#endif

BOOL
WINAPI
RtlQueryPerformanceCounter(PLARGE_INTEGER Counter)
{
    NTSTATUS Status;
#if defined(_M_ARM64) && (defined(__GNUC__) || defined(__clang__))
    LARGE_INTEGER Value;
#endif

    if (!Counter)
        return FALSE;

#if defined(_M_ARM64) && (defined(__GNUC__) || defined(__clang__))
    if (RtlpQueryArm64PerformanceCounter(&Value))
    {
        _SEH2_TRY
        {
            *Counter = Value;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(goto SlowPath);
        }
        _SEH2_END;
        return TRUE;
    }

SlowPath:
#endif
    Status = NtQueryPerformanceCounter(Counter, NULL);
    return NT_SUCCESS(Status);
}

BOOL
WINAPI
RtlQueryPerformanceFrequency(PLARGE_INTEGER Frequency)
{
    LARGE_INTEGER Counter;
    NTSTATUS Status;
#if defined(_M_ARM64) && (defined(__GNUC__) || defined(__clang__))
    LARGE_INTEGER Value;
#endif

    if (!Frequency)
        return FALSE;

#if defined(_M_ARM64) && (defined(__GNUC__) || defined(__clang__))
    if (RtlpQueryArm64PerformanceFrequency(&Value))
    {
        _SEH2_TRY
        {
            *Frequency = Value;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(goto SlowPath);
        }
        _SEH2_END;
        return TRUE;
    }

SlowPath:
#endif
    Status = NtQueryPerformanceCounter(&Counter, Frequency);
    return NT_SUCCESS(Status);
}

VOID
WINAPI
RtlQuerySystemTime(PLARGE_INTEGER SystemTime)
{
    if (SystemTime)
        NtQuerySystemTime(SystemTime);
}

VOID
WINAPI
RtlSystemTimeToTimeFields(const LARGE_INTEGER *SystemTime,
                          PTIME_FIELDS TimeFields)
{
    if (!SystemTime || !TimeFields)
        return;

    RtlTimeToTimeFields((PLARGE_INTEGER)SystemTime, TimeFields);
}
