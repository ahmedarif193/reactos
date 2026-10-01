/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Open Firmware machine vector
 */

#include <freeldr.h>

VOID
MachInit(const char *CmdLine)
{
    UNREFERENCED_PARAMETER(CmdLine);

    RtlZeroMemory(&MachVtbl, sizeof(MachVtbl));

    MachVtbl.ConsPutChar = OfwConsPutChar;
    MachVtbl.ConsKbHit = OfwConsKbHit;
    MachVtbl.ConsGetCh = OfwConsGetCh;
    MachVtbl.VideoClearScreen = OfwVideoClearScreen;
    MachVtbl.VideoSetDisplayMode = OfwVideoSetDisplayMode;
    MachVtbl.VideoGetDisplaySize = OfwVideoGetDisplaySize;
    MachVtbl.VideoGetBufferSize = OfwVideoGetBufferSize;
    MachVtbl.VideoGetFontsFromFirmware = OfwVideoGetFontsFromFirmware;
    MachVtbl.VideoSetTextCursorPosition = OfwVideoSetTextCursorPosition;
    MachVtbl.VideoHideShowTextCursor = OfwVideoHideShowTextCursor;
    MachVtbl.VideoPutChar = OfwVideoPutChar;
    MachVtbl.VideoCopyOffScreenBufferToVRAM = OfwVideoCopyOffScreenBufferToVRAM;
    MachVtbl.VideoIsPaletteFixed = OfwVideoIsPaletteFixed;
    MachVtbl.VideoSetPaletteColor = OfwVideoSetPaletteColor;
    MachVtbl.VideoGetPaletteColor = OfwVideoGetPaletteColor;
    MachVtbl.VideoSync = OfwVideoSync;
    MachVtbl.Beep = OfwBeep;
    MachVtbl.PrepareForReactOS = OfwPrepareForReactOS;
    MachVtbl.GetMemoryMap = OfwMemGetMemoryMap;
    MachVtbl.GetExtendedBIOSData = OfwGetExtendedBIOSData;
    MachVtbl.GetFloppyCount = OfwGetFloppyCount;
    MachVtbl.DiskReadLogicalSectors = OfwDiskReadLogicalSectors;
    MachVtbl.DiskGetDriveGeometry = OfwDiskGetDriveGeometry;
    MachVtbl.DiskGetCacheableBlockCount = OfwDiskGetCacheableBlockCount;
    MachVtbl.GetTime = OfwGetTime;
    MachVtbl.GetRelativeTime = OfwGetRelativeTime;
    MachVtbl.InitializeBootDevices = OfwInitializeBootDevices;
    MachVtbl.HwDetect = OfwHwDetect;
    MachVtbl.HwIdle = OfwHwIdle;
}
