/*
 * PROJECT:     Freeldr UEFI Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI "mach" header
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <machine.h>
#include <drivers/acpi/acpi.h>

VOID
UefiConsPutChar(int Ch);

BOOLEAN
UefiConsKbHit(VOID);

int
UefiConsGetCh(void);

EFI_STATUS
UefiInitializeVideo(VOID);

VOID
UefiVideoClearScreen(UCHAR Attr);

VIDEODISPLAYMODE
UefiVideoSetDisplayMode(PCSTR DisplayMode, BOOLEAN Init);

VOID
UefiVideoGetDisplaySize(PULONG Width, PULONG Height, PULONG Depth);

ULONG
UefiVideoGetBufferSize(VOID);

VOID
UefiVideoGetFontsFromFirmware(PULONG RomFontPointers);

VOID
UefiVideoSetTextCursorPosition(UCHAR X, UCHAR Y);

VOID
UefiVideoHideShowTextCursor(BOOLEAN Show);

VOID
UefiVideoPutChar(int Ch, UCHAR Attr,
                 unsigned X, unsigned Y);


VOID
UefiVideoCopyOffScreenBufferToVRAM(PVOID Buffer);

BOOLEAN
UefiVideoIsPaletteFixed(VOID);

VOID
UefiVideoSetPaletteColor(UCHAR Color, UCHAR Red,
                         UCHAR Green, UCHAR Blue);

VOID
UefiVideoGetPaletteColor(UCHAR Color, UCHAR* Red,
                         UCHAR* Green, UCHAR* Blue);

PDESCRIPTION_HEADER
UefiFindAcpiTable(
    _In_ ULONG Signature);

VOID
UefiVideoSync(VOID);

VOID
UefiVideoScrollUp(UCHAR Attr, ULONG Lines);

VOID
UefiVideoPrepareForExitBootServices(VOID);

VOID
UefiVideoExitBootServices(VOID);

VOID
UefiPcBeep(VOID);

#if defined(_M_ARM64)
BOOLEAN
UefiSerialInitialize(VOID);

VOID
UefiSerialPutChar(
    _In_ UCHAR Character);
#endif

VOID
UefiSerialDisableFirmware(VOID);

PFREELDR_MEMORY_DESCRIPTOR
UefiMemGetMemoryMap(ULONG *MemoryMapSize);

VOID
UefiGetExtendedBIOSData(PULONG ExtendedBIOSDataArea,
                        PULONG ExtendedBIOSDataSize);

PVOID
UefiGetSmbiosEpsPointer(VOID);

UCHAR
UefiGetFloppyCount(VOID);

BOOLEAN
UefiDiskReadLogicalSectors(IN UCHAR DriveNumber,
                           IN ULONGLONG SectorNumber,
                           IN ULONG SectorCount,
                           OUT PVOID Buffer);

BOOLEAN
UefiDiskGetDriveGeometry(UCHAR DriveNumber,
                         PGEOMETRY Geometry);

ULONG
UefiDiskGetCacheableBlockCount(UCHAR DriveNumber);

TIMEINFO*
UefiGetTime(VOID);

BOOLEAN
UefiInitializeBootDevices(VOID);

PCONFIGURATION_COMPONENT_DATA
UefiHwDetect(
    _In_opt_ PCSTR Options);

VOID
UefiPrepareForReactOS(VOID);

VOID
UefiHwIdle(VOID);

VOID
UefiInitializeFileSystemSupport(_In_ EFI_HANDLE ImageHandle,
                                _In_ EFI_SYSTEM_TABLE *SystemTable);

#ifdef FREELDR_HTTP_BOOT
BOOLEAN
UefiHttpBootDownload(
    _In_ PCSTR Url);
#endif
