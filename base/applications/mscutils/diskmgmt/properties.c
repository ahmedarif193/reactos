/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/applications/mscutils/diskmgmt/properties.c
 * PURPOSE:     Read-only properties helpers for the Disk Management UI
 */

#include "properties.h"
#include "labels.h"

#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>
#include <strsafe.h>
#include <winuser.h>

static VOID
DmAppendFormat(
    _Inout_updates_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer,
    _In_z_ PCWSTR Format,
    ...)
{
    va_list Args;
    SIZE_T Length;

    Length = wcslen(Buffer);
    if (Length >= cchBuffer)
        return;

    va_start(Args, Format);
    StringCchVPrintfW(Buffer + Length, cchBuffer - Length, Format, Args);
    va_end(Args);
}

static VOID
DmAppendLine(
    _Inout_updates_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer,
    _In_z_ PCWSTR Label,
    _In_z_ PCWSTR Value)
{
    DmAppendFormat(Buffer, cchBuffer, L"%s: %s\r\n", Label, Value);
}

static VOID
DmAppendLineU64(
    _Inout_updates_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer,
    _In_z_ PCWSTR Label,
    _In_ ULONGLONG Value)
{
    WCHAR Temp[64];

    StringCchPrintfW(Temp, ARRAYSIZE(Temp), L"%I64u", Value);
    DmAppendLine(Buffer, cchBuffer, Label, Temp);
}

static VOID
DmAppendLineU32(
    _Inout_updates_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer,
    _In_z_ PCWSTR Label,
    _In_ ULONG Value)
{
    WCHAR Temp[32];

    StringCchPrintfW(Temp, ARRAYSIZE(Temp), L"%lu", Value);
    DmAppendLine(Buffer, cchBuffer, Label, Temp);
}

static VOID
DmAppendLineBool(
    _Inout_updates_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer,
    _In_z_ PCWSTR Label,
    _In_ BOOLEAN Value)
{
    WCHAR Text[8];

    DmGetYesNoLabel(Value, Text, ARRAYSIZE(Text));
    DmAppendLine(Buffer, cchBuffer, Label, Text);
}

static VOID
DmAppendMountPathLines(
    _Inout_updates_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer,
    _In_z_ PCWSTR Label,
    _In_ const DM_VOLUME *Volume)
{
    ULONG Index;

    if (Volume == NULL || Volume->FolderMountPointCount == 0)
    {
        DmAppendLine(Buffer, cchBuffer, Label, L"-");
        return;
    }

    DmAppendLine(Buffer, cchBuffer, Label, Volume->FolderMountPoints[0]);
    for (Index = 1; Index < Volume->FolderMountPointCount; Index++)
    {
        DmAppendFormat(Buffer,
                       cchBuffer,
                       L"    %s\r\n",
                       Volume->FolderMountPoints[Index]);
    }
}

static VOID
DmFormatBytes(
    _In_ ULONGLONG Value,
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
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Units); i++)
    {
        if (Value >= Units[i].Scale)
        {
            StringCchPrintfW(Buffer,
                             cchBuffer,
                             L"%I64u.%02I64u %s",
                             Value / Units[i].Scale,
                             ((Value % Units[i].Scale) * 100ULL) / Units[i].Scale,
                             Units[i].Suffix);
            return;
        }
    }

    StringCchPrintfW(Buffer, cchBuffer, L"%I64u B", Value);
}

static VOID
DmFormatPercent(
    _In_ ULONGLONG Total,
    _In_ ULONGLONG Free,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Total == 0)
    {
        StringCchCopyW(Buffer, cchBuffer, L"-");
        return;
    }

    StringCchPrintfW(Buffer,
                     cchBuffer,
                     L"%I64u%%",
                     (Free * 100ULL) / Total);
}

static VOID
DmFormatStyleLabel(
    _In_ PARTITION_STYLE Style,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    DmGetPartitionStyleLabel(Style, Buffer, cchBuffer);
}

static VOID
DmFormatBusTypeLabel(
    _In_ STORAGE_BUS_TYPE BusType,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    DmGetBusTypeLabel(BusType, Buffer, cchBuffer);
}

static VOID
DmFormatGuid(
    _In_ const GUID *Guid,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Guid == NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, L"-");
        return;
    }

    StringCchPrintfW(Buffer,
                     cchBuffer,
                     L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                     Guid->Data1,
                     Guid->Data2,
                     Guid->Data3,
                     Guid->Data4[0],
                     Guid->Data4[1],
                     Guid->Data4[2],
                     Guid->Data4[3],
                     Guid->Data4[4],
                     Guid->Data4[5],
                     Guid->Data4[6],
                     Guid->Data4[7]);
}

static VOID
DmFormatBinaryId(
    _In_reads_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    ULONG Index;
    WCHAR Item[4];

    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Data == NULL || Length == 0)
    {
        StringCchCopyW(Buffer, cchBuffer, L"-");
        return;
    }

    Buffer[0] = UNICODE_NULL;
    for (Index = 0; Index < Length; Index++)
    {
        if (Index != 0)
        {
            if (FAILED(StringCchCatW(Buffer, cchBuffer, L" ")))
                break;
        }

        StringCchPrintfW(Item, ARRAYSIZE(Item), L"%02X", Data[Index]);
        if (FAILED(StringCchCatW(Buffer, cchBuffer, Item)))
        {
            if (wcslen(Buffer) + 3 < cchBuffer)
                StringCchCatW(Buffer, cchBuffer, L"...");
            return;
        }
    }
}

static VOID
DmFormatPartitionRoleText(
    _In_ const DM_REGION *Region,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Region == NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, L"-");
        return;
    }

    if (Region->Type == DmRegionFree)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Unallocated");
        return;
    }

    switch (Region->PartitionStyle)
    {
        case PARTITION_STYLE_MBR:
            if (Region->IsDynamic && !Region->IsContainer)
                DmGetMbrTypeLabel(Region->Data.Mbr.PartitionType, Buffer, cchBuffer);
            else
                DmGetMbrRoleLabel(Region->IsLogical, Region->IsContainer, Buffer, cchBuffer);
            break;

        case PARTITION_STYLE_GPT:
            DmGetGptTypeLabel(&Region->Data.Gpt.PartitionType, Buffer, cchBuffer);
            break;

        default:
            DmGetPartitionStyleLabel(Region->PartitionStyle, Buffer, cchBuffer);
            break;
    }
}

static VOID
DmFormatDiskStorageModel(
    _In_ const DM_DISK *Disk,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Disk == NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, L"-");
        return;
    }

    StringCchCopyW(Buffer,
                   cchBuffer,
                   Disk->IsDynamic ? L"Dynamic / LDM" : L"Basic");
}

static VOID
DmFormatVolumeStorageModel(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    WCHAR RoleText[128];

    if (Volume == NULL)
    {
        StringCchCopyW(Buffer, cchBuffer, L"-");
        return;
    }

    if (Volume->IsDynamic)
    {
        if (Volume->Region != NULL)
        {
            DmFormatPartitionRoleText(Volume->Region, RoleText, ARRAYSIZE(RoleText));
            if (RoleText[0] != UNICODE_NULL &&
                _wcsicmp(RoleText, L"-") != 0 &&
                _wcsicmp(RoleText, L"Primary Partition") != 0 &&
                _wcsicmp(RoleText, L"Partition") != 0)
            {
                StringCchPrintfW(Buffer,
                                 cchBuffer,
                                 L"Dynamic / LDM (%s)",
                                 RoleText);
                return;
            }
        }

        StringCchCopyW(Buffer, cchBuffer, L"Dynamic / LDM-backed");
        return;
    }

    if (Volume->IsMultiExtent)
    {
        StringCchCopyW(Buffer, cchBuffer, L"Basic multi-extent");
        return;
    }

    StringCchCopyW(Buffer, cchBuffer, L"Basic");
}

static ULONG
DmCountExtentDisks(
    _In_ const DM_VOLUME *Volume)
{
    ULONG Index;
    ULONG UniqueCount;
    ULONG UniqueDisks[32];

    if (Volume == NULL || Volume->Extents == NULL || Volume->ExtentCount == 0)
        return 0;

    UniqueCount = 0;
    for (Index = 0; Index < Volume->ExtentCount; Index++)
    {
        ULONG DiskNumber;
        ULONG UniqueIndex;
        BOOL Seen;

        DiskNumber = Volume->Extents->Extents[Index].DiskNumber;
        Seen = FALSE;
        for (UniqueIndex = 0; UniqueIndex < UniqueCount; UniqueIndex++)
        {
            if (UniqueDisks[UniqueIndex] == DiskNumber)
            {
                Seen = TRUE;
                break;
            }
        }

        if (!Seen && UniqueCount < ARRAYSIZE(UniqueDisks))
            UniqueDisks[UniqueCount++] = DiskNumber;
    }

    return UniqueCount;
}

static VOID
DmFormatLabelAndFlags(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    WCHAR SizeText[64];
    WCHAR FreeText[64];
    WCHAR PercentText[16];
    WCHAR DriveText[16];
    WCHAR StorageText[128];
    WCHAR UniqueIdText[DM_MAX_UNIQUE_ID_BYTES * 3];

    DmFormatBytes(Volume->Size.QuadPart, SizeText, ARRAYSIZE(SizeText));
    DmFormatBytes(Volume->FreeBytes.QuadPart, FreeText, ARRAYSIZE(FreeText));
    DmFormatPercent(Volume->Size.QuadPart, Volume->FreeBytes.QuadPart, PercentText, ARRAYSIZE(PercentText));
    DmFormatVolumeStorageModel(Volume, StorageText, ARRAYSIZE(StorageText));
    DmFormatBinaryId(Volume->UniqueId,
                     min(Volume->UniqueIdLength, (ULONG)ARRAYSIZE(Volume->UniqueId)),
                     UniqueIdText,
                     ARRAYSIZE(UniqueIdText));

    if (Volume->HasDriveLetter && Volume->DriveLetter != 0)
    {
        StringCchPrintfW(DriveText, ARRAYSIZE(DriveText), L"%C:", towupper(Volume->DriveLetter));
    }
    else
    {
        StringCchCopyW(DriveText, ARRAYSIZE(DriveText), L"none");
    }

    Buffer[0] = UNICODE_NULL;
    DmAppendLine(Buffer, cchBuffer, L"Volume name", Volume->VolumeName);
    DmAppendLine(Buffer, cchBuffer, L"Device path", Volume->DeviceName);
    DmAppendLine(Buffer, cchBuffer, L"Mount point", Volume->MountPoint[0] ? Volume->MountPoint : L"-");
    DmAppendMountPathLines(Buffer, cchBuffer, L"Folder mount points", Volume);
    DmAppendLine(Buffer, cchBuffer, L"Volume label", Volume->Label[0] ? Volume->Label : L"-");
    DmAppendLine(Buffer, cchBuffer, L"File system", Volume->FileSystem[0] ? Volume->FileSystem : L"-");
    DmAppendLine(Buffer, cchBuffer, L"Drive letter", DriveText);
    DmAppendLine(Buffer, cchBuffer, L"Storage model", StorageText);
    DmAppendLineU32(Buffer, cchBuffer, L"MountMgr unique ID length", Volume->UniqueIdLength);
    DmAppendLine(Buffer, cchBuffer, L"MountMgr unique ID", UniqueIdText);
    DmAppendLineBool(Buffer, cchBuffer, L"DMIO-style unique ID", Volume->HasDmioUniqueId);
    DmAppendLine(Buffer, cchBuffer, L"Capacity", SizeText);
    DmAppendLine(Buffer, cchBuffer, L"Free space", FreeText);
    DmAppendLine(Buffer, cchBuffer, L"Free space %", PercentText);
    DmAppendLineU32(Buffer, cchBuffer, L"Volume serial", Volume->SerialNumber);
    DmAppendLineU32(Buffer, cchBuffer, L"Sectors per allocation unit", Volume->SectorsPerAllocationUnit);
    DmAppendLineU32(Buffer, cchBuffer, L"Bytes/sector", Volume->BytesPerSector);
    DmAppendLineBool(Buffer, cchBuffer, L"System volume", Volume->IsSystem);
    DmAppendLineBool(Buffer, cchBuffer, L"Boot volume", Volume->IsBoot);
    DmAppendLineBool(Buffer, cchBuffer, L"Dynamic volume", Volume->IsDynamic);
    DmAppendLineBool(Buffer, cchBuffer, L"Multi-extent volume", Volume->IsMultiExtent);
}

BOOL
DmBuildDiskPropertiesString(
    _In_ const DM_DISK *Disk,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    WCHAR SizeText[64];
    WCHAR BusText[64];
    WCHAR IdText[64];
    WCHAR StorageText[64];

    if (Disk == NULL || Buffer == NULL || cchBuffer == 0)
        return FALSE;

    Buffer[0] = UNICODE_NULL;

    DmFormatBytes(Disk->Size, SizeText, ARRAYSIZE(SizeText));
    DmFormatStyleLabel(Disk->PartitionStyle, BusText, ARRAYSIZE(BusText));
    DmFormatDiskStorageModel(Disk, StorageText, ARRAYSIZE(StorageText));

    DmAppendLineU32(Buffer, cchBuffer, L"Disk", Disk->DiskNumber);
    DmAppendLine(Buffer, cchBuffer, L"Device path", Disk->DeviceName);
    DmAppendLine(Buffer, cchBuffer, L"Model", Disk->Description[0] ? Disk->Description : L"-");
    DmAppendLine(Buffer, cchBuffer, L"Location", Disk->Location[0] ? Disk->Location : L"-");
    DmAppendLine(Buffer, cchBuffer, L"Partition style", BusText);
    if (Disk->PartitionStyle == PARTITION_STYLE_MBR)
    {
        StringCchPrintfW(IdText, ARRAYSIZE(IdText), L"0x%08lX", Disk->MbrSignature);
        DmAppendLine(Buffer, cchBuffer, L"Disk signature", IdText);
    }
    else if (Disk->PartitionStyle == PARTITION_STYLE_GPT)
    {
        DmFormatGuid(&Disk->GptDiskId, IdText, ARRAYSIZE(IdText));
        DmAppendLine(Buffer, cchBuffer, L"Disk ID", IdText);
    }
    DmFormatBusTypeLabel(Disk->BusType, BusText, ARRAYSIZE(BusText));
    DmAppendLine(Buffer, cchBuffer, L"Bus type", BusText);
    DmAppendLine(Buffer, cchBuffer, L"Storage model", StorageText);
    DmAppendLine(Buffer, cchBuffer, L"Capacity", SizeText);
    DmAppendLineU64(Buffer, cchBuffer, L"Bytes per sector", Disk->BytesPerSector);
    DmAppendLineU64(Buffer, cchBuffer, L"Sectors per track", Disk->SectorsPerTrack);
    DmAppendLineU64(Buffer, cchBuffer, L"Tracks per cylinder", Disk->TracksPerCylinder);
    DmAppendLineU64(Buffer, cchBuffer, L"Cylinders", Disk->Cylinders);
    DmAppendLineU32(Buffer, cchBuffer, L"Regions", Disk->RegionCount);
    DmAppendLineU32(Buffer, cchBuffer, L"Partitions", Disk->PartitionCount);
    DmAppendLineBool(Buffer, cchBuffer, L"Boot disk", Disk->IsBoot);
    DmAppendLineBool(Buffer, cchBuffer, L"System disk", Disk->IsSystem);
    DmAppendLineBool(Buffer, cchBuffer, L"Removable media", Disk->IsRemovable);
    DmAppendLineBool(Buffer, cchBuffer, L"Offline", Disk->IsOffline);
    DmAppendLineBool(Buffer, cchBuffer, L"Read-only", Disk->IsReadOnly);
    DmAppendLineBool(Buffer, cchBuffer, L"Dynamic disk", Disk->IsDynamic);
    return TRUE;
}

BOOL
DmBuildVolumePropertiesString(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (Volume == NULL || Buffer == NULL || cchBuffer == 0)
        return FALSE;

    Buffer[0] = UNICODE_NULL;
    DmFormatLabelAndFlags(Volume, Buffer, cchBuffer);

    if (Volume->Disk != NULL)
    {
        WCHAR DiskText[64];

        StringCchPrintfW(DiskText, ARRAYSIZE(DiskText), L"%lu", Volume->Disk->DiskNumber);
        DmAppendLine(Buffer, cchBuffer, L"Disk", DiskText);
    }

    if (Volume->Region != NULL)
    {
        WCHAR Text[128];

        StringCchPrintfW(Text, ARRAYSIZE(Text), L"%lu", Volume->Region->PartitionNumber);
        DmAppendLine(Buffer, cchBuffer, L"Partition number", Text);
        DmFormatPartitionRoleText(Volume->Region, Text, ARRAYSIZE(Text));
        DmAppendLine(Buffer, cchBuffer, L"Partition role", Text);
        if (Volume->Region->PartitionStyle == PARTITION_STYLE_MBR && Volume->Region->Type == DmRegionPartition)
        {
            DmGetMbrTypeLabel(Volume->Region->Data.Mbr.PartitionType, Text, ARRAYSIZE(Text));
            DmAppendLine(Buffer, cchBuffer, L"Partition type", Text);
        }
        DmAppendLineBool(Buffer, cchBuffer, L"System region", Volume->Region->IsSystem);
        DmAppendLineBool(Buffer, cchBuffer, L"Boot region", Volume->Region->IsBoot);
    }

    if (Volume->ExtentCount > 0)
    {
        WCHAR CountText[32];
        WCHAR DiskSpanText[32];
        ULONG Index;

        StringCchPrintfW(CountText, ARRAYSIZE(CountText), L"%lu", Volume->ExtentCount);
        DmAppendLine(Buffer, cchBuffer, L"Volume extents", CountText);
        StringCchPrintfW(DiskSpanText, ARRAYSIZE(DiskSpanText), L"%lu", DmCountExtentDisks(Volume));
        DmAppendLine(Buffer, cchBuffer, L"Extent disks", DiskSpanText);

        for (Index = 0; Index < Volume->ExtentCount; Index++)
        {
            WCHAR ExtentText[160];
            WCHAR OffsetText[64];
            WCHAR LengthText[64];

            DmFormatBytes(Volume->Extents->Extents[Index].StartingOffset.QuadPart,
                          OffsetText,
                          ARRAYSIZE(OffsetText));
            DmFormatBytes(Volume->Extents->Extents[Index].ExtentLength.QuadPart,
                          LengthText,
                          ARRAYSIZE(LengthText));

            StringCchPrintfW(ExtentText,
                             ARRAYSIZE(ExtentText),
                             L"Disk %lu, start %s, length %s",
                             Volume->Extents->Extents[Index].DiskNumber,
                             OffsetText,
                             LengthText);
            DmAppendLine(Buffer, cchBuffer, L"Extent", ExtentText);
        }
    }

    return TRUE;
}

static VOID
DmShowPropertiesMessage(
    _In_opt_ HWND hWndOwner,
    _In_z_ PCWSTR Title,
    _In_z_ PCWSTR Body)
{
    MessageBoxW(hWndOwner,
                Body,
                Title,
                MB_OK | MB_ICONINFORMATION);
}

BOOL
DmShowDiskProperties(
    _In_opt_ HWND hWndOwner,
    _In_ const DM_DISK *Disk)
{
    WCHAR Buffer[4096];

    if (!DmBuildDiskPropertiesString(Disk, Buffer, ARRAYSIZE(Buffer)))
        return FALSE;

    DmShowPropertiesMessage(hWndOwner, L"Disk Properties", Buffer);
    return TRUE;
}

BOOL
DmShowVolumeProperties(
    _In_opt_ HWND hWndOwner,
    _In_ const DM_VOLUME *Volume)
{
    WCHAR Buffer[4096];

    if (!DmBuildVolumePropertiesString(Volume, Buffer, ARRAYSIZE(Buffer)))
        return FALSE;

    DmShowPropertiesMessage(hWndOwner, L"Volume Properties", Buffer);
    return TRUE;
}
