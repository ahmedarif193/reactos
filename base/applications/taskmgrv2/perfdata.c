/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     PerfDataV2 backend — per-LP, NUMA, memory, uptime, counts
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Design notes:
 *  - One CRITICAL_SECTION g_perfV2Lock guards all shared state.
 *    PerfDataV2Tick takes it exclusively; every read accessor takes it
 *    briefly to copy out a snapshot, then releases.
 *  - Ring buffers: BYTE[PERFV2_HISTORY_SECONDS] per series.
 *    g_histHead is the index of the slot that will be written NEXT.
 *    After N ticks (N <= PERFV2_HISTORY_SECONDS) the valid sample count
 *    is g_histSamples.  Oldest sample is at
 *    buf[(head - samples + PERFV2_HISTORY_SECONDS) % PERFV2_HISTORY_SECONDS].
 *  - Per-LP times come from NtQuerySystemInformation(
 *      SystemProcessorPerformanceInformation, ...).
 *    Canonical reference: base/applications/taskmgr/perfdata.c:204-205.
 *  - NUMA: SystemNumaProcessorMap (NDK class 55, index == 55).
 *    If the call fails, we fall back to 1 node containing all LPs per §11.
 */

#include "precomp.h"

#define WIN32_NO_STATUS
#define NTOS_MODE_USER
#include <ndk/exfuncs.h>
#include <ndk/psfuncs.h>

/* --------------------------------------------------------------------------
 * Lock
 * -------------------------------------------------------------------------- */

CRITICAL_SECTION g_perfV2Lock;

/* --------------------------------------------------------------------------
 * Ring-buffer helpers
 * -------------------------------------------------------------------------- */

#define HIST_LEN  PERFV2_HISTORY_SECONDS

/* One ring for user%, one for kernel% */
typedef struct _LP_RING
{
    BYTE  user[HIST_LEN];
    BYTE  kern[HIST_LEN];
} LP_RING;

/* --------------------------------------------------------------------------
 * Per-LP state
 * -------------------------------------------------------------------------- */

static DWORD   g_lpCount  = 0;
static LP_RING g_lpRings[PERFV2_MAX_LPS];

/* Saved previous tick values for delta computation */
static LARGE_INTEGER g_prevIdle  [PERFV2_MAX_LPS];
static LARGE_INTEGER g_prevKernel[PERFV2_MAX_LPS];
static LARGE_INTEGER g_prevUser  [PERFV2_MAX_LPS];

/* Current snapshot (0..100) */
static BYTE g_curLP    [PERFV2_MAX_LPS];  /* total utilization */
static BYTE g_curLPKern[PERFV2_MAX_LPS];  /* kernel portion */

/* --------------------------------------------------------------------------
 * Overall ring
 * -------------------------------------------------------------------------- */

static BYTE g_overallUser[HIST_LEN];
static BYTE g_overallKern[HIST_LEN];
static BYTE g_curOverall;
static BYTE g_curOverallKern;

/* --------------------------------------------------------------------------
 * NUMA state
 * -------------------------------------------------------------------------- */

#define MAX_NUMA_NODES 16  /* matches NDK MAXIMUM_NUMA_NODES */

static DWORD   g_numaNodeCount = 1;
static ULONG64 g_numaAffinityMasks[MAX_NUMA_NODES];

/* Per-NUMA node rings (mean of LPs in the node) */
typedef struct _NUMA_RING
{
    BYTE user[HIST_LEN];
    BYTE kern[HIST_LEN];
} NUMA_RING;

static NUMA_RING g_numaRings[MAX_NUMA_NODES];
static BYTE      g_curNuma    [MAX_NUMA_NODES];
static BYTE      g_curNumaKern[MAX_NUMA_NODES];

/* --------------------------------------------------------------------------
 * Ring head / sample count (shared by all series in one tick)
 * -------------------------------------------------------------------------- */

static DWORD g_histHead    = 0;   /* next slot to write */
static DWORD g_histSamples = 0;   /* how many valid samples (capped at HIST_LEN) */

/* --------------------------------------------------------------------------
 * Memory snapshot
 * -------------------------------------------------------------------------- */

static ULONG64 g_memTotalBytes      = 0;
static DWORD   g_memInUsePct        = 0;
static ULONG64 g_memCommittedBytes  = 0;
static ULONG64 g_memLimitBytes      = 0;
static ULONG64 g_memCachedBytes     = 0;
static ULONG64 g_memPagedPool       = 0;
static ULONG64 g_memNonPagedPool    = 0;

/* --------------------------------------------------------------------------
 * Counts snapshot
 * -------------------------------------------------------------------------- */

static DWORD g_procCount = 0;
static DWORD g_thrCount  = 0;
static DWORD g_hndCount  = 0;

/* --------------------------------------------------------------------------
 * Uptime
 * -------------------------------------------------------------------------- */

static ULONG64 g_uptimeSecs = 0;

/* --------------------------------------------------------------------------
 * Speed (MHz)
 * -------------------------------------------------------------------------- */

static DWORD g_currentMhz = 0;

/* --------------------------------------------------------------------------
 * Internal: fill ring at current head with a value
 * -------------------------------------------------------------------------- */

static void RingPush(BYTE *ring, BYTE value)
{
    ring[g_histHead] = value;
}

/* --------------------------------------------------------------------------
 * Internal: copy ring contents into a caller buffer, oldest first.
 * head and samples are already captured under the lock by the caller.
 * -------------------------------------------------------------------------- */

static void RingCopyOut(const BYTE *ring, DWORD head, DWORD samples,
                        BYTE *out_array, DWORD *out_count)
{
    DWORD i, src;

    *out_count = samples;
    if (samples == 0) return;

    for (i = 0; i < samples; i++)
    {
        /* oldest is at (head - samples + i) mod HIST_LEN */
        src = (head + HIST_LEN - samples + i) % HIST_LEN;
        out_array[i] = ring[src];
    }
}

/* --------------------------------------------------------------------------
 * NUMA initialisation
 * -------------------------------------------------------------------------- */

/* Compute the fallback affinity mask: (1ULL << LPCount) - 1, capped at 64 LPs.
 * For LPCount == 64 the shift would overflow, so we special-case it. */
static ULONG64 NumaFallbackMask(void)
{
    if (g_lpCount == 0)   return 1ULL;
    if (g_lpCount >= 64)  return (ULONG64)0xFFFFFFFFFFFFFFFFULL;
    return ((ULONG64)1 << g_lpCount) - 1;
}

static void InitNuma(void)
{
    SYSTEM_NUMA_INFORMATION numaInfo;
    NTSTATUS status;
    ULONG ret = 0;
    DWORD i;

    ZeroMemory(&numaInfo, sizeof(numaInfo));

    status = NtQuerySystemInformation(SystemNumaProcessorMap,
                                      &numaInfo,
                                      sizeof(numaInfo),
                                      &ret);

    /* Fallback conditions per DRAFT 3 §3.5:
     *   - any failure status (includes STATUS_NOT_IMPLEMENTED)
     *   - empty buffer (ret == 0)
     * In all cases: 1 node containing all LPs, mask = (1ULL << LPCount) - 1. */
    if (!NT_SUCCESS(status) || ret == 0)
    {
        g_numaNodeCount = 1;
        g_numaAffinityMasks[0] = NumaFallbackMask();
        return;
    }

    /* HighestNodeNumber is 0-based. */
    g_numaNodeCount = numaInfo.HighestNodeNumber + 1;
    if (g_numaNodeCount > MAX_NUMA_NODES)
        g_numaNodeCount = MAX_NUMA_NODES;

    for (i = 0; i < g_numaNodeCount; i++)
        g_numaAffinityMasks[i] = numaInfo.ActiveProcessorsAffinityMask[i];
}

/* --------------------------------------------------------------------------
 * PerfDataV2Initialize / PerfDataV2Uninitialize
 * -------------------------------------------------------------------------- */

BOOL PerfDataV2Initialize(void)
{
    SYSTEM_BASIC_INFORMATION sbi;
    NTSTATUS status;

    InitializeCriticalSection(&g_perfV2Lock);

    ZeroMemory(&g_lpRings,    sizeof(g_lpRings));
    ZeroMemory(&g_numaRings,  sizeof(g_numaRings));
    ZeroMemory(g_overallUser, sizeof(g_overallUser));
    ZeroMemory(g_overallKern, sizeof(g_overallKern));
    ZeroMemory(g_prevIdle,    sizeof(g_prevIdle));
    ZeroMemory(g_prevKernel,  sizeof(g_prevKernel));
    ZeroMemory(g_prevUser,    sizeof(g_prevUser));
    ZeroMemory(g_curLP,       sizeof(g_curLP));
    ZeroMemory(g_curLPKern,   sizeof(g_curLPKern));

    g_histHead    = 0;
    g_histSamples = 0;

    /* Get LP count from the kernel. */
    status = NtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof(sbi), NULL);
    if (!NT_SUCCESS(status))
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        g_lpCount = si.dwNumberOfProcessors;
    }
    else
    {
        g_lpCount = (DWORD)sbi.NumberOfProcessors;
    }

    if (g_lpCount == 0) g_lpCount = 1;
    if (g_lpCount > PERFV2_MAX_LPS) g_lpCount = PERFV2_MAX_LPS;

    /* NUMA topology */
    InitNuma();

    /* Trigger CPUID topology layer */
    CpuTopo_Detect();

    /* Take an initial tick so first real tick has delta data. */
    {
        PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION pti;
        ULONG tiLen = g_lpCount * sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION);
        DWORD lp;

        pti = (PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)HeapAlloc(
            GetProcessHeap(), 0, tiLen);
        if (pti)
        {
            ULONG retLen = 0;
            status = NtQuerySystemInformation(SystemProcessorPerformanceInformation,
                                              pti, tiLen, &retLen);
            if (NT_SUCCESS(status))
            {
                for (lp = 0; lp < g_lpCount; lp++)
                {
                    g_prevIdle  [lp] = pti[lp].IdleTime;
                    /* KernelTime in the NDK struct includes idle time. */
                    g_prevKernel[lp] = pti[lp].KernelTime;
                    g_prevUser  [lp] = pti[lp].UserTime;
                }
            }
            HeapFree(GetProcessHeap(), 0, pti);
        }
    }

    return TRUE;
}

void PerfDataV2Uninitialize(void)
{
    DeleteCriticalSection(&g_perfV2Lock);
}

/* --------------------------------------------------------------------------
 * PerfDataV2Tick — called by the shell timer once per refresh interval
 * -------------------------------------------------------------------------- */

void PerfDataV2Tick(void)
{
    NTSTATUS status;
    ULONG retLen = 0;

    PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION pti = NULL;
    ULONG tiLen;

    SYSTEM_PERFORMANCE_INFORMATION  spi;
    SYSTEM_BASIC_INFORMATION        sbi;
    SYSTEM_FILECACHE_INFORMATION    sci;
    SYSTEM_TIMEOFDAY_INFORMATION    stod;

    DWORD lp, node;

    EnterCriticalSection(&g_perfV2Lock);

    /* ------------------------------------------------------------------ */
    /* 1. Per-LP times (canonical pattern from legacy perfdata.c:204-205) */
    /* ------------------------------------------------------------------ */

    tiLen = g_lpCount * sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION);
    pti = (PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)HeapAlloc(
              GetProcessHeap(), 0, tiLen);

    if (pti)
    {
        status = NtQuerySystemInformation(SystemProcessorPerformanceInformation,
                                          pti, tiLen, &retLen);
        if (NT_SUCCESS(status))
        {
            for (lp = 0; lp < g_lpCount; lp++)
            {
                LONGLONG idleDelta, kernDelta, userDelta, totalDelta;
                DWORD util, kutil;

                /* KernelTime from the NDK struct includes idle time.
                 * True kernel-only = KernelTime - IdleTime. */
                idleDelta  = pti[lp].IdleTime.QuadPart   - g_prevIdle  [lp].QuadPart;
                kernDelta  = pti[lp].KernelTime.QuadPart  - g_prevKernel[lp].QuadPart;
                userDelta  = pti[lp].UserTime.QuadPart    - g_prevUser  [lp].QuadPart;

                totalDelta = kernDelta + userDelta;
                if (totalDelta <= 0) totalDelta = 1;  /* guard against zero delta */

                /* Total utilization = (total - idle) / total */
                util = (DWORD)(((totalDelta - idleDelta) * 100) / totalDelta);
                if (util > 100) util = 100;

                /* Kernel (non-idle) = (kernDelta - idleDelta) / totalDelta */
                {
                    LONGLONG kernNonIdle = kernDelta - idleDelta;
                    if (kernNonIdle < 0) kernNonIdle = 0;
                    kutil = (DWORD)((kernNonIdle * 100) / totalDelta);
                    if (kutil > 100) kutil = 100;
                }

                g_curLP    [lp] = (BYTE)util;
                g_curLPKern[lp] = (BYTE)kutil;

                RingPush(g_lpRings[lp].user, (BYTE)util);
                RingPush(g_lpRings[lp].kern, (BYTE)kutil);

                /* Save for next delta */
                g_prevIdle  [lp] = pti[lp].IdleTime;
                g_prevKernel[lp] = pti[lp].KernelTime;
                g_prevUser  [lp] = pti[lp].UserTime;
            }
        }

        HeapFree(GetProcessHeap(), 0, pti);
        pti = NULL;
    }

    /* ------------------------------------------------------------------ */
    /* 2. Overall — arithmetic mean of per-LP values                      */
    /* ------------------------------------------------------------------ */
    {
        DWORD oUtil = 0, oKutil = 0;
        ULONG64 sumU = 0, sumK = 0;

        for (lp = 0; lp < g_lpCount; lp++)
        {
            sumU += g_curLP    [lp];
            sumK += g_curLPKern[lp];
        }
        if (g_lpCount > 0)
        {
            oUtil  = (DWORD)(sumU / g_lpCount);
            oKutil = (DWORD)(sumK / g_lpCount);
        }

        g_curOverall     = (BYTE)oUtil;
        g_curOverallKern = (BYTE)oKutil;

        RingPush(g_overallUser, (BYTE)oUtil);
        RingPush(g_overallKern, (BYTE)oKutil);
    }

    /* ------------------------------------------------------------------ */
    /* 3. NUMA nodes — arithmetic mean of per-LP values in each node      */
    /* ------------------------------------------------------------------ */
    for (node = 0; node < g_numaNodeCount; node++)
    {
        ULONG64 mask = g_numaAffinityMasks[node];
        ULONG64 sumU = 0, sumK = 0;
        DWORD count = 0;

        for (lp = 0; lp < g_lpCount; lp++)
        {
            if (mask & ((ULONG64)1 << lp))
            {
                sumU += g_curLP    [lp];
                sumK += g_curLPKern[lp];
                count++;
            }
        }

        if (count == 0) count = 1;
        g_curNuma    [node] = (BYTE)(sumU / count);
        g_curNumaKern[node] = (BYTE)(sumK / count);

        RingPush(g_numaRings[node].user, g_curNuma    [node]);
        RingPush(g_numaRings[node].kern, g_curNumaKern[node]);
    }

    /* ------------------------------------------------------------------ */
    /* 4. Advance ring head                                                */
    /* ------------------------------------------------------------------ */
    g_histHead = (g_histHead + 1) % HIST_LEN;
    if (g_histSamples < HIST_LEN) g_histSamples++;

    /* ------------------------------------------------------------------ */
    /* 5. Memory: SystemPerformanceInformation + Basic + FileCache         */
    /* ------------------------------------------------------------------ */
    if (NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation,
                                            &sbi, sizeof(sbi), NULL)))
    {
        g_memTotalBytes = (ULONG64)sbi.NumberOfPhysicalPages *
                          (ULONG64)sbi.PageSize;
    }

    if (NT_SUCCESS(NtQuerySystemInformation(SystemPerformanceInformation,
                                            &spi, sizeof(spi), NULL)))
    {
        ULONG64 available = (ULONG64)spi.AvailablePages *
                            (ULONG64)(g_memTotalBytes > 0 ?
                                (g_memTotalBytes / (sbi.NumberOfPhysicalPages > 0 ?
                                                    sbi.NumberOfPhysicalPages : 1))
                                : 4096);
        ULONG64 used;

        /* Page size from sbi */
        ULONG pageSize = (sbi.PageSize > 0) ? sbi.PageSize : 4096;

        available = (ULONG64)spi.AvailablePages * pageSize;

        used = (g_memTotalBytes > available) ? (g_memTotalBytes - available) : 0;

        g_memInUsePct = (g_memTotalBytes > 0) ?
                        (DWORD)((used * 100) / g_memTotalBytes) : 0;

        g_memCommittedBytes  = (ULONG64)spi.CommittedPages   * pageSize;
        g_memLimitBytes      = (ULONG64)spi.CommitLimit      * pageSize;
        g_memPagedPool       = (ULONG64)spi.PagedPoolPages   * pageSize;
        g_memNonPagedPool    = (ULONG64)spi.NonPagedPoolPages * pageSize;
    }

    if (NT_SUCCESS(NtQuerySystemInformation(SystemFileCacheInformation,
                                            &sci, sizeof(sci), NULL)))
    {
        g_memCachedBytes = (ULONG64)sci.CurrentSize;
    }

    /* ------------------------------------------------------------------ */
    /* 6. Uptime: SystemTimeOfDayInformation                               */
    /* ------------------------------------------------------------------ */
    if (NT_SUCCESS(NtQuerySystemInformation(SystemTimeOfDayInformation,
                                            &stod, sizeof(stod), NULL)))
    {
        /* BootTime and CurrentTime are LARGE_INTEGER (100-ns intervals). */
        LONGLONG diff = stod.CurrentTime.QuadPart - stod.BootTime.QuadPart;
        if (diff > 0)
            g_uptimeSecs = (ULONG64)(diff / 10000000LL);
        else
            g_uptimeSecs = 0;
    }

    /* ------------------------------------------------------------------ */
    /* 7. Counts: process/thread from SystemProcessInformation,            */
    /*            handle count from SystemHandleInformation                */
    /* ------------------------------------------------------------------ */
    {
        /* Enumerate processes — ask for the buffer size, then allocate. */
        ULONG procBufLen = 0x20000;  /* start with 128 KB, grow if needed */
        PVOID procBuf = NULL;
        DWORD retries = 5;

        while (retries--)
        {
            procBuf = HeapAlloc(GetProcessHeap(), 0, procBufLen);
            if (!procBuf) break;

            status = NtQuerySystemInformation(SystemProcessInformation,
                                              procBuf, procBufLen, &retLen);
            if (NT_SUCCESS(status)) break;

            HeapFree(GetProcessHeap(), 0, procBuf);
            procBuf = NULL;

            if (status == STATUS_INFO_LENGTH_MISMATCH)
                procBufLen = retLen + 0x1000;
            else
                break;
        }

        if (procBuf)
        {
            PSYSTEM_PROCESS_INFORMATION entry =
                (PSYSTEM_PROCESS_INFORMATION)procBuf;
            DWORD procs = 0, threads = 0;

            for (;;)
            {
                procs++;
                threads += entry->NumberOfThreads;
                if (entry->NextEntryOffset == 0) break;
                entry = (PSYSTEM_PROCESS_INFORMATION)
                        ((PUCHAR)entry + entry->NextEntryOffset);
            }

            g_procCount = procs;
            g_thrCount  = threads;

            HeapFree(GetProcessHeap(), 0, procBuf);
        }

        /* Handle count: SystemHandleInformation returns STATUS_INFO_LENGTH_MISMATCH
         * with the real count in the first ULONG, which is all we need. */
        {
            SYSTEM_HANDLE_INFORMATION shi = {0};
            status = NtQuerySystemInformation(SystemHandleInformation,
                                              &shi, sizeof(shi), &retLen);
            /* Per legacy pattern (perfdata.c ~217-222): */
            if (status != STATUS_SUCCESS)
                g_hndCount = shi.NumberOfHandles;
        }
    }

    /* ------------------------------------------------------------------ */
    /* 8. Current MHz (QueryProcessorCycleTime is not available for system-*/
    /*    wide use; best-effort via GetSystemInfo or registry power key).  */
    /*    For v1, report the brand-string value from CpuTopo.             */
    /* ------------------------------------------------------------------ */
    if (g_currentMhz == 0)
        g_currentMhz = CpuTopo_GetMaxMhzHint();

    LeaveCriticalSection(&g_perfV2Lock);
}

/* --------------------------------------------------------------------------
 * Public read accessors — each takes the lock briefly to copy out data.
 * -------------------------------------------------------------------------- */

DWORD PerfV2_GetLPCount(void)
{
    DWORD v;
    EnterCriticalSection(&g_perfV2Lock);
    v = g_lpCount;
    LeaveCriticalSection(&g_perfV2Lock);
    return v;
}

DWORD PerfV2_GetNumaNodeCount(void)
{
    DWORD v;
    EnterCriticalSection(&g_perfV2Lock);
    v = g_numaNodeCount;
    LeaveCriticalSection(&g_perfV2Lock);
    return v;
}

BOOL PerfV2_GetLPHistory(DWORD lpIndex, BYTE *out_array, DWORD *out_count)
{
    DWORD head, samples;
    if (!out_array || !out_count) return FALSE;

    EnterCriticalSection(&g_perfV2Lock);

    if (lpIndex >= g_lpCount)
    {
        LeaveCriticalSection(&g_perfV2Lock);
        *out_count = 0;
        return FALSE;
    }

    head    = g_histHead;
    samples = g_histSamples;

    RingCopyOut(g_lpRings[lpIndex].user, head, samples, out_array, out_count);

    LeaveCriticalSection(&g_perfV2Lock);
    return TRUE;
}

BOOL PerfV2_GetLPKernelHistory(DWORD lpIndex, BYTE *out_array, DWORD *out_count)
{
    DWORD head, samples;
    if (!out_array || !out_count) return FALSE;

    EnterCriticalSection(&g_perfV2Lock);

    if (lpIndex >= g_lpCount)
    {
        LeaveCriticalSection(&g_perfV2Lock);
        *out_count = 0;
        return FALSE;
    }

    head    = g_histHead;
    samples = g_histSamples;

    RingCopyOut(g_lpRings[lpIndex].kern, head, samples, out_array, out_count);

    LeaveCriticalSection(&g_perfV2Lock);
    return TRUE;
}

BOOL PerfV2_GetOverallHistory(BYTE *out_array, DWORD *out_count)
{
    DWORD head, samples;
    if (!out_array || !out_count) return FALSE;

    EnterCriticalSection(&g_perfV2Lock);
    head    = g_histHead;
    samples = g_histSamples;
    RingCopyOut(g_overallUser, head, samples, out_array, out_count);
    LeaveCriticalSection(&g_perfV2Lock);
    return TRUE;
}

BOOL PerfV2_GetOverallKernelHistory(BYTE *out_array, DWORD *out_count)
{
    DWORD head, samples;
    if (!out_array || !out_count) return FALSE;

    EnterCriticalSection(&g_perfV2Lock);
    head    = g_histHead;
    samples = g_histSamples;
    RingCopyOut(g_overallKern, head, samples, out_array, out_count);
    LeaveCriticalSection(&g_perfV2Lock);
    return TRUE;
}

BOOL PerfV2_GetNumaHistory(DWORD nodeIndex, BYTE *out_array, DWORD *out_count)
{
    DWORD head, samples;
    if (!out_array || !out_count) return FALSE;

    EnterCriticalSection(&g_perfV2Lock);

    if (nodeIndex >= g_numaNodeCount)
    {
        LeaveCriticalSection(&g_perfV2Lock);
        *out_count = 0;
        return FALSE;
    }

    head    = g_histHead;
    samples = g_histSamples;

    RingCopyOut(g_numaRings[nodeIndex].user, head, samples, out_array, out_count);

    LeaveCriticalSection(&g_perfV2Lock);
    return TRUE;
}

BOOL PerfV2_GetNumaKernelHistory(DWORD nodeIndex, BYTE *out_array, DWORD *out_count)
{
    DWORD head, samples;
    if (!out_array || !out_count) return FALSE;

    EnterCriticalSection(&g_perfV2Lock);

    if (nodeIndex >= g_numaNodeCount)
    {
        LeaveCriticalSection(&g_perfV2Lock);
        *out_count = 0;
        return FALSE;
    }

    head    = g_histHead;
    samples = g_histSamples;

    RingCopyOut(g_numaRings[nodeIndex].kern, head, samples, out_array, out_count);

    LeaveCriticalSection(&g_perfV2Lock);
    return TRUE;
}

LPCWSTR PerfV2_GetCpuBrandString(void)
{
    /* CpuTopo_Detect is idempotent; the brand buffer is static inside
     * cpu_topology.c.  No need to lock — it is written once at init. */
    return CpuTopo_GetBrandString();
}

void PerfV2_GetTopology(PERFV2_TOPOLOGY *out)
{
    if (!out) return;
    CpuTopo_GetCounts(&out->Sockets, &out->Cores, &out->LogicalProcessors);
}

void PerfV2_GetCacheSizes(SIZE_T *out_l1, SIZE_T *out_l2, SIZE_T *out_l3)
{
    CpuTopo_GetCacheSizes(out_l1, out_l2, out_l3);
}

DWORD PerfV2_GetSpeedMHz(void)
{
    DWORD v;
    EnterCriticalSection(&g_perfV2Lock);
    v = g_currentMhz;
    LeaveCriticalSection(&g_perfV2Lock);
    return v;
}

DWORD PerfV2_GetBaseSpeedMHz(void)
{
    return CpuTopo_GetMaxMhzHint();
}

BOOL PerfV2_IsVirtualizationEnabled(void)
{
    return CpuTopo_HasVirtualization();
}

ULONG64 PerfV2_GetUptime(void)
{
    ULONG64 v;
    EnterCriticalSection(&g_perfV2Lock);
    v = g_uptimeSecs;
    LeaveCriticalSection(&g_perfV2Lock);
    return v;
}

void PerfV2_GetCounts(DWORD *out_proc, DWORD *out_thr, DWORD *out_hnd)
{
    EnterCriticalSection(&g_perfV2Lock);
    if (out_proc) *out_proc = g_procCount;
    if (out_thr)  *out_thr  = g_thrCount;
    if (out_hnd)  *out_hnd  = g_hndCount;
    LeaveCriticalSection(&g_perfV2Lock);
}

void PerfV2_GetMemory(ULONG64 *out_total_bytes,
                      DWORD   *out_inuse_pct,
                      ULONG64 *out_committed_bytes,
                      ULONG64 *out_limit_bytes,
                      ULONG64 *out_cached_bytes,
                      ULONG64 *out_paged_pool_bytes,
                      ULONG64 *out_nonpaged_pool_bytes)
{
    EnterCriticalSection(&g_perfV2Lock);
    if (out_total_bytes)       *out_total_bytes       = g_memTotalBytes;
    if (out_inuse_pct)         *out_inuse_pct         = g_memInUsePct;
    if (out_committed_bytes)   *out_committed_bytes   = g_memCommittedBytes;
    if (out_limit_bytes)       *out_limit_bytes        = g_memLimitBytes;
    if (out_cached_bytes)      *out_cached_bytes       = g_memCachedBytes;
    if (out_paged_pool_bytes)  *out_paged_pool_bytes   = g_memPagedPool;
    if (out_nonpaged_pool_bytes) *out_nonpaged_pool_bytes = g_memNonPagedPool;
    LeaveCriticalSection(&g_perfV2Lock);
}

ULONG64 PerfV2_GetNumaProcessorMask(DWORD nodeIndex)
{
    ULONG64 v = 0;
    EnterCriticalSection(&g_perfV2Lock);
    if (nodeIndex < g_numaNodeCount)
        v = g_numaAffinityMasks[nodeIndex];
    LeaveCriticalSection(&g_perfV2Lock);
    return v;
}

DWORD PerfV2_GetCurrentLPUtilization(DWORD lpIndex)
{
    DWORD v = 0;
    EnterCriticalSection(&g_perfV2Lock);
    if (lpIndex < g_lpCount)
        v = g_curLP[lpIndex];
    LeaveCriticalSection(&g_perfV2Lock);
    return v;
}

DWORD PerfV2_GetCurrentLPKernelUtilization(DWORD lpIndex)
{
    DWORD v = 0;
    EnterCriticalSection(&g_perfV2Lock);
    if (lpIndex < g_lpCount)
        v = g_curLPKern[lpIndex];
    LeaveCriticalSection(&g_perfV2Lock);
    return v;
}

DWORD PerfV2_GetCurrentOverallUtilization(void)
{
    DWORD v;
    EnterCriticalSection(&g_perfV2Lock);
    v = g_curOverall;
    LeaveCriticalSection(&g_perfV2Lock);
    return v;
}

DWORD PerfV2_GetCurrentOverallKernelUtilization(void)
{
    DWORD v;
    EnterCriticalSection(&g_perfV2Lock);
    v = g_curOverallKern;
    LeaveCriticalSection(&g_perfV2Lock);
    return v;
}
