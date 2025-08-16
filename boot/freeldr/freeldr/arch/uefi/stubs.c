/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Function stubs
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>

#ifndef _M_ARM
/* TODO: Handle this with custom Disk / partition setup */
UCHAR
DriveMapGetBiosDriveNumber(PCSTR DeviceName)
{
    return 0;
}
#endif

VOID
StallExecutionProcessor(ULONG Microseconds)
{

}

VOID
UefiVideoGetFontsFromFirmware(PULONG RomFontPointers)
{

}

VOID
UefiVideoSync(VOID)
{

}

VOID
UefiGetExtendedBIOSData(PULONG ExtendedBIOSDataArea,
                        PULONG ExtendedBIOSDataSize)
{

}

VOID
UefiPcBeep(VOID)
{
    /* Not possible on UEFI, for now */
}

#ifdef _M_ARM64
/* Bootloader-specific implementation for KeGetCurrentIrql */
/* This is needed because ARM64 defines it as a macro accessing PCR */
/* but in bootloader we don't have a PCR structure */
#undef KeGetCurrentIrql
KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    /* Boot loader always runs at passive level */
    return 0;
}
#endif


VOID
UefiHwIdle(VOID)
{

}
