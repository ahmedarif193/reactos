/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS File System Recognizer
 * FILE:             drivers/filesystems/fs_rec/fat.c
 * PURPOSE:          FAT Recognizer
 * PROGRAMMER:       Alex Ionescu (alex.ionescu@reactos.org)
 *                   Eric Kohl
 */

/* INCLUDES *****************************************************************/

#include "fs_rec.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/

static
VOID
NTAPI
FsRecLogFatBpb(
    _In_ PPACKED_BOOT_SECTOR PackedBootSector,
    _In_ PBIOS_PARAMETER_BLOCK Bpb)
{
    DPRINT1("fs_rec: FAT probe jump=%02x %02x %02x oem='%.*s' bps=%u spc=%u reserved=%u fats=%u root=%u sectors=%u large=%lu media=%02x spf=%u hidden=%lu\n",
            PackedBootSector->Jump[0],
            PackedBootSector->Jump[1],
            PackedBootSector->Jump[2],
            (INT)sizeof(PackedBootSector->Oem),
            PackedBootSector->Oem,
            Bpb->BytesPerSector,
            Bpb->SectorsPerCluster,
            Bpb->ReservedSectors,
            Bpb->Fats,
            Bpb->RootEntries,
            Bpb->Sectors,
            Bpb->LargeSectors,
            Bpb->Media,
            Bpb->SectorsPerFat,
            Bpb->HiddenSectors);
}

BOOLEAN
NTAPI
FsRecIsFatVolume(IN PPACKED_BOOT_SECTOR PackedBootSector)
{
    BIOS_PARAMETER_BLOCK Bpb;
    BOOLEAN Result = TRUE;
    PAGED_CODE();

    RtlZeroMemory(&Bpb, sizeof(BIOS_PARAMETER_BLOCK));

    /* Unpack the BPB and do a small fix up */
    FatUnpackBios(&Bpb, &PackedBootSector->PackedBpb);
    if (Bpb.Sectors) Bpb.LargeSectors = 0;
    FsRecLogFatBpb(PackedBootSector, &Bpb);

    /* Recognize jump */
    if ((PackedBootSector->Jump[0] != 0x49) &&
        (PackedBootSector->Jump[0] != 0xE9) &&
        (PackedBootSector->Jump[0] != 0xEB))
    {
        /* Fail */
        DPRINT1("fs_rec: FAT reject invalid jump opcode %02x\n",
                PackedBootSector->Jump[0]);
        Result = FALSE;
    }
    else if ((Bpb.BytesPerSector != 128) &&
             (Bpb.BytesPerSector != 256) &&
             (Bpb.BytesPerSector != 512) &&
             (Bpb.BytesPerSector != 1024) &&
             (Bpb.BytesPerSector != 2048) &&
             (Bpb.BytesPerSector != 4096))
    {
        /* Fail */
        DPRINT1("fs_rec: FAT reject invalid bytes/sector %u\n",
                Bpb.BytesPerSector);
        Result = FALSE;
    }
    else if ((Bpb.SectorsPerCluster != 1) &&
             (Bpb.SectorsPerCluster != 2) &&
             (Bpb.SectorsPerCluster != 4) &&
             (Bpb.SectorsPerCluster != 8) &&
             (Bpb.SectorsPerCluster != 16) &&
             (Bpb.SectorsPerCluster != 32) &&
             (Bpb.SectorsPerCluster != 64) &&
             (Bpb.SectorsPerCluster != 128))
    {
        /* Fail */
        DPRINT1("fs_rec: FAT reject invalid sectors/cluster %u\n",
                Bpb.SectorsPerCluster);
        Result = FALSE;
    }
    else if (!Bpb.ReservedSectors)
    {
        /* Fail */
        DPRINT1("fs_rec: FAT reject reserved sectors == 0\n");
        Result = FALSE;
    }
    else if (!(Bpb.Sectors) && !(Bpb.LargeSectors))
    {
        /* Fail */
        DPRINT1("fs_rec: FAT reject total sectors == 0\n");
        Result = FALSE;
    }
    else if ((Bpb.Media != 0x00) &&
             (Bpb.Media != 0x01) &&
             (Bpb.Media != 0xf0) &&
             (Bpb.Media != 0xf8) &&
             (Bpb.Media != 0xf9) &&
             (Bpb.Media != 0xfa) &&
             (Bpb.Media != 0xfb) &&
             (Bpb.Media != 0xfc) &&
             (Bpb.Media != 0xfd) &&
             (Bpb.Media != 0xfe) &&
             (Bpb.Media != 0xff))
    {
        /* Fail */
        DPRINT1("fs_rec: FAT reject invalid media byte %02x\n",
                Bpb.Media);
        Result = FALSE;
    }
    else if ((Bpb.SectorsPerFat) && !(Bpb.RootEntries))
    {
        /* Fail */
        DPRINT1("fs_rec: FAT reject FAT12/16 BPB with zero root entries\n");
        Result = FALSE;
    }

    if (Result)
    {
        DPRINT1("fs_rec: FAT recognizer accepted volume\n");
    }

    /* Return the result */
    return Result;
}

NTSTATUS
NTAPI
FsRecVfatFsControl(IN PDEVICE_OBJECT DeviceObject,
                   IN PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;
    PDEVICE_OBJECT MountDevice;
    PPACKED_BOOT_SECTOR Bpb = NULL;
    ULONG SectorSize;
    LARGE_INTEGER Offset = {{0, 0}};
    BOOLEAN DeviceError = FALSE;
    PAGED_CODE();

    /* Get the I/O Stack and check the function type */
    Stack = IoGetCurrentIrpStackLocation(Irp);
    switch (Stack->MinorFunction)
    {
        case IRP_MN_MOUNT_VOLUME:

            /* Assume failure */
            Status = STATUS_UNRECOGNIZED_VOLUME;

            /* Get the device object and request the sector size */
            MountDevice = Stack->Parameters.MountVolume.DeviceObject;
            DPRINT1("fs_rec: FAT mount probe Device=%p\n", MountDevice);
            if (FsRecGetDeviceSectorSize(MountDevice, &SectorSize))
            {
                DPRINT1("fs_rec: FAT sector size %lu\n", SectorSize);
                /* Try to read the BPB */
                if (FsRecReadBlock(MountDevice,
                                   &Offset,
                                   512,
                                   SectorSize,
                                   (PVOID)&Bpb,
                                   &DeviceError))
                {
                    /* Check if it's an actual FAT volume */
                    if (FsRecIsFatVolume(Bpb))
                    {
                        /* It is! */
                        Status = STATUS_FS_DRIVER_REQUIRED;
                        DPRINT1("fs_rec: FAT mount requires fastfat\n");
                    }
                    else
                    {
                        DPRINT1("fs_rec: FAT mount probe rejected volume\n");
                    }
                }
                else
                {
                    DPRINT1("fs_rec: FAT boot sector read failed DeviceError=%u\n",
                            DeviceError);
                }

                /* Free the boot sector if we have one */
                ExFreePool(Bpb);
            }
            else
            {
                /* We have some sort of failure in the storage stack */
                DeviceError = TRUE;
                DPRINT1("fs_rec: FAT failed to query sector size\n");
            }

            /* Check if we have an error on the stack */
            if (DeviceError)
            {
                /* Was this because of a floppy? */
                if (MountDevice->Characteristics & FILE_FLOPPY_DISKETTE)
                {
                    /* Let the FS try anyway */
                    Status = STATUS_FS_DRIVER_REQUIRED;
                    DPRINT1("fs_rec: FAT forcing mount for floppy path\n");
                }
            }

            break;

        case IRP_MN_LOAD_FILE_SYSTEM:

            /* Load the file system */
            DPRINT1("fs_rec: loading fastfat\n");
            Status = FsRecLoadFileSystem(DeviceObject,
                                         L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\fastfat");
            DPRINT1("fs_rec: fastfat load returned 0x%08lx\n", Status);
            break;

        default:

            /* Invalid request */
            Status = STATUS_INVALID_DEVICE_REQUEST;
    }

    /* Return Status */
    return Status;
}

/* EOF */
