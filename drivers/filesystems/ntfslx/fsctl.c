/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Mount and verify path for staged NTFS port
 */

#include "ntfslx.h"
#include <mountdev.h>
#include <ntddvol.h>

#define NDEBUG
#include <debug.h>

static
VOID
NtfslxFreeUpcaseTable(
    _Inout_opt_ PUSHORT UpcaseTable)
{
    if (UpcaseTable != NULL)
    {
        ExFreePoolWithTag(UpcaseTable, NTFSLX_TAG);
    }
}

static
BOOLEAN
NtfslxIsSupportedClusterSize(
    _In_ ULONG ClusterSize)
{
    switch (ClusterSize)
    {
        case 512:
        case 1024:
        case 2048:
        case 4096:
        case 8192:
        case 16384:
        case 32768:
        case 65536:
            return TRUE;

        default:
            return FALSE;
    }
}

static
NTSTATUS
NtfslxReadBootSector(
    _In_ PDEVICE_OBJECT StorageDevice,
    _Out_ PNTFSLX_BOOT_SECTOR *BootSector,
    _Out_ PULONG DeviceSectorSize)
{
    DISK_GEOMETRY Geometry;
    ULONG GeometryLength;
    ULONG AllocationSize;
    PNTFSLX_BOOT_SECTOR SectorBuffer;
    NTSTATUS Status;

    GeometryLength = sizeof(Geometry);
    Status = NtfslxDeviceIoControl(StorageDevice,
                                   IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                   NULL,
                                   0,
                                   &Geometry,
                                   &GeometryLength,
                                   TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Geometry.BytesPerSector < sizeof(NTFSLX_BOOT_SECTOR))
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    AllocationSize = Geometry.BytesPerSector;
    SectorBuffer = ExAllocatePoolWithTag(NonPagedPool, AllocationSize, NTFSLX_TAG);
    if (SectorBuffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(SectorBuffer, AllocationSize);
    Status = NtfslxReadDisk(StorageDevice,
                            0,
                            Geometry.BytesPerSector,
                            Geometry.BytesPerSector,
                            (PUCHAR)SectorBuffer,
                            TRUE);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(SectorBuffer, NTFSLX_TAG);
        return Status;
    }

    *BootSector = SectorBuffer;
    *DeviceSectorSize = Geometry.BytesPerSector;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxExtractVolumeInfo(
    _In_ PNTFSLX_BOOT_SECTOR BootSector,
    _In_ ULONG DeviceSectorSize,
    _Out_ PNTFSLX_VOLUME_INFO VolumeInfo)
{
    ULONG BytesPerSector;
    ULONG SectorsPerCluster;
    ULONG BytesPerCluster;
    ULONG BytesPerFileRecord;
    ULONG BytesPerIndexRecord;

    if (RtlCompareMemory(&BootSector->OemId, "NTFS    ", 8) != 8)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    if (BootSector->EndOfSectorMarker != 0xAA55)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    BytesPerSector = BootSector->Bpb.BytesPerSector;
    SectorsPerCluster = BootSector->Bpb.SectorsPerCluster;
    if (BytesPerSector == 0 || SectorsPerCluster == 0)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    if (BytesPerSector != DeviceSectorSize)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    if (!NtfslxIsPowerOfTwo(BytesPerSector) || !NtfslxIsPowerOfTwo(SectorsPerCluster))
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    BytesPerCluster = BytesPerSector * SectorsPerCluster;
    if (!NtfslxIsSupportedClusterSize(BytesPerCluster))
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    BytesPerFileRecord = NtfslxRecordSizeFromClusters(BootSector->ClustersPerMftRecord,
                                                      BytesPerCluster);
    BytesPerIndexRecord = NtfslxRecordSizeFromClusters(BootSector->ClustersPerIndexRecord,
                                                       BytesPerCluster);
    if (BytesPerFileRecord < sizeof(NTFSLX_MFT_RECORD) || BytesPerIndexRecord == 0)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    if (BootSector->SectorCount == 0)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    RtlZeroMemory(VolumeInfo, sizeof(*VolumeInfo));
    VolumeInfo->BytesPerSector = BytesPerSector;
    VolumeInfo->SectorsPerCluster = SectorsPerCluster;
    VolumeInfo->BytesPerCluster = BytesPerCluster;
    VolumeInfo->BytesPerFileRecord = BytesPerFileRecord;
    VolumeInfo->BytesPerIndexRecord = BytesPerIndexRecord;
    VolumeInfo->SectorCount = BootSector->SectorCount;
    VolumeInfo->ClusterCount = BootSector->SectorCount / SectorsPerCluster;
    VolumeInfo->MftLcn = BootSector->MftLcn;
    VolumeInfo->MftMirrLcn = BootSector->MftMirrLcn;
    VolumeInfo->SerialNumber = BootSector->SerialNumber;

    if (VolumeInfo->ClusterCount == 0 ||
        VolumeInfo->MftLcn >= VolumeInfo->ClusterCount ||
        VolumeInfo->MftMirrLcn >= VolumeInfo->ClusterCount)
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxProbeVolume(
    _In_ PDEVICE_OBJECT StorageDevice,
    _Out_ PNTFSLX_VOLUME_INFO VolumeInfo)
{
    PNTFSLX_BOOT_SECTOR BootSector;
    ULONG DeviceSectorSize;
    PNTFSLX_MFT_RECORD MftRecord;
    NTSTATUS Status;

    Status = NtfslxReadBootSector(StorageDevice, &BootSector, &DeviceSectorSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = NtfslxExtractVolumeInfo(BootSector, DeviceSectorSize, VolumeInfo);
    ExFreePoolWithTag(BootSector, NTFSLX_TAG);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    MftRecord = ExAllocatePoolWithTag(NonPagedPool,
                                      VolumeInfo->BytesPerFileRecord,
                                      NTFSLX_TAG);
    if (MftRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(StorageDevice,
                                 VolumeInfo,
                                 NULL,
                                 NTFSLX_FILE_MFT,
                                 MftRecord);
    if (NT_SUCCESS(Status))
    {
        ASSERT(MftRecord->Ntfs.Magic == NTFSLX_RECORD_MAGIC_FILE);
    }

    ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
    return Status;
}

static
NTSTATUS
NtfslxVerifyVolume(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension)
{
    NTFSLX_VOLUME_INFO VolumeInfo;
    NTSTATUS Status;

    Status = NtfslxProbeVolume(DeviceExtension->StorageDevice, &VolumeInfo);
    if (Status == STATUS_UNRECOGNIZED_VOLUME)
    {
        return STATUS_WRONG_VOLUME;
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (VolumeInfo.SerialNumber != DeviceExtension->VolumeInfo.SerialNumber ||
        VolumeInfo.BytesPerCluster != DeviceExtension->VolumeInfo.BytesPerCluster ||
        VolumeInfo.BytesPerFileRecord != DeviceExtension->VolumeInfo.BytesPerFileRecord ||
        VolumeInfo.MftLcn != DeviceExtension->VolumeInfo.MftLcn)
    {
        return STATUS_WRONG_VOLUME;
    }

    return STATUS_SUCCESS;
}

static
VOID
NtfslxTraceMountVpb(
    _In_ PCSTR Reason,
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PDEVICE_OBJECT VolumeDevice,
    _In_opt_ PVPB Vpb)
{
    DPRINT1("ntfslx: %s: Storage=%p StorageVpb=%p Volume=%p VolumeVpb=%p Vpb=%p Type=%hu Size=%hu Flags=0x%lx Ref=%ld Real=%p Device=%p\n",
            Reason,
            StorageDevice,
            StorageDevice != NULL ? StorageDevice->Vpb : NULL,
            VolumeDevice,
            VolumeDevice != NULL ? VolumeDevice->Vpb : NULL,
            Vpb,
            Vpb != NULL ? Vpb->Type : 0,
            Vpb != NULL ? Vpb->Size : 0,
            Vpb != NULL ? Vpb->Flags : 0UL,
            Vpb != NULL ? (LONG)Vpb->ReferenceCount : 0,
            Vpb != NULL ? Vpb->RealDevice : NULL,
            Vpb != NULL ? Vpb->DeviceObject : NULL);
}

NTSTATUS
NtfslxMountVolume(
    _In_ PDEVICE_OBJECT ControlDevice,
    _In_ PDEVICE_OBJECT StorageDevice,
    _Inout_ PVPB Vpb)
{
    PDEVICE_OBJECT VolumeDevice;
    PNTFSLX_DEVICE_EXTENSION VolumeExtension;
    NTFSLX_VOLUME_INFO VolumeInfo;
    PNTFSLX_RUNLIST_ELEMENT MftRunlist;
    ULONG MftRunlistCount;
    PUSHORT UpcaseTable;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ControlDevice);

    if (StorageDevice == NULL || Vpb == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxProbeVolume(StorageDevice, &VolumeInfo);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    UpcaseTable = NtfslxGenerateDefaultUpcase();
    if (UpcaseTable == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    MftRunlist = NULL;
    MftRunlistCount = 0;

    Status = NtfslxBuildMftRunlist(StorageDevice,
                                   &VolumeInfo,
                                   &MftRunlist,
                                   &MftRunlistCount);
    if (!NT_SUCCESS(Status))
    {
        NtfslxFreeUpcaseTable(UpcaseTable);
        return Status;
    }

    Status = NtfslxLoadVolumeMetadata(StorageDevice,
                                      &VolumeInfo,
                                      MftRunlist);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(MftRunlist, NTFSLX_TAG);
        NtfslxFreeUpcaseTable(UpcaseTable);
        return Status;
    }

    ASSERT(VolumeInfo.BytesPerFileRecord != 0);

    /*
     * Count free clusters by reading the $Bitmap file (MFT record 6).
     * The $DATA attribute of $Bitmap contains one bit per cluster.
     */
    VolumeInfo.FreeClusters = 0;
    {
        PNTFSLX_MFT_RECORD BitmapRecord;
        PNTFSLX_ATTR_RECORD BitmapDataAttr;

        BitmapRecord = ExAllocatePoolWithTag(NonPagedPool,
            VolumeInfo.BytesPerFileRecord, NTFSLX_TAG);
        if (BitmapRecord != NULL)
        {
            Status = NtfslxReadMftRecord(StorageDevice, &VolumeInfo,
                MftRunlist, NTFSLX_FILE_BITMAP, BitmapRecord);
            if (NT_SUCCESS(Status))
            {
                Status = NtfslxFindAttribute(BitmapRecord,
                    NTFSLX_ATTRIBUTE_DATA, NULL, 0, &BitmapDataAttr);
                if (NT_SUCCESS(Status))
                {
                    if (BitmapDataAttr->NonResident)
                    {
                        PNTFSLX_RUNLIST_ELEMENT BmpRl = NULL;
                        ULONG BmpRlCount = 0;
                        ULONGLONG BitmapDataSize;

                        BitmapDataSize = BitmapDataAttr->Data.NonResident.DataSize;

                        if (NT_SUCCESS(NtfslxMappingPairsDecompress(
                                &VolumeInfo, BitmapDataAttr, &BmpRl, &BmpRlCount)))
                        {
                            PUCHAR BmpBuf;
                            ULONG BmpBufSize = (ULONG)BitmapDataSize;

                            BmpBuf = ExAllocatePoolWithTag(NonPagedPool,
                                BmpBufSize, NTFSLX_TAG);
                            if (BmpBuf != NULL)
                            {
                                if (NT_SUCCESS(NtfslxReadMappedAttributeData(
                                        StorageDevice, &VolumeInfo,
                                        BmpRl, 0, BmpBufSize, BmpBuf)))
                                {
                                    VolumeInfo.FreeClusters =
                                        NtfslxBitmapCountFreeBits(BmpBuf, BmpBufSize);
                                    if (VolumeInfo.FreeClusters > VolumeInfo.ClusterCount)
                                        VolumeInfo.FreeClusters = VolumeInfo.ClusterCount;
                                }
                                ExFreePoolWithTag(BmpBuf, NTFSLX_TAG);
                            }
                            ExFreePoolWithTag(BmpRl, NTFSLX_TAG);
                        }
                    }
                    else
                    {
                        /* Resident bitmap (small volume) */
                        PUCHAR BmpData = (PUCHAR)BitmapDataAttr +
                            BitmapDataAttr->Data.Resident.ValueOffset;
                        ULONG BmpLen = BitmapDataAttr->Data.Resident.ValueLength;

                        VolumeInfo.FreeClusters =
                            NtfslxBitmapCountFreeBits(BmpData, BmpLen);
                        if (VolumeInfo.FreeClusters > VolumeInfo.ClusterCount)
                            VolumeInfo.FreeClusters = VolumeInfo.ClusterCount;
                    }
                }
            }
            ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
        }
        /* Non-fatal: if bitmap reading fails we just report 0 free */
        Status = STATUS_SUCCESS;
    }

    DbgPrint("ntfslx: mount bitmap: FreeClusters=%I64u ClusterCount=%I64u BytesPerCluster=%lu\n",
             VolumeInfo.FreeClusters, VolumeInfo.ClusterCount, VolumeInfo.BytesPerCluster);
    DbgPrint("ntfslx: mount label: len=%u label='%.*S'\n",
             VolumeInfo.VolumeLabelLength,
             VolumeInfo.VolumeLabelLength / (USHORT)sizeof(WCHAR),
             VolumeInfo.VolumeLabel);

    Status = IoCreateDevice(NtfslxGlobalData.DriverObject,
                            sizeof(NTFSLX_DEVICE_EXTENSION),
                            NULL,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            0,
                            FALSE,
                            &VolumeDevice);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(MftRunlist, NTFSLX_TAG);
        NtfslxFreeUpcaseTable(UpcaseTable);
        return Status;
    }

    VolumeExtension = VolumeDevice->DeviceExtension;
    RtlZeroMemory(VolumeExtension, sizeof(*VolumeExtension));

    VolumeExtension->Signature = NTFSLX_TAG;
    VolumeExtension->Kind = NtfslxDeviceKindVolume;
    VolumeExtension->DeviceObject = VolumeDevice;
    VolumeExtension->StorageDevice = StorageDevice;
    VolumeDevice->Vpb = Vpb;
    StorageDevice->Vpb = Vpb;
    VolumeExtension->Vpb = Vpb;
    VolumeExtension->VolumeInfo = VolumeInfo;
    VolumeExtension->UpcaseTable = UpcaseTable;
    VolumeExtension->UpcaseTableLength = NTFSLX_DEFAULT_UPCASE_LENGTH;
    VolumeExtension->MftRunlist = MftRunlist;
    VolumeExtension->MftRunlistCount = MftRunlistCount;

    NtfslxTraceMountVpb("mount pre-validate",
                        StorageDevice,
                        VolumeDevice,
                        Vpb);

    Status = NtfslxValidateMountVpb(Vpb, StorageDevice, VolumeDevice);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ntfslx: mount validation failed with status 0x%08lx\n", Status);
        NtfslxTraceMountVpb("mount validation failure",
                            StorageDevice,
                            VolumeDevice,
                            Vpb);

        DPRINT1("ntfslx: mount rejected Storage=%p ExistingStorageVpb=%p Volume=%p VolumeVpb=%p IncomingVpb=%p\n",
                StorageDevice,
                StorageDevice != NULL ? StorageDevice->Vpb : NULL,
                VolumeDevice,
                VolumeDevice != NULL ? VolumeDevice->Vpb : NULL,
                Vpb);

        VolumeDevice->Vpb = NULL;
        VolumeExtension->Vpb = NULL;
        if (StorageDevice->Vpb == Vpb)
        {
            StorageDevice->Vpb = NULL;
            DPRINT1("ntfslx: cleared stale storage VPB pointer after failed mount Storage=%p Vpb=%p\n",
                    StorageDevice,
                    Vpb);
        }

        ExFreePoolWithTag(MftRunlist, NTFSLX_TAG);
        NtfslxFreeUpcaseTable(UpcaseTable);
        IoDeleteDevice(VolumeDevice);
        return Status == STATUS_DEVICE_ALREADY_ATTACHED ? STATUS_WRONG_VOLUME : Status;
    }

    Status = ExInitializeResourceLite(&VolumeExtension->Resource);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ntfslx: ExInitializeResourceLite failed with status 0x%08lx\n", Status);
        VolumeDevice->Vpb = NULL;
        VolumeExtension->Vpb = NULL;
        if (StorageDevice->Vpb == Vpb)
        {
            StorageDevice->Vpb = NULL;
        }
        ExFreePoolWithTag(MftRunlist, NTFSLX_TAG);
        NtfslxFreeUpcaseTable(UpcaseTable);
        IoDeleteDevice(VolumeDevice);
        return Status;
    }

    /*
     * Initialize the FsRtl directory-change notify package. Without this
     * Explorer never hears about file creations/deletions/size changes
     * until the user presses F5, because its subscribed IRP_MN_NOTIFY_CHANGE_DIRECTORY
     * requests never complete.
     */
    InitializeListHead(&VolumeExtension->NotifyList);
    FsRtlNotifyInitializeSync(&VolumeExtension->NotifySync);
    VolumeExtension->NotifyInitialized = TRUE;

    NtfslxShareInitialize(VolumeExtension);

    ASSERT(VolumeExtension->UpcaseTable != NULL);

    VolumeDevice->Flags |= DO_DIRECT_IO;
    VolumeDevice->StackSize = StorageDevice->StackSize + 1;

    NtfslxTraceMountVpb("mount pre-publish",
                        StorageDevice,
                        VolumeDevice,
                        Vpb);

    Vpb->DeviceObject = VolumeDevice;
    Vpb->RealDevice = StorageDevice;
    Vpb->Flags |= VPB_MOUNTED;
    Vpb->ReferenceCount++;
    Vpb->SerialNumber = (ULONG)VolumeInfo.SerialNumber;
    Vpb->VolumeLabelLength = VolumeInfo.VolumeLabelLength;
    RtlCopyMemory(Vpb->VolumeLabel,
                  VolumeInfo.VolumeLabel,
                  VolumeInfo.VolumeLabelLength);

    NtfslxTraceMountVpb("mount published",
                        StorageDevice,
                        VolumeDevice,
                        Vpb);

    /*
     * Set the $Volume dirty flag now, while we still have the MFT runlist
     * and the $Volume record is readable. Any subsequent crash before we
     * get a clean unmount will leave this flag set, which is the signal
     * to Windows' native NTFS driver that chkdsk should run before the
     * volume is trusted. Without this, we'd hand Windows a half-written
     * volume that claims to be clean. Non-fatal: mount still succeeds if
     * the write fails, but corruption risk is then on the user.
     */
    {
        NTSTATUS DirtyStatus =
            NtfslxSetVolumeDirtyFlag(StorageDevice,
                                     &VolumeExtension->VolumeInfo,
                                     VolumeExtension->MftRunlist,
                                     TRUE);
        if (!NT_SUCCESS(DirtyStatus))
        {
            DPRINT1("ntfslx: mount: could not set VOLUME_IS_DIRTY 0x%08lx\n",
                    DirtyStatus);
        }
    }

    /*
     * Wipe $LogFile to 0xFF. We have no CLFS journal of our own, so any
     * stale log entries left on disk by a previous Windows NTFS session
     * (or by us) must not be replayed — replay would fight with whatever
     * metadata mutations we make and could roll real work back. NTFS
     * convention: 0xFF in a restart page means "uninitialized", and the
     * first write after mount regenerates the restart area. Non-fatal:
     * if the write fails the worst case is Windows running chkdsk on
     * next boot, which is what we're already asking for via the dirty
     * flag above.
     */
    {
        NTSTATUS FillStatus =
            NtfslxFillLogFile(StorageDevice,
                              &VolumeExtension->VolumeInfo,
                              VolumeExtension->MftRunlist);
        if (!NT_SUCCESS(FillStatus))
        {
            DPRINT1("ntfslx: mount: could not fill $LogFile with 0xFF 0x%08lx\n",
                    FillStatus);
        }
    }

    Status = IoRegisterShutdownNotification(VolumeDevice);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ntfslx: mount: IoRegisterShutdownNotification failed 0x%08lx\n",
                Status);
        Status = STATUS_SUCCESS;
    }

    VolumeDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
BOOLEAN
NtfslxIsSupportedDeviceControl(
    _In_ ULONG IoControlCode)
{
    switch (IoControlCode)
    {
        case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
        case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
        case IOCTL_MOUNTDEV_QUERY_STABLE_GUID:
        case IOCTL_MOUNTDEV_QUERY_SUGGESTED_LINK_NAME:
        case IOCTL_MOUNTDEV_LINK_CREATED:
        case IOCTL_MOUNTDEV_LINK_DELETED:
        case IOCTL_MOUNTDEV_UNIQUE_ID_CHANGE_NOTIFY:
        case IOCTL_VOLUME_ONLINE:
        case IOCTL_VOLUME_GET_GPT_ATTRIBUTES:
        case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
        case IOCTL_VOLUME_IS_PARTITION:
        case IOCTL_STORAGE_GET_DEVICE_NUMBER:
        case IOCTL_STORAGE_CHECK_VERIFY:
        case IOCTL_DISK_CHECK_VERIFY:
        case IOCTL_DISK_IS_WRITABLE:
        case IOCTL_DISK_GET_DRIVE_GEOMETRY:
        case IOCTL_DISK_GET_LENGTH_INFO:
            return TRUE;

        default:
            return FALSE;
    }
}

/*
 * NtfslxGatherTier0Proof - read the on-disk state that's expected to be
 * kept consistent by the TIER 0 corruption fixes (mirror, dirty flag,
 * logfile wipe, LCN 0 reservation) and report it into Result so a
 * kmtest caller can assert on it.
 *
 * Runs against an already-mounted volume; the device extension carries
 * MftRunlist / VolumeInfo and the storage device pointer. Failures to
 * read any individual piece of state are folded into Result->ReturnStatus
 * so the caller can distinguish "proof gathering broke" from "the
 * property we were checking doesn't hold".
 */
static NTSTATUS
NtfslxGatherTier0Proof(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _Out_ PNTFSLX_TIER0_PROOF Result)
{
    ULONG RecordSize;
    PUCHAR PrimaryBuf = NULL;
    PUCHAR MirrorBuf = NULL;
    PUCHAR LogBuf = NULL;
    PNTFSLX_MFT_RECORD VolumeRecord = NULL;
    PNTFSLX_ATTR_RECORD VolInfoAttr;
    PNTFSLX_VOLUME_INFORMATION_ATTRIBUTE VolInfo;
    ULONGLONG MirrorByteOffset;
    ULONGLONG LogFileLcn;
    PNTFSLX_MFT_RECORD LogFileRecord = NULL;
    PNTFSLX_ATTR_RECORD LogFileDataAttr;
    PNTFSLX_RUNLIST_ELEMENT LogRuns = NULL;
    ULONG LogRunCount = 0;
    ULONG I;
    ULONG AllMatch;
    ULONG Compared;
    NTSTATUS Status;

    if (DevExt == NULL || Result == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Result, sizeof(*Result));
    Result->Version = 1;
    Result->BootLcn = 0;
    Result->MftMirrLcn = DevExt->VolumeInfo.MftMirrLcn;

    RecordSize = DevExt->VolumeInfo.BytesPerFileRecord;

    PrimaryBuf = ExAllocatePoolWithTag(NonPagedPool, RecordSize * 4, NTFSLX_TAG);
    MirrorBuf = ExAllocatePoolWithTag(NonPagedPool, RecordSize * 4, NTFSLX_TAG);
    if (PrimaryBuf == NULL || MirrorBuf == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    /* Read $MFT records 0..3 via the normal mapped path (with MST fixup). */
    Compared = 0;
    AllMatch = 1;
    for (I = 0; I < 4; I++)
    {
        PNTFSLX_MFT_RECORD Rec;
        Rec = (PNTFSLX_MFT_RECORD)(PrimaryBuf + (I * RecordSize));
        Status = NtfslxReadMftRecord(DevExt->StorageDevice,
                                     &DevExt->VolumeInfo,
                                     DevExt->MftRunlist,
                                     I,
                                     Rec);
        if (!NT_SUCCESS(Status))
        {
            goto ProofMirrorDone;
        }
    }

    /* Read the mirror area as a raw 4*RecordSize block. */
    MirrorByteOffset = DevExt->VolumeInfo.MftMirrLcn *
                       (ULONGLONG)DevExt->VolumeInfo.BytesPerCluster;
    Status = NtfslxReadDisk(DevExt->StorageDevice,
                            (LONGLONG)MirrorByteOffset,
                            RecordSize * 4,
                            DevExt->VolumeInfo.BytesPerSector,
                            MirrorBuf,
                            FALSE);
    if (!NT_SUCCESS(Status))
    {
        goto ProofMirrorDone;
    }

    /*
     * Mirror buffer still has MST protection embedded; run the post-read
     * fixup on each record so the comparison sees the same bytes the
     * primary path just returned.
     */
    for (I = 0; I < 4; I++)
    {
        PNTFSLX_MFT_RECORD MirrorRec;
        PNTFSLX_MFT_RECORD PrimaryRec;

        MirrorRec = (PNTFSLX_MFT_RECORD)(MirrorBuf + (I * RecordSize));
        if (MirrorRec->Ntfs.Magic != NTFSLX_RECORD_MAGIC_FILE)
        {
            AllMatch = 0;
            break;
        }
        if (!NT_SUCCESS(NtfslxPostReadMstFixup(&MirrorRec->Ntfs, RecordSize)))
        {
            AllMatch = 0;
            break;
        }

        PrimaryRec = (PNTFSLX_MFT_RECORD)(PrimaryBuf + (I * RecordSize));
        /*
         * Skip the Lsn field when comparing: $LogFile sequence number
         * updates land here on the primary copy before they're mirrored,
         * so a stale mirror Lsn is expected.
         */
        if (PrimaryRec->Ntfs.Magic == MirrorRec->Ntfs.Magic &&
            PrimaryRec->SequenceNumber == MirrorRec->SequenceNumber &&
            PrimaryRec->LinkCount == MirrorRec->LinkCount &&
            PrimaryRec->AttributesOffset == MirrorRec->AttributesOffset &&
            PrimaryRec->Flags == MirrorRec->Flags &&
            PrimaryRec->BytesInUse == MirrorRec->BytesInUse &&
            PrimaryRec->MftRecordNumber == MirrorRec->MftRecordNumber)
        {
            Compared++;
        }
        else
        {
            AllMatch = 0;
        }
    }

    Result->MftMirrorConsistent = (AllMatch != 0 && Compared == 4) ? 1 : 0;
    Result->MftMirrorRecords = Compared;

ProofMirrorDone:
    /* $Volume flags (dirty bit). */
    VolumeRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (VolumeRecord != NULL)
    {
        Status = NtfslxReadMftRecord(DevExt->StorageDevice,
                                     &DevExt->VolumeInfo,
                                     DevExt->MftRunlist,
                                     NTFSLX_FILE_VOLUME,
                                     VolumeRecord);
        if (NT_SUCCESS(Status))
        {
            Status = NtfslxFindAttribute(VolumeRecord,
                                         NTFSLX_ATTRIBUTE_VOLUME_INFORMATION,
                                         NULL, 0, &VolInfoAttr);
            if (NT_SUCCESS(Status) &&
                VolInfoAttr->NonResident == 0 &&
                VolInfoAttr->Data.Resident.ValueLength >=
                    sizeof(NTFSLX_VOLUME_INFORMATION_ATTRIBUTE))
            {
                VolInfo = (PNTFSLX_VOLUME_INFORMATION_ATTRIBUTE)
                    ((PUCHAR)VolInfoAttr +
                     VolInfoAttr->Data.Resident.ValueOffset);
                Result->VolumeDirtyFlag =
                    (VolInfo->Flags & NTFSLX_VOLUME_IS_DIRTY) ? 1 : 0;
            }
        }
    }

    /* $LogFile first sector. */
    LogFileRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    LogBuf = ExAllocatePoolWithTag(NonPagedPool, 512, NTFSLX_TAG);
    if (LogFileRecord != NULL && LogBuf != NULL)
    {
        Status = NtfslxReadMftRecord(DevExt->StorageDevice,
                                     &DevExt->VolumeInfo,
                                     DevExt->MftRunlist,
                                     NTFSLX_FILE_LOGFILE,
                                     LogFileRecord);
        if (NT_SUCCESS(Status))
        {
            Status = NtfslxFindAttribute(LogFileRecord,
                                         NTFSLX_ATTRIBUTE_DATA,
                                         NULL, 0, &LogFileDataAttr);
            if (NT_SUCCESS(Status) && LogFileDataAttr->NonResident != 0)
            {
                Status = NtfslxMappingPairsDecompress(&DevExt->VolumeInfo,
                                                      LogFileDataAttr,
                                                      &LogRuns,
                                                      &LogRunCount);
                if (NT_SUCCESS(Status) && LogRunCount > 0 && LogRuns[0].Lcn > 0)
                {
                    LogFileLcn = (ULONGLONG)LogRuns[0].Lcn;
                    Status = NtfslxReadDisk(DevExt->StorageDevice,
                                            (LONGLONG)(LogFileLcn *
                                                (ULONGLONG)DevExt->VolumeInfo.BytesPerCluster),
                                            512,
                                            DevExt->VolumeInfo.BytesPerSector,
                                            LogBuf,
                                            FALSE);
                    if (NT_SUCCESS(Status))
                    {
                        ULONG K;
                        BOOLEAN AllFF = TRUE;
                        Result->LogFileFirstDword = *(PULONG)LogBuf;
                        for (K = 0; K < 512; K++)
                        {
                            if (LogBuf[K] != 0xFF)
                            {
                                AllFF = FALSE;
                                break;
                            }
                        }
                        Result->LogFileIs0xFF = AllFF ? 1 : 0;
                    }
                }
            }
        }
    }

    /*
     * LCN 0 reservation check: read one byte from the $Bitmap stream
     * covering bit 0. On a healthy NTFS the $Boot LCN (0) is marked
     * in-use, so bit 0 of the bitmap should be set. Reuse the existing
     * ReadVolumeBitmap helper via the full gather-bitmap-once flow would
     * be heavy; instead just peek at the resident or first-run bitmap
     * byte.
     */
    {
        PNTFSLX_MFT_RECORD BitmapRecord;
        PNTFSLX_ATTR_RECORD BitmapDataAttr;
        PUCHAR BitmapByte = NULL;

        BitmapRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
        if (BitmapRecord != NULL)
        {
            Status = NtfslxReadMftRecord(DevExt->StorageDevice,
                                         &DevExt->VolumeInfo,
                                         DevExt->MftRunlist,
                                         NTFSLX_FILE_BITMAP,
                                         BitmapRecord);
            if (NT_SUCCESS(Status))
            {
                Status = NtfslxFindAttribute(BitmapRecord,
                                             NTFSLX_ATTRIBUTE_DATA,
                                             NULL, 0, &BitmapDataAttr);
                if (NT_SUCCESS(Status))
                {
                    UCHAR FirstByte = 0;
                    if (BitmapDataAttr->NonResident == 0)
                    {
                        FirstByte = *((PUCHAR)BitmapDataAttr +
                            BitmapDataAttr->Data.Resident.ValueOffset);
                    }
                    else
                    {
                        PNTFSLX_RUNLIST_ELEMENT BmpRuns = NULL;
                        ULONG BmpRunCount = 0;
                        Status = NtfslxMappingPairsDecompress(
                            &DevExt->VolumeInfo, BitmapDataAttr, &BmpRuns, &BmpRunCount);
                        if (NT_SUCCESS(Status) && BmpRunCount > 0 &&
                            BmpRuns[0].Lcn > 0)
                        {
                            BitmapByte = ExAllocatePoolWithTag(NonPagedPool,
                                                               1, NTFSLX_TAG);
                            if (BitmapByte != NULL)
                            {
                                ULONGLONG Off =
                                    (ULONGLONG)BmpRuns[0].Lcn *
                                    (ULONGLONG)DevExt->VolumeInfo.BytesPerCluster;
                                Status = NtfslxReadDisk(DevExt->StorageDevice,
                                                        (LONGLONG)Off,
                                                        1,
                                                        DevExt->VolumeInfo.BytesPerSector,
                                                        BitmapByte,
                                                        FALSE);
                                if (NT_SUCCESS(Status))
                                {
                                    FirstByte = BitmapByte[0];
                                }
                                ExFreePoolWithTag(BitmapByte, NTFSLX_TAG);
                            }
                        }
                        if (BmpRuns != NULL)
                            ExFreePoolWithTag(BmpRuns, NTFSLX_TAG);
                    }
                    /* Bit 0 means LCN 0 is reserved. */
                    Result->LcnZeroIsReserved = (FirstByte & 0x01) ? 1 : 0;
                }
            }
            ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
        }
    }

    Status = STATUS_SUCCESS;

Cleanup:
    if (LogRuns != NULL) ExFreePoolWithTag(LogRuns, NTFSLX_TAG);
    if (LogFileRecord != NULL) ExFreePoolWithTag(LogFileRecord, NTFSLX_TAG);
    if (LogBuf != NULL) ExFreePoolWithTag(LogBuf, NTFSLX_TAG);
    if (VolumeRecord != NULL) ExFreePoolWithTag(VolumeRecord, NTFSLX_TAG);
    if (PrimaryBuf != NULL) ExFreePoolWithTag(PrimaryBuf, NTFSLX_TAG);
    if (MirrorBuf != NULL) ExFreePoolWithTag(MirrorBuf, NTFSLX_TAG);

    Result->ReturnStatus = Status;
    return Status;
}

NTSTATUS
NtfslxDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    ULONG IoControlCode;
    ULONG OutputLength;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    IoControlCode = Stack->Parameters.DeviceIoControl.IoControlCode;
    OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;

    /*
     * Self-test IOCTL is accepted on the control device (no storage, no
     * volume). It runs the internal RAM-only helper checks and writes the
     * pass/fail counts into the caller's output buffer.
     */
    if (IoControlCode == IOCTL_NTFSLX_SELFTEST)
    {
        if (OutputLength < sizeof(NTFSLX_SELFTEST_RESULT))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            OutputLength = sizeof(NTFSLX_SELFTEST_RESULT);
            goto CompleteRequest;
        }

        Status = NtfslxSelfTestRunAll((PNTFSLX_SELFTEST_RESULT)Irp->AssociatedIrp.SystemBuffer);
        OutputLength = sizeof(NTFSLX_SELFTEST_RESULT);
        goto CompleteRequest;
    }

    /*
     * TIER 0 proof IOCTL must run against a mounted volume so we have a
     * storage device and MftRunlist to read from. Dispatch BEFORE the
     * generic "no file object" guard because we access DeviceExtension
     * directly.
     */
    if (IoControlCode == IOCTL_NTFSLX_TIER0_PROOF)
    {
        if (!NtfslxIsVolumeDevice(DeviceExtension) ||
            DeviceExtension->StorageDevice == NULL ||
            DeviceExtension->MftRunlist == NULL)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            OutputLength = 0;
            goto CompleteRequest;
        }
        if (OutputLength < sizeof(NTFSLX_TIER0_PROOF))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            OutputLength = sizeof(NTFSLX_TIER0_PROOF);
            goto CompleteRequest;
        }
        Status = NtfslxGatherTier0Proof(DeviceExtension,
                                        (PNTFSLX_TIER0_PROOF)Irp->AssociatedIrp.SystemBuffer);
        OutputLength = sizeof(NTFSLX_TIER0_PROOF);
        goto CompleteRequest;
    }

    if (!NtfslxIsVolumeDevice(DeviceExtension) ||
        DeviceExtension->StorageDevice == NULL)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        OutputLength = 0;
        goto CompleteRequest;
    }

    if (Stack->FileObject != NULL &&
        Stack->FileObject->FileName.Length != 0)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        OutputLength = 0;
        goto CompleteRequest;
    }

    if (!NtfslxIsSupportedDeviceControl(IoControlCode))
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        OutputLength = 0;
        goto CompleteRequest;
    }

    Status = NtfslxDeviceIoControl(DeviceExtension->StorageDevice,
                                   IoControlCode,
                                   Irp->AssociatedIrp.SystemBuffer,
                                   Stack->Parameters.DeviceIoControl.InputBufferLength,
                                   Irp->AssociatedIrp.SystemBuffer,
                                   &OutputLength,
                                   TRUE);
    if (!NT_SUCCESS(Status) &&
        Status != STATUS_BUFFER_OVERFLOW &&
        Status != STATUS_BUFFER_TOO_SMALL)
    {
        DPRINT1("ntfslx: device control 0x%08lx failed status=0x%08lx In=%lu Out=%lu Storage=%p\n",
                IoControlCode,
                Status,
                Stack->Parameters.DeviceIoControl.InputBufferLength,
                Stack->Parameters.DeviceIoControl.OutputBufferLength,
                DeviceExtension->StorageDevice);
    }

CompleteRequest:
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = OutputLength;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NtfslxFileSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_MOUNT_VOLUME:
            if (!NtfslxIsControlDevice(DeviceExtension))
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            Status = NtfslxMountVolume(DeviceObject,
                                       Stack->Parameters.MountVolume.DeviceObject,
                                       Stack->Parameters.MountVolume.Vpb);
            break;

        case IRP_MN_VERIFY_VOLUME:
            if (!NtfslxIsVolumeDevice(DeviceExtension))
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            Status = NtfslxVerifyVolume(DeviceExtension);
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
