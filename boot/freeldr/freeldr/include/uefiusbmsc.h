#pragma once

/*
 * Minimal subset of the EFI USB I/O protocol definitions needed by FreeLDR.
 * Based on the UEFI specification and the EDK2 UsbIo protocol header.
 * Copyright : Ahmed ARIF <arif.ing@outlook.com>
 */

#include <uefildr.h>

#ifdef __cplusplus
extern "C" {
#endif

PVOID
UefiUsbMscTryBind(
    EFI_HANDLE Handle,
    ULONG SectorSize);

VOID
UefiUsbMscRelease(
    PVOID Context);

BOOLEAN
UefiUsbMscRead(
    PVOID Context,
    ULONGLONG Lba,
    ULONG SectorCount,
    ULONG SectorSize,
    PVOID Buffer);

BOOLEAN
UefiUsbMscWrite(
    PVOID Context,
    ULONGLONG Lba,
    ULONG SectorCount,
    ULONG SectorSize,
    PVOID Buffer);

#ifdef __cplusplus
}
#endif
