/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/applications/mscutils/diskmgmt/labels.c
 * PURPOSE:     Type and status label helpers for the Disk Management UI
 */

#include "labels.h"

#include <strsafe.h>

#ifndef PARTITION_LDM
#define PARTITION_LDM 0x42
#endif

typedef struct _DM_GUID_LABEL
{
    GUID Guid;
    PCWSTR Label;
} DM_GUID_LABEL, *PDM_GUID_LABEL;

static const GUID DmPartitionUnusedGuid =
{ 0x00000000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };

static const GUID DmRecoveryPartitionGuid =
{ 0xDE94BBA4, 0x06D1, 0x4D40, { 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC } };

static const DM_GUID_LABEL DmGptLabels[] =
{
    { DmPartitionUnusedGuid, L"Unallocated" },
    { { 0xC12A7328, 0xF81F, 0x11D2, { 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B } }, L"EFI System Partition" },
    { { 0xE3C9E316, 0x0B5C, 0x4DB8, { 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE } }, L"Microsoft Reserved Partition" },
    { { 0xEBD0A0A2, 0xB9E5, 0x4433, { 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 } }, L"Basic Data Partition" },
    { { 0x5808C8AA, 0x7E8F, 0x42E0, { 0x85, 0xD2, 0xE1, 0xE9, 0x04, 0x34, 0xCF, 0xB3 } }, L"LDM Metadata Partition" },
    { { 0xAF9B60A0, 0x1431, 0x4F62, { 0xBC, 0x68, 0x33, 0x11, 0x71, 0x4A, 0x69, 0xAD } }, L"LDM Data Partition" },
    { DmRecoveryPartitionGuid, L"Recovery Partition" }
};

static VOID
DmWriteLabel(
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer,
    _In_opt_z_ PCWSTR Label)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    if (Label == NULL)
    {
        Buffer[0] = UNICODE_NULL;
        return;
    }

    StringCchCopyW(Buffer, cchBuffer, Label);
}

static VOID
DmWriteFormattedLabel(
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer,
    _In_z_ PCWSTR Format,
    _In_ ULONG Value)
{
    if (Buffer == NULL || cchBuffer == 0)
        return;

    StringCchPrintfW(Buffer, cchBuffer, Format, Value);
}

VOID
DmGetPartitionStyleLabel(
    _In_ PARTITION_STYLE Style,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    switch (Style)
    {
        case PARTITION_STYLE_MBR:
            DmWriteLabel(Buffer, cchBuffer, L"MBR");
            break;

        case PARTITION_STYLE_GPT:
            DmWriteLabel(Buffer, cchBuffer, L"GPT");
            break;

        case PARTITION_STYLE_RAW:
            DmWriteLabel(Buffer, cchBuffer, L"RAW");
            break;

        default:
            DmWriteLabel(Buffer, cchBuffer, L"Unknown");
            break;
    }
}

VOID
DmGetBusTypeLabel(
    _In_ STORAGE_BUS_TYPE BusType,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    switch (BusType)
    {
        case BusTypeScsi: DmWriteLabel(Buffer, cchBuffer, L"SCSI"); break;
        case BusTypeAtapi: DmWriteLabel(Buffer, cchBuffer, L"ATAPI"); break;
        case BusTypeAta: DmWriteLabel(Buffer, cchBuffer, L"ATA"); break;
        case BusType1394: DmWriteLabel(Buffer, cchBuffer, L"1394"); break;
        case BusTypeSsa: DmWriteLabel(Buffer, cchBuffer, L"SSA"); break;
        case BusTypeFibre: DmWriteLabel(Buffer, cchBuffer, L"Fibre Channel"); break;
        case BusTypeUsb: DmWriteLabel(Buffer, cchBuffer, L"USB"); break;
        case BusTypeRAID: DmWriteLabel(Buffer, cchBuffer, L"RAID"); break;
        case BusTypeiScsi: DmWriteLabel(Buffer, cchBuffer, L"iSCSI"); break;
        case BusTypeSas: DmWriteLabel(Buffer, cchBuffer, L"SAS"); break;
        case BusTypeSata: DmWriteLabel(Buffer, cchBuffer, L"SATA"); break;
        case BusTypeSd: DmWriteLabel(Buffer, cchBuffer, L"SD"); break;
        case BusTypeMmc: DmWriteLabel(Buffer, cchBuffer, L"MMC"); break;
        case BusTypeVirtual: DmWriteLabel(Buffer, cchBuffer, L"Virtual"); break;
        case BusTypeFileBackedVirtual: DmWriteLabel(Buffer, cchBuffer, L"File-backed Virtual"); break;
        default:
            DmWriteFormattedLabel(Buffer, cchBuffer, L"Bus %lu", (ULONG)BusType);
            break;
    }
}

VOID
DmGetMbrRoleLabel(
    _In_ BOOLEAN IsLogical,
    _In_ BOOLEAN IsContainer,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (IsContainer)
    {
        DmWriteLabel(Buffer, cchBuffer, L"Extended Partition");
    }
    else if (IsLogical)
    {
        DmWriteLabel(Buffer, cchBuffer, L"Logical Drive");
    }
    else
    {
        DmWriteLabel(Buffer, cchBuffer, L"Primary Partition");
    }
}

VOID
DmGetMbrTypeLabel(
    _In_ UCHAR PartitionType,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    switch (PartitionType)
    {
        case PARTITION_FAT_12:
            DmWriteLabel(Buffer, cchBuffer, L"FAT12");
            break;

        case PARTITION_FAT_16:
            DmWriteLabel(Buffer, cchBuffer, L"FAT16");
            break;

        case PARTITION_HUGE:
            DmWriteLabel(Buffer, cchBuffer, L"FAT16");
            break;

        case PARTITION_XINT13:
            DmWriteLabel(Buffer, cchBuffer, L"FAT16 (LBA)");
            break;

        case PARTITION_IFS:
            DmWriteLabel(Buffer, cchBuffer, L"NTFS");
            break;

        case PARTITION_FAT32:
            DmWriteLabel(Buffer, cchBuffer, L"FAT32");
            break;

        case PARTITION_FAT32_XINT13:
            DmWriteLabel(Buffer, cchBuffer, L"FAT32 (LBA)");
            break;

        case PARTITION_EXTENDED:
        case PARTITION_XINT13_EXTENDED:
            DmWriteLabel(Buffer, cchBuffer, L"Extended Partition");
            break;

#ifdef PARTITION_LINUX
        case PARTITION_LINUX:
            DmWriteLabel(Buffer, cchBuffer, L"Linux");
            break;
#endif

#ifdef PARTITION_OLD_LINUX
        case PARTITION_OLD_LINUX:
            DmWriteLabel(Buffer, cchBuffer, L"Linux");
            break;
#endif

#ifdef PARTITION_ISO9660
        case PARTITION_ISO9660:
            DmWriteLabel(Buffer, cchBuffer, L"CD-ROM");
            break;
#endif

        case PARTITION_LDM:
            DmWriteLabel(Buffer, cchBuffer, L"LDM Data");
            break;

#ifdef PARTITION_FREEBSD
        case PARTITION_FREEBSD:
            DmWriteLabel(Buffer, cchBuffer, L"FreeBSD");
            break;
#endif

#ifdef PARTITION_OPENBSD
        case PARTITION_OPENBSD:
            DmWriteLabel(Buffer, cchBuffer, L"OpenBSD");
            break;
#endif

#ifdef PARTITION_NETBSD
        case PARTITION_NETBSD:
            DmWriteLabel(Buffer, cchBuffer, L"NetBSD");
            break;
#endif

        default:
            DmWriteFormattedLabel(Buffer, cchBuffer, L"Type 0x%02X", PartitionType);
            break;
    }
}

VOID
DmGetGptTypeLabel(
    _In_ const GUID *PartitionType,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    SIZE_T i;

    if (PartitionType == NULL)
    {
        DmWriteLabel(Buffer, cchBuffer, L"Unknown");
        return;
    }

    for (i = 0; i < ARRAYSIZE(DmGptLabels); i++)
    {
        if (IsEqualGUID(PartitionType, &DmGptLabels[i].Guid))
        {
            DmWriteLabel(Buffer, cchBuffer, DmGptLabels[i].Label);
            return;
        }
    }

    DmWriteLabel(Buffer, cchBuffer, L"Unknown");
}

VOID
DmGetHealthLabel(
    _In_ DM_HEALTH_STATE Health,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    switch (Health)
    {
        case DmHealthHealthy:
            DmWriteLabel(Buffer, cchBuffer, L"Healthy");
            break;

        case DmHealthSick:
            DmWriteLabel(Buffer, cchBuffer, L"Sick");
            break;

        case DmHealthUnavailable:
            DmWriteLabel(Buffer, cchBuffer, L"UNAVAILABLE");
            break;

        default:
            DmWriteLabel(Buffer, cchBuffer, L"Unknown");
            break;
    }
}

VOID
DmGetAccessStateLabel(
    _In_ DM_ACCESS_STATE State,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    switch (State)
    {
        case DmAccessOnline:
            DmWriteLabel(Buffer, cchBuffer, L"Online");
            break;

        case DmAccessOffline:
            DmWriteLabel(Buffer, cchBuffer, L"Offline");
            break;

        case DmAccessNoMedia:
            DmWriteLabel(Buffer, cchBuffer, L"No Media");
            break;

        default:
            DmWriteLabel(Buffer, cchBuffer, L"Unknown");
            break;
    }
}

VOID
DmGetYesNoLabel(
    _In_ BOOL Value,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    DmWriteLabel(Buffer, cchBuffer, Value ? L"Yes" : L"No");
}

VOID
DmGetBootSystemLabel(
    _In_ BOOL IsBoot,
    _In_ BOOL IsSystem,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer)
{
    if (IsBoot && IsSystem)
    {
        DmWriteLabel(Buffer, cchBuffer, L"Boot, System");
    }
    else if (IsBoot)
    {
        DmWriteLabel(Buffer, cchBuffer, L"Boot");
    }
    else if (IsSystem)
    {
        DmWriteLabel(Buffer, cchBuffer, L"System");
    }
    else
    {
        DmWriteLabel(Buffer, cchBuffer, L"-");
    }
}
