/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Performance page — CPU/Mem/Disk/Net/GPU categories, graph, stats
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.8.
 */
#pragma once

/* -------------------------------------------------------------------------
 * Category enum — maps to left category list
 * ----------------------------------------------------------------------- */
typedef enum _PERF_CATEGORY
{
    PERFCAT_CPU = 0,
    PERFCAT_MEMORY,
    PERFCAT_DISK,
    PERFCAT_NET,
    PERFCAT_GPU,
    PERFCAT__COUNT
} PERF_CATEGORY;

/* -------------------------------------------------------------------------
 * CPU graph sub-modes
 * ----------------------------------------------------------------------- */
typedef enum _CPU_GRAPH_MODE
{
    CPU_GMODE_OVERALL = 0,
    CPU_GMODE_LOGICAL,
    CPU_GMODE_NUMA
} CPU_GRAPH_MODE;

/* -------------------------------------------------------------------------
 * Page vtbl entry points (spec §3.8)
 * ----------------------------------------------------------------------- */
HWND PagePerf_Create (HWND hHost, HINSTANCE hInst);
void PagePerf_Tick   (HWND hPage);
void PagePerf_Resize (HWND hPage, int cx, int cy);
void PagePerf_Destroy(HWND hPage);
void PagePerf_Theme  (HWND hPage);

/* -------------------------------------------------------------------------
 * Category / mode control
 * ----------------------------------------------------------------------- */
void           PagePerf_SelectCategory (HWND hPage, PERF_CATEGORY cat);
void           PagePerf_SetCpuGraphMode(HWND hPage, CPU_GRAPH_MODE mode);
CPU_GRAPH_MODE PagePerf_GetCpuGraphMode(HWND hPage);

/* -------------------------------------------------------------------------
 * Persistence (spec §7.1 — HKCU\...\PerfPage)
 * ----------------------------------------------------------------------- */
void PagePerf_LoadPrefs(void);
void PagePerf_SavePrefs(void);
