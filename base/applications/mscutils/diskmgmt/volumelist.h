/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/applications/mscutils/diskmgmt/volumelist.h
 * PURPOSE:     Volume list helpers for the Disk Management UI
 */

#ifndef DISKMGMT_VOLUMELIST_H
#define DISKMGMT_VOLUMELIST_H

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <commctrl.h>

#include "snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _DM_VOLUME_LIST_COLUMN
{
    UINT StringId;
    INT Width;
} DM_VOLUME_LIST_COLUMN, *PDM_VOLUME_LIST_COLUMN;

typedef struct _DM_VOLUME_LIST_CONTEXT
{
    WCHAR SelectedVolumeName[MAX_PATH];
} DM_VOLUME_LIST_CONTEXT, *PDM_VOLUME_LIST_CONTEXT;

extern const DM_VOLUME_LIST_COLUMN DmVolumeListColumns[];
enum { DmVolumeListColumnCount = 8 };
enum
{
    DmVolumeListImageVolume = 0,
    DmVolumeListImagePartition
};

VOID
DmVolumeListInitializeContext(
    _Out_ PDM_VOLUME_LIST_CONTEXT Context);

VOID
DmVolumeListCacheSelection(
    _Inout_ PDM_VOLUME_LIST_CONTEXT Context,
    _In_ HWND hListView);

VOID
DmVolumeListRestoreSelection(
    _In_ const DM_VOLUME_LIST_CONTEXT *Context,
    _In_ HWND hListView,
    _In_ const DM_SNAPSHOT *Snapshot);

VOID
DmVolumeListPopulate(
    _In_ HWND hListView,
    _In_ const DM_SNAPSHOT *Snapshot);

PDM_VOLUME
DmVolumeListGetSelectedVolume(
    _In_ HWND hListView);

BOOL
DmVolumeListSelectVolume(
    _In_ HWND hListView,
    _In_opt_ PDM_VOLUME Volume);

VOID
DmVolumeListInsertColumns(
    _In_ HWND hListView,
    _In_ HINSTANCE hInstance);

VOID
DmVolumeListFormatSize(
    _In_ ULONGLONG Size,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmVolumeListFormatPercent(
    _In_ ULONGLONG FreeBytes,
    _In_ ULONGLONG TotalBytes,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmVolumeListBuildName(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmVolumeListBuildLayout(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmVolumeListBuildType(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmVolumeListBuildStatus(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmVolumeListBuildCapacity(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmVolumeListBuildFreeSpace(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmVolumeListBuildFreePercent(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmVolumeListBuildSummary(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

PCWSTR
DmVolumeListPartitionStyleString(
    _In_ PARTITION_STYLE Style);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_VOLUMELIST_H */
