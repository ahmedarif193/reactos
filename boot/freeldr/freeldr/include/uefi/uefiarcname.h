/*
 * PROJECT:         ReactOS FreeLoader
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * FILE:            boot/freeldr/freeldr/include/uefi/uefiarcname.h
 * PURPOSE:         UEFI ARC naming support
 * COPYRIGHT:       Copyright 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#ifndef _UEFI_ARCNAME_H_
#define _UEFI_ARCNAME_H_

#include <disk.h>

#ifndef FIRST_BIOS_DISK
#define FIRST_BIOS_DISK 0x80
#endif

#ifdef UEFIBOOT
/* Forward declaration for EFI_HANDLE when UEFI headers aren't included */
#ifndef __EFI_TYPES_H__
typedef PVOID EFI_HANDLE;
#endif

BOOLEAN UefiEnumerateArcDisks(VOID);
BOOLEAN UefiInitializeArcDisks(PLOADER_PARAMETER_BLOCK LoaderBlock);
BOOLEAN UefiGetBootPartitionInfo(OUT PULONG RDiskNumber,
                                 OUT PULONG PartitionNumber,
                                 OUT PCHAR BootDevice,
                                 IN ULONG BootDeviceSize);
BOOLEAN UefiGetBootPartitionEntry(IN UCHAR DriveNumber,
                                  OUT PPARTITION_TABLE_ENTRY PartitionTableEntry,
                                  OUT PULONG BootPartition);
BOOLEAN UefiDiskIsUsb(IN UCHAR DriveNumber);

extern BOOLEAN UefiBootHasDiskArc;
extern ULONG UefiBootDiskArcNumber;
extern ULONG UefiBootDiskArcPartition;
ULONG MapToRdiskIndex(IN EFI_HANDLE DiskHandle);
ULONG MapToCdromIndex(IN EFI_HANDLE CdHandle);

ULONG UefiGetPhysicalDiskCount(VOID);
EFI_HANDLE UefiGetPhysicalDiskHandle(ULONG ArcIndex);
#endif /* UEFIBOOT */

#endif /* _UEFI_ARCNAME_H_ */
