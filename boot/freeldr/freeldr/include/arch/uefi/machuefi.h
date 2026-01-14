/*
 * PROJECT:     Freeldr UEFI Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI "mach" header
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#ifndef FREELDR_ARCH_UEFI_MACHUEFI_H
#define FREELDR_ARCH_UEFI_MACHUEFI_H

#include <machine.h>

VOID
UefiConsPutChar(int Ch);

BOOLEAN
UefiConsKbHit(VOID);

int
UefiConsGetCh(void);

EFI_STATUS
UefiInitializeVideo(VOID);

BOOLEAN
UefiIsFramebufferReady(VOID);

VOID
UefiVideoClearScreen(UCHAR Attr);

VIDEODISPLAYMODE
UefiVideoSetDisplayMode(char *DisplayMode, BOOLEAN Init);

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

/* UEFI-specific partial commit for semi-text updates. Coordinates are
 * text-cell units (chars). */
VOID
UefiVideoCopyOffScreenBufferRectToVRAM(
    PVOID Buffer,
    ULONG Left,
    ULONG Top,
    ULONG Right,
    ULONG Bottom);

BOOLEAN
UefiVideoIsPaletteFixed(VOID);

VOID
UefiVideoSetPaletteColor(UCHAR Color, UCHAR Red,
                         UCHAR Green, UCHAR Blue);

VOID
UefiVideoGetPaletteColor(UCHAR Color, UCHAR* Red,
                         UCHAR* Green, UCHAR* Blue);

VOID
UefiVideoSync(VOID);

VOID
UefiPcBeep(VOID);

BOOLEAN
UefiVideoDisplayBootLogo(VOID);

BOOLEAN
UefiVideoIsBootLogoDrawn(VOID);

VOID
UefiVideoRefreshBootLogo(VOID);

PFREELDR_MEMORY_DESCRIPTOR
UefiMemGetMemoryMap(ULONG *MemoryMapSize);

VOID
UefiGetExtendedBIOSData(PULONG ExtendedBIOSDataArea,
                        PULONG ExtendedBIOSDataSize);

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

VOID
UefiVideoScrollUp(VOID);

/* GOP console helpers used once the firmware text console becomes unavailable. */
VOID
UefiGopConsolePutChar(CHAR Ch);

VOID
UefiGopConsolePutString(PCSTR String);

VOID
UefiGopConsoleClear(VOID);

VOID
UefiGopConsoleSetCursor(UINT32 X, UINT32 Y);

BOOLEAN
UefiGopConsoleIsInitialized(VOID);

VOID
UefiConsMarkBootServicesExited(VOID);

/*
 * Clear all x64 debug registers and sanitize CPU debug state.
 * Must be called before ExitBootServices to prevent #DB exceptions
 * from stale UEFI firmware debug state (especially on VirtualBox).
 */
VOID
UefiClearDebugState(VOID);

BOOLEAN
UefiIsCdRomHandle(IN EFI_HANDLE Handle);

EFI_HANDLE
UefiGetCdromHandle(ULONG Index);

ULONG
UefiGetCdromCount(VOID);


/* Informational media classification (for diagnostics/UI only) */
typedef enum _UEFI_MEDIA_KIND
{
    UefiMediaUnknown = 0,
    UefiMediaDisk,
    UefiMediaCdrom
} UEFI_MEDIA_KIND;

typedef struct _UEFI_MEDIA_INFO
{
    UEFI_MEDIA_KIND Kind;
    BOOLEAN Removable;
    BOOLEAN ReadOnly;
    ULONG BlockSize;
    BOOLEAN HasPartitionInfo;
} UEFI_MEDIA_INFO, *PUEFI_MEDIA_INFO;

BOOLEAN
UefiClassifyMediaFromHandle(
    _In_ EFI_HANDLE Handle,
    _Out_ PUEFI_MEDIA_INFO Info);


/* Retrieve the EFI block handle and ARC device path for an opened FileId */
EFI_HANDLE
UefiGetBlockHandleForFileId(ULONG FileId);

PCCHAR
UefiGetArcPathForFileId(ULONG FileId);


#endif /* FREELDR_ARCH_UEFI_MACHUEFI_H */
