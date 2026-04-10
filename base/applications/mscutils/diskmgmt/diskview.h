/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/applications/mscutils/diskmgmt/diskview.h
 * PURPOSE:     Owner-drawn disk map helpers for the Disk Management UI
 */

#ifndef DISKMGMT_DISKVIEW_H
#define DISKMGMT_DISKVIEW_H

#include <windef.h>
#include <wingdi.h>
#include <winuser.h>

#include "snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DM_DISKVIEW_DEFAULT_ROW_HEIGHT  118
#define DM_DISKVIEW_MIN_REGION_WIDTH     72
#define DM_DISKVIEW_PARTITION_WIDTH     112
#define DM_DISKVIEW_LABEL_WIDTH         124
#define DM_DISKVIEW_REGION_GAP            1
#define DM_DISKVIEW_BAR_HEIGHT            8
#define DM_DISKVIEW_ROW_SPACING           4
#define DM_DISKVIEW_MARGIN_TOP            4
#define DM_DISKVIEW_MARGIN_BOTTOM         4
#define DM_DISKVIEW_LEGEND_HEIGHT        26

VOID
DmDiskViewFormatSize(
    _In_ ULONGLONG Size,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmDiskViewFormatPercent(
    _In_ ULONGLONG Total,
    _In_ ULONGLONG Free,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

PCWSTR
DmDiskViewPartitionStyleToString(
    _In_ PARTITION_STYLE Style);

VOID
DmDiskViewBuildDiskSummary(
    _In_ const DM_DISK *Disk,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmDiskViewBuildDiskStatus(
    _In_ const DM_DISK *Disk,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmDiskViewBuildRegionTitle(
    _In_ const DM_REGION *Region,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmDiskViewBuildRegionDetail(
    _In_ const DM_REGION *Region,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

INT
DmDiskViewComputeRegionWidths(
    _In_ const DM_DISK *Disk,
    _In_ INT AvailableWidth,
    _Out_writes_opt_(RegionCount) PINT Widths,
    _In_ SIZE_T RegionCount);

INT
DmDiskViewGetPreferredHeight(
    _In_ const DM_SNAPSHOT *Snapshot);

BOOL
DmDiskViewHitTest(
    _In_ const RECT *ClientRect,
    _In_ const DM_SNAPSHOT *Snapshot,
    _In_ INT ScrollOffset,
    _In_ const POINT *Point,
    _Outptr_opt_result_maybenull_ const DM_DISK **Disk,
    _Outptr_opt_result_maybenull_ const DM_REGION **Region);

VOID
DmDiskViewPaintEmptyState(
    _In_ HDC hdc,
    _In_ const RECT *ClientRect,
    _In_opt_ HFONT Font);

VOID
DmDiskViewPaintSnapshot(
    _In_ HDC hdc,
    _In_ const RECT *ClientRect,
    _In_ const DM_SNAPSHOT *Snapshot,
    _In_opt_ const DM_VOLUME *SelectedVolume,
    _In_opt_ HFONT Font,
    _In_ INT ScrollOffset);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_DISKVIEW_H */
