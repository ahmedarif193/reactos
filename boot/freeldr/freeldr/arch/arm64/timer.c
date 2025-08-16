/*
 * PROJECT:     FreeLoader for ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Timer Support
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#include <freeldr.h>
#include <arch/arm64/arm64.h>

/* Debug support */
#include <debug.h>

/* Generic Timer registers */
#define CNTFRQ_EL0      "cntfrq_el0"    /* Counter frequency */
#define CNTPCT_EL0      "cntpct_el0"    /* Physical counter */
#define CNTVCT_EL0      "cntvct_el0"    /* Virtual counter */
#define CNTP_TVAL_EL0   "cntp_tval_el0" /* Physical timer value */
#define CNTP_CTL_EL0    "cntp_ctl_el0"  /* Physical timer control */
#define CNTV_TVAL_EL0   "cntv_tval_el0" /* Virtual timer value */
#define CNTV_CTL_EL0    "cntv_ctl_el0"  /* Virtual timer control */

/* Timer control register bits */
#define TIMER_CTL_ENABLE    (1 << 0)
#define TIMER_CTL_IMASK     (1 << 1)
#define TIMER_CTL_ISTATUS   (1 << 2)

static ULONG64 TimerFrequency = 0;

/* Read timer frequency */
/* Assembly helper functions - should be implemented in timer_asm.S */
extern ULONG64 TimerReadFrequency(VOID);
extern ULONG64 TimerReadPhysicalCount(VOID);
extern ULONG64 TimerReadVirtualCount(VOID);
extern VOID TimerSetPhysicalValue(ULONG64 Value);
extern VOID TimerSetPhysicalControl(ULONG32 Control);
extern VOID TimerSetVirtualControl(ULONG32 Control);

static ULONG64
TimerGetFrequency(VOID)
{
    /* TODO: Implement TimerReadFrequency in assembly */
    return 19200000; /* Default 19.2 MHz for now */
}

/* Read physical counter */
static ULONG64
TimerGetPhysicalCount(VOID)
{
    /* TODO: Implement TimerReadPhysicalCount in assembly */
    static ULONG64 Counter = 0;
    return Counter++;
}

/* Read virtual counter */
static ULONG64
TimerGetVirtualCount(VOID)
{
    /* TODO: Implement TimerReadVirtualCount in assembly */
    return TimerGetPhysicalCount();
}

/* Initialize timer */
VOID
TimerInit(VOID)
{
    /* TRACE */ printf("Initializing ARM64 Generic Timer\n");
    
    /* Get timer frequency */
    TimerFrequency = TimerGetFrequency();
    /* TRACE */ printf("Timer frequency: %llu Hz\n", TimerFrequency);
    
    /* TODO: Disable timers via assembly helpers */
    
    /* TRACE */ printf("Timer initialized\n");
}

/* Get current time in microseconds */
ULONG64
TimerGetMicroseconds(VOID)
{
    ULONG64 Count;
    
    if (TimerFrequency == 0)
    {
        /* Timer not initialized */
        return 0;
    }
    
    /* Get current counter value */
    Count = TimerGetPhysicalCount();
    
    /* Convert to microseconds */
    return (Count * 1000000) / TimerFrequency;
}

/* Get current time in milliseconds */
ULONG
TimerGetMilliseconds(VOID)
{
    return (ULONG)(TimerGetMicroseconds() / 1000);
}

/* Delay for specified microseconds */
VOID
TimerDelayMicroseconds(ULONG Microseconds)
{
    ULONG64 StartTime, CurrentTime, EndTime;
    
    if (TimerFrequency == 0)
    {
        /* Timer not initialized, use busy loop */
        volatile ULONG i;
        for (i = 0; i < Microseconds * 100; i++)
        {
            /* NOP delay */
        }
        return;
    }
    
    StartTime = TimerGetMicroseconds();
    EndTime = StartTime + Microseconds;
    
    /* Wait until time elapsed */
    do
    {
        CurrentTime = TimerGetMicroseconds();
        /* Yield to other threads/cores */
    } while (CurrentTime < EndTime);
}

/* Delay for specified milliseconds */
VOID
TimerDelayMilliseconds(ULONG Milliseconds)
{
    TimerDelayMicroseconds(Milliseconds * 1000);
}

/* Stall execution for specified microseconds - compatibility function */
VOID
StallExecutionProcessor(ULONG Microseconds)
{
    TimerDelayMicroseconds(Microseconds);
}

/* Set timer for interrupt after specified microseconds */
VOID
TimerSetInterrupt(ULONG Microseconds)
{
    ULONG64 Ticks;
    
    if (TimerFrequency == 0)
    {
        /* Timer not initialized */
        return;
    }
    
    /* Calculate ticks */
    Ticks = (TimerFrequency * Microseconds) / 1000000;
    
    /* TODO: Set timer via assembly helpers */
}

/* Clear timer interrupt */
VOID
TimerClearInterrupt(VOID)
{
    /* TODO: Clear timer via assembly helper */
}

/* Get system time for RTC emulation */
TIMEINFO*
TimerGetTime(VOID)
{
    static TIMEINFO TimeInfo;
    ULONG64 Seconds;
    
    if (TimerFrequency == 0)
    {
        /* Timer not initialized */
        return NULL;
    }
    
    /* Get seconds since boot */
    Seconds = TimerGetPhysicalCount() / TimerFrequency;
    
    /* TODO: Add RTC base time from UEFI */
    /* For now, return a fixed date */
    TimeInfo.Year = 2025;
    TimeInfo.Month = 1;
    TimeInfo.Day = 1;
    TimeInfo.Hour = 0;
    TimeInfo.Minute = 0;
    TimeInfo.Second = (UCHAR)(Seconds % 60);
    
    return &TimeInfo;
}