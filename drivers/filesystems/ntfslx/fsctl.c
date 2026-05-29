/*
 * PROJECT:     ReactOS ntfslx driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Mount, verify, and FSCTL handling
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
    NTFSDBG("ntfslx: %s: Storage=%p StorageVpb=%p Volume=%p VolumeVpb=%p Vpb=%p Type=%hu Size=%hu Flags=0x%lx Ref=%ld Real=%p Device=%p\n",
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

    NTFSDBG("ntfslx: mount bitmap: FreeClusters=%I64u ClusterCount=%I64u BytesPerCluster=%lu\n",
             VolumeInfo.FreeClusters, VolumeInfo.ClusterCount, VolumeInfo.BytesPerCluster);
    NTFSDBG("ntfslx: mount label: len=%u label='%.*S'\n",
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
    /*
     * Allocator locks: KGUARDED_MUTEX, narrow scope. The mutex is held
     * only for the duration of the in-memory bitmap scan + reserve;
     * disk writes happen OUTSIDE the lock. See clusteralloc.c and
     * mftwrite.c for the canonical sequence. KGUARDED_MUTEX (not
     * FAST_MUTEX) was chosen because the prior code held FAST_MUTEX
     * across IoBuildSynchronousFsdRequest, which APC-deadlocks under
     * a held FAST_MUTEX. KGUARDED_MUTEX has the same APC-blocking
     * behaviour but with a distinct primitive name to make the
     * narrow-scope contract explicit at every call site.
     */
    KeInitializeGuardedMutex(&VolumeExtension->MftAllocLock);
    VolumeExtension->MftAllocLockInitialized = TRUE;
    KeInitializeGuardedMutex(&VolumeExtension->ClusterAllocLock);
    VolumeExtension->ClusterAllocLockInitialized = TRUE;

    NtfslxTraceMountVpb("mount pre-validate",
                        StorageDevice,
                        VolumeDevice,
                        Vpb);

    Status = NtfslxValidateMountVpb(Vpb, StorageDevice, VolumeDevice);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: mount validation failed with status 0x%08lx\n", Status);
        NtfslxTraceMountVpb("mount validation failure",
                            StorageDevice,
                            VolumeDevice,
                            Vpb);

        NTFSDBG("ntfslx: mount rejected Storage=%p ExistingStorageVpb=%p Volume=%p VolumeVpb=%p IncomingVpb=%p\n",
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
            NTFSDBG("ntfslx: cleared stale storage VPB pointer after failed mount Storage=%p Vpb=%p\n",
                    StorageDevice,
                    Vpb);
        }

        ExFreePoolWithTag(MftRunlist, NTFSLX_TAG);
        NtfslxFreeUpcaseTable(UpcaseTable);
        IoDeleteDevice(VolumeDevice);
        return Status == STATUS_DEVICE_ALREADY_ATTACHED ? STATUS_WRONG_VOLUME : Status;
    }

    /*
     * VolumeResource - top of the lock-order. See fcb.c for canonical
     * lock-acquisition order. Initialized AFTER the validate step so a
     * failed mount tears down without leaving an initialized resource
     * orphaned in the device extension.
     */
    Status = ExInitializeResourceLite(&VolumeExtension->VolumeResource);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: ExInitializeResourceLite failed with status 0x%08lx\n", Status);
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
    VolumeExtension->VolumeResourceInitialized = TRUE;

    /*
     * Obsolete-MFT-runlist parking list. Each NtfslxExtendMftRunlist
     * pushes the previous runlist here instead of freeing it inline; the
     * dismount path drains the list. See ntfslx.h for the rationale and
     * mftwrite.c:NtfslxExtendMftRunlist for the publisher.
     */
    KeInitializeSpinLock(&VolumeExtension->ObsoleteMftRunlistsLock);
    InitializeListHead(&VolumeExtension->ObsoleteMftRunlists);
    VolumeExtension->ObsoleteMftRunlistsInitialized = TRUE;

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
            NTFSDBG("ntfslx: mount: could not set VOLUME_IS_DIRTY 0x%08lx\n",
                    DirtyStatus);
        }
    }

    /*
     * $LogFile handling: only wipe to 0xFF if there is no structurally
     * valid restart page on disk. Wiping unconditionally would destroy
     * any in-flight journal state that we (or, eventually, full LFS
     * recovery) might want to replay. The dirty flag set above is what
     * keeps Windows from trusting a partially-mutated volume; the log
     * itself can stay intact across mounts.
     *
     * NtfslxCheckLogFile returns STATUS_SUCCESS when at least one
     * restart page validates (regardless of clean / dirty); on success
     * we leave $LogFile alone. If neither page validates the log is
     * fresh or corrupted and we fall back to the historical 0xFF wipe.
     * Non-fatal in either branch: failure here means the next Windows
     * boot will run chkdsk, which is already what the dirty flag asks
     * for.
     */
    {
        BOOLEAN IsClean = FALSE;
        BOOLEAN LogWasWiped = FALSE;
        NTSTATUS CheckStatus =
            NtfslxCheckLogFile(StorageDevice,
                               &VolumeExtension->VolumeInfo,
                               VolumeExtension->MftRunlist,
                               &IsClean);
        if (NT_SUCCESS(CheckStatus))
        {
            NTFSDBG("ntfslx: mount: $LogFile has valid restart page (clean=%u)"
                    " — preserving on-disk journal state\n",
                    IsClean ? 1 : 0);
        }
        else
        {
            NTSTATUS FillStatus =
                NtfslxFillLogFile(StorageDevice,
                                  &VolumeExtension->VolumeInfo,
                                  VolumeExtension->MftRunlist);
            NTFSDBG("ntfslx: mount: $LogFile has no valid restart page"
                    " (check=0x%08lx) — wiping to 0xFF (fill=0x%08lx)\n",
                    CheckStatus,
                    FillStatus);
            if (NT_SUCCESS(FillStatus))
                LogWasWiped = TRUE;
        }

        /*
         * Bring up the Phase-1 journal subsystem. On a freshly-wiped
         * log we stamp the synthetic 'XLOG' restart page and start
         * appending records; on a preserved Microsoft RSTR we leave
         * the log alone and disable Phase-1 journaling for this
         * mount. JournalInitialize is non-fatal: any failure leaves
         * JournalEnabled = FALSE and the rest of the driver runs
         * exactly as it did before T1.3 landed.
         */
        {
            NTSTATUS JournalStatus =
                NtfslxJournalInitialize(VolumeExtension, LogWasWiped);
            if (!NT_SUCCESS(JournalStatus))
            {
                NTFSDBG("ntfslx: mount: journal init failed 0x%08lx\n",
                        JournalStatus);
            }
        }

    }

    Status = IoRegisterShutdownNotification(VolumeDevice);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: mount: IoRegisterShutdownNotification failed 0x%08lx\n",
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

static
BOOLEAN
NtfslxIsReparseDeviceControl(
    _In_ ULONG IoControlCode)
{
    switch (IoControlCode)
    {
        case FSCTL_SET_REPARSE_POINT:
        case FSCTL_GET_REPARSE_POINT:
        case FSCTL_DELETE_REPARSE_POINT:
            return TRUE;

        default:
            return FALSE;
    }
}

static
NTSTATUS
NtfslxHandleReparseDeviceControl(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID Buffer,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG ReturnLength)
{
    if (ReturnLength != NULL)
    {
        *ReturnLength = 0;
    }

    switch (IoControlCode)
    {
        case FSCTL_SET_REPARSE_POINT:
            return NtfslxSetReparsePoint(FileObject, Buffer, InputLength);

        case FSCTL_GET_REPARSE_POINT:
            return NtfslxGetReparsePoint(FileObject,
                                        Buffer,
                                        OutputLength,
                                        ReturnLength);

        case FSCTL_DELETE_REPARSE_POINT:
            return NtfslxDeleteReparsePoint(FileObject, Buffer, InputLength);

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

static
NTSTATUS
NtfslxReportVolumeDirty(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp,
    _In_ ULONG OutputBufferLength)
{
    PULONG VolumeState;

    if (Irp->AssociatedIrp.SystemBuffer != NULL)
    {
        VolumeState = (PULONG)Irp->AssociatedIrp.SystemBuffer;
    }
    else if (Irp->MdlAddress != NULL)
    {
        VolumeState = (PULONG)MmGetSystemAddressForMdlSafe(Irp->MdlAddress,
                                                           NormalPagePriority);
    }
    else
    {
        return STATUS_INVALID_USER_BUFFER;
    }

    if (VolumeState == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (OutputBufferLength < sizeof(ULONG))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    *VolumeState = 0;
    if (DeviceExtension->VolumeInfo.Flags & NTFSLX_VOLUME_IS_DIRTY)
    {
        *VolumeState |= VOLUME_IS_DIRTY;
    }

    Irp->IoStatus.Information = sizeof(ULONG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxUserFsRequest(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;

    if (!NtfslxIsVolumeDevice(DeviceExtension) || Stack->FileObject == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (NtfslxIsReparseDeviceControl(Stack->Parameters.FileSystemControl.FsControlCode))
    {
        NTSTATUS Status;
        ULONG ReparseReturnLength;

        ReparseReturnLength = 0;
        Status = NtfslxHandleReparseDeviceControl(Stack->FileObject,
                                                  Stack->Parameters.FileSystemControl.FsControlCode,
                                                  Irp->AssociatedIrp.SystemBuffer,
                                                  Stack->Parameters.FileSystemControl.InputBufferLength,
                                                  Stack->Parameters.FileSystemControl.OutputBufferLength,
                                                  &ReparseReturnLength);
        Irp->IoStatus.Information = ReparseReturnLength;
        return Status;
    }

    switch (Stack->Parameters.FileSystemControl.FsControlCode)
    {
        case FSCTL_IS_VOLUME_DIRTY:
        {
            NTSTATUS Status;
            ULONG DirtyFlags = 0;

            Status = NtfslxReportVolumeDirty(DeviceExtension,
                                             Irp,
                                             Stack->Parameters.FileSystemControl.OutputBufferLength);
            if (NT_SUCCESS(Status) && Irp->AssociatedIrp.SystemBuffer != NULL)
            {
                DirtyFlags = *(PULONG)Irp->AssociatedIrp.SystemBuffer;
            }

            NTFSDBG("ntfslx: UserFsRequest FSCTL_IS_VOLUME_DIRTY flags=0x%08lx volumeFlags=0x%04x\n",
                     DirtyFlags,
                     DeviceExtension->VolumeInfo.Flags);
            return Status;
        }

        case FSCTL_LOCK_VOLUME:
        {
            /*
             * Capture the caller's FileObject as the lock owner. While
             * the owner is set, IRP_MJ_CREATE for the volume device
             * rejects any non-owner open with STATUS_ACCESS_DENIED.
             * Re-locking by the same handle is idempotent. A different
             * handle issuing LOCK_VOLUME while another holds the lock
             * fails with STATUS_ACCESS_DENIED, matching Windows.
             */
            PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
            PFILE_OBJECT Caller = Stack ? Stack->FileObject : NULL;
            NTSTATUS LockStatus;

            if (Caller == NULL)
                return STATUS_INVALID_PARAMETER;

            ExAcquireResourceExclusiveLite(&DeviceExtension->VolumeResource, TRUE);
            if (DeviceExtension->DismountPending)
            {
                LockStatus = STATUS_VOLUME_DISMOUNTED;
            }
            else if (DeviceExtension->VolumeLockOwner == NULL ||
                     DeviceExtension->VolumeLockOwner == Caller)
            {
                DeviceExtension->VolumeLockOwner = Caller;
                LockStatus = STATUS_SUCCESS;
            }
            else
            {
                LockStatus = STATUS_ACCESS_DENIED;
            }
            ExReleaseResourceLite(&DeviceExtension->VolumeResource);
            return LockStatus;
        }

        case FSCTL_UNLOCK_VOLUME:
        {
            /*
             * Only the lock-owning FileObject may unlock. Returning
             * STATUS_NOT_LOCKED matches Windows for any other case.
             */
            PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
            PFILE_OBJECT Caller = Stack ? Stack->FileObject : NULL;
            NTSTATUS UnlockStatus;

            ExAcquireResourceExclusiveLite(&DeviceExtension->VolumeResource, TRUE);
            if (DeviceExtension->VolumeLockOwner != NULL &&
                DeviceExtension->VolumeLockOwner == Caller)
            {
                DeviceExtension->VolumeLockOwner = NULL;
                UnlockStatus = STATUS_SUCCESS;
            }
            else
            {
                UnlockStatus = STATUS_NOT_LOCKED;
            }
            ExReleaseResourceLite(&DeviceExtension->VolumeResource);
            return UnlockStatus;
        }

        case FSCTL_MARK_VOLUME_DIRTY:
        {
            /*
             * Explicit "mark this volume dirty". We already set the
             * flag on every mount, so this is a re-arm; succeed
             * idempotently.
             */
            NTSTATUS Status =
                NtfslxSetVolumeDirtyFlag(DeviceExtension->StorageDevice,
                                          &DeviceExtension->VolumeInfo,
                                          DeviceExtension->MftRunlist,
                                          TRUE);
            if (!NT_SUCCESS(Status))
            {
                NTFSDBG("ntfslx: mark-dirty: set dirty flag failed 0x%08lx\n",
                         Status);
            }
            return Status;
        }

        case FSCTL_DISMOUNT_VOLUME:
        {
            NTSTATUS Status;

            /*
             * Explicit dismount path. The shutdown handler covers the
             * "the OS is going down" case; FSCTL_DISMOUNT_VOLUME covers
             * "an admin asked to take this volume offline". In both
             * cases we have to clear VOLUME_IS_DIRTY before letting go
             * of the volume — otherwise next mount will see a dirty
             * volume even though we left it consistent. Because this
             * driver is write-through there is no FCB cache to flush.
             */
            Status = NtfslxSetVolumeDirtyFlag(DeviceExtension->StorageDevice,
                                              &DeviceExtension->VolumeInfo,
                                              DeviceExtension->MftRunlist,
                                              FALSE);
            if (!NT_SUCCESS(Status))
            {
                NTFSDBG("ntfslx: dismount: clear dirty flag failed 0x%08lx\n",
                         Status);
                return Status;
            }
            NTFSDBG("ntfslx: dismount: volume marked clean flags=0x%04x\n",
                     DeviceExtension->VolumeInfo.Flags);

            /*
             * Reclaim parked MFT runlists from MFT-extend operations
             * (T0.4). Dismount happens after all opens drain, so no
             * lock-free reader can still dereference any parked pointer.
             */
            NtfslxDrainObsoleteMftRunlists(DeviceExtension);

            /* Free the cached $LogFile runlist Phase-1 journal held. */
            NtfslxJournalTeardown(DeviceExtension);

            /*
             * Mark the volume dismount-pending so any subsequent
             * IRP_MJ_CREATE rejects the open with STATUS_VOLUME_DISMOUNTED
             * instead of returning a fresh handle on what the caller
             * just took offline. The flag stays set until a fresh mount
             * clears it; we deliberately do NOT clear VPB_MOUNTED on the
             * VPB because that would cause the IO Manager to schedule a
             * fresh mount on the next access and bypass the
             * STATUS_VOLUME_DISMOUNTED return entirely. The handler also
             * drops any held VolumeLockOwner — semantically the dismount
             * supersedes the lock state.
             */
            ExAcquireResourceExclusiveLite(&DeviceExtension->VolumeResource, TRUE);
            DeviceExtension->DismountPending = TRUE;
            DeviceExtension->VolumeLockOwner = NULL;
            ExReleaseResourceLite(&DeviceExtension->VolumeResource);

            return STATUS_SUCCESS;
        }

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
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

/*
 * Regression test for the FAST_MUTEX/sync-IRP deadlock guarded by
 * diskwrite.c. The worker acquires DevExt->MftAllocLock (mirroring the
 * NtfslxCreateNewFile call site that hits the bug first), reads sector 0
 * into a buffer, then calls NtfslxWriteDisk to write the same bytes back.
 *
 * Pre-IRP-completion-routine NtfslxWriteDisk used IoBuildSynchronousFsdRequest,
 * which signals via a kernel APC into the originating thread. APCs are
 * disabled while a FAST_MUTEX is held (FAST_MUTEX raises to APC_LEVEL via
 * KeEnterCriticalRegion), so the wait would never wake. The current
 * implementation uses an async IRP + custom completion routine and is
 * correct under FAST_MUTEX. This worker proves both the lock acquire
 * and the write complete; if the deadlock returns it never reaches
 * KeSetEvent and the IOCTL caller times out.
 */
typedef struct _NTFSLX_TC1_WORK
{
    PNTFSLX_DEVICE_EXTENSION DevExt;
    KEVENT Done;
    NTSTATUS WriteStatus;
    ULONG ElapsedMs;
} NTFSLX_TC1_WORK, *PNTFSLX_TC1_WORK;

static VOID NTAPI
NtfslxTc1Worker(
    _In_ PVOID Context)
{
    PNTFSLX_TC1_WORK Work = (PNTFSLX_TC1_WORK)Context;
    PNTFSLX_DEVICE_EXTENSION DevExt = Work->DevExt;
    PUCHAR SectorBuf;
    ULONG SectorSize;
    LARGE_INTEGER Start;
    LARGE_INTEGER End;
    LARGE_INTEGER Frequency;
    NTSTATUS Status;

    Work->WriteStatus = STATUS_UNSUCCESSFUL;
    Work->ElapsedMs = 0;

    SectorSize = DevExt->VolumeInfo.BytesPerSector;
    if (SectorSize == 0 || !NtfslxIsPowerOfTwo(SectorSize))
    {
        Work->WriteStatus = STATUS_INVALID_DEVICE_STATE;
        goto Done;
    }

    SectorBuf = ExAllocatePoolWithTag(NonPagedPool, SectorSize, NTFSLX_TAG);
    if (SectorBuf == NULL)
    {
        Work->WriteStatus = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    /*
     * Read the boot sector before grabbing the lock. We rewrite this
     * exact buffer back in step 3 so the on-disk state is unchanged.
     */
    Status = NtfslxReadDisk(DevExt->StorageDevice,
                            0,
                            SectorSize,
                            SectorSize,
                            SectorBuf,
                            FALSE);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(SectorBuf, NTFSLX_TAG);
        Work->WriteStatus = Status;
        goto Done;
    }

    Start = KeQueryPerformanceCounter(&Frequency);

    /*
     * The property under test: NtfslxWriteDisk must complete while
     * MftAllocLock is held by the same thread. Acquire, write, release.
     *
     * After T1.5.2 the production code no longer holds MftAllocLock
     * across disk writes — but the underlying property still matters:
     * KGUARDED_MUTEX (like FAST_MUTEX) raises IRQL to APC_LEVEL and
     * blocks special kernel APC delivery, so a sync-IRP using
     * IoBuildSynchronousFsdRequest's APC-driven completion would
     * still hang under it. NtfslxWriteDisk uses an async IRP plus a
     * custom completion routine signalled via KEVENT, which is APC-
     * independent. This worker keeps verifying that property so a
     * future regression to APC-based completion is caught.
     */
    KeAcquireGuardedMutex(&DevExt->MftAllocLock);
    Status = NtfslxWriteDisk(DevExt->StorageDevice,
                             0,
                             SectorSize,
                             SectorSize,
                             SectorBuf,
                             FALSE);
    KeReleaseGuardedMutex(&DevExt->MftAllocLock);

    End = KeQueryPerformanceCounter(NULL);
    if (Frequency.QuadPart > 0)
    {
        LONGLONG Delta = End.QuadPart - Start.QuadPart;
        if (Delta < 0)
            Delta = 0;
        Work->ElapsedMs = (ULONG)((Delta * 1000) / Frequency.QuadPart);
    }

    Work->WriteStatus = Status;
    ExFreePoolWithTag(SectorBuf, NTFSLX_TAG);

Done:
    KeSetEvent(&Work->Done, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static NTSTATUS
NtfslxRunTc1ReentrancyTest(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _Out_ PNTFSLX_TC1_REENTRANCY_RESULT Result)
{
    NTFSLX_TC1_WORK Work;
    HANDLE ThreadHandle;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    NTSTATUS WaitStatus;

    RtlZeroMemory(Result, sizeof(*Result));
    Result->Version = 1;

    if (DevExt->StorageDevice == NULL || !DevExt->MftAllocLockInitialized)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Work.DevExt = DevExt;
    KeInitializeEvent(&Work.Done, NotificationEvent, FALSE);
    Work.WriteStatus = STATUS_UNSUCCESSFUL;
    Work.ElapsedMs = 0;

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL,
                                  NULL, NtfslxTc1Worker, &Work);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* 10-second watchdog: if the worker deadlocks, it never signals. */
    Timeout.QuadPart = -((LONGLONG)10 * 1000 * 1000 * 10);
    WaitStatus = KeWaitForSingleObject(&Work.Done, Executive, KernelMode,
                                       FALSE, &Timeout);
    if (WaitStatus == STATUS_SUCCESS)
    {
        Result->TestCompleted = 1;
        Result->WriteStatus = Work.WriteStatus;
        Result->ElapsedMs = Work.ElapsedMs;
        ZwClose(ThreadHandle);
        return STATUS_SUCCESS;
    }

    /*
     * Timeout. The worker is presumed deadlocked under MftAllocLock; we
     * leak the thread handle (the system thread will never finish). The
     * IOCTL caller still gets a structured failure rather than hanging.
     */
    Result->TestCompleted = 0;
    Result->WriteStatus = STATUS_TIMEOUT;
    Result->ElapsedMs = 0;
    ZwClose(ThreadHandle);
    return STATUS_SUCCESS;
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

    if (IoControlCode == IOCTL_NTFSLX_TC1_MFTLOCK_REENTRANCY)
    {
        if (!NtfslxIsVolumeDevice(DeviceExtension) ||
            DeviceExtension->StorageDevice == NULL)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            OutputLength = 0;
            goto CompleteRequest;
        }
        if (OutputLength < sizeof(NTFSLX_TC1_REENTRANCY_RESULT))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            OutputLength = sizeof(NTFSLX_TC1_REENTRANCY_RESULT);
            goto CompleteRequest;
        }
        Status = NtfslxRunTc1ReentrancyTest(DeviceExtension,
            (PNTFSLX_TC1_REENTRANCY_RESULT)Irp->AssociatedIrp.SystemBuffer);
        OutputLength = sizeof(NTFSLX_TC1_REENTRANCY_RESULT);
        goto CompleteRequest;
    }

    if (IoControlCode == IOCTL_NTFSLX_FILEBODY_STATS)
    {
        PNTFSLX_FILEBODY_STATS Out;

        if (OutputLength < sizeof(NTFSLX_FILEBODY_STATS))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            OutputLength = sizeof(NTFSLX_FILEBODY_STATS);
            goto CompleteRequest;
        }
        Out = (PNTFSLX_FILEBODY_STATS)Irp->AssociatedIrp.SystemBuffer;
        RtlZeroMemory(Out, sizeof(*Out));
        Out->Version = 1;
        OutputLength = sizeof(*Out);
        Status = STATUS_SUCCESS;
        goto CompleteRequest;
    }

    if (IoControlCode == IOCTL_NTFSLX_MFT_CACHE_STATS)
    {
        PNTFSLX_MFT_CACHE_STATS Out;

        if (!NtfslxIsVolumeDevice(DeviceExtension))
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            OutputLength = 0;
            goto CompleteRequest;
        }
        if (OutputLength < sizeof(NTFSLX_MFT_CACHE_STATS))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            OutputLength = sizeof(NTFSLX_MFT_CACHE_STATS);
            goto CompleteRequest;
        }
        Out = (PNTFSLX_MFT_CACHE_STATS)Irp->AssociatedIrp.SystemBuffer;
        RtlZeroMemory(Out, sizeof(*Out));
        Out->Version = 1;
        Out->RecordSize = DeviceExtension->VolumeInfo.BytesPerFileRecord;
        Status = STATUS_SUCCESS;
        OutputLength = sizeof(NTFSLX_MFT_CACHE_STATS);
        goto CompleteRequest;
    }

    if (NtfslxIsReparseDeviceControl(IoControlCode))
    {
        Status = NtfslxHandleReparseDeviceControl(Stack->FileObject,
                                                  IoControlCode,
                                                  Irp->AssociatedIrp.SystemBuffer,
                                                  Stack->Parameters.DeviceIoControl.InputBufferLength,
                                                  OutputLength,
                                                  &OutputLength);
        goto CompleteRequest;
    }

    if (!NtfslxIsVolumeDevice(DeviceExtension) ||
        DeviceExtension->StorageDevice == NULL)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        OutputLength = 0;
        goto CompleteRequest;
    }

    if (IoControlCode == FSCTL_IS_VOLUME_DIRTY)
    {
        if (Stack->FileObject == NULL)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            OutputLength = 0;
            goto CompleteRequest;
        }

        Status = NtfslxReportVolumeDirty(DeviceExtension,
                                         Irp,
                                         OutputLength);
        if (NT_SUCCESS(Status))
        {
            OutputLength = sizeof(ULONG);
        }
        else if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            OutputLength = sizeof(ULONG);
        }
        else
        {
            OutputLength = 0;
        }

        NTFSDBG("ntfslx: DeviceControl FSCTL_IS_VOLUME_DIRTY status=0x%08lx flags=0x%04x file='%wZ'\n",
                 Status,
                 DeviceExtension->VolumeInfo.Flags,
                 &Stack->FileObject->FileName);
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
        NTFSDBG("ntfslx: device control 0x%08lx failed status=0x%08lx In=%lu Out=%lu Storage=%p\n",
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
    Irp->IoStatus.Information = 0;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_USER_FS_REQUEST:
            Status = NtfslxUserFsRequest(DeviceObject, Irp);
            break;

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
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
