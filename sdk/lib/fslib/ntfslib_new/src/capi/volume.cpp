/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     C interface for Volume class
 * COPYRIGHT:   Copyright 2026 Carl Bialorucki <carl.bialorucki@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Applied to volumes subsequently opened by this library instance. */
BOOLEAN NtfsDefaultShowMetadataFiles = FALSE;
BOOLEAN NtfsDefaultReadOnlyMode = FALSE;

void
NtfsSetShowMetadataFiles(
    _In_ BOOLEAN Show)
{
    NtfsDefaultShowMetadataFiles = Show;
}

void
NtfsSetReadOnlyMode(
    _In_ BOOLEAN ReadOnly)
{
    NtfsDefaultReadOnlyMode = ReadOnly;
}

NTSTATUS
NtfsVolumeGetADSPreference(
    _In_ PNtfsVolume DiskVolume,
    _In_ PUNICODE_STRING FileName,
    _Out_ AttributeType* RequestedType,
    _Out_ PWSTR* RequestedStream)
{
    return reinterpret_cast<PVolume>(DiskVolume)->GetADSPreference(FileName,
                                                                   RequestedType,
                                                                   RequestedStream);
}

NTSTATUS
NtfsVolumeReadSecurityDescriptorById(
    _In_ PNtfsVolume DiskVolume,
    _In_ ULONG SecurityId,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength)
{
    if (!DiskVolume)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PVolume>(DiskVolume)->
        ReadSecurityDescriptorById(
            SecurityId,
            Buffer,
            BufferLength);
}

void
NtfsVolumeDestroy(
    _In_opt_ PNtfsVolume DiskVolume)
{
    delete reinterpret_cast<PVolume>(DiskVolume);
}

ULONG
NtfsVolumeGetBytesPerSector(
    _In_ PNtfsVolume DiskVolume)
{
    return reinterpret_cast<PVolume>(DiskVolume)->BytesPerSector;
}

UINT64
NtfsVolumeGetClustersInVolume(
    _In_ PNtfsVolume DiskVolume)
{
    return reinterpret_cast<PVolume>(DiskVolume)->ClustersInVolume;
}

NTSTATUS
NtfsVolumeGetFreeClusters(
    _In_ PNtfsVolume DiskVolume,
    _Out_ PLARGE_INTEGER FreeClusters)
{
    return reinterpret_cast<PVolume>(DiskVolume)->GetFreeClusters(FreeClusters);
}

NTSTATUS
NtfsVolumeQueryInformation(
    _In_ PNtfsVolume DiskVolume,
    _Out_ PNtfsVolumeInformation Information)
{
    if (!DiskVolume)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PVolume>(DiskVolume)->
        QueryInformation(Information);
}

NTSTATUS
NtfsVolumeReadBitmap(
    _In_ PNtfsVolume DiskVolume,
    _In_ ULONGLONG StartingLcn,
    _Out_ PULONGLONG ReturnedStartingLcn,
    _Out_ PULONGLONG BitmapSize,
    _Out_opt_ PUCHAR Bitmap,
    _Inout_ PULONG BitmapLength)
{
    if (!DiskVolume)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PVolume>(DiskVolume)->ReadBitmap(
        StartingLcn,
        ReturnedStartingLcn,
        BitmapSize,
        Bitmap,
        BitmapLength);
}

PNtfsLogFileService
NtfsVolumeGetLFS(
    _In_ PNtfsVolume DiskVolume)
{
    return reinterpret_cast<PNtfsLogFileService>(
        reinterpret_cast<PVolume>(DiskVolume)->LFS);
}

PNtfsMasterFileTable
NtfsVolumeGetMft(
    _In_ PNtfsVolume DiskVolume)
{
    return reinterpret_cast<PNtfsMasterFileTable>(
        reinterpret_cast<PVolume>(DiskVolume)->MFT);
}

USHORT
NtfsVolumeGetMajorVersion(
    _In_ PNtfsVolume DiskVolume)
{
    return reinterpret_cast<PVolume>(DiskVolume)->NtfsMajorVersion;
}

USHORT
NtfsVolumeGetMinorVersion(
    _In_ PNtfsVolume DiskVolume)
{
    return reinterpret_cast<PVolume>(DiskVolume)->NtfsMinorVersion;
}

UINT8
NtfsVolumeGetSectorsPerCluster(
    _In_ PNtfsVolume DiskVolume)
{
    return reinterpret_cast<PVolume>(DiskVolume)->SectorsPerCluster;
}

UINT64
NtfsVolumeGetSerialNumber(
    _In_ PNtfsVolume DiskVolume)
{
    return reinterpret_cast<PVolume>(DiskVolume)->SerialNumber;
}

NTSTATUS
NtfsVolumeGetVolumeLabel(
    _In_ PNtfsVolume DiskVolume,
    _Out_ PWSTR VolumeLabel,
    _Out_ PUSHORT Length)
{
    return reinterpret_cast<PVolume>(DiskVolume)->GetVolumeLabel(VolumeLabel, Length);
}

BOOLEAN
NtfsVolumeIsReadOnly(
    _In_ PNtfsVolume DiskVolume)
{
    return reinterpret_cast<PVolume>(DiskVolume)->IsReadOnly;
}

NTSTATUS
NtfsVolumeSetVolumeLabel(
    _In_ PNtfsVolume DiskVolume,
    _In_ PWSTR VolumeLabel,
    _In_ ULONG Length)
{
    return reinterpret_cast<PVolume>(DiskVolume)->SetVolumeLabel(VolumeLabel, Length);
}

#ifdef __cplusplus
}
#endif
