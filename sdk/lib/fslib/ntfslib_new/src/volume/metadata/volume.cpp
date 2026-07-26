/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

NTSTATUS
Volume::GetVolumeLabel(_Inout_ PWSTR   VolumeLabel,
                       _Inout_ PUSHORT Length)
{
    NTSTATUS Status;
    PFileRecord VolumeFile;
    PAttribute VolumeNameAttr;
    ULONG BytesRemaining, LabelLength;

    if (!VolumeLabel || !Length)
        return STATUS_INVALID_PARAMETER;

    if (!VolumeLabelCached)
    {
        Status = MFT->GetFileAttributeFromFileRecordNumber(TypeVolumeName,
                                                           NULL,
                                                           _Volume,
                                                           &VolumeFile,
                                                           &VolumeNameAttr);
        if (!NT_SUCCESS(Status))
            return Status;

        LabelLength = GetAttributeDataSize(VolumeNameAttr);
        if (LabelLength > sizeof(VolumeLabelCache))
        {
            delete VolumeFile;
            return STATUS_FILE_CORRUPT_ERROR;
        }

        BytesRemaining = LabelLength;
        Status = LabelLength == 0
                 ? STATUS_SUCCESS
                 : VolumeFile->CopyData(VolumeNameAttr,
                                        (PUCHAR)VolumeLabelCache,
                                        &BytesRemaining);
        delete VolumeFile;
        if (!NT_SUCCESS(Status) || BytesRemaining != 0)
            return NT_SUCCESS(Status) ? STATUS_END_OF_FILE : Status;

        VolumeLabelCacheLength = (USHORT)LabelLength;
        VolumeLabelCached = TRUE;
    }

    *Length = VolumeLabelCacheLength;
    RtlCopyMemory(VolumeLabel, VolumeLabelCache, VolumeLabelCacheLength);
    return STATUS_SUCCESS;
}

NTSTATUS
Volume::SetVolumeLabel(_In_ PWSTR VolumeLabel,
                       _In_ ULONG Length)
{
    NTSTATUS Status;
    FileRecord* VolumeFile;
    PAttribute VolumeNameAttr;

    if ((!VolumeLabel && Length != 0) ||
        Length > sizeof(VolumeLabelCache))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (IsReadOnly)
        return STATUS_ACCESS_DENIED;

    /* Allocate memory for $Volume file record, retrieve the file record, and
     * get a pointer to the $VOLUME_NAME attribute.
     */
    Status = MFT->GetFileAttributeFromFileRecordNumber(TypeVolumeName,
                                                       NULL,
                                                       _Volume,
                                                       &VolumeFile,
                                                       &VolumeNameAttr);

    if (!NT_SUCCESS(Status))
        return Status;

    // Replace and repack the complete resident $VOLUME_NAME value.
    Status = VolumeFile->ReplaceResidentData(
        VolumeNameAttr,
        reinterpret_cast<const UCHAR*>(VolumeLabel),
        Length);

    if (NT_SUCCESS(Status))
    {
        // Write the volume file to disk.
        Status = MFT->WriteFileRecordToMFT(VolumeFile);
        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(VolumeLabelCache, VolumeLabel, Length);
            VolumeLabelCacheLength = (USHORT)Length;
            VolumeLabelCached = TRUE;
        }
    }

    delete VolumeFile;
    return Status;
}
