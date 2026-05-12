/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     PerfDataV2 — per-LP, NUMA, topology, brand, cache, memory, counts
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * This file is the contract between task #4 (perfdata backend) and
 * task #3 (performance page). Any change to signatures requires
 * architect sign-off. — §3.5
 */
#pragma once

#define PERFV2_HISTORY_SECONDS  60
#define PERFV2_MAX_LPS         256

typedef struct _PERFV2_TOPOLOGY
{
    DWORD Sockets;
    DWORD Cores;            /* total physical cores across all sockets */
    DWORD LogicalProcessors;
} PERFV2_TOPOLOGY;

typedef struct _PERFV2_CACHE
{
    SIZE_T L1Bytes;         /* sum of L1D + L1I across cores */
    SIZE_T L2Bytes;         /* sum across cores */
    SIZE_T L3Bytes;         /* shared, single value */
} PERFV2_CACHE;

/* Lifecycle ---------------------------------------------------------------- */

BOOL    PerfDataV2Initialize(void);
void    PerfDataV2Uninitialize(void);

/* Called once per refresh interval (default 1000 ms) by the shell timer. */
void    PerfDataV2Tick(void);

/* Topology / counts -------------------------------------------------------- */

DWORD   PerfV2_GetLPCount(void);
DWORD   PerfV2_GetNumaNodeCount(void);

/* History accessors --------------------------------------------------------
 * Caller passes out_array of at least PERFV2_HISTORY_SECONDS bytes (BYTE).
 * *out_count is set to the number of valid samples (0..PERFV2_HISTORY_SECONDS).
 * Newest sample is at out_array[*out_count - 1].
 * Returns FALSE if lpIndex / nodeIndex is out of range.
 */
BOOL    PerfV2_GetLPHistory          (DWORD lpIndex, BYTE *out_array, DWORD *out_count);
BOOL    PerfV2_GetLPKernelHistory    (DWORD lpIndex, BYTE *out_array, DWORD *out_count);
BOOL    PerfV2_GetOverallHistory     (BYTE *out_array, DWORD *out_count);
BOOL    PerfV2_GetOverallKernelHistory(BYTE *out_array, DWORD *out_count);
BOOL    PerfV2_GetNumaHistory        (DWORD nodeIndex, BYTE *out_array, DWORD *out_count);
BOOL    PerfV2_GetNumaKernelHistory  (DWORD nodeIndex, BYTE *out_array, DWORD *out_count);

/* CPU identity / topology -------------------------------------------------- */

/* Points to an internal static buffer — do not free. */
LPCWSTR PerfV2_GetCpuBrandString(void);

void    PerfV2_GetTopology(PERFV2_TOPOLOGY *out);
void    PerfV2_GetCacheSizes(SIZE_T *out_l1, SIZE_T *out_l2, SIZE_T *out_l3);

DWORD   PerfV2_GetSpeedMHz(void);      /* current instantaneous speed (TSC-based, best-effort) */
DWORD   PerfV2_GetBaseSpeedMHz(void);  /* from brand string "@ X.XXGHz" */
BOOL    PerfV2_IsVirtualizationEnabled(void);

/* System counters ---------------------------------------------------------- */

ULONG64 PerfV2_GetUptime(void);  /* seconds since boot */
void    PerfV2_GetCounts(DWORD *out_proc, DWORD *out_thr, DWORD *out_hnd);

/* Memory ------------------------------------------------------------------- */

void    PerfV2_GetMemory(ULONG64 *out_total_bytes,
                         DWORD   *out_inuse_pct,
                         ULONG64 *out_committed_bytes,
                         ULONG64 *out_limit_bytes,
                         ULONG64 *out_cached_bytes,
                         ULONG64 *out_paged_pool_bytes,
                         ULONG64 *out_nonpaged_pool_bytes);

/* NUMA affinity mask for a node (bitmask of LP indices). */
ULONG64 PerfV2_GetNumaProcessorMask(DWORD nodeIndex);

/* Single-sample snapshots (latest tick) ------------------------------------ */

DWORD   PerfV2_GetCurrentLPUtilization(DWORD lpIndex);
DWORD   PerfV2_GetCurrentLPKernelUtilization(DWORD lpIndex);
DWORD   PerfV2_GetCurrentOverallUtilization(void);
DWORD   PerfV2_GetCurrentOverallKernelUtilization(void);
