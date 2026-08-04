/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later
 * PURPOSE:         Internal-debugger log-stall watchdog
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KD_LOG_WATCHDOG_DEFAULT_SECONDS 5
#define KD_LOG_WATCHDOG_MINIMUM_SECONDS 5
#define KD_LOG_WATCHDOG_MAXIMUM_SECONDS 3600
#define KD_LOG_WATCHDOG_100NS_PER_SECOND 10000000ULL
#define KD_LOG_WATCHDOG_BUGCHECK_TYPE 2

static DECLSPEC_ALIGN(8) volatile LONG64 KdpLogWatchdogLastPrintTime;
static ULONGLONG KdpLogWatchdogTimeout;
static volatile LONG KdpLogWatchdogState;

#if defined(KDBG) && DBG
static BOOLEAN KdpLogWatchdogIsSeparator(_In_ CHAR Character)
{
    return Character == ' ' || Character == '\t' || Character == ',' || Character == '/';
}

static ULONG KdpLogWatchdogReadOption(_In_opt_ PCSTR LoadOptions)
{
    static const CHAR OptionName[] = "KDWATCHDOG";
    PCSTR Option;
    PCSTR Cursor;
    ULONG Seconds;

    if (LoadOptions == NULL)
        return KD_LOG_WATCHDOG_DEFAULT_SECONDS;

    Option = LoadOptions;
    while ((Option = strstr(Option, OptionName)) != NULL)
    {
        if (Option != LoadOptions && !KdpLogWatchdogIsSeparator(Option[-1]))
        {
            Option += RTL_NUMBER_OF(OptionName) - 1;
            continue;
        }

        Cursor = Option + RTL_NUMBER_OF(OptionName) - 1;
        if (*Cursor == ANSI_NULL || KdpLogWatchdogIsSeparator(*Cursor))
            return KD_LOG_WATCHDOG_DEFAULT_SECONDS;
        if (*Cursor != '=')
        {
            Option = Cursor;
            continue;
        }
        Cursor++;
        if (strncmp(Cursor, "OFF", 3) == 0 && (Cursor[3] == ANSI_NULL || KdpLogWatchdogIsSeparator(Cursor[3])))
            return 0;

        Seconds = 0;
        if (*Cursor < '0' || *Cursor > '9')
            return KD_LOG_WATCHDOG_DEFAULT_SECONDS;
        while (*Cursor >= '0' && *Cursor <= '9')
        {
            if (Seconds > (KD_LOG_WATCHDOG_MAXIMUM_SECONDS / 10))
                return KD_LOG_WATCHDOG_MAXIMUM_SECONDS;
            Seconds = (Seconds * 10) + (*Cursor++ - '0');
        }
        if (*Cursor != ANSI_NULL && !KdpLogWatchdogIsSeparator(*Cursor))
            return KD_LOG_WATCHDOG_DEFAULT_SECONDS;
        if (Seconds == 0)
            return 0;
        return min(max(Seconds, KD_LOG_WATCHDOG_MINIMUM_SECONDS), KD_LOG_WATCHDOG_MAXIMUM_SECONDS);
    }

    return KD_LOG_WATCHDOG_DEFAULT_SECONDS;
}
#endif

VOID
NTAPI
KdpLogWatchdogInitialize(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
#if defined(KDBG) && DBG
    ULONG Seconds;

    Seconds = KdpLogWatchdogReadOption(LoaderBlock != NULL ? LoaderBlock->LoadOptions : NULL);
    if (Seconds == 0)
    {
        InterlockedExchange(&KdpLogWatchdogState, 0);
        KdpDprintf("KDLOGWD: disabled by /KDWATCHDOG=OFF\n");
        return;
    }

    KdpLogWatchdogTimeout = (ULONGLONG)Seconds * KD_LOG_WATCHDOG_100NS_PER_SECOND;
    InterlockedExchange64(&KdpLogWatchdogLastPrintTime, KeQueryInterruptTime());
    InterlockedExchange(&KdpLogWatchdogState, 1);
    KdpDprintf("KDLOGWD: enabled, timeout %lu seconds; /KDWATCHDOG=OFF disables it\n", Seconds);
#else
    UNREFERENCED_PARAMETER(LoaderBlock);
#endif
}

VOID
NTAPI
KdpLogWatchdogDisable(VOID)
{
#if defined(KDBG) && DBG
    InterlockedExchange(&KdpLogWatchdogState, 0);
#endif
}

VOID
NTAPI
KdpLogWatchdogNotePrint(VOID)
{
#if defined(KDBG) && DBG
    if (InterlockedCompareExchange(&KdpLogWatchdogState, 1, 1) == 1)
        InterlockedExchange64(&KdpLogWatchdogLastPrintTime, KeQueryInterruptTime());
#endif
}

VOID
NTAPI
KdpLogWatchdogCheck(_In_ ULONGLONG CurrentInterruptTime)
{
#if defined(KDBG) && DBG
    ULONGLONG LastPrintTime;
    ULONGLONG Elapsed;

    if (InterlockedCompareExchange(&KdpLogWatchdogState, 1, 1) != 1)
        return;
    if (!KdDebuggerEnabled || KdPitchDebugger || KdEnteredDebugger)
    {
        InterlockedExchange64(&KdpLogWatchdogLastPrintTime, CurrentInterruptTime);
        return;
    }

    LastPrintTime = (ULONGLONG)InterlockedCompareExchange64(&KdpLogWatchdogLastPrintTime, 0, 0);
    if (CurrentInterruptTime <= LastPrintTime)
        return;
    Elapsed = CurrentInterruptTime - LastPrintTime;
    if (Elapsed <= KdpLogWatchdogTimeout)
        return;
    if (InterlockedCompareExchange(&KdpLogWatchdogState, 2, 1) != 1)
        return;

    KeBugCheckEx(DPC_WATCHDOG_VIOLATION, KD_LOG_WATCHDOG_BUGCHECK_TYPE, (ULONG_PTR)(Elapsed / KD_LOG_WATCHDOG_100NS_PER_SECOND), (ULONG_PTR)(KdpLogWatchdogTimeout / KD_LOG_WATCHDOG_100NS_PER_SECOND), KeGetCurrentProcessorNumber());
#else
    UNREFERENCED_PARAMETER(CurrentInterruptTime);
#endif
}
