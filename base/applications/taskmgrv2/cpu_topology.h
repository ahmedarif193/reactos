/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     CPUID-based CPU topology, brand string, cache, and virtualization detection
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 */
#pragma once

typedef enum _CPU_VENDOR
{
    CPU_VENDOR_UNKNOWN = 0,
    CPU_VENDOR_INTEL,
    CPU_VENDOR_AMD
} CPU_VENDOR;

/* Must be called once before any other CpuTopo_* function.
 * Idempotent: safe to call multiple times. */
void       CpuTopo_Detect(void);

CPU_VENDOR CpuTopo_GetVendor(void);

/* Wide brand string from CPUID leaves 0x80000002..4.
 * Points to an internal static buffer — do not free. */
LPCWSTR    CpuTopo_GetBrandString(void);

/* Total physical socket / core / logical-processor counts. */
void       CpuTopo_GetCounts(DWORD *out_sockets, DWORD *out_cores, DWORD *out_lps);

/* Sum of L1D+L1I across cores, sum of L2 across cores, L3 shared total. */
void       CpuTopo_GetCacheSizes(SIZE_T *out_l1, SIZE_T *out_l2, SIZE_T *out_l3);

/* TRUE if VMX (Intel) or SVM (AMD) bit is set. */
BOOL       CpuTopo_HasVirtualization(void);

/* Base/maximum speed in MHz parsed from the brand string "@ X.XXGHz".
 * Returns 0 if not found. */
DWORD      CpuTopo_GetMaxMhzHint(void);
