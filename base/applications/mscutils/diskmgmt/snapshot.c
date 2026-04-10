/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/applications/mscutils/diskmgmt/snapshot.c
 * PURPOSE:     Read-only storage snapshot helpers for the Disk Management UI
 */

#include "snapshot.h"
#include "snapshotmatch.h"
#include "snapshotenum.h"

#include <stdlib.h>
#include <strsafe.h>
#include <wchar.h>
#include <wctype.h>

static const UCHAR DmDmioIdSignature[] = { 'D', 'M', 'I', 'O', ':', 'I', 'D', ':' };

static const GUID DmUnusedPartitionGuid =
{
    0x00000000, 0x0000, 0x0000,
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};
static const GUID DmLdmMetadataPartitionGuid =
{
    0x5808C8AA, 0x7E8F, 0x42E0,
    { 0x85, 0xD2, 0xE1, 0xE9, 0x04, 0x34, 0xCF, 0xB3 }
};
static const GUID DmLdmDataPartitionGuid =
{
    0xAF9B60A0, 0x1431, 0x4F62,
    { 0xBC, 0x68, 0x33, 0x11, 0x71, 0x4A, 0x69, 0xAD }
};

#ifndef PARTITION_LDM
#define PARTITION_LDM 0x42
#endif

static BOOLEAN
DmIsDynamicMbrPartitionType(
    _In_ UCHAR PartitionType)
{
    return (PartitionType == PARTITION_LDM);
}

static BOOLEAN
DmIsDynamicGptPartitionType(
    _In_ const GUID *PartitionType)
{
    return (PartitionType != NULL &&
            (IsEqualGUID(PartitionType, &DmLdmMetadataPartitionGuid) ||
             IsEqualGUID(PartitionType, &DmLdmDataPartitionGuid)));
}

static BOOLEAN
DmIsSyntheticMbrLinkEntry(
    _In_ ULONG PartitionIndex,
    _In_ const PARTITION_INFORMATION_EX *Part)
{
    return (Part != NULL &&
            IsContainerPartition(Part->Mbr.PartitionType) &&
            PartitionIndex >= 4 &&
            Part->PartitionNumber == 0);
}

static void
DmFreeExtents(
    _In_opt_ PVOLUME_DISK_EXTENTS Extents)
{
    if (Extents != NULL)
    {
        HeapFree(GetProcessHeap(), 0, Extents);
    }
}

static void
DmFreeVolume(
    _In_ PDM_VOLUME Volume)
{
    DmFreeExtents(Volume->Extents);
}

static void
DmFreeDisk(
    _In_ PDM_DISK Disk)
{
    if (Disk->Regions != NULL)
    {
        HeapFree(GetProcessHeap(), 0, Disk->Regions);
    }
}

static void
DmZeroDisk(
    _Out_ PDM_DISK Disk)
{
    ZeroMemory(Disk, sizeof(*Disk));
}

static void
DmZeroVolume(
    _Out_ PDM_VOLUME Volume)
{
    ZeroMemory(Volume, sizeof(*Volume));
}

static void
DmSnapshotEnsureEmpty(
    _Inout_ PDM_SNAPSHOT Snapshot)
{
    if (Snapshot->Disks != NULL)
    {
        HeapFree(GetProcessHeap(), 0, Snapshot->Disks);
    }

    if (Snapshot->Volumes != NULL)
    {
        HeapFree(GetProcessHeap(), 0, Snapshot->Volumes);
    }

    Snapshot->Disks = NULL;
    Snapshot->Volumes = NULL;
    Snapshot->DiskCount = 0;
    Snapshot->VolumeCount = 0;
}

static void *
DmHeapReallocZero(
    _In_opt_ void *Memory,
    _In_ SIZE_T OldSize,
    _In_ SIZE_T NewSize)
{
    void *Result;

    if (Memory == NULL)
    {
        Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NewSize);
    }
    else
    {
        Result = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Memory, NewSize);
    }

    if (Result != NULL && NewSize > OldSize)
    {
        ZeroMemory((PBYTE)Result + OldSize, NewSize - OldSize);
    }

    return Result;
}

static PDM_DISK
DmAppendDisk(
    _Inout_ PDM_SNAPSHOT Snapshot)
{
    PDM_DISK NewDisks;
    SIZE_T OldSize;
    SIZE_T NewSize;

    OldSize = Snapshot->DiskCount * sizeof(DM_DISK);
    NewSize = (Snapshot->DiskCount + 1) * sizeof(DM_DISK);

    NewDisks = DmHeapReallocZero(Snapshot->Disks, OldSize, NewSize);
    if (NewDisks == NULL)
    {
        return NULL;
    }

    Snapshot->Disks = NewDisks;
    return &Snapshot->Disks[Snapshot->DiskCount++];
}

static PDM_VOLUME
DmAppendVolume(
    _Inout_ PDM_SNAPSHOT Snapshot)
{
    PDM_VOLUME NewVolumes;
    SIZE_T OldSize;
    SIZE_T NewSize;

    OldSize = Snapshot->VolumeCount * sizeof(DM_VOLUME);
    NewSize = (Snapshot->VolumeCount + 1) * sizeof(DM_VOLUME);

    NewVolumes = DmHeapReallocZero(Snapshot->Volumes, OldSize, NewSize);
    if (NewVolumes == NULL)
    {
        return NULL;
    }

    Snapshot->Volumes = NewVolumes;
    return &Snapshot->Volumes[Snapshot->VolumeCount++];
}

static PDM_REGION
DmAppendRegion(
    _Inout_ PDM_DISK Disk)
{
    PDM_REGION NewRegions;
    SIZE_T OldSize;
    SIZE_T NewSize;

    OldSize = Disk->RegionCount * sizeof(DM_REGION);
    NewSize = (Disk->RegionCount + 1) * sizeof(DM_REGION);

    NewRegions = DmHeapReallocZero(Disk->Regions, OldSize, NewSize);
    if (NewRegions == NULL)
    {
        return NULL;
    }

    Disk->Regions = NewRegions;
    return &Disk->Regions[Disk->RegionCount++];
}

static int __cdecl
DmCompareRegionStart(
    _In_ const void *Left,
    _In_ const void *Right)
{
    const DM_REGION *A = (const DM_REGION *)Left;
    const DM_REGION *B = (const DM_REGION *)Right;

    if (A->StartOffset < B->StartOffset)
        return -1;
    if (A->StartOffset > B->StartOffset)
        return 1;
    return (int)A->Type - (int)B->Type;
}

static BOOL
DmQueryDosDeviceList(
    _Outptr_result_buffer_(*CharacterCount) PWSTR *DeviceList,
    _Out_ PDWORD CharacterCount)
{
    DWORD BufferChars = 4096;

    *DeviceList = NULL;
    *CharacterCount = 0;

    for (;;)
    {
        PWSTR Buffer;
        DWORD Result;

        Buffer = HeapAlloc(GetProcessHeap(), 0, BufferChars * sizeof(WCHAR));
        if (Buffer == NULL)
        {
            return FALSE;
        }

        Result = QueryDosDeviceW(NULL, Buffer, BufferChars);
        if (Result != 0)
        {
            *DeviceList = Buffer;
            *CharacterCount = Result;
            return TRUE;
        }

        HeapFree(GetProcessHeap(), 0, Buffer);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || BufferChars >= 0x100000)
        {
            return FALSE;
        }

        BufferChars *= 2;
    }
}

static BOOL
DmEnumerateDiskNumbers(
    _Outptr_result_buffer_maybenull_(*DiskCount) PULONG *DiskNumbers,
    _Out_ PULONG DiskCount)
{
    PWSTR DeviceList;
    DWORD CharacterCount;
    BOOL Success;

    *DiskNumbers = NULL;
    *DiskCount = 0;

    if (!DmQueryDosDeviceList(&DeviceList, &CharacterCount))
    {
        return FALSE;
    }

    UNREFERENCED_PARAMETER(CharacterCount);

    Success = DmSnapshotEnumParseDiskNumbers(DeviceList, DiskNumbers, DiskCount);

    HeapFree(GetProcessHeap(), 0, DeviceList);
    return Success;
}

static BOOL
DmReadStorageDescriptor(
    _In_ HANDLE Handle,
    _Out_ PSTORAGE_DEVICE_DESCRIPTOR *DescriptorOut)
{
    BYTE Buffer[1024];
    STORAGE_PROPERTY_QUERY Query;
    DWORD BytesReturned;
    PSTORAGE_DEVICE_DESCRIPTOR Descriptor;

    *DescriptorOut = NULL;
    ZeroMemory(&Query, sizeof(Query));
    Query.PropertyId = StorageDeviceProperty;
    Query.QueryType = PropertyStandardQuery;

    if (!DeviceIoControl(Handle,
                         IOCTL_STORAGE_QUERY_PROPERTY,
                         &Query,
                         sizeof(Query),
                         Buffer,
                         sizeof(Buffer),
                         &BytesReturned,
                         NULL))
    {
        return FALSE;
    }

    Descriptor = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BytesReturned);
    if (Descriptor == NULL)
    {
        return FALSE;
    }

    CopyMemory(Descriptor, Buffer, BytesReturned);
    *DescriptorOut = Descriptor;
    return TRUE;
}

static VOID
DmFillDiskDescriptorText(
    _Inout_ PDM_DISK Disk,
    _In_ PSTORAGE_DEVICE_DESCRIPTOR Descriptor)
{
    CHAR TextA[DM_MAX_STRING] = "";
    WCHAR TextW[DM_MAX_STRING];
    SIZE_T LenA = 0;

    if (Descriptor->VendorIdOffset != 0)
    {
        StringCchCopyA(TextA, ARRAYSIZE(TextA), (PCSTR)((PBYTE)Descriptor + Descriptor->VendorIdOffset));
        LenA = strlen(TextA);
    }

    if (Descriptor->ProductIdOffset != 0)
    {
        if (LenA != 0 && LenA + 1 < ARRAYSIZE(TextA))
        {
            TextA[LenA++] = ' ';
            TextA[LenA] = '\0';
        }

        StringCchCatA(TextA, ARRAYSIZE(TextA), (PCSTR)((PBYTE)Descriptor + Descriptor->ProductIdOffset));
    }

    if (Descriptor->ProductRevisionOffset != 0)
    {
        StringCchCatA(TextA, ARRAYSIZE(TextA), " ");
        StringCchCatA(TextA, ARRAYSIZE(TextA), (PCSTR)((PBYTE)Descriptor + Descriptor->ProductRevisionOffset));
    }

    if (TextA[0] != '\0' && MultiByteToWideChar(CP_ACP, 0, TextA, -1, TextW, ARRAYSIZE(TextW)) > 0)
    {
        StringCchCopyW(Disk->Description, ARRAYSIZE(Disk->Description), TextW);
    }
}

static NTSTATUS
DmQueryDiskGeometry(
    _In_ HANDLE Handle,
    _Inout_ PDM_DISK Disk)
{
    DISK_GEOMETRY_EX Geometry;
    DWORD BytesReturned;

    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                         NULL,
                         0,
                         &Geometry,
                         sizeof(Geometry),
                         &BytesReturned,
                         NULL))
    {
        return STATUS_UNSUCCESSFUL;
    }

    Disk->BytesPerSector = Geometry.Geometry.BytesPerSector;
    Disk->SectorsPerTrack = Geometry.Geometry.SectorsPerTrack;
    Disk->TracksPerCylinder = Geometry.Geometry.TracksPerCylinder;
    Disk->Cylinders = Geometry.Geometry.Cylinders.QuadPart;
    Disk->Size = Geometry.DiskSize.QuadPart;
    return STATUS_SUCCESS;
}

static NTSTATUS
DmQueryDiskLength(
    _In_ HANDLE Handle,
    _Out_ PULONGLONG Length)
{
    GET_LENGTH_INFORMATION LengthInfo;
    DWORD BytesReturned;

    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_GET_LENGTH_INFO,
                         NULL,
                         0,
                         &LengthInfo,
                         sizeof(LengthInfo),
                         &BytesReturned,
                         NULL))
    {
        return STATUS_UNSUCCESSFUL;
    }

    *Length = LengthInfo.Length.QuadPart;
    return STATUS_SUCCESS;
}

static NTSTATUS
DmQueryDiskLayout(
    _In_ HANDLE Handle,
    _Outptr_result_maybenull_ PDRIVE_LAYOUT_INFORMATION_EX *LayoutOut)
{
    BYTE StackBuffer[sizeof(DRIVE_LAYOUT_INFORMATION_EX) + sizeof(PARTITION_INFORMATION_EX) * 32];
    PDRIVE_LAYOUT_INFORMATION_EX Buffer;
    PVOID NewBuffer;
    DWORD BytesReturned;
    DWORD Size;
    DWORD Error;

    *LayoutOut = NULL;
    Buffer = (PDRIVE_LAYOUT_INFORMATION_EX)StackBuffer;
    Size = sizeof(StackBuffer);

    for (;;)
    {
        if (DeviceIoControl(Handle,
                            IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                            NULL,
                            0,
                            Buffer,
                            Size,
                            &BytesReturned,
                            NULL))
        {
            *LayoutOut = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BytesReturned);
            if (*LayoutOut == NULL)
            {
                if (Buffer != (PDRIVE_LAYOUT_INFORMATION_EX)StackBuffer)
                {
                    HeapFree(GetProcessHeap(), 0, Buffer);
                }
                return STATUS_NO_MEMORY;
            }

            CopyMemory(*LayoutOut, Buffer, BytesReturned);
            if (Buffer != (PDRIVE_LAYOUT_INFORMATION_EX)StackBuffer)
            {
                HeapFree(GetProcessHeap(), 0, Buffer);
            }

            return STATUS_SUCCESS;
        }

        Error = GetLastError();
        if (Error != ERROR_INSUFFICIENT_BUFFER && Error != ERROR_MORE_DATA)
        {
            if (Buffer != (PDRIVE_LAYOUT_INFORMATION_EX)StackBuffer)
            {
                HeapFree(GetProcessHeap(), 0, Buffer);
            }
            return STATUS_UNSUCCESSFUL;
        }

        if (Size >= 64 * 1024)
        {
            if (Buffer != (PDRIVE_LAYOUT_INFORMATION_EX)StackBuffer)
            {
                HeapFree(GetProcessHeap(), 0, Buffer);
            }
            return STATUS_BUFFER_TOO_SMALL;
        }

        Size *= 2;
        if (Size > 64 * 1024)
        {
            Size = 64 * 1024;
        }

        if (Buffer == (PDRIVE_LAYOUT_INFORMATION_EX)StackBuffer)
        {
            Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
        }
        else
        {
            NewBuffer = HeapReAlloc(GetProcessHeap(), 0, Buffer, Size);
            if (NewBuffer == NULL)
            {
                HeapFree(GetProcessHeap(), 0, Buffer);
                return STATUS_NO_MEMORY;
            }

            Buffer = (PDRIVE_LAYOUT_INFORMATION_EX)NewBuffer;
        }

        if (Buffer == NULL)
        {
            return STATUS_NO_MEMORY;
        }
    }
}

static ULONGLONG
DmAlignDownU64(
    _In_ ULONGLONG Value,
    _In_ ULONGLONG Alignment)
{
    if (Alignment <= 1)
        return Value;

    return Value - (Value % Alignment);
}

static VOID
DmComputeDiskUsableBounds(
    _Inout_ PDM_DISK Disk,
    _In_opt_ const DRIVE_LAYOUT_INFORMATION_EX *Layout)
{
    ULONGLONG BytesPerSector;
    ULONGLONG SectorAlignment;
    ULONGLONG TotalSectors;
    ULONGLONG StartSector;
    ULONGLONG EndSector;
    ULONGLONG UsableSectorCount;

    if (Disk == NULL)
        return;

    Disk->UsableStartOffset = 0;
    Disk->UsableEndOffset = Disk->Size;

    BytesPerSector = max(Disk->BytesPerSector, 512ULL);
    SectorAlignment = max((ULONGLONG)Disk->SectorAlignment, 1ULL);
    TotalSectors = Disk->Size / BytesPerSector;

    if (Layout == NULL || Disk->Size == 0)
        return;

    if (Layout->PartitionStyle == PARTITION_STYLE_MBR)
    {
        StartSector = SectorAlignment;
        EndSector = (TotalSectors != 0) ? min(TotalSectors, 0x100000000ULL) - 1 : 0;

        if (TotalSectors == 0 || StartSector > EndSector)
        {
            Disk->UsableEndOffset = Disk->UsableStartOffset;
            return;
        }

        Disk->UsableStartOffset = min(StartSector * BytesPerSector, Disk->Size);
        Disk->UsableEndOffset = min((EndSector + 1) * BytesPerSector, Disk->Size);
    }
    else if (Layout->PartitionStyle == PARTITION_STYLE_GPT)
    {
        StartSector = DmAlignDownU64((ULONGLONG)Layout->Gpt.StartingUsableOffset.QuadPart / BytesPerSector,
                                     SectorAlignment) + SectorAlignment;
        UsableSectorCount = (ULONGLONG)Layout->Gpt.UsableLength.QuadPart / BytesPerSector;

        if (UsableSectorCount == 0)
        {
            Disk->UsableEndOffset = Disk->UsableStartOffset;
            return;
        }

        EndSector = DmAlignDownU64(StartSector + UsableSectorCount - 1, SectorAlignment);
        if (StartSector > EndSector)
        {
            Disk->UsableEndOffset = Disk->UsableStartOffset;
            return;
        }

        Disk->UsableStartOffset = min(StartSector * BytesPerSector, Disk->Size);
        Disk->UsableEndOffset = min((EndSector + 1) * BytesPerSector, Disk->Size);
    }
}

static NTSTATUS
DmQueryVolumeExtents(
    _In_ HANDLE Handle,
    _Inout_ PDM_VOLUME Volume)
{
    PVOLUME_DISK_EXTENTS Extents;
    PVOID NewExtents;
    DWORD BytesReturned;
    DWORD Size;
    DWORD Error;

    Size = sizeof(VOLUME_DISK_EXTENTS);
    Extents = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (Extents == NULL)
    {
        return STATUS_NO_MEMORY;
    }

    for (;;)
    {
        if (DeviceIoControl(Handle,
                            IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                            NULL,
                            0,
                            Extents,
                            Size,
                            &BytesReturned,
                            NULL))
        {
            Volume->Extents = Extents;
            Volume->ExtentCount = Extents->NumberOfDiskExtents;
            Volume->IsMultiExtent = (Volume->ExtentCount > 1);
            Volume->Size.QuadPart = 0;
            {
                ULONG Index;
                for (Index = 0; Index < Extents->NumberOfDiskExtents; Index++)
                {
                    Volume->Size.QuadPart += Extents->Extents[Index].ExtentLength.QuadPart;
                }
            }
            return STATUS_SUCCESS;
        }

        Error = GetLastError();
        if (Error != ERROR_MORE_DATA)
        {
            HeapFree(GetProcessHeap(), 0, Extents);
            Volume->IsMultiExtent = FALSE;
            return STATUS_UNSUCCESSFUL;
        }

        Size = sizeof(VOLUME_DISK_EXTENTS) +
               (Extents->NumberOfDiskExtents - 1) * sizeof(DISK_EXTENT);
        {
            NewExtents = HeapReAlloc(GetProcessHeap(), 0, Extents, Size);
            if (NewExtents == NULL)
            {
                HeapFree(GetProcessHeap(), 0, Extents);
                return STATUS_NO_MEMORY;
            }

            Extents = (PVOLUME_DISK_EXTENTS)NewExtents;
        }
    }
}

static NTSTATUS
DmQueryVolumeUniqueId(
    _In_ HANDLE Handle,
    _Inout_ PDM_VOLUME Volume)
{
    PMOUNTDEV_UNIQUE_ID UniqueId;
    PVOID NewBuffer;
    DWORD BytesReturned;
    DWORD Size;
    DWORD HeaderSize;
    DWORD Error;
    ULONG CopyLength;

    if (Handle == INVALID_HANDLE_VALUE || Volume == NULL)
        return STATUS_INVALID_PARAMETER;

    HeaderSize = FIELD_OFFSET(MOUNTDEV_UNIQUE_ID, UniqueId);
    Size = sizeof(MOUNTDEV_UNIQUE_ID);
    UniqueId = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (UniqueId == NULL)
        return STATUS_NO_MEMORY;

    for (;;)
    {
        BytesReturned = 0;
        if (DeviceIoControl(Handle,
                            IOCTL_MOUNTDEV_QUERY_UNIQUE_ID,
                            NULL,
                            0,
                            UniqueId,
                            Size,
                            &BytesReturned,
                            NULL))
        {
            Volume->HasUniqueId = TRUE;
            Volume->UniqueIdLength = UniqueId->UniqueIdLength;
            CopyLength = min((ULONG)UniqueId->UniqueIdLength, (ULONG)ARRAYSIZE(Volume->UniqueId));
            if (CopyLength != 0)
            {
                CopyMemory(Volume->UniqueId, UniqueId->UniqueId, CopyLength);
                if (CopyLength >= ARRAYSIZE(DmDmioIdSignature) &&
                    memcmp(Volume->UniqueId, DmDmioIdSignature, ARRAYSIZE(DmDmioIdSignature)) == 0)
                {
                    Volume->HasDmioUniqueId = TRUE;
                }
            }

            HeapFree(GetProcessHeap(), 0, UniqueId);
            return STATUS_SUCCESS;
        }

        Error = GetLastError();
        if (Error != ERROR_MORE_DATA || UniqueId->UniqueIdLength == 0)
            break;

        Size = HeaderSize + UniqueId->UniqueIdLength;
        NewBuffer = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, UniqueId, Size);
        if (NewBuffer == NULL)
        {
            HeapFree(GetProcessHeap(), 0, UniqueId);
            return STATUS_NO_MEMORY;
        }

        UniqueId = (PMOUNTDEV_UNIQUE_ID)NewBuffer;
    }

    HeapFree(GetProcessHeap(), 0, UniqueId);
    return STATUS_UNSUCCESSFUL;
}

static VOID
DmUpdateBootFlags(
    _Inout_ PDM_VOLUME Volume)
{
    WCHAR SystemDirectory[MAX_PATH];

    Volume->IsBoot = FALSE;
    if (Volume->DriveLetter == 0)
    {
        return;
    }

    if (GetSystemDirectoryW(SystemDirectory, ARRAYSIZE(SystemDirectory)) != 0 &&
        towupper(SystemDirectory[0]) == towupper(Volume->DriveLetter))
    {
        Volume->IsBoot = TRUE;
    }
}

static VOID
DmUpdateSystemFlags(
    _Inout_ PDM_VOLUME Volume)
{
    HKEY Key;
    WCHAR SystemPartition[MAX_PATH];
    DWORD Type;
    DWORD Size;

    Volume->IsSystem = FALSE;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\Setup",
                      0,
                      KEY_READ,
                      &Key) != ERROR_SUCCESS)
    {
        return;
    }

    Size = sizeof(SystemPartition);
    if (RegQueryValueExW(Key,
                         L"SystemPartition",
                         NULL,
                         &Type,
                         (LPBYTE)SystemPartition,
                         &Size) == ERROR_SUCCESS &&
        Type == REG_SZ)
    {
        if (_wcsicmp(SystemPartition, Volume->DeviceName) == 0)
        {
            Volume->IsSystem = TRUE;
        }
    }

    RegCloseKey(Key);
}

static VOID
DmPopulateDiskRegions(
    _Inout_ PDM_DISK Disk,
    _In_ PDRIVE_LAYOUT_INFORMATION_EX Layout)
{
    ULONG Index;
    PDM_REGION Region;
    PPARTITION_INFORMATION_EX Part;

    for (Index = 0; Index < Layout->PartitionCount; Index++)
    {
        Part = &Layout->PartitionEntry[Index];

        if (Layout->PartitionStyle == PARTITION_STYLE_MBR)
        {
            if (Part->Mbr.PartitionType == PARTITION_ENTRY_UNUSED)
            {
                continue;
            }

            if (DmIsSyntheticMbrLinkEntry(Index, Part))
            {
                continue;
            }
        }
        else if (Layout->PartitionStyle == PARTITION_STYLE_GPT)
        {
            if (IsEqualGUID(&Part->Gpt.PartitionType, &DmUnusedPartitionGuid))
            {
                continue;
            }
        }

        if (Layout->PartitionStyle == PARTITION_STYLE_MBR &&
            IsContainerPartition(Part->Mbr.PartitionType))
        {
            Disk->HasExtendedPartition = TRUE;
            Disk->ExtendedPartitionOffset = Part->StartingOffset.QuadPart;
            Disk->ExtendedPartitionLength = Part->PartitionLength.QuadPart;
        }

        Region = DmAppendRegion(Disk);
        if (Region == NULL)
        {
            Disk->PartitionCount++;
            continue;
        }

        Region->Type = DmRegionPartition;
        Region->PartitionStyle = Layout->PartitionStyle;
        Region->DiskNumber = Disk->DiskNumber;
        Region->PartitionNumber = Part->PartitionNumber;
        Region->PartitionIndex = Index;
        Region->StartOffset = Part->StartingOffset.QuadPart;
        Region->Length = Part->PartitionLength.QuadPart;
        Region->IsLogical = FALSE;

        if (Layout->PartitionStyle == PARTITION_STYLE_MBR)
        {
            Region->Data.Mbr.BootIndicator = Part->Mbr.BootIndicator;
            Region->Data.Mbr.PartitionType = Part->Mbr.PartitionType;
            Region->IsContainer = IsContainerPartition(Part->Mbr.PartitionType);
            Region->IsLogical = (!Region->IsContainer &&
                                 Index >= 4 &&
                                 Part->PartitionNumber != 0);
            Region->IsDynamic = DmIsDynamicMbrPartitionType(Part->Mbr.PartitionType);
            Region->IsHidden = Region->IsContainer;
        }
        else
        {
            CopyMemory(&Region->Data.Gpt.PartitionType,
                       &Part->Gpt.PartitionType,
                       sizeof(GUID));
            CopyMemory(&Region->Data.Gpt.PartitionId,
                       &Part->Gpt.PartitionId,
                       sizeof(GUID));
            Region->Data.Gpt.Attributes = Part->Gpt.Attributes;
            Region->IsDynamic = DmIsDynamicGptPartitionType(&Part->Gpt.PartitionType);
        }

        if (Region->IsDynamic)
        {
            Disk->IsDynamic = TRUE;
        }

        Disk->PartitionCount++;
    }
}

static VOID
DmBuildFreeRegions(
    _Inout_ PDM_DISK Disk)
{
    ULONG Index;
    ULONG InitialCount;
    ULONGLONG Cursor;
    ULONGLONG UsableEnd;
    ULONGLONG RegionEnd;
    PDM_REGION FreeRegion;

    if (Disk == NULL)
        return;

    Cursor = min(Disk->UsableStartOffset, Disk->Size);
    UsableEnd = min(Disk->UsableEndOffset, Disk->Size);
    if (UsableEnd < Cursor)
        UsableEnd = Cursor;

    if (Disk->RegionCount == 0)
    {
        FreeRegion = DmAppendRegion(Disk);
        if (FreeRegion != NULL && UsableEnd > Cursor)
        {
            FreeRegion->Type = DmRegionFree;
            FreeRegion->PartitionStyle = Disk->PartitionStyle;
            FreeRegion->DiskNumber = Disk->DiskNumber;
            FreeRegion->StartOffset = Cursor;
            FreeRegion->Length = UsableEnd - Cursor;
        }
        return;
    }

    qsort(Disk->Regions, Disk->RegionCount, sizeof(DM_REGION), DmCompareRegionStart);

    InitialCount = Disk->RegionCount;
    for (Index = 0; Index < InitialCount; Index++)
    {
        if (Disk->Regions[Index].Length == 0)
            continue;

        if (Disk->Regions[Index].StartOffset >= UsableEnd)
            break;

        if (Disk->Regions[Index].StartOffset > Cursor)
        {
            FreeRegion = DmAppendRegion(Disk);
            if (FreeRegion != NULL)
            {
                FreeRegion->Type = DmRegionFree;
                FreeRegion->PartitionStyle = Disk->PartitionStyle;
                FreeRegion->DiskNumber = Disk->DiskNumber;
                FreeRegion->StartOffset = Cursor;
                FreeRegion->Length = min(Disk->Regions[Index].StartOffset, UsableEnd) - Cursor;
            }
        }

        RegionEnd = Disk->Regions[Index].StartOffset + Disk->Regions[Index].Length;
        if (RegionEnd > Cursor)
            Cursor = min(RegionEnd, UsableEnd);
    }

    if (Cursor < UsableEnd)
    {
        FreeRegion = DmAppendRegion(Disk);
        if (FreeRegion != NULL)
        {
            FreeRegion->Type = DmRegionFree;
            FreeRegion->PartitionStyle = Disk->PartitionStyle;
            FreeRegion->DiskNumber = Disk->DiskNumber;
            FreeRegion->StartOffset = Cursor;
            FreeRegion->Length = UsableEnd - Cursor;
        }
    }

    qsort(Disk->Regions, Disk->RegionCount, sizeof(DM_REGION), DmCompareRegionStart);
}

static VOID
DmBuildExtendedFreeRegions(
    _Inout_ PDM_DISK Disk)
{
    ULONG Index;
    ULONG InitialCount;
    ULONGLONG AlignmentBytes;
    ULONGLONG Cursor;
    ULONGLONG ExtendedEnd;
    ULONGLONG Boundary;
    PDM_REGION FreeRegion;

    if (Disk == NULL || !Disk->HasExtendedPartition || Disk->ExtendedPartitionLength == 0)
        return;

    AlignmentBytes = max(Disk->BytesPerSector, 512ULL) * max((ULONGLONG)Disk->SectorAlignment, 1ULL);
    Cursor = min(Disk->ExtendedPartitionOffset + AlignmentBytes,
                 Disk->ExtendedPartitionOffset + Disk->ExtendedPartitionLength);
    ExtendedEnd = min(Disk->ExtendedPartitionOffset + Disk->ExtendedPartitionLength, Disk->Size);
    if (Cursor >= ExtendedEnd)
        return;

    qsort(Disk->Regions, Disk->RegionCount, sizeof(DM_REGION), DmCompareRegionStart);

    InitialCount = Disk->RegionCount;
    for (Index = 0; Index < InitialCount; Index++)
    {
        PDM_REGION Region;

        Region = &Disk->Regions[Index];
        if (Region->Type != DmRegionPartition || !Region->IsLogical || Region->Length == 0)
            continue;

        if (Region->StartOffset >= ExtendedEnd)
            break;

        Boundary = (Region->StartOffset > AlignmentBytes) ? (Region->StartOffset - AlignmentBytes) : 0;
        if (Boundary > Cursor)
        {
            FreeRegion = DmAppendRegion(Disk);
            if (FreeRegion != NULL)
            {
                FreeRegion->Type = DmRegionFree;
                FreeRegion->PartitionStyle = Disk->PartitionStyle;
                FreeRegion->DiskNumber = Disk->DiskNumber;
                FreeRegion->StartOffset = Cursor;
                FreeRegion->Length = min(Boundary, ExtendedEnd) - Cursor;
                FreeRegion->IsLogical = TRUE;
            }
        }

        if (Region->StartOffset + Region->Length > Cursor)
            Cursor = min(Region->StartOffset + Region->Length, ExtendedEnd);
    }

    if (Cursor < ExtendedEnd)
    {
        FreeRegion = DmAppendRegion(Disk);
        if (FreeRegion != NULL)
        {
            FreeRegion->Type = DmRegionFree;
            FreeRegion->PartitionStyle = Disk->PartitionStyle;
            FreeRegion->DiskNumber = Disk->DiskNumber;
            FreeRegion->StartOffset = Cursor;
            FreeRegion->Length = ExtendedEnd - Cursor;
            FreeRegion->IsLogical = TRUE;
        }
    }

    qsort(Disk->Regions, Disk->RegionCount, sizeof(DM_REGION), DmCompareRegionStart);
}

static VOID
DmQueryDiskAttributes(
    _In_ HANDLE Handle,
    _Inout_ PDM_DISK Disk)
{
    GET_DISK_ATTRIBUTES Attributes;
    DWORD BytesReturned;

    ZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Version = sizeof(Attributes);
    BytesReturned = 0;

    if (!DeviceIoControl(Handle,
                         IOCTL_DISK_GET_DISK_ATTRIBUTES,
                         NULL,
                         0,
                         &Attributes,
                         sizeof(Attributes),
                         &BytesReturned,
                         NULL))
    {
        return;
    }

    Disk->IsOffline = ((Attributes.Attributes & DISK_ATTRIBUTE_OFFLINE) != 0);
    Disk->IsReadOnly = ((Attributes.Attributes & DISK_ATTRIBUTE_READ_ONLY) != 0);
}

static NTSTATUS
DmPopulateDisk(
    _Inout_ PDM_SNAPSHOT Snapshot,
    _In_ ULONG DiskNumber)
{
    WCHAR Path[MAX_PATH];
    HANDLE Handle;
    PDM_DISK Disk;
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PSTORAGE_DEVICE_DESCRIPTOR Descriptor;
    NTSTATUS Status;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", DiskNumber);
    Handle = CreateFileW(Path,
                         GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        return STATUS_UNSUCCESSFUL;
    }

    Disk = DmAppendDisk(Snapshot);
    if (Disk == NULL)
    {
        CloseHandle(Handle);
        return STATUS_NO_MEMORY;
    }

    DmZeroDisk(Disk);
    Disk->DiskNumber = DiskNumber;
    StringCchCopyW(Disk->DeviceName, ARRAYSIZE(Disk->DeviceName), Path);
    StringCchPrintfW(Disk->Location, ARRAYSIZE(Disk->Location), L"Disk %lu", DiskNumber);
    Disk->SectorAlignment = 1;

    Status = DmQueryDiskGeometry(Handle, Disk);
    if (!NT_SUCCESS(Status) && Disk->BytesPerSector == 0)
    {
        Disk->BytesPerSector = 512;
    }
    if (Disk->BytesPerSector != 0)
    {
        Disk->SectorAlignment = (ULONG)max((1024ULL * 1024ULL) / Disk->BytesPerSector, 1ULL);
    }

    Status = DmQueryDiskLength(Handle, &Disk->Size);
    if (!NT_SUCCESS(Status) && Disk->Size == 0)
    {
        if (Disk->BytesPerSector != 0)
        {
            Disk->Size = Disk->BytesPerSector * 1024ULL * 1024ULL;
        }
    }

    if (DmReadStorageDescriptor(Handle, &Descriptor))
    {
        Disk->BusType = Descriptor->BusType;
        Disk->IsRemovable = (Descriptor->RemovableMedia != 0);
        DmFillDiskDescriptorText(Disk, Descriptor);
        HeapFree(GetProcessHeap(), 0, Descriptor);
    }

    if (Disk->Description[0] == UNICODE_NULL)
    {
        StringCchCopyW(Disk->Description, ARRAYSIZE(Disk->Description), Path + 4);
    }

    DmQueryDiskAttributes(Handle, Disk);

    Status = DmQueryDiskLayout(Handle, &Layout);
    if (NT_SUCCESS(Status) && Layout != NULL)
    {
        Disk->PartitionStyle = Layout->PartitionStyle;
        if (Layout->PartitionStyle == PARTITION_STYLE_MBR)
        {
            Disk->MbrSignature = Layout->Mbr.Signature;
        }
        else if (Layout->PartitionStyle == PARTITION_STYLE_GPT)
        {
            Disk->GptDiskId = Layout->Gpt.DiskId;
        }
        DmComputeDiskUsableBounds(Disk, Layout);
        DmPopulateDiskRegions(Disk, Layout);
        HeapFree(GetProcessHeap(), 0, Layout);
    }
    else
    {
        Disk->PartitionStyle = PARTITION_STYLE_RAW;
        DmComputeDiskUsableBounds(Disk, NULL);
    }

    DmBuildFreeRegions(Disk);
    DmBuildExtendedFreeRegions(Disk);
    CloseHandle(Handle);
    return STATUS_SUCCESS;
}

static NTSTATUS
DmPopulateVolume(
    _Inout_ PDM_SNAPSHOT Snapshot,
    _In_ ULONG VolumeNumber,
    _In_z_ PWSTR VolumeName)
{
    PDM_VOLUME Volume;
    HANDLE Handle;
    WCHAR Paths[4096];
    DWORD PathLength;
    WCHAR Label[MAX_PATH];
    WCHAR FsName[MAX_PATH];
    WCHAR TempName[MAX_PATH];
    ULARGE_INTEGER FreeBytesAvailable;
    ULARGE_INTEGER TotalBytes;
    ULARGE_INTEGER TotalFreeBytes;
    STORAGE_DEVICE_NUMBER DeviceNumber;
    DWORD BytesReturned;

    Volume = DmAppendVolume(Snapshot);
    if (Volume == NULL)
    {
        return STATUS_NO_MEMORY;
    }

    DmZeroVolume(Volume);
    Volume->VolumeNumber = VolumeNumber;
    StringCchCopyW(Volume->VolumeName, ARRAYSIZE(Volume->VolumeName), VolumeName);
    StringCchCopyW(TempName, ARRAYSIZE(TempName), VolumeName);

    if (wcslen(TempName) > 0 && TempName[wcslen(TempName) - 1] == L'\\')
    {
        TempName[wcslen(TempName) - 1] = UNICODE_NULL;
    }

    if (wcslen(TempName) > 4)
    {
        if (QueryDosDeviceW(&TempName[4], Volume->DeviceName, ARRAYSIZE(Volume->DeviceName)) == 0)
        {
            Volume->DeviceName[0] = UNICODE_NULL;
        }
    }

    if (GetVolumePathNamesForVolumeNameW(VolumeName, Paths, ARRAYSIZE(Paths), &PathLength))
    {
        PWSTR Path = Paths;
        while (*Path != UNICODE_NULL)
        {
            if (Path[0] != UNICODE_NULL && Path[1] == L':' && Path[2] == L'\\')
            {
                if (!Volume->HasDriveLetter)
                {
                    Volume->DriveLetter = Path[0];
                    Volume->HasDriveLetter = TRUE;
                }
            }
            else if (Volume->FolderMountPoint[0] == UNICODE_NULL)
            {
                StringCchCopyW(Volume->FolderMountPoint, ARRAYSIZE(Volume->FolderMountPoint), Path);
            }

            if (!(Path[0] != UNICODE_NULL && Path[1] == L':' && Path[2] == L'\\') &&
                Volume->FolderMountPointCount < DM_MAX_MOUNT_PATHS)
            {
                StringCchCopyW(Volume->FolderMountPoints[Volume->FolderMountPointCount],
                               ARRAYSIZE(Volume->FolderMountPoints[Volume->FolderMountPointCount]),
                               Path);
                Volume->FolderMountPointCount++;
            }

            Path += wcslen(Path) + 1;
        }

        if (Volume->HasDriveLetter)
        {
            StringCchPrintfW(Volume->MountPoint,
                             ARRAYSIZE(Volume->MountPoint),
                             L"%C:\\",
                             towupper(Volume->DriveLetter));
        }
        else if (Volume->FolderMountPoint[0] != UNICODE_NULL)
        {
            StringCchCopyW(Volume->MountPoint,
                           ARRAYSIZE(Volume->MountPoint),
                           Volume->FolderMountPoint);
        }
    }

    if (GetVolumeInformationW(VolumeName,
                              Label,
                              ARRAYSIZE(Label),
                              &Volume->SerialNumber,
                              NULL,
                              NULL,
                              FsName,
                              ARRAYSIZE(FsName)))
    {
        StringCchCopyW(Volume->Label, ARRAYSIZE(Volume->Label), Label);
        StringCchCopyW(Volume->FileSystem, ARRAYSIZE(Volume->FileSystem), FsName);
    }
    else if (GetLastError() == ERROR_UNRECOGNIZED_VOLUME)
    {
        StringCchCopyW(Volume->FileSystem, ARRAYSIZE(Volume->FileSystem), L"RAW");
    }

    Handle = CreateFileW(VolumeName,
                         0,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle != INVALID_HANDLE_VALUE)
    {
        ZeroMemory(&FreeBytesAvailable, sizeof(FreeBytesAvailable));
        ZeroMemory(&TotalBytes, sizeof(TotalBytes));
        ZeroMemory(&TotalFreeBytes, sizeof(TotalFreeBytes));

        if (GetDiskFreeSpaceExW(VolumeName,
                                &FreeBytesAvailable,
                                &TotalBytes,
                                &TotalFreeBytes))
        {
            DWORD SectorsPerCluster;
            DWORD BytesPerSector;
            DWORD NumberOfFreeClusters;
            DWORD NumberOfClusters;

            if (Volume->Size.QuadPart == 0)
            {
                Volume->Size = TotalBytes;
            }

            Volume->FreeBytes = TotalFreeBytes;

            if (GetDiskFreeSpaceW(VolumeName,
                                  &SectorsPerCluster,
                                  &BytesPerSector,
                                  &NumberOfFreeClusters,
                                  &NumberOfClusters))
            {
                Volume->SectorsPerAllocationUnit = SectorsPerCluster;
                Volume->BytesPerSector = BytesPerSector;
                Volume->AvailableAllocationUnits.QuadPart = NumberOfFreeClusters;
                Volume->TotalAllocationUnits.QuadPart = NumberOfClusters;
            }
        }

        if (!NT_SUCCESS(DmQueryVolumeExtents(Handle, Volume)))
        {
            DmFreeExtents(Volume->Extents);
            Volume->Extents = NULL;
            Volume->ExtentCount = 0;
        }
        else if (Volume->ExtentCount > 1)
        {
            Volume->IsMultiExtent = TRUE;
        }

        ZeroMemory(&DeviceNumber, sizeof(DeviceNumber));
        BytesReturned = 0;
        if (DeviceIoControl(Handle,
                            IOCTL_STORAGE_GET_DEVICE_NUMBER,
                            NULL,
                            0,
                            &DeviceNumber,
                            sizeof(DeviceNumber),
                            &BytesReturned,
                            NULL))
        {
            Volume->StorageDiskNumber = DeviceNumber.DeviceNumber;
            Volume->StoragePartitionNumber = DeviceNumber.PartitionNumber;
            Volume->HasStorageDeviceNumber = (DeviceNumber.DeviceNumber != ULONG_MAX &&
                                             DeviceNumber.PartitionNumber != ULONG_MAX &&
                                             DeviceNumber.PartitionNumber != 0);
        }

        DmQueryVolumeUniqueId(Handle, Volume);

        CloseHandle(Handle);
    }

    DmUpdateSystemFlags(Volume);
    DmUpdateBootFlags(Volume);
    return STATUS_SUCCESS;
}

VOID
DmSnapshotInitialize(
    _Out_ PDM_SNAPSHOT Snapshot)
{
    ZeroMemory(Snapshot, sizeof(*Snapshot));
}

VOID
DmSnapshotClear(
    _Inout_ PDM_SNAPSHOT Snapshot)
{
    ULONG i;

    if (Snapshot->Disks != NULL)
    {
        for (i = 0; i < Snapshot->DiskCount; i++)
        {
            DmFreeDisk(&Snapshot->Disks[i]);
        }
    }

    if (Snapshot->Volumes != NULL)
    {
        for (i = 0; i < Snapshot->VolumeCount; i++)
        {
            DmFreeVolume(&Snapshot->Volumes[i]);
        }
    }

    DmSnapshotEnsureEmpty(Snapshot);
    Snapshot->Generation++;
}

NTSTATUS
DmSnapshotRefresh(
    _Inout_ PDM_SNAPSHOT Snapshot)
{
    PULONG DiskNumbers = NULL;
    ULONG DiskNumber;
    ULONG DiskIndex;
    ULONG DiskCount = 0;
    ULONG ConsecutiveFailures;
    HANDLE VolumeHandle;
    WCHAR VolumeName[MAX_PATH];
    NTSTATUS Status;
    BOOL Enumerated;

    DmSnapshotClear(Snapshot);

    Enumerated = DmEnumerateDiskNumbers(&DiskNumbers, &DiskCount);
    if (Enumerated && DiskCount != 0)
    {
        for (DiskIndex = 0; DiskIndex < DiskCount; DiskIndex++)
        {
            DmPopulateDisk(Snapshot, DiskNumbers[DiskIndex]);
        }
    }
    else
    {
        ConsecutiveFailures = 0;
        for (DiskNumber = 0; DiskNumber < 256; DiskNumber++)
        {
            Status = DmPopulateDisk(Snapshot, DiskNumber);
            if (NT_SUCCESS(Status))
            {
                ConsecutiveFailures = 0;
                continue;
            }

            if (DiskNumber > 0)
            {
                ConsecutiveFailures++;
                if (ConsecutiveFailures >= 8)
                {
                    break;
                }
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, DiskNumbers);

    VolumeHandle = FindFirstVolumeW(VolumeName, ARRAYSIZE(VolumeName));
    if (VolumeHandle != INVALID_HANDLE_VALUE)
    {
        ULONG VolumeNumber = 0;

        DmPopulateVolume(Snapshot, VolumeNumber++, VolumeName);
        for (;;)
        {
            if (!FindNextVolumeW(VolumeHandle, VolumeName, ARRAYSIZE(VolumeName)))
            {
                break;
            }

            DmPopulateVolume(Snapshot, VolumeNumber++, VolumeName);
        }

        FindVolumeClose(VolumeHandle);
    }

    DmSnapshotMatchVolumesToRegions(Snapshot);
    return STATUS_SUCCESS;
}

NTSTATUS
DmSnapshotCreate(
    _Out_ PDM_SNAPSHOT *Snapshot)
{
    PDM_SNAPSHOT Result;
    NTSTATUS Status;

    if (Snapshot == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Result));
    if (Result == NULL)
    {
        return STATUS_NO_MEMORY;
    }

    DmSnapshotInitialize(Result);
    Status = DmSnapshotRefresh(Result);
    if (!NT_SUCCESS(Status))
    {
        DmSnapshotDestroy(Result);
        return Status;
    }

    *Snapshot = Result;
    return STATUS_SUCCESS;
}

VOID
DmSnapshotDestroy(
    _In_opt_ PDM_SNAPSHOT Snapshot)
{
    if (Snapshot == NULL)
    {
        return;
    }

    DmSnapshotClear(Snapshot);
    HeapFree(GetProcessHeap(), 0, Snapshot);
}

PDM_DISK
DmSnapshotFindDiskByNumber(
    _In_ PDM_SNAPSHOT Snapshot,
    _In_ ULONG DiskNumber)
{
    ULONG i;

    for (i = 0; i < Snapshot->DiskCount; i++)
    {
        if (Snapshot->Disks[i].DiskNumber == DiskNumber)
        {
            return &Snapshot->Disks[i];
        }
    }

    return NULL;
}

PDM_VOLUME
DmSnapshotFindVolumeByName(
    _In_ PDM_SNAPSHOT Snapshot,
    _In_ PCWSTR VolumeName)
{
    ULONG i;

    for (i = 0; i < Snapshot->VolumeCount; i++)
    {
        if (_wcsicmp(Snapshot->Volumes[i].VolumeName, VolumeName) == 0)
        {
            return &Snapshot->Volumes[i];
        }
    }

    return NULL;
}
