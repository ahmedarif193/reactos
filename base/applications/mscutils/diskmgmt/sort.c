/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Volume list sorting helpers.
 */

#include "precomp.h"

static INT
DmCompareWideString(
    _In_opt_ PCWSTR Left,
    _In_opt_ PCWSTR Right)
{
    if (Left == NULL || Left[0] == UNICODE_NULL)
    {
        if (Right == NULL || Right[0] == UNICODE_NULL)
            return 0;
        return -1;
    }

    if (Right == NULL || Right[0] == UNICODE_NULL)
        return 1;

    return _wcsicmp(Left, Right);
}

static ULONGLONG
DmVolumeFreeBytes(
    _In_ const DM_VOLUME *Volume)
{
    if (Volume == NULL)
        return 0;

    return Volume->FreeBytes.QuadPart;
}

static ULONGLONG
DmVolumePercentFree(
    _In_ const DM_VOLUME *Volume)
{
    if (Volume == NULL || Volume->Size.QuadPart == 0)
        return 0;

    return (DmVolumeFreeBytes(Volume) * 100ULL) / Volume->Size.QuadPart;
}

VOID
DmSortInitializeContext(
    _Out_ PDM_SORT_CONTEXT Context)
{
    if (Context == NULL)
        return;

    Context->Column = DmSortColumnVolume;
    Context->Ascending = TRUE;
}

VOID
DmSortSetColumn(
    _Inout_ PDM_SORT_CONTEXT Context,
    _In_ INT Column)
{
    if (Context == NULL)
        return;

    Context->Column = Column;
    Context->Ascending = TRUE;
}

VOID
DmSortToggleColumn(
    _Inout_ PDM_SORT_CONTEXT Context,
    _In_ INT Column)
{
    if (Context == NULL)
        return;

    if (Context->Column == Column)
    {
        Context->Ascending = !Context->Ascending;
    }
    else
    {
        Context->Column = Column;
        Context->Ascending = TRUE;
    }
}

BOOL
DmSortIsNumericColumn(
    _In_ INT Column)
{
    switch (Column)
    {
        case DmSortColumnCapacity:
        case DmSortColumnFreeSpace:
        case DmSortColumnFreePercent:
            return TRUE;

        default:
            return FALSE;
    }
}

static INT
DmCompareVolumeNames(
    _In_ const DM_VOLUME *Left,
    _In_ const DM_VOLUME *Right)
{
    WCHAR LeftName[MAX_PATH];
    WCHAR RightName[MAX_PATH];

    if (Left == NULL || Right == NULL)
        return 0;

    DmVolumeListBuildName(Left, LeftName, ARRAYSIZE(LeftName));
    DmVolumeListBuildName(Right, RightName, ARRAYSIZE(RightName));
    return DmCompareWideString(LeftName, RightName);
}

static INT
DmCompareVolumeLayout(
    _In_ const DM_VOLUME *Left,
    _In_ const DM_VOLUME *Right)
{
    WCHAR LeftText[64];
    WCHAR RightText[64];

    DmVolumeListBuildLayout(Left, LeftText, ARRAYSIZE(LeftText));
    DmVolumeListBuildLayout(Right, RightText, ARRAYSIZE(RightText));
    return DmCompareWideString(LeftText, RightText);
}

static INT
DmCompareVolumeType(
    _In_ const DM_VOLUME *Left,
    _In_ const DM_VOLUME *Right)
{
    WCHAR LeftText[64];
    WCHAR RightText[64];

    DmVolumeListBuildType(Left, LeftText, ARRAYSIZE(LeftText));
    DmVolumeListBuildType(Right, RightText, ARRAYSIZE(RightText));
    return DmCompareWideString(LeftText, RightText);
}

static INT
DmCompareVolumeFileSystem(
    _In_ const DM_VOLUME *Left,
    _In_ const DM_VOLUME *Right)
{
    WCHAR LeftText[32];
    WCHAR RightText[32];

    if (Left != NULL && Left->FileSystem[0] != UNICODE_NULL)
        StringCchCopyW(LeftText, ARRAYSIZE(LeftText), Left->FileSystem);
    else
        StringCchCopyW(LeftText, ARRAYSIZE(LeftText), L"RAW");

    if (Right != NULL && Right->FileSystem[0] != UNICODE_NULL)
        StringCchCopyW(RightText, ARRAYSIZE(RightText), Right->FileSystem);
    else
        StringCchCopyW(RightText, ARRAYSIZE(RightText), L"RAW");

    return DmCompareWideString(LeftText, RightText);
}

static INT
DmCompareVolumeStatus(
    _In_ const DM_VOLUME *Left,
    _In_ const DM_VOLUME *Right)
{
    WCHAR LeftText[128];
    WCHAR RightText[128];

    DmVolumeListBuildStatus(Left, LeftText, ARRAYSIZE(LeftText));
    DmVolumeListBuildStatus(Right, RightText, ARRAYSIZE(RightText));
    return DmCompareWideString(LeftText, RightText);
}

static INT
DmCompareULongLong(
    _In_ ULONGLONG Left,
    _In_ ULONGLONG Right)
{
    if (Left < Right)
        return -1;
    if (Left > Right)
        return 1;
    return 0;
}

INT
CALLBACK
DmSortCompareItems(
    _In_ LPARAM lParam1,
    _In_ LPARAM lParam2,
    _In_ LPARAM lParamSort)
{
    const DM_SORT_CONTEXT *Context;
    const DM_VOLUME *Left;
    const DM_VOLUME *Right;
    INT Result;

    Context = (const DM_SORT_CONTEXT *)lParamSort;
    if (Context == NULL)
        return 0;

    Left = (const DM_VOLUME *)lParam1;
    Right = (const DM_VOLUME *)lParam2;
    if (Left == NULL || Right == NULL)
        return 0;

    switch (Context->Column)
    {
        case DmSortColumnCapacity:
            Result = DmCompareULongLong(Left->Size.QuadPart, Right->Size.QuadPart);
            break;

        case DmSortColumnFreeSpace:
            Result = DmCompareULongLong(DmVolumeFreeBytes(Left), DmVolumeFreeBytes(Right));
            break;

        case DmSortColumnFreePercent:
            Result = DmCompareULongLong(DmVolumePercentFree(Left), DmVolumePercentFree(Right));
            break;

        case DmSortColumnLayout:
            Result = DmCompareVolumeLayout(Left, Right);
            break;

        case DmSortColumnType:
            Result = DmCompareVolumeType(Left, Right);
            break;

        case DmSortColumnFileSystem:
            Result = DmCompareVolumeFileSystem(Left, Right);
            break;

        case DmSortColumnStatus:
            Result = DmCompareVolumeStatus(Left, Right);
            break;

        case DmSortColumnVolume:
        default:
            Result = DmCompareVolumeNames(Left, Right);
            break;
    }

    if (!Context->Ascending)
        Result = -Result;

    return Result;
}

VOID
DmSortApplyToListView(
    _In_ HWND hListView,
    _In_ const DM_SORT_CONTEXT *Context)
{
    if (hListView == NULL || Context == NULL)
        return;

    ListView_SortItemsEx(hListView,
                         DmSortCompareItems,
                         (LPARAM)Context);
}

VOID
DmSortHandleColumnClick(
    _In_ HWND hListView,
    _Inout_ PDM_SORT_CONTEXT Context,
    _In_ INT Column)
{
    if (Context == NULL)
        return;

    DmSortToggleColumn(Context, Column);
    DmSortApplyToListView(hListView, Context);
}
