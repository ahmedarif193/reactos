# taskmgrv2 — Win11-Style ReactOS Task Manager — Architecture Spec

Status: DRAFT 3 — APPROVED FOR IMPLEMENTATION (2026-04-07)
Author: architect (team-lead delegated)
Branch: dev-smp-2

This document is the single source of truth that the four implementer
specialists (`shell`, `perfdata`, `performance-page`, `other-pages`) MUST
follow. Any deviation from the function signatures, file layout, or build
integration listed here requires going back to the architect first.

## Ground rules — read this first

1. **`taskmgrv2` is its own application.** Source root:
   `base/applications/taskmgrv2/`. Output binary: `taskmgrv2.exe`,
   installed at `reactos/system32/taskmgrv2.exe`. The classic
   `taskmgr.exe` is shipped unchanged alongside it.
2. **The legacy `base/applications/taskmgr/` folder is read-only.** You
   may *read* anything in there (and the perfdata.c at
   `base/applications/taskmgr/perfdata.c:204-205` is the canonical
   reference for `NtQuerySystemInformation(SystemProcessorPerformance
   Information, ...)` — copy that pattern). You may NOT propose any
   modification to any file in the legacy folder.
3. **No header crosses the folder boundary.** taskmgrv2 has its own
   fresh `precomp.h`, `taskmgrv2.h`, `resource.h`. We do not
   `#include "../taskmgr/foo.h"` from anywhere.
4. **The only edit outside `taskmgrv2/` is one line** in
   `base/applications/CMakeLists.txt`:
   `add_subdirectory(taskmgrv2)`, inserted after `add_subdirectory(taskmgr)`
   to keep the alphabetical ordering.
5. No `--modern` flag. No shared registry switch with classic. taskmgrv2
   stores everything under `HKCU\Software\ReactOS\TaskMgrV2\`. Single-
   instance mutex name `taskmgrv2ros` (distinct from classic's
   `taskmgrros`, so both apps can run concurrently if the user wants
   side-by-side comparisons).
6. MinGW-friendly C only (no C++). Win32 + NDK headers, same style as
   classic. GDI+ via the C flat API (`GdipCreate*`).

---

## 1. Approach

Greenfield. The "additive vs replace" question from the prior draft is
moot — taskmgrv2 is its own program in its own folder, the classic app
keeps shipping unmodified, and the user (or the start menu shortcut)
chooses which to launch. Both can run simultaneously.

If/when taskmgrv2 reaches feature parity, a follow-up patch (out of
scope for this spec) can change the system32 default shortcut. That is
not this team's problem.

---

## 2. File list — `base/applications/taskmgrv2/`

All files live directly under `base/applications/taskmgrv2/` with two
exceptions: `res/` (icons + bitmap resources) and `res/modern/` (PNG
sidebar icons). No other subdirectories.

### 2.1 Source files

| File | Purpose |
|---|---|
| `taskmgrv2.c` / `taskmgrv2.h` | `wWinMain` entry, single-instance mutex, top-level state, settings load/save, helpers shared by all pages. |
| `precomp.h` | Precompiled header for the whole app. Pulls in `<windef.h>`, `<winbase.h>`, `<wingdi.h>`, `<winuser.h>`, `<commctrl.h>`, `<shellapi.h>`, NDK as needed, then `taskmgrv2.h`, `theme.h`, `perfdata.h`, `cpu_topology.h`, `graphctl.h`, `shell.h`, `pages.h`. |
| `resource.h` | Resource ID master header for taskmgrv2. Fresh, no overlap with classic. |
| `taskmgrv2.rc` | Top-level resource script: app icon, version info, accelerators, all dialog templates, all string tables. Includes everything via `#include` from a single root. |
| `shell.c` / `shell.h` | Outer top-level window, sidebar nav rail, content host, hamburger toggle, settings page docking, main message pump, refresh timer, page registry. |
| `theme.c` / `theme.h` | Win11 light/dark color tables, accent color, fonts, DPI scaling helpers, GDI+ init/shutdown, fluent geometry helpers (rounded rect, hairline). |
| `perfdata.c` / `perfdata.h` | The taskmgrv2 perf backend: per-LP, NUMA, topology, brand, cache, memory, counts. **Standalone — no relationship to the classic file with the same name; the two never link together.** Implements the contract from task #4. |
| `cpu_topology.c` / `cpu_topology.h` | Pure CPUID layer: vendor detect, brand string, topology (sockets/cores/LPs), cache (L1/L2/L3), virtualization flag. |
| `graphctl.c` / `graphctl.h` | Graph control: single graph, mini-graph grid (Logical processors mode), kernel-times overlay, fluent fill via GDI+. |
| `pages.h` | Common page-vtbl typedefs and the `MODERN_PAGE_ID` enum used by both `shell.c` and every page. |
| `page_performance.c` / `page_performance.h` | Performance page: left category list (CPU/Mem/Disk/Net/GPU), right detail pane, embeds `graphctl`. |
| `page_processes.c` / `page_processes.h` | Processes page: grouped list, expandable rows, columns, end-task. |
| `page_details.c` / `page_details.h` | Details page: flat ListView per process, all columns. |
| `page_services.c` / `page_services.h` | Services list (`OpenSCManagerW` + `EnumServicesStatusExW`). |
| `page_users.c` / `page_users.h` | Logged-on users list via wtsapi32. |
| `page_startup.c` / `page_startup.h` | Startup apps list (Run keys + Startup folder). |
| `page_apphistory.c` / `page_apphistory.h` | App history page (placeholder content; data layer is a no-op stub — see open question 5). |
| `page_settings.c` / `page_settings.h` | Settings page (5 controls — see §3.9.1). |
| `registry.c` / `registry.h` | Tiny helper layer over `RegOpenKeyExW` / `RegQueryValueExW` / `RegSetValueExW` for DWORD + string. Used by every page that persists state. |

### 2.2 Resource files

**v1 ships with a single placeholder BMP icon strip — no PNG pack.**
This is decided (open question 3 → answered: ship the BMP, mark as TODO
to replace with a Fluent PNG pack later). The shell still supports the
PNG path in code, so a future patch can drop in PNGs without touching
shell.c.

| File | Purpose |
|---|---|
| `res/taskmgrv2.ico` | App icon, copied to system32 with the binary. **Fresh asset, do NOT reference legacy `taskmgr.ico`.** |
| `res/icons.bmp` | **v1 ship asset.** Monochrome (1bpp) bitmap strip, 14 cells x 1 row. Each cell 20x20. Cell order, left to right: `processes, performance, apphistory, startup, users, details, services, settings, hamburger, cpu, memory, disk, net, gpu` (cells 9..13 are 24x24 — see below). The first 9 cells are 20x20 nav-rail icons; cells 9..13 are 24x24 perf-category icons, so the strip is actually two rows: row 0 = 9 cells of 20x20 = 180px wide; row 1 = 5 cells of 24x24 = 120px wide. Total bitmap dimensions: 180x44, with the second row offset down by 20px and left-aligned. Mask color: pure white (`RGB(255,255,255)`), drawn via `TransparentBlt`. |
| `res/modern/` (subfolder) | Reserved for the future Fluent PNG drop-in. **Empty in v1**, but the folder is created so a follow-up patch can add `icon_processes.png` etc. without re-running CMake configure. |

> TODO (post-v1): replace `res/icons.bmp` with a license-clean Fluent
> PNG pack at `res/modern/icon_*.png` (14 files: 9x 20x20 nav, 5x 24x24
> perf-category). Sized for 96 DPI; the shell up-scales via GDI+. The
> shell's icon loader tries the PNG path first and falls back to the
> BMP strip if any PNG is missing — no code change needed when the art
> lands.

### 2.3 Resource ID layout (`resource.h`)

Fresh and conflict-free, since taskmgrv2 has no link to classic.

| Range | Use |
|---|---|
| 100..199 | Top-level: `IDI_TASKMGRV2`, `IDR_MAINFRAME`, `IDR_TASKMGRV2_MENU`, version info, accelerators. |
| 1000..1099 | Dialog templates (`IDD_*`). |
| 1100..1199 | Control IDs (`IDC_*`). |
| 1200..1299 | Menu / command IDs (`IDM_*`, `IDC_CTX_*`). |
| 1300..1499 | String table (`IDS_*`). |
| 1500..1599 | Bitmap / icon IDs (`IDB_*`, `IDI_*` other than top-level). |

---

## 3. Module boundaries — function signatures

All exported functions are plain C. All strings are wide
(`WCHAR*` / `LPCWSTR`). Every header `#pragma once`. No header includes
anything from `base/applications/taskmgr/`.

### 3.1 `taskmgrv2.h`

```c
extern HINSTANCE g_hInst;
extern HWND      g_hMainWnd;
extern HANDLE    g_hMutex;

int  TmgrV2_Run(HINSTANCE hInst, LPWSTR lpCmdLine, int nCmdShow);
void TmgrV2_LoadSettings(void);
void TmgrV2_SaveSettings(void);

/* Last-resort error popup that doesn't depend on the shell window. */
void TmgrV2_FatalError(LPCWSTR title, LPCWSTR fmt, ...);
```

### 3.2 `pages.h`

```c
typedef enum _MODERN_PAGE_ID {
    MPAGE_PROCESSES = 0,
    MPAGE_PERFORMANCE,
    MPAGE_APPHISTORY,
    MPAGE_STARTUP,
    MPAGE_USERS,
    MPAGE_DETAILS,
    MPAGE_SERVICES,
    MPAGE_SETTINGS,
    MPAGE__COUNT
} MODERN_PAGE_ID;

typedef HWND (CALLBACK *PAGE_CREATE_FN)(HWND hHost, HINSTANCE hInst);
typedef void (CALLBACK *PAGE_TICK_FN)  (HWND hPage);
typedef void (CALLBACK *PAGE_RESIZE_FN)(HWND hPage, int cx, int cy);
typedef void (CALLBACK *PAGE_DESTROY_FN)(HWND hPage);
typedef void (CALLBACK *PAGE_THEME_FN) (HWND hPage); /* called on dark/light flip */

typedef struct _PAGE_VTBL {
    LPCWSTR          displayName;     /* IDS_NAV_* string */
    int              iconResId;       /* IDB_NAV_* */
    PAGE_CREATE_FN   pfnCreate;
    PAGE_TICK_FN     pfnTick;         /* nullable */
    PAGE_RESIZE_FN   pfnResize;       /* nullable */
    PAGE_DESTROY_FN  pfnDestroy;      /* nullable */
    PAGE_THEME_FN    pfnTheme;        /* nullable */
} PAGE_VTBL;
```

### 3.3 `shell.h`

```c
HWND    Shell_GetMainWnd(void);
HWND    Shell_GetContentHost(void);
void    Shell_NavigateTo(MODERN_PAGE_ID id);
MODERN_PAGE_ID Shell_GetCurrentPage(void);
void    Shell_RegisterPage(MODERN_PAGE_ID id, const PAGE_VTBL *vtbl);
void    Shell_RequestRefresh(void);          /* posts WM_TIMER */
DWORD   Shell_GetRefreshIntervalMs(void);    /* default 1000 */
void    Shell_SetRefreshIntervalMs(DWORD ms);/* clamped 250..5000 */
BOOL    Shell_IsDarkMode(void);
void    Shell_SetDarkMode(BOOL bDark);
BOOL    Shell_IsSidebarExpanded(void);
void    Shell_SetSidebarExpanded(BOOL bExpanded);

/* Called once from TmgrV2_Run. Owns the main window class registration,
 * page registration, message pump, and shutdown. Returns the message
 * loop's wParam. */
int     Shell_Main(HINSTANCE hInst, LPWSTR lpCmdLine, int nCmdShow);
```

### 3.4 `theme.h`

```c
typedef struct _MODERN_THEME {
    COLORREF clrBg;            /* page background */
    COLORREF clrBgAlt;         /* sidebar / card background */
    COLORREF clrText;          /* primary text */
    COLORREF clrTextDim;       /* secondary text */
    COLORREF clrAccent;        /* selection / accent pill */
    COLORREF clrAccentText;    /* text on accent */
    COLORREF clrDivider;       /* 1px hairline */
    COLORREF clrGraphLine;     /* CPU graph stroke */
    COLORREF clrGraphFill;     /* base color for the alpha gradient */
    COLORREF clrGraphKernel;   /* kernel-times overlay */
    COLORREF clrGraphGrid;     /* graph grid lines */
    COLORREF clrCardBorder;    /* mini-graph border */
    BYTE     graphFillAlpha;   /* 0..255 — see §8 */
} MODERN_THEME;

BOOL              Theme_Init(void);
void              Theme_Shutdown(void);
const MODERN_THEME *Theme_Current(void);
void              Theme_SetDark(BOOL bDark);
BOOL              Theme_IsDark(void);

HFONT             Theme_FontUI(void);          /* Segoe UI Variable Display 9pt fallback Segoe UI 9pt */
HFONT             Theme_FontUISmall(void);     /* 8pt */
HFONT             Theme_FontTitle(void);       /* 14pt SemiBold */
HFONT             Theme_FontHeading(void);     /* 18pt SemiBold */
HFONT             Theme_FontMono(void);        /* Cascadia Mono fallback Consolas 9pt */
HFONT             Theme_FontStatNumber(void);  /* 24pt Light — the big "2%" number */

int               Theme_DpiScale(int unscaledPx);
void              Theme_SetShellDpi(UINT dpi);
UINT              Theme_GetShellDpi(void);

HBRUSH            Theme_BrushBg(void);
HBRUSH            Theme_BrushBgAlt(void);
HBRUSH            Theme_BrushAccent(void);
HPEN              Theme_PenDivider(void);
HPEN              Theme_PenGraphLine(void);
HPEN              Theme_PenGraphKernel(void);

void              Theme_FillRoundRect(HDC hdc, const RECT *rc, int radius, COLORREF clr);
void              Theme_DrawHairline(HDC hdc, int x1, int y1, int x2, int y2);
```

### 3.5 `perfdata.h` — **MUST exactly match task #4**

```c
#pragma once

#define PERFV2_HISTORY_SECONDS  60
#define PERFV2_MAX_LPS         256

typedef struct _PERFV2_TOPOLOGY {
    DWORD Sockets;
    DWORD Cores;            /* total physical cores across all sockets */
    DWORD LogicalProcessors;
} PERFV2_TOPOLOGY;

typedef struct _PERFV2_CACHE {
    SIZE_T L1Bytes;         /* sum of L1D + L1I across cores */
    SIZE_T L2Bytes;         /* sum across cores */
    SIZE_T L3Bytes;         /* shared, single value */
} PERFV2_CACHE;

BOOL    PerfDataV2Initialize(void);
void    PerfDataV2Uninitialize(void);

/* Called once per refresh interval (default 1000ms) by the shell. */
void    PerfDataV2Tick(void);

DWORD   PerfV2_GetLPCount(void);
DWORD   PerfV2_GetNumaNodeCount(void);

/* Out: utilization% history, newest at out_array[out_count-1].
 * Caller passes out_array of at least PERFV2_HISTORY_SECONDS bytes (BYTE).
 * out_count is set to the number of valid samples.
 * For kernel-times history use the dedicated *KernelHistory* call. */
BOOL    PerfV2_GetLPHistory          (DWORD lpIndex, BYTE *out_array, DWORD *out_count);
BOOL    PerfV2_GetLPKernelHistory    (DWORD lpIndex, BYTE *out_array, DWORD *out_count);
BOOL    PerfV2_GetOverallHistory     (BYTE *out_array, DWORD *out_count);
BOOL    PerfV2_GetOverallKernelHistory(BYTE *out_array, DWORD *out_count);
BOOL    PerfV2_GetNumaHistory        (DWORD nodeIndex, BYTE *out_array, DWORD *out_count);
BOOL    PerfV2_GetNumaKernelHistory  (DWORD nodeIndex, BYTE *out_array, DWORD *out_count);

LPCWSTR PerfV2_GetCpuBrandString(void);                     /* internal static buffer */
void    PerfV2_GetTopology(PERFV2_TOPOLOGY *out);
void    PerfV2_GetCacheSizes(SIZE_T *out_l1, SIZE_T *out_l2, SIZE_T *out_l3);

DWORD   PerfV2_GetSpeedMHz(void);
DWORD   PerfV2_GetBaseSpeedMHz(void);
BOOL    PerfV2_IsVirtualizationEnabled(void);

ULONG64 PerfV2_GetUptime(void);                              /* seconds */
void    PerfV2_GetCounts(DWORD *out_proc, DWORD *out_thr, DWORD *out_hnd);

void    PerfV2_GetMemory(ULONG64 *out_total_bytes,
                         DWORD   *out_inuse_pct,
                         ULONG64 *out_committed_bytes,
                         ULONG64 *out_limit_bytes,
                         ULONG64 *out_cached_bytes,
                         ULONG64 *out_paged_pool_bytes,
                         ULONG64 *out_nonpaged_pool_bytes);

ULONG64 PerfV2_GetNumaProcessorMask(DWORD nodeIndex);

/* Single-value snapshots of the latest tick. */
DWORD   PerfV2_GetCurrentLPUtilization(DWORD lpIndex);
DWORD   PerfV2_GetCurrentLPKernelUtilization(DWORD lpIndex);
DWORD   PerfV2_GetCurrentOverallUtilization(void);
DWORD   PerfV2_GetCurrentOverallKernelUtilization(void);
```

Implementation notes for task #4:

- Per-LP times come from `NtQuerySystemInformation(SystemProcessor
  PerformanceInformation, buf, n * sizeof(SYSTEM_PROCESSOR_PERFORMANCE
  _INFORMATION), &len)`. Canonical reference call is in legacy
  `base/applications/taskmgr/perfdata.c:204-205`. Copy that pattern;
  do not call into the legacy code.
- Memory: `SystemPerformanceInformation`, `SystemBasicInformation`,
  `SystemFileCacheInformation`, `SystemTimeOfDayInformation`.
- Counts: `SystemHandleInformation` for handle count; walk
  `SystemProcessInformation` for processes/threads.
- NUMA: `SystemNumaProcessorMap` (NDK class index 55). **Required
  fallback:** if the call returns `STATUS_NOT_IMPLEMENTED`, an empty
  buffer, or any failure status (likely on dev-smp-2 today), the
  backend reports a single NUMA node containing every LP. Concretely:
  `PerfV2_GetNumaNodeCount()` returns 1, and
  `PerfV2_GetNumaProcessorMask(0)` returns `(1ULL << LPCount) - 1` for
  `LPCount <= 64`. v1 does not support more than 64 LPs (open the
  topic in §11.2 if needed). The page grays the NUMA menu item when
  the count is ≤ 1 (see §6.3 / §7).
- Topology / brand / cache: CPUID via `__cpuid` from `<intrin.h>`.
- Thread safety: a single `CRITICAL_SECTION g_perfV2Lock`. Tick takes
  it exclusively; accessors take it briefly to copy out their result.
- The 60-second ring buffers are `BYTE[60]` per series (utilization%
  clipped to 0..100). One ring per LP, one ring per NUMA node, one
  ring for overall, plus matching kernel-times rings.

### 3.6 `cpu_topology.h`

```c
typedef enum _CPU_VENDOR { CPU_VENDOR_UNKNOWN, CPU_VENDOR_INTEL, CPU_VENDOR_AMD } CPU_VENDOR;

void       CpuTopo_Detect(void);                /* idempotent */
CPU_VENDOR CpuTopo_GetVendor(void);
LPCWSTR    CpuTopo_GetBrandString(void);        /* widened ASCII from CPUID 0x80000002..4 */
void       CpuTopo_GetCounts(DWORD *out_sockets, DWORD *out_cores, DWORD *out_lps);
void       CpuTopo_GetCacheSizes(SIZE_T *out_l1, SIZE_T *out_l2, SIZE_T *out_l3);
BOOL       CpuTopo_HasVirtualization(void);     /* CPUID.1:ECX.VMX or CPUID.80000001:ECX.SVM */
DWORD      CpuTopo_GetMaxMhzHint(void);         /* parsed from brand string, 0 if not found */
```

### 3.7 `graphctl.h`

```c
typedef enum _GC_MODE {
    GC_MODE_SINGLE,        /* one full-area line+fill graph */
    GC_MODE_GRID           /* NxM mini-graphs, one per series */
} GC_MODE;

typedef struct _GC_FORMAT {
    COLORREF clrBg;
    COLORREF clrLine;
    COLORREF clrFill;
    BYTE     fillAlpha;
    COLORREF clrKernel;
    COLORREF clrGrid;
    COLORREF clrBorder;
    int      gridCellPx;    /* unscaled */
    BOOL     showKernel;
    BOOL     summaryView;
} GC_FORMAT;

typedef struct _GC_CONTROL GC_CONTROL;       /* opaque */

GC_CONTROL* GraphCtl_Create (HWND hParent, int ctlId, const GC_FORMAT *fmt);
void        GraphCtl_Destroy(GC_CONTROL *gc);
HWND        GraphCtl_Hwnd   (GC_CONTROL *gc);

void        GraphCtl_SetMode    (GC_CONTROL *gc, GC_MODE mode);
void        GraphCtl_SetSeries  (GC_CONTROL *gc, DWORD seriesCount);
void        GraphCtl_SetFormat  (GC_CONTROL *gc, const GC_FORMAT *fmt);
void        GraphCtl_SetTitles  (GC_CONTROL *gc, LPCWSTR const *titles, DWORD count);
void        GraphCtl_SetShowKernel(GC_CONTROL *gc, BOOL bShow);
void        GraphCtl_SetSummaryView(GC_CONTROL *gc, BOOL bSummary);

void        GraphCtl_PushFrame  (GC_CONTROL *gc, const BYTE *values, const BYTE *kernels, DWORD count);
void        GraphCtl_SetHistory (GC_CONTROL *gc, DWORD seriesIdx,
                                 const BYTE *vals, const BYTE *kernels,
                                 DWORD count);

void        GraphCtl_Invalidate (GC_CONTROL *gc);
int         GraphCtl_HitTestTile(GC_CONTROL *gc, POINT pt);  /* -1 if no tile */
```

> Naming note: this control lives in `taskmgrv2/graphctl.c`. The legacy
> classic also has `graphctl.c` — the two are different files, in
> different folders, in different binaries, and never share a translation
> unit. The simple name is allowed because there is no link conflict.

### 3.8 `page_performance.h`

```c
typedef enum _PERF_CATEGORY {
    PERFCAT_CPU = 0,
    PERFCAT_MEMORY,
    PERFCAT_DISK,
    PERFCAT_NET,
    PERFCAT_GPU,
    PERFCAT__COUNT
} PERF_CATEGORY;

typedef enum _CPU_GRAPH_MODE {
    CPU_GMODE_OVERALL = 0,
    CPU_GMODE_LOGICAL,
    CPU_GMODE_NUMA
} CPU_GRAPH_MODE;

HWND  PagePerf_Create  (HWND hHost, HINSTANCE hInst);
void  PagePerf_Tick    (HWND hPage);
void  PagePerf_Resize  (HWND hPage, int cx, int cy);
void  PagePerf_Destroy (HWND hPage);
void  PagePerf_Theme   (HWND hPage);

void  PagePerf_SelectCategory (HWND hPage, PERF_CATEGORY cat);
void  PagePerf_SetCpuGraphMode(HWND hPage, CPU_GRAPH_MODE mode);
CPU_GRAPH_MODE PagePerf_GetCpuGraphMode(HWND hPage);

void  PagePerf_LoadPrefs(void);
void  PagePerf_SavePrefs(void);
```

### 3.9 Other pages — minimal export shape

```c
HWND  PageProcesses_Create  (HWND hHost, HINSTANCE hInst);
void  PageProcesses_Tick    (HWND hPage);
void  PageProcesses_Resize  (HWND hPage, int cx, int cy);
void  PageProcesses_Destroy (HWND hPage);
void  PageProcesses_Theme   (HWND hPage);
/* …same five for Details, Services, Users, Startup, AppHistory, Settings */
```

Each page is its own child window (`WS_CHILD | WS_CLIPCHILDREN`), classed
under `"TaskMgrV2.Page.<Name>"`. The shell creates exactly one instance
per page on first navigation and caches the HWND; on every subsequent
navigation it just shows/hides.

### 3.9.1 Settings page — v1 control set

The Settings page is an inline page in the sidebar (decided, see §11.10),
not a modal dialog. It hosts exactly five controls in v1, laid out in a
single vertical stack with 16 px gaps and 24 px outer padding:

| # | Control | Type | Persists to | Notes |
|---|---|---|---|---|
| 1 | **Theme** | radio group of 3 (Light / Dark / Use system setting) | `Shell\DarkMode` (0/1/2) | "Use system setting" is wired to the same DWORD value 2 but in v1 simply maps to Light at startup (TODO: query Personalize\AppsUseLightTheme later). The control is shown so users see the future option. |
| 2 | **Refresh interval** | radio group of 3 (Low = 4000ms / Medium = 1000ms / High = 250ms) | `Shell\RefreshIntervalMs` | Picks the closest match to the stored DWORD on load. Selecting a radio updates `Shell_SetRefreshIntervalMs`. |
| 3 | **Default startup page** | combo box, 8 entries from `MODERN_PAGE_ID` (Processes…Settings) | `Shell\DefaultPage` | The combo's index is the enum value. |
| 4 | **Show full account name on the Users page** | checkbox | `Shell\UsersShowFullName` (DWORD, default 0) | Page Users reads this DWORD on every tick. |
| 5 | **Warn before changing a process to Realtime priority** | checkbox | `Shell\RealtimeWarn` (DWORD, default 1) | Pages Processes and Details read this; if set, they put up a `MessageBoxW` confirmation before applying `REALTIME_PRIORITY_CLASS`. |

There is no "Use modern shell" toggle (the binary IS the modern shell).
There is no "Apply" or "OK" button — settings are saved on the spot when
the user changes a control (call `Shell_SetXxx` then `Reg_WriteDword`).

Settings page exports beyond the standard 5:

```c
void  PageSettings_OnThemeChanged(HWND hPage, BOOL bDark);
void  PageSettings_OnRefreshChanged(HWND hPage, DWORD ms);
void  PageSettings_OnDefaultPageChanged(HWND hPage, MODERN_PAGE_ID id);
void  PageSettings_OnUsersFullNameChanged(HWND hPage, BOOL bShow);
void  PageSettings_OnRealtimeWarnChanged(HWND hPage, BOOL bWarn);
```

### 3.10 `registry.h`

```c
BOOL    Reg_ReadDword (HKEY hRoot, LPCWSTR subkey, LPCWSTR value, DWORD *out);
BOOL    Reg_WriteDword(HKEY hRoot, LPCWSTR subkey, LPCWSTR value, DWORD val);
BOOL    Reg_ReadString(HKEY hRoot, LPCWSTR subkey, LPCWSTR value, LPWSTR out, DWORD ccOut);
BOOL    Reg_WriteString(HKEY hRoot, LPCWSTR subkey, LPCWSTR value, LPCWSTR val);
```

All persistence keys live under `HKCU\Software\ReactOS\TaskMgrV2\` and
its subkeys (see §7.1).

---

## 4. Sidebar nav layout

Pixel sizes are at 96 DPI; multiply by `Theme_DpiScale()` at runtime.

```
+------------------------------------------------------------------------+
|  hamburger (40x40)                                                     |
+------+-----------------------------------------------------------------+
|  P   |                                                                 |
|  P   |                                                                 |
|  A   |                                                                 |
|  S   |                            CONTENT HOST                         |
|  U   |                            (current page)                       |
|  D   |                                                                 |
|  V   |                                                                 |
|  ... |                                                                 |
+------+                                                                 |
|  S   |                                                                 |
+------+-----------------------------------------------------------------+
```

### 4.1 Sidebar geometry

| Property | Expanded | Collapsed |
|---|---|---|
| Rail width | **220 px** | **48 px** |
| Hamburger button height | 40 px | 40 px |
| Nav item height | **48 px** | 48 px |
| Nav item icon box | 20x20 (left-padded 14 px) | 20x20 (centered) |
| Nav item label X | 48 px (after icon column) | hidden |
| Selection accent pill | 3 px wide x 24 px tall, **left** edge of item, vertically centered, accent color, 1.5 px corner radius | same |
| Hover background | `clrBgAlt` blended +6% accent | same |
| Settings button | pinned to bottom of rail, 48 px tall | same |
| Divider | 1 px `clrDivider`, vertical, on the right edge of rail | same |
| Padding above first item | 8 px | 8 px |
| Spacing between items | 0 px (items are flush) | 0 px |

### 4.2 Nav item order (top to bottom)

1. Processes  → `MPAGE_PROCESSES`
2. Performance → `MPAGE_PERFORMANCE`
3. App history → `MPAGE_APPHISTORY`
4. Startup apps → `MPAGE_STARTUP`
5. Users → `MPAGE_USERS`
6. Details → `MPAGE_DETAILS`
7. Services → `MPAGE_SERVICES`
8. *(spring)*
9. Settings → `MPAGE_SETTINGS` (always pinned to bottom)

### 4.3 Fonts

- Nav labels: `Theme_FontUI()` — Segoe UI Variable Display 9pt regular,
  fallback Segoe UI 9pt regular.
- Settings/About: same.
- Page titles (top of each page): `Theme_FontHeading()` — 18pt SemiBold.
- Stat numbers: `Theme_FontStatNumber()` — 24pt Light.
- Stat labels: `Theme_FontUISmall()` — 8pt regular dim.

Fallback chain is implemented in `Theme_Init` by trying
`EnumFontFamiliesEx` for each name in turn and storing the first hit.

### 4.4 Sidebar icons

v1 ships **only** the BMP strip `res/icons.bmp` (see §2.2 for cell
layout). The shell loads it once at startup via `LoadBitmapW` and
slices the cells into 14 cached HBITMAPs at first use, painting them
with `TransparentBlt` keyed on `RGB(255,255,255)`.

The shell's icon loader is structured so that adding the future Fluent
PNG pack at `res/modern/icon_*.png` (TODO, post-v1) is a drop-in: when
the PNG files are present, GDI+ decoding takes over and the BMP strip
is ignored. No code change required.

---

## 5. Performance page layout

Two columns. The screenshot reference is the AMD Ryzen 9 7950X / 8-LP
example.

```
+-------------------------------------------------------------------------+
|  Performance                                       <heading 18pt>       |
+-----------------------+-------------------------------------------------+
| CPU       2%  4.50GHz |  CPU                AMD Ryzen 9 7950X 16-Core   |
| [mini-graph 168x42]   |                                                 |
|-----------------------|  +-------------------------------------------+  |
| Memory  30/64 GB      |  |              MAIN GRAPH AREA              |  |
| [mini-graph 168x42]   |  |   (single big graph OR NxM tile grid)     |  |
|-----------------------|  |                                           |  |
| Disk 0 (C:) 4%        |  |                                           |  |
| [mini-graph 168x42]   |  |                                           |  |
|-----------------------|  |                                           |  |
| Ethernet 0 R 1 Mbps   |  +-------------------------------------------+  |
| [mini-graph 168x42]   |  60 seconds                            <axis>   |
|-----------------------|                                                 |
| GPU 0  (placeholder)  |  +---- Stats grid (2 columns x 6 rows) -------+ |
| [mini-graph 168x42]   |  | Utilization 2%      Maximum speed 4.50 GHz | |
|                       |  | Speed 4.50 GHz      Sockets 1              | |
|                       |  | Processes 227       Cores 8                | |
|                       |  | Threads 2700        Logical processors 8   | |
|                       |  | Handles 116790      Virtualization Enabled | |
|                       |  | Up time 1:19:39:13  L1 cache 512 KB        | |
|                       |  |                     L2 cache 8.0 MB        | |
|                       |  |                     L3 cache 32.0 MB       | |
|                       |  +--------------------------------------------+ |
+-----------------------+-------------------------------------------------+
```

### 5.1 Dimensions (96 DPI)

| Region | Size |
|---|---|
| Page padding (outer) | 24 px all sides |
| Heading row height | 32 px |
| Left category list | **256 px** wide |
| Category list item height | **76 px** |
| Category mini-graph inside item | **168 x 42 px**, top-right of item |
| Category list selected accent pill | 3 x 24 px, left edge |
| Right detail pane padding | 24 px all sides |
| CPU brand label (top right of detail pane) | right-aligned, 12pt SemiBold dim |
| Main graph area | fills detail pane width, **height = max(220, paneHeight - statsHeight - headerHeight - 24)** |
| Main graph y-axis range | 0..100 % |
| Main graph time axis | "60 seconds" left, "0" right, 8pt dim |
| Stats grid | fixed **216 px** tall, two columns of equal width, 8 rows. Numbers 24pt Light, labels 9pt dim above |

### 5.2 Layout in code

The page uses manual layout in its `WM_SIZE` handler — no resource
template for the body. The splitter between left list and right pane is
**fixed**, not draggable, in v1.

### 5.3 Graph rendering

- Stroke width: 1.5 px (use a GDI+ `Pen` from the C flat API).
- Fill: vertical gradient from `clrLine` at top to `clrBg` at bottom, at
  `graphFillAlpha` (see §8).
- Kernel-times overlay: same line shape, drawn with a darker variant of
  the line color (`Theme_PenGraphKernel()`). Fill alpha for the kernel
  series: 0 (line only).
- Grid: light hairlines every 10% vertically, every 6 seconds
  horizontally. In `summaryView` mode, the grid is hidden entirely.
- Border: 1 px `clrCardBorder` rounded 2 px corners around the graph
  card.

---

## 6. CPU graph modes

Three modes mapped to the right-click context menu (§7).

### 6.1 Overall

Single graph, full pane. Series = `PerfV2_GetOverallHistory`. Optional
kernel overlay = `PerfV2_GetOverallKernelHistory`.

### 6.2 Logical processors — auto-grid algorithm

Given `LP = PerfV2_GetLPCount()` and the available graph rect of width
`W` and height `H`:

```
aspect = (double)W / (double)H;
cols   = (DWORD)ceil(sqrt((double)LP * aspect));
if (cols == 0) cols = 1;
rows   = (LP + cols - 1) / cols;          /* ceil(LP/cols) */

/* Re-balance so we don't get a near-empty last row */
while (rows > 1 && (rows - 1) * cols >= LP) rows--;

tileW = (W - (cols + 1) * gutter) / cols;
tileH = (H - (rows + 1) * gutter) / rows;
```

`gutter = Theme_DpiScale(4)`. Tiles are filled left-to-right,
top-to-bottom in LP-index order. Worked examples:

| LP count | aspect | `ceil(sqrt(LP*aspect))` | rebalanced (cols x rows) |
|---|---|---|---|
| 1  | 1.78 | 2 | 1 x 1 |
| 4  | 1.78 | 3 → 2 (rebalance) | 2 x 2 |
| 8  | 1.78 | 4 | 4 x 2 (matches the screenshot) |
| 16 | 1.78 | 6 | 6 x 3 |
| 32 | 1.78 | 8 | 8 x 4 |
| 64 | 1.78 | 11 | 11 x 6 |

Each tile renders as a single mini-graph: line + alpha-fill, no axis,
no labels by default. In `summaryView` mode the tile is even barer (no
border). In normal mode each tile shows the LP index ("CPU 0",
"CPU 1", …) at the top-left in 8pt dim.

### 6.3 NUMA

Series count = `PerfV2_GetNumaNodeCount()`. **Aggregation rule**: for
each node, the shown utilization is the **arithmetic mean** of the
per-LP utilization values for the LPs in `PerfV2_GetNumaProcessorMask`,
each weighted by 1 (i.e. unweighted mean). The backend
(`PerfV2_GetNumaHistory`) computes this so the page just consumes it.
If the system reports a single NUMA node, this mode renders identically
to Overall and is grayed in the menu.

NUMA mode uses the same auto-grid algorithm (§6.2) but on
`NumaNodeCount` instead of `LPCount`.

---

## 7. Context menu

Right-clicking the main graph area (any mode) shows a popup. Menu IDs
live in `resource.h` at 1200..1299.

```
Change graph to     ▸  Overall utilization        IDC_CTX_MODE_OVERALL  1210
                       Logical processors         IDC_CTX_MODE_LOGICAL  1211
                       NUMA nodes (grayed if N=1) IDC_CTX_MODE_NUMA     1212
─────────────────
Show kernel times      checkmark                  IDC_CTX_KERNEL        1220
Graph summary view     checkmark                  IDC_CTX_SUMMARY       1221
─────────────────
View                ▸  Refresh now      F5        IDC_CTX_REFRESH       1230
                       Update speed     ▸                                1231..1234
                       (High / Normal / Low / Paused)
─────────────────
Copy                Ctrl+C                        IDC_CTX_COPY          1240
```

Accelerators are loaded from `taskmgrv2.rc` accelerator table
`IDA_PERF`:

| Key | ID |
|---|---|
| F5 | `IDC_CTX_REFRESH` |
| Ctrl+C | `IDC_CTX_COPY` |
| Ctrl+1 | `IDC_CTX_MODE_OVERALL` |
| Ctrl+2 | `IDC_CTX_MODE_LOGICAL` |
| Ctrl+3 | `IDC_CTX_MODE_NUMA` |
| Ctrl+K | `IDC_CTX_KERNEL` |

### 7.1 Persistence

All keys live under `HKCU\Software\ReactOS\TaskMgrV2\`. Subkeys:

`HKCU\Software\ReactOS\TaskMgrV2\PerfPage`:

| Value | Type | Default | Meaning |
|---|---|---|---|
| `CpuGraphMode` | DWORD | 0 | 0 = overall, 1 = logical, 2 = NUMA |
| `ShowKernelTimes` | DWORD | 0 | bool |
| `SummaryView` | DWORD | 0 | bool |
| `LastCategory` | DWORD | 0 | `PERF_CATEGORY` enum |

`HKCU\Software\ReactOS\TaskMgrV2\Shell`:

| Value | Type | Default | Meaning |
|---|---|---|---|
| `DarkMode` | DWORD | 0 | 0 = light, 1 = dark, 2 = follow system (maps to light in v1) |
| `SidebarExpanded` | DWORD | 1 | bool |
| `RefreshIntervalMs` | DWORD | 1000 | clamped 250..5000; settings UI exposes only 250/1000/4000 |
| `DefaultPage` | DWORD | 1 | `MODERN_PAGE_ID` to land on |
| `UsersShowFullName` | DWORD | 0 | bool, controls Users-page display |
| `RealtimeWarn` | DWORD | 1 | bool, prompt before applying REALTIME_PRIORITY_CLASS |
| `WindowPlacement` | binary | — | `WINDOWPLACEMENT` blob |

`HKCU\Software\ReactOS\TaskMgrV2\Processes`:

| Value | Type | Default | Meaning |
|---|---|---|---|
| `Columns` | binary | — | column visibility bitmask |
| `SortColumn` | DWORD | 0 | last sort column |
| `SortAscending` | DWORD | 1 | bool |

> Note: there is **no** `UseModernShell` value, **no** `Software\ReactOS
> \TaskMgr\Modern` subkey, **no** shared switch with classic. taskmgrv2
> reads and writes only its own `TaskMgrV2` tree.

---

## 8. Theming — exact COLORREFs

All values are `RGB(r,g,b)`. The "Win11 accent blue" matches the
Microsoft Fluent palette. The graph fill is rendered via GDI+ with the
listed alpha (0..255).

### 8.1 Light theme (default)

| Slot | Hex | RGB |
|---|---|---|
| `clrBg` | `#F3F3F3` | 243, 243, 243 |
| `clrBgAlt` | `#FAFAFA` | 250, 250, 250 |
| `clrText` | `#1C1C1C` | 28, 28, 28 |
| `clrTextDim` | `#5B5B5B` | 91, 91, 91 |
| `clrAccent` | `#0078D4` | 0, 120, 212 |
| `clrAccentText` | `#FFFFFF` | 255, 255, 255 |
| `clrDivider` | `#E5E5E5` | 229, 229, 229 |
| `clrGraphLine` | `#0078D4` | 0, 120, 212 |
| `clrGraphFill` | `#0078D4` | 0, 120, 212 |
| `clrGraphKernel` | `#1B3A57` | 27, 58, 87 |
| `clrGraphGrid` | `#E0E0E0` | 224, 224, 224 |
| `clrCardBorder` | `#D6D6D6` | 214, 214, 214 |
| `graphFillAlpha` | — | **64** (≈25%) |

### 8.2 Dark theme

| Slot | Hex | RGB |
|---|---|---|
| `clrBg` | `#202020` | 32, 32, 32 |
| `clrBgAlt` | `#2B2B2B` | 43, 43, 43 |
| `clrText` | `#F2F2F2` | 242, 242, 242 |
| `clrTextDim` | `#A6A6A6` | 166, 166, 166 |
| `clrAccent` | `#60CDFF` | 96, 205, 255 |
| `clrAccentText` | `#000000` | 0, 0, 0 |
| `clrDivider` | `#3A3A3A` | 58, 58, 58 |
| `clrGraphLine` | `#60CDFF` | 96, 205, 255 |
| `clrGraphFill` | `#60CDFF` | 96, 205, 255 |
| `clrGraphKernel` | `#9EE6FF` | 158, 230, 255 |
| `clrGraphGrid` | `#3A3A3A` | 58, 58, 58 |
| `clrCardBorder` | `#3F3F3F` | 63, 63, 63 |
| `graphFillAlpha` | — | **64** |

The fill is implemented via a `LinearGradientBrush` from `clrGraphLine`
at the top to `clrBg` at the bottom, applied at `graphFillAlpha` —
`GdipCreateLineBrush` from the GDI+ flat C API. No C++ required.

---

## 9. Build integration

### 9.1 New file: `base/applications/taskmgrv2/CMakeLists.txt`

```cmake

list(APPEND SOURCE
    taskmgrv2.c
    shell.c
    theme.c
    perfdata.c
    cpu_topology.c
    graphctl.c
    registry.c
    page_performance.c
    page_processes.c
    page_details.c
    page_services.c
    page_users.c
    page_startup.c
    page_apphistory.c
    page_settings.c
    precomp.h)

file(GLOB taskmgrv2_rc_deps res/*.* res/modern/*.*)
add_rc_deps(taskmgrv2.rc ${taskmgrv2_rc_deps})

add_executable(taskmgrv2 ${SOURCE} taskmgrv2.rc)
set_module_type(taskmgrv2 win32gui UNICODE)
add_importlibs(taskmgrv2
    advapi32
    user32
    gdi32
    shell32
    shlwapi
    comctl32
    msvcrt
    kernel32
    ntdll
    uxtheme
    dwmapi
    wtsapi32
    powrprof
    gdiplus)
add_pch(taskmgrv2 precomp.h SOURCE)
add_cd_file(TARGET taskmgrv2 DESTINATION reactos/system32 FOR all)
```

### 9.2 One-line edit to the parent CMakeLists

`base/applications/CMakeLists.txt`, immediately after line 45
(`add_subdirectory(taskmgr)`):

```cmake
add_subdirectory(taskmgr)
add_subdirectory(taskmgrv2)
add_subdirectory(utilman)
```

This is the **only** edit anywhere outside `base/applications/taskmgrv2/`.

### 9.3 `taskmgrv2.rc`

Top-level RC. Includes (via `#include`) per-area sub-RC files only if
the implementer chooses to split — the spec is happy with one big file.
Required ingredients:

- `IDI_TASKMGRV2 ICON "res/taskmgrv2.ico"`
- `IDA_PERF ACCELERATORS` table for the keys in §7.
- `IDD_SETTINGS DIALOGEX` template for the Settings page (or built
  manually in C — implementer's choice).
- `IDR_TASKMGRV2_MENU MENU` for the optional menu bar (hidden by
  default in v1; the hamburger replaces it).
- `STRINGTABLE` blocks for `IDS_NAV_*`, `IDS_PERFCAT_*`, `IDS_STAT_*`.
- `IDB_NAV_*` BMP/PNG include lines via `RT_RCDATA`.
- Version info block (1.0.0.0 / "ReactOS Task Manager v2").
- One language block: English. Other languages added in a follow-up.

### 9.4 `precomp.h`

```c
#pragma once
#ifndef UNICODE
#error taskmgrv2 uses NDK functions, build must be UNICODE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winnls.h>
#include <winuser.h>
#include <winreg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <strsafe.h>

#include "taskmgrv2.h"
#include "resource.h"
#include "registry.h"
#include "theme.h"
#include "perfdata.h"
#include "cpu_topology.h"
#include "graphctl.h"
#include "pages.h"
#include "shell.h"
#include "page_performance.h"
#include "page_processes.h"
#include "page_details.h"
#include "page_services.h"
#include "page_users.h"
#include "page_startup.h"
#include "page_apphistory.h"
#include "page_settings.h"
```

No `#include` of anything under `../taskmgr/`.

---

## 10. Entry point

`taskmgrv2.c` defines a vanilla `wWinMain`:

```c
int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPWSTR    lpCmdLine,
                      int       nCmdShow)
{
    g_hInst   = hInstance;
    g_hMutex  = CreateMutexW(NULL, TRUE, L"taskmgrv2ros");
    if (g_hMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        /* Bring the existing taskmgrv2 window forward, then exit. */
        HWND hPrev = FindWindowW(L"TaskMgrV2.MainWindow", NULL);
        if (hPrev) {
            SendMessageW(hPrev, WM_SYSCOMMAND, SC_RESTORE, 0);
            SetForegroundWindow(hPrev);
        }
        CloseHandle(g_hMutex);
        return 0;
    }
    if (!g_hMutex) return 1;

    /* Match classic's behavior: high prio + SE_DEBUG_NAME */
    {
        HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());
        SetPriorityClass(hProc, HIGH_PRIORITY_CLASS);
        CloseHandle(hProc);
    }
    /* … OpenProcessToken / AdjustTokenPrivileges for SE_DEBUG_NAME … */

    TmgrV2_LoadSettings();

    if (!Theme_Init())          { TmgrV2_FatalError(L"taskmgrv2", L"Theme_Init failed"); return 2; }
    if (!PerfDataV2Initialize()){ TmgrV2_FatalError(L"taskmgrv2", L"PerfData init failed"); return 3; }
    CpuTopo_Detect();

    SetProcessShutdownParameters(1, SHUTDOWN_NORETRY);

    int rc = Shell_Main(hInstance, lpCmdLine, nCmdShow);

    TmgrV2_SaveSettings();
    PerfDataV2Uninitialize();
    Theme_Shutdown();
    CloseHandle(g_hMutex);
    return rc;
}
```

There is **no** code-flow branch into the legacy app, no `--modern`
flag, no fallback to classic. taskmgrv2 is its own program.

---

## 11. Decisions (locked) and post-v1 TODOs

All open questions from drafts 1 and 2 have been answered by the
team-lead. The architect is **not blocking** on anything; implementers
may start immediately.

### 11.1 Locked decisions

1. **Default UI** — N/A. taskmgrv2.exe is its own binary; users launch
   it directly. Whether the Start Menu / Ctrl+Shift+Esc shortcut should
   later point at taskmgrv2 is out of scope for this team.
2. **GDI+** — yes, link against `gdiplus`. Listed in §9.1 importlibs.
   Implementers MAY use the GDI+ flat C API freely; the gradient fill
   for graph areas is non-negotiable visual quality.
3. **Icons** — v1 ships a single placeholder monochrome BMP strip at
   `res/icons.bmp` (see §2.2). PNG Fluent pack is a post-v1 TODO; the
   shell's icon loader is structured to drop in PNGs without code
   changes.
4. **NUMA on dev-smp-2** — `perfdata.c` calls
   `NtQuerySystemInformation(SystemNumaProcessorMap, …)`. If it returns
   `STATUS_NOT_IMPLEMENTED`, an empty buffer, or any failure status,
   the backend silently falls back to **one** NUMA node containing
   every LP (mask = `(1ULL << LPCount) - 1` for ≤64 LPs;
   `0xFFFFFFFFFFFFFFFFULL` for >64 LPs which we don't support in v1).
   `PerfV2_GetNumaNodeCount` returns 1 in that fallback. The
   performance page MUST gray the "NUMA nodes" context-menu item when
   `PerfV2_GetNumaNodeCount() <= 1` (this is already specified in
   §6.3 and §7).
5. **App history page** — ships as a placeholder. Body content: a
   centered `STATIC` displaying the localized string `IDS_APPHISTORY_NA
   = "Application history is not available on this system."` Font:
   `Theme_FontUI()`. Color: `clrTextDim`. No tick handler, no data
   layer, no ETW. The Page tick callback is `NULL`.
6. **Services page** — uses `OpenSCManagerW` +
   `EnumServicesStatusExW`. No new importlib. Refreshes on the
   standard tick interval.
7. **Settings page v1 scope** — exactly the five controls listed in
   §3.9.1 (Theme, Refresh interval, Default startup page, Show full
   account name on the Users page, Realtime priority warning). No
   other controls in v1.
8. **Localization** — v1 is **English-only**. The implementer of
   `taskmgrv2.rc` writes a single English `LANGUAGE` block. Adding
   `lang/*.rc` is a post-v1 TODO; the resource layout is structured so
   that each string id maps to one row that future translators can
   touch in isolation.
9. **graphctl reuse** — every page that wants a graph (Performance
   sub-categories CPU/Mem/Disk/Net/GPU and the left-rail mini-graphs)
   uses `taskmgrv2/graphctl.c`. The legacy `base/applications/taskmgr
   /graphctl.c` is **not** reused, recompiled, or referenced. taskmgrv2
   owns ~300 lines of new graph code.
10. **Settings UI shape** — inline page in the sidebar (Win11 style),
    not a modal dialog. See §3.9.1 for the control list.
11. **Side-by-side execution with classic** — yes, allowed. taskmgrv2
    uses its own single-instance mutex `taskmgrv2ros` (distinct from
    classic's `taskmgrros`). Both apps may run simultaneously on the
    same desktop.
12. **Process termination privileges** — taskmgrv2 acquires
    `SE_DEBUG_NAME` at startup, same pattern as classic
    (`OpenProcessToken` + `LookupPrivilegeValueW` +
    `AdjustTokenPrivileges`). Required for End-task on a process owned
    by another user.

### 11.2 Post-v1 TODOs (do NOT implement in this team's tasks)

- TODO: replace `res/icons.bmp` with `res/modern/icon_*.png` Fluent
  pack (14 PNGs, see §2.2 layout).
- TODO: add `lang/*.rc` translations for the string table.
- TODO: wire `DarkMode == 2` ("follow system") to query
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\
  AppsUseLightTheme` and listen for `WM_SETTINGCHANGE`.
- TODO: implement Disk and Net pages with actual counters (v1 ships
  the categories but the data backend is a placeholder; perf-page
  implementer is told to render zero-history graphs gracefully).
- TODO: GPU page real data (v1 = placeholder).
- TODO: App history once ReactOS gains an equivalent of
  `Microsoft-Windows-Application-Resource-Usage` ETW.

---

## 12. Cross-task contract summary

| Task | File(s) owned | Depends on |
|---|---|---|
| #1 architect | this spec at `base/applications/taskmgrv2/SPEC.md` | — |
| #2 shell | `taskmgrv2.c/h`, `precomp.h`, `resource.h`, `taskmgrv2.rc`, `shell.c/h`, `theme.c/h`, `registry.c/h`, `pages.h`, `CMakeLists.txt` (new), parent `base/applications/CMakeLists.txt` (one line) | #1 |
| #3 perf-page | `page_performance.c/h`, `graphctl.c/h` | #1, #2, #4 |
| #4 perfdata | `perfdata.c/h`, `cpu_topology.c/h` | #1 |
| #5 other-pages | `page_processes.c/h`, `page_details.c/h`, `page_services.c/h`, `page_users.c/h`, `page_startup.c/h`, `page_apphistory.c/h`, `page_settings.c/h` | #1, #2 |
| #6 build/QA | builds, smoke test in QEMU, screenshots | #2..#5 |

Tasks #3 and #5 may be implemented in parallel once #2 has the page-host
contract honored. Task #4 has no UI dependency and can start immediately
after this spec is read. **None** of these tasks may modify any file
under `base/applications/taskmgr/`.

— end of spec —
