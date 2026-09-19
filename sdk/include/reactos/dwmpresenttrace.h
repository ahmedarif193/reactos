/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * ReactOS-private, versioned presentation telemetry. No pointers in wire data.
 * Times are QPC wall time, not GPU timestamps or CPU execution time. Nested
 * stages overlap. Kernel counters cover all adapters/processes; user counters
 * cover the DWM process. Stopping preserves entered-but-unfinished operations.
 */
#ifndef ROS_DWM_PRESENT_TRACE_H
#define ROS_DWM_PRESENT_TRACE_H

#define DPT_VERSION 4
#define DPT_START 1
#define DPT_STOP 2
#define DPT_QUERY 3 /* Only frozen snapshots; never races live counters. */
#define DPT_COPYDATA 0x44505431
#define DPT_REPLY 0x44505432
#define CDD_ESCAPE_PRESENT_CAPTURE 0x44574D07
#define IOCTL_VIDEO_DXGK_PRESENT_CAPTURE \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x92a, METHOD_BUFFERED, FILE_ANY_ACCESS)

enum DPT_METRIC
{
    DPT_FRAME, DPT_FETCH, DPT_PREPARE, DPT_BEGIN, DPT_WINDOW,
    DPT_TEXTURE, DPT_BLUR, DPT_SHADOW, DPT_SWAP, DPT_ACK,
    DPT_MESA_PRESENT, DPT_PRIMARY_QUERY, DPT_BLIT, DPT_FLUSH,
    DPT_DEVICE_LOCK, DPT_FENCE_WAIT, DPT_INVALIDATE, DPT_KMT_PRESENT,
    DPT_KMT_SUBMIT, DPT_IOCTL, DPT_TILING,
    DPT_KERNEL_ADMIT, DPT_KERNEL_TRACK, DPT_KMD_RENDER, DPT_KMD_SUBMIT,
    DPT_KERNEL_PRESENT, DPT_QUEUE, DPT_RETIRE, DPT_SIGNAL,
    DPT_BLUR_HIT, DPT_BLUR_FILTER, DPT_BO_CREATE, DPT_CPU_UPLOAD,
    DPT_WGL_FLUSH, DPT_WGL_PACE, DPT_WGL_CALLBACK, DPT_SHARED_COMPOSE,
    DPT_BO_WAIT, DPT_KERNEL_CACHE_CLEAN, DPT_KERNEL_RESIDENCY_PIN,
    DPT_KERNEL_PACKET_PREPARE, DPT_KERNEL_FENCE_RESERVE,
    DPT_KERNEL_TRACK_PREPARE, DPT_KERNEL_PATCH, DPT_KERNEL_DMA_CLEAN,
    DPT_KERNEL_LIFECYCLE_WAIT, DPT_KERNEL_TRACK_ACTIVATE,
    DPT_KERNEL_CONTEXT_ADMIT, DPT_KERNEL_SCHED_ADMIT, DPT_KERNEL_CONTEXT_KICK,
    DPT_METRIC_COUNT
};

#pragma pack(push, 8)
typedef struct _DPT_REQUEST
{
    ULONG Size, Version, Operation, Session;
} DPT_REQUEST;

typedef struct _DPT_COUNTER
{
    ULONGLONG Entered, Completed, Failed, Ticks, MaxTicks, Bytes;
    ULONGLONG MinTicks; /* UINT64_MAX for counters without timed samples. */
} DPT_COUNTER;

typedef struct _DPT_DOMAIN
{
    ULONG Size, Version, Session, ProcessId; /* 0 = system-wide */
    ULONGLONG Frequency, Start, Stop;
    DPT_COUNTER Counter[DPT_METRIC_COUNT];
} DPT_DOMAIN;

typedef struct _DPT_SNAPSHOT
{
    ULONG Size, Version, Session, Available; /* bit 0 DWM, 1 ICD, 2 kernel */
    LONG Status[3]; /* HRESULT for each domain, unavailable is explicit */
    ULONG Reserved;
    ULONGLONG CpuKernel100ns, CpuUser100ns;
    DPT_DOMAIN Domain[3];
} DPT_SNAPSHOT;
#pragma pack(pop)

#ifndef DPT_KERNEL
typedef BOOL (WINAPI *PFNWGLCONTROLPRESENTATIONTRACEROS)(
    const DPT_REQUEST *, DPT_DOMAIN *, ULONG);
BOOL WINAPI wglControlPresentationTraceROS(const DPT_REQUEST *, DPT_DOMAIN *, ULONG);
typedef HRESULT (WINAPI *PFNDWMPRESENTATIONTRACECONTROL)(
    const DPT_REQUEST *, DPT_SNAPSHOT *, ULONG);
#endif
#endif
