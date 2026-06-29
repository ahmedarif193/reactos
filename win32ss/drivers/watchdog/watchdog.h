/*
 * PROJECT:     ReactOS Watchdog Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Watchdog object structures and internal declarations
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#pragma once

#include <ntifs.h>
#include <ntddk.h>

/* Pool tags */
#define WD_TAG_DEFERRED     'WdDW'
#define WD_TAG_STANDARD     'WdSW'

/* Watchdog type values */
#define WD_TYPE_KERNEL_TIME     1
#define WD_TYPE_USER_TIME       2
#define WD_TYPE_BOTH            3

/* LastEvent values */
#define WD_EVENT_IDLE           1
#define WD_EVENT_TIMEOUT        2
#define WD_EVENT_RECOVERY       3

/* TimeoutState values */
#define WD_TIMEOUT_IDLE         0
#define WD_TIMEOUT_MONITORING   1
#define WD_TIMEOUT_FIRED        2

/* RunState values */
#define WD_RUN_ALLOCATED        1
#define WD_RUN_RUNNING          2

/*
 * Common watchdog header shared between deferred and standard watchdog objects.
 */
typedef struct _WD_HEADER
{
    ULONG Tag;                      /* +0x00: 'WdDW' or 'WdSW' */
    volatile LONG ReferenceCount;   /* +0x04: Interlocked refcount */
    ULONG Flags;                    /* +0x08: Caller-supplied flags */
    ULONG Padding0;                 /* +0x0C: alignment */
    PDEVICE_OBJECT DeviceObject;    /* +0x10: Referenced device object */
    ULONG WatchdogType;             /* +0x18: 1=kernel, 2=user, 3=both */
    ULONG LastEvent;                /* +0x1C: 1=idle, 2=timeout, 3=recovery */
    PKTHREAD NotifiedThread;        /* +0x20: Thread that was notified of timeout */
    KSPIN_LOCK SpinLock;            /* +0x28: Protects mutable fields */
    PVOID ContextBuffer;            /* +0x30: Caller-attached context */
} WD_HEADER, *PWD_HEADER;

/*
 * Deferred watchdog object -- used by dxgkrnl for GPU TDR detection.
 * Total size: 0x100 bytes, pool tag 'WdDW'.
 */
typedef struct _WD_DEFERRED_WATCHDOG
{
    /* Common header (0x00 - 0x37) */
    WD_HEADER Header;

    /* Deferred-specific fields (0x38+) */
    ULONG TimeoutSeconds;           /* +0x38: Timeout threshold in seconds */
    volatile LONG SuspendCount;     /* +0x3C: Interlocked suspend counter */
    volatile LONG EnterCount;       /* +0x40: Interlocked enter section counter */
    volatile LONG ExitCount;        /* +0x44: Interlocked exit section counter */
    PKTHREAD MonitorThread;         /* +0x48: Atomically exchanged monitor thread */
    ULONG LastKernelSnapshot;       /* +0x4C: Last known kernel time */
    ULONG LastUserSnapshot;         /* +0x50: Last known user time */
    ULONG KernelTimeBaseline;       /* +0x54: Baseline kernel time for current period */
    ULONG UserTimeBaseline;         /* +0x58: Baseline user time for current period */
    ULONG TimeIncrement;            /* +0x5C: KeQueryTimeIncrement result */
    ULONG TimeoutState;             /* +0x60: 0=idle, 1=monitoring, 2=fired */
    ULONG RunState;                 /* +0x64: 1=allocated, 2=running */
    PKTHREAD MonitoredThread;       /* +0x68: Thread being monitored */
    KTIMER Timer;                   /* +0x70: Periodic timer */
    KDPC Dpc;                       /* +0xB0: Internal DPC for timer callback */
    PKDEFERRED_ROUTINE ClientDpc;   /* +0xF0: Client's DPC callback (e.g., dxgkrnl TDR) */
    BOOLEAN Suspended;              /* +0xF8: Suspended flag */
} WD_DEFERRED_WATCHDOG, *PWD_DEFERRED_WATCHDOG;

/*
 * Standard watchdog object -- used by videoprt for legacy video driver monitoring.
 * Total size: 0xF0 bytes, pool tag 'WdSW'.
 */
typedef struct _WD_STANDARD_WATCHDOG
{
    /* Common header (0x00 - 0x37) */
    WD_HEADER Header;

    /* Standard-specific fields (0x38+) */
    ULONG TimeoutCount;             /* +0x38: Countdown to timeout */
    volatile LONG SuspendCount;     /* +0x3C: Interlocked suspend counter */
    ULONG LastKernelTime;           /* +0x40: Last kernel time snapshot */
    ULONG LastUserTime;             /* +0x44: Last user time snapshot */
    ULONG TimeIncrement;            /* +0x48: KeQueryTimeIncrement result */
    ULONG Padding1;                 /* +0x4C: alignment */
    LARGE_INTEGER DueTime;          /* +0x50: Timer due time (negative=relative, 100ns) */
    ULONG Padding2;                 /* +0x58: alignment */
    ULONG Padding3;                 /* +0x5C: alignment */
    PKTHREAD MonitoredThread;       /* +0x60: Thread being monitored */
    KTIMER Timer;                   /* +0x68: Timer object */
    KDPC Dpc;                       /* +0xA8: Internal DPC */
    PKDEFERRED_ROUTINE ClientDpc;   /* +0xE8: Client's DPC callback */
} WD_STANDARD_WATCHDOG, *PWD_STANDARD_WATCHDOG;

/* Internal helper prototypes */
VOID
NTAPI
WdpDeferredWatchdogDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2);

VOID
NTAPI
WdpStandardWatchdogDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2);

VOID
WdpNotifyTimeout(
    _In_ PWD_DEFERRED_WATCHDOG Watchdog,
    _In_ ULONG EventType);

VOID
WdpFreeObject(
    _In_ PWD_HEADER Header);
