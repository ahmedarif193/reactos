/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/applications/mscutils/diskmgmt/volumelist.c
 * PURPOSE:     Volume list helpers for the Disk Management UI
 */

#include "volumelist.h"
#include "resource.h"

#include <strsafe.h>
#include <wctype.h>

const DM_VOLUME_LIST_COLUMN DmVolumeListColumns[] =
{
    { IDS_COL_VOLUME,      150 },
    { IDS_COL_LAYOUT,       72 },
    { IDS_COL_TYPE,         70 },
    { IDS_COL_FILESYSTEM,   88 },
    { IDS_COL_STATUS,      330 },
    { IDS_COL_CAPACITY,     92 },
    { IDS_COL_FREE,         92 },
    { IDS_COL_FREE_PCT,     58 }
};

static const GUID DmGptEfiSystemPartitionGuid =
{ 0xC12A7328, 0xF81F, 0x11D2, { 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B } };

static const GUID DmGptMsrPartitionGuid =
{ 0xE3C9E316, 0x0B5C, 0x4DB8, { 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE } };

static const GUID DmGptBasicDataPartitionGuid =
{ 0xEBD0A0A2, 0xB9E5, 0x4433, { 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 } };

static const GUID DmGptRecoveryPartitionGuid =
{ 0xDE94BBA4, 0x06D1, 0x4D40, { 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC } };

static const GUID DmGptLdmMetadataPartitionGuid =
{ 0x5808C8AA, 0x7E8F, 0x42E0, { 0x85, 0xD2, 0xE1, 0xE9, 0x04, 0x34, 0xCF, 0xB3 } };

static const GUID DmGptLdmDataPartitionGuid =
{ 0xAF9B60A0, 0x1431, 0x4F62, { 0xBC, 0x68, 0x33, 0x11, 0x71, 0x4A, 0x69, 0xAD } };

static VOID
DmFormatByteString(
    _In_ ULONGLONG Size,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    static const struct
    {
        ULONGLONG Scale;
        PCWSTR Suffix;
    } Units[] =
    {
        { 1024ULL * 1024ULL * 1024ULL * 1024ULL, L"TB" },
        { 1024ULL * 1024ULL * 1024ULL, L"GB" },
        { 1024ULL * 1024ULL, L"MB" },
        { 1024ULL, L"KB" }
    };
    ULONGLONG Whole;
    ULONGLONG Fraction;
    UINT Index;

    if (Buffer == NULL || cchBuffer == 0)
        return;

    for (Index = 0; Index < ARRAYSIZE(Units); Index++)
    {
        if (Size >= Units[Index].Scale)
        {
            Whole = Size / Units[Index].Scale;
            Fraction = ((Size % Units[Index].Scale) * 100ULL) / Units[Index].Scale;
            StringCchPrintfW(Buffer,
                             cchBuffer,
                             L"%I64u.%02I64u %s",
                             Whole,
                             Fraction,
                             Units[Index].Suffix);
            return;
        }
    }

    StringCchPrintfW(Buffer,
                     cchBuffer,
                     L"%I64u B",
                     Size);
}

static PCWSTR
DmPartitionStyleToString(
    _In_ PARTITION_STYLE Style)
{
    switch (Style)
    {
        case PARTITION_STYLE_MBR:
            return L"MBR";

        case PARTITION_STYLE_GPT:
            return L"GPT";

        case PARTITION_STYLE_RAW:
            return L"RAW";

        default:
            return L"Unknown";
    }
}

static VOID
DmAppendStatusComponent(
    _Inout_updates_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer,
    _Inout_ BOOL *First,
    _In_z_ PCWSTR Component)
{
    if (Buffer == NULL || First == NULL || Component == NULL || Component[0] == UNICODE_NULL)
        return;

    if (!*First)
    {
        StringCchCatW(Buffer, cchBuffer, L", ");
    }

    StringCchCatW(Buffer, cchBuffer, Component);
    *First = FALSE;
}

static PCWSTR
DmVolumeListGetPartitionRole(
    _In_ const DM_VOLUME *Volume)
{
    const DM_REGION *Region;

    if (Volume == NULL)
        return NULL;

    Region = Volume->Region;
    if (Region == NULL || Region->Type != DmRegionPartition)
        return NULL;

    if (Region->PartitionStyle == PARTITION_STYLE_GPT)
    {
        if (IsEqualGUID(&Region->Data.Gpt.PartitionType, &DmGptEfiSystemPartitionGuid))
        {
            return L"EFI System Partition";
        }

        if (IsEqualGUID(&Region->Data.Gpt.PartitionType, &DmGptRecoveryPartitionGuid))
        {
            return L"Recovery Partition";
        }

        if (IsEqualGUID(&Region->Data.Gpt.PartitionType, &DmGptBasicDataPartitionGuid))
        {
            return L"Primary Partition";
        }

        if (IsEqualGUID(&Region->Data.Gpt.PartitionType, &DmGptMsrPartitionGuid))
        {
            return L"Microsoft Reserved Partition";
        }

        if (IsEqualGUID(&Region->Data.Gpt.PartitionType, &DmGptLdmMetadataPartitionGuid))
        {
            return L"LDM Metadata Partition";
        }

        if (IsEqualGUID(&Region->Data.Gpt.PartitionType, &DmGptLdmDataPartitionGuid))
        {
            return L"LDM Data Partition";
        }
    }
    else if (Region->PartitionStyle == PARTITION_STYLE_MBR)
    {
        if (Region->IsDynamic)
        {
            return L"LDM Data";
        }

        if (Region->IsContainer)
        {
            return L"Extended Partition";
        }

        if (Region->IsLogical)
        {
            return L"Logical Drive";
        }

        return L"Primary Partition";
    }

    return NULL;
}

static BOOL
DmVolumeListIsGenericPartitionRole(
    _In_opt_ PCWSTR Role)
{
    return (Role == NULL ||
            _wcsicmp(Role, L"Primary Partition") == 0 ||
            _wcsicmp(Role, L"Logical Drive") == 0 ||
            _wcsicmp(Role, L"Extended Partition") == 0);
}

static VOID
DmBuildFileSystemText(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    UNREFERENCED_PARAMETER(Volume);

    if (Volume->FileSystem[0] != UNICODE_NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, Volume->FileSystem);
    }
    else
    {
        Buffer[0] = UNICODE_NULL;
    }
}

VOID
DmVolumeListInitializeContext(
    _Out_ PDM_VOLUME_LIST_CONTEXT Context)
{
    if (Context != NULL)
    {
        ZeroMemory(Context, sizeof(*Context));
    }
}

VOID
DmVolumeListFormatSize(
    _In_ ULONGLONG Size,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    DmFormatByteString(Size, Buffer, cchBuffer);
}

VOID
DmVolumeListFormatPercent(
    _In_ ULONGLONG FreeBytes,
    _In_ ULONGLONG TotalBytes,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (TotalBytes == 0)
    {
        StringCchCopyW(Buffer, cchBuffer, L"-");
        return;
    }

    StringCchPrintfW(Buffer,
                     cchBuffer,
                     L"%I64u%%",
                     (FreeBytes * 100ULL) / TotalBytes);
}

VOID
DmVolumeListBuildName(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Volume == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    if (Volume->HasDriveLetter)
    {
        if (Volume->Label[0] != UNICODE_NULL)
        {
            StringCchPrintfW(Buffer,
                             cchBuffer,
                             L"%s (%C:)",
                             Volume->Label,
                             towupper(Volume->DriveLetter));
        }
        else
        {
            StringCchPrintfW(Buffer,
                             cchBuffer,
                             L"%C:",
                             towupper(Volume->DriveLetter));
        }
        return;
    }

    if (Volume->Label[0] != UNICODE_NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, Volume->Label);
        return;
    }

    if (Volume->Region != NULL &&
        Volume->Disk != NULL &&
        Volume->Region->PartitionNumber != 0)
    {
        StringCchPrintfW(Buffer,
                         cchBuffer,
                         L"(Disk %lu partition %lu)",
                         Volume->Disk->DiskNumber,
                         Volume->Region->PartitionNumber);
        return;
    }

    if (Volume->MountPoint[0] != UNICODE_NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, Volume->MountPoint);
        return;
    }

    StringCchCopyW(Buffer, cchBuffer, Volume->VolumeName);
}

VOID
DmVolumeListBuildLayout(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Volume == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    if (Volume->IsDynamic || (Volume->Disk != NULL && Volume->Disk->IsDynamic))
    {
        if (Volume->IsMultiExtent)
            StringCchCopyW(Buffer, cchBuffer, L"Multi-extent");
        else
            StringCchCopyW(Buffer, cchBuffer, L"Simple");
    }
    else if (Volume->IsMultiExtent)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Multi-extent");
    }
    else if (Volume->Region != NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Simple");
    }
    else
    {
        StringCchCopyW(Buffer, cchBuffer, L"Unknown");
    }
}

VOID
DmVolumeListBuildType(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Volume == NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Unknown");
        return;
    }

    if (Volume->IsDynamic)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Dynamic");
        return;
    }

    if (Volume->Disk == NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Unknown");
        return;
    }

    if (Volume->Disk->PartitionStyle == PARTITION_STYLE_RAW)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Unknown");
        return;
    }

    if (Volume->Disk->IsDynamic ||
        (Volume->Region != NULL && Volume->Region->IsDynamic))
    {
        StringCchCopyW(Buffer, cchBuffer, L"Dynamic");
        return;
    }

    StringCchCopyW(Buffer, cchBuffer, L"Basic");
}

VOID
DmVolumeListBuildStatus(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Volume == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    StringCchCopyW(Buffer, cchBuffer, L"Healthy");

    if (!Volume->IsSystem &&
        !Volume->IsBoot &&
        DmVolumeListGetPartitionRole(Volume) == NULL &&
        !Volume->IsMultiExtent &&
        !Volume->IsDynamic &&
        !(Volume->Disk != NULL && Volume->Disk->IsDynamic))
    {
        return;
    }

    StringCchCatW(Buffer, cchBuffer, L" (");

    {
        BOOL First = TRUE;
        const DM_REGION *Region = Volume->Region;
        PCWSTR Role;

        if (Volume->IsSystem || (Region != NULL && Region->IsSystem))
        {
            DmAppendStatusComponent(Buffer, cchBuffer, &First, L"System");
        }

        if (Volume->IsBoot || (Region != NULL && Region->IsBoot))
        {
            DmAppendStatusComponent(Buffer, cchBuffer, &First, L"Boot");
        }

        if (!Volume->IsDynamic &&
            !Volume->IsMultiExtent &&
            Region != NULL &&
            Region->PartitionStyle == PARTITION_STYLE_MBR &&
            Region->Data.Mbr.BootIndicator)
        {
            DmAppendStatusComponent(Buffer, cchBuffer, &First, L"Active");
        }

        Role = DmVolumeListGetPartitionRole(Volume);
        if (Volume->IsMultiExtent)
        {
            DmAppendStatusComponent(Buffer, cchBuffer, &First, L"Multi-extent");
        }

        if (Volume->IsDynamic ||
            (Volume->Disk != NULL && Volume->Disk->IsDynamic) ||
            (Region != NULL && Region->IsDynamic))
        {
            DmAppendStatusComponent(Buffer, cchBuffer, &First, L"Dynamic");
        }

        if (Role != NULL &&
            (!Volume->IsMultiExtent || !DmVolumeListIsGenericPartitionRole(Role)))
        {
            DmAppendStatusComponent(Buffer, cchBuffer, &First, Role);
        }

        if (First)
        {
            StringCchCopyW(Buffer, cchBuffer, L"Healthy");
            return;
        }
    }

    StringCchCatW(Buffer, cchBuffer, L")");
}

VOID
DmVolumeListBuildCapacity(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Volume == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    DmFormatByteString(Volume->Size.QuadPart, Buffer, cchBuffer);
}

VOID
DmVolumeListBuildFreeSpace(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Volume == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    DmFormatByteString(Volume->FreeBytes.QuadPart, Buffer, cchBuffer);
}

VOID
DmVolumeListBuildFreePercent(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Volume == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    DmVolumeListFormatPercent(Volume->FreeBytes.QuadPart,
                              Volume->Size.QuadPart,
                              Buffer,
                              cchBuffer);
}

VOID
DmVolumeListBuildSummary(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    WCHAR Capacity[32];
    WCHAR FreeSpace[32];
    WCHAR Percent[16];
    WCHAR FsName[32];

    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Volume == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    DmVolumeListBuildCapacity(Volume, Capacity, ARRAYSIZE(Capacity));
    DmVolumeListBuildFreeSpace(Volume, FreeSpace, ARRAYSIZE(FreeSpace));
    DmVolumeListBuildFreePercent(Volume, Percent, ARRAYSIZE(Percent));
    DmBuildFileSystemText(Volume, FsName, ARRAYSIZE(FsName));

    StringCchPrintfW(Buffer,
                     cchBuffer,
                     L"%s  %s  Free %s (%s)",
                     FsName,
                     Capacity,
                     FreeSpace,
                     Percent);
}

PCWSTR
DmVolumeListPartitionStyleString(
    _In_ PARTITION_STYLE Style)
{
    return DmPartitionStyleToString(Style);
}

VOID
DmVolumeListInsertColumns(
    _In_ HWND hListView,
    _In_ HINSTANCE hInstance)
{
    LVCOLUMNW Column;
    WCHAR Text[64];
    UINT Index;

    ZeroMemory(&Column, sizeof(Column));
    Column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    Column.fmt = LVCFMT_LEFT;

    for (Index = 0; Index < DmVolumeListColumnCount; Index++)
    {
        if (LoadStringW(hInstance,
                        DmVolumeListColumns[Index].StringId,
                        Text,
                        ARRAYSIZE(Text)) == 0)
        {
            Text[0] = UNICODE_NULL;
        }

        Column.pszText = Text;
        Column.cx = DmVolumeListColumns[Index].Width;
        ListView_InsertColumn(hListView, (INT)Index, &Column);
    }
}

PDM_VOLUME
DmVolumeListGetSelectedVolume(
    _In_ HWND hListView)
{
    INT ItemIndex;
    LVITEMW Item;

    ItemIndex = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
    if (ItemIndex < 0)
        return NULL;

    ZeroMemory(&Item, sizeof(Item));
    Item.mask = LVIF_PARAM;
    Item.iItem = ItemIndex;
    if (!ListView_GetItem(hListView, &Item))
        return NULL;

    return (PDM_VOLUME)Item.lParam;
}

BOOL
DmVolumeListSelectVolume(
    _In_ HWND hListView,
    _In_opt_ PDM_VOLUME Volume)
{
    INT ItemCount;
    INT Index;

    if (hListView == NULL)
        return FALSE;

    ListView_SetItemState(hListView,
                          -1,
                          0,
                          LVIS_SELECTED | LVIS_FOCUSED);

    if (Volume == NULL)
        return TRUE;

    ItemCount = ListView_GetItemCount(hListView);
    for (Index = 0; Index < ItemCount; Index++)
    {
        LVITEMW Item;

        ZeroMemory(&Item, sizeof(Item));
        Item.mask = LVIF_PARAM;
        Item.iItem = Index;
        if (!ListView_GetItem(hListView, &Item))
            continue;

        if ((PDM_VOLUME)Item.lParam != Volume)
            continue;

        ListView_SetItemState(hListView,
                              Index,
                              LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(hListView, Index, FALSE);
        return TRUE;
    }

    return FALSE;
}

VOID
DmVolumeListCacheSelection(
    _Inout_ PDM_VOLUME_LIST_CONTEXT Context,
    _In_ HWND hListView)
{
    PDM_VOLUME Volume;

    if (Context == NULL)
        return;

    Context->SelectedVolumeName[0] = UNICODE_NULL;
    Volume = DmVolumeListGetSelectedVolume(hListView);
    if (Volume != NULL)
    {
        StringCchCopyW(Context->SelectedVolumeName,
                       ARRAYSIZE(Context->SelectedVolumeName),
                       Volume->VolumeName);
    }
}

VOID
DmVolumeListRestoreSelection(
    _In_ const DM_VOLUME_LIST_CONTEXT *Context,
    _In_ HWND hListView,
    _In_ const DM_SNAPSHOT *Snapshot)
{
    ULONG Index;

    if (Context == NULL ||
        Context->SelectedVolumeName[0] == UNICODE_NULL ||
        Snapshot == NULL)
    {
        return;
    }

    for (Index = 0; Index < Snapshot->VolumeCount; Index++)
    {
        LVITEMW Item;
        PDM_VOLUME Volume;

        Volume = &Snapshot->Volumes[Index];
        if (_wcsicmp(Volume->VolumeName, Context->SelectedVolumeName) != 0)
            continue;

        ZeroMemory(&Item, sizeof(Item));
        Item.mask = LVIF_PARAM;
        Item.iItem = (INT)Index;
        if (!ListView_GetItem(hListView, &Item))
            continue;

        ListView_SetItemState(hListView,
                              (INT)Index,
                              LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(hListView, (INT)Index, FALSE);
        break;
    }
}

VOID
DmVolumeListPopulate(
    _In_ HWND hListView,
    _In_ const DM_SNAPSHOT *Snapshot)
{
    ULONG Index;

    if (Snapshot == NULL)
    {
        ListView_DeleteAllItems(hListView);
        return;
    }

    SendMessageW(hListView, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hListView);

    for (Index = 0; Index < Snapshot->VolumeCount; Index++)
    {
        const DM_VOLUME *Volume = &Snapshot->Volumes[Index];
        LVITEMW Item;
        WCHAR Text[160];

        ZeroMemory(&Item, sizeof(Item));
        Item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        Item.iItem = (INT)Index;
        Item.lParam = (LPARAM)Volume;
        Item.iImage = (Volume->HasDriveLetter || Volume->MountPoint[0] != UNICODE_NULL) ?
                      DmVolumeListImageVolume :
                      DmVolumeListImagePartition;
        DmVolumeListBuildName(Volume, Text, ARRAYSIZE(Text));
        Item.pszText = Text;
        Item.iItem = ListView_InsertItem(hListView, &Item);

        DmVolumeListBuildLayout(Volume, Text, ARRAYSIZE(Text));
        ListView_SetItemText(hListView, Item.iItem, 1, Text);

        DmVolumeListBuildType(Volume, Text, ARRAYSIZE(Text));
        ListView_SetItemText(hListView, Item.iItem, 2, Text);

        DmBuildFileSystemText(Volume, Text, ARRAYSIZE(Text));
        ListView_SetItemText(hListView, Item.iItem, 3, Text);

        DmVolumeListBuildStatus(Volume, Text, ARRAYSIZE(Text));
        ListView_SetItemText(hListView, Item.iItem, 4, Text);

        DmVolumeListBuildCapacity(Volume, Text, ARRAYSIZE(Text));
        ListView_SetItemText(hListView, Item.iItem, 5, Text);

        DmVolumeListBuildFreeSpace(Volume, Text, ARRAYSIZE(Text));
        ListView_SetItemText(hListView, Item.iItem, 6, Text);

        DmVolumeListBuildFreePercent(Volume, Text, ARRAYSIZE(Text));
        ListView_SetItemText(hListView, Item.iItem, 7, Text);
    }

    SendMessageW(hListView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hListView, NULL, TRUE);
}
