/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Volume list sorting helpers.
 */

#ifndef DISKMGMT_SORT_H
#define DISKMGMT_SORT_H

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <commctrl.h>

#include "snapshot.h"
#include "volumelist.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _DM_SORT_COLUMN
{
    DmSortColumnVolume = 0,
    DmSortColumnLayout,
    DmSortColumnType,
    DmSortColumnFileSystem,
    DmSortColumnStatus,
    DmSortColumnCapacity,
    DmSortColumnFreeSpace,
    DmSortColumnFreePercent
} DM_SORT_COLUMN, *PDM_SORT_COLUMN;

typedef struct _DM_SORT_CONTEXT
{
    INT Column;
    BOOL Ascending;
} DM_SORT_CONTEXT, *PDM_SORT_CONTEXT;

VOID
DmSortInitializeContext(
    _Out_ PDM_SORT_CONTEXT Context);

VOID
DmSortSetColumn(
    _Inout_ PDM_SORT_CONTEXT Context,
    _In_ INT Column);

VOID
DmSortToggleColumn(
    _Inout_ PDM_SORT_CONTEXT Context,
    _In_ INT Column);

INT
CALLBACK
DmSortCompareItems(
    _In_ LPARAM lParam1,
    _In_ LPARAM lParam2,
    _In_ LPARAM lParamSort);

BOOL
DmSortIsNumericColumn(
    _In_ INT Column);

VOID
DmSortApplyToListView(
    _In_ HWND hListView,
    _In_ const DM_SORT_CONTEXT *Context);

VOID
DmSortHandleColumnClick(
    _In_ HWND hListView,
    _Inout_ PDM_SORT_CONTEXT Context,
    _In_ INT Column);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_SORT_H */
