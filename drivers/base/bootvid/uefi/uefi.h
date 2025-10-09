/*
 * PROJECT:     ReactOS Boot Video Driver (UEFI backend)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     UEFI-specific BOOTVID arch hooks declarations
 */

#pragma once

BOOLEAN
NTAPI
UefiVidInitialize(
    _In_ BOOLEAN ResetMode);

VOID
NTAPI
UefiVidResetDisplay(
    _In_ BOOLEAN HalReset);

VOID
NTAPI
UefiVidCleanUp(VOID);

VOID
NTAPI
UefiVidScreenToBufferBlt(
    _Out_writes_bytes_(Delta * Height) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Delta);

VOID
NTAPI
UefiVidBufferToScreenBlt(
    _In_reads_bytes_(Delta * Height) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Delta);

VOID
NTAPI
UefiVidDisplayString(
    _In_z_ PUCHAR String);

ULONG
NTAPI
UefiVidSetTextColor(
    _In_ ULONG Color);

VOID
NTAPI
UefiVidSolidColorFill(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ UCHAR Color);

VOID
NTAPI
UefiVidSetScrollRegion(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom);

VOID
NTAPI
UefiVidDisplayStringXY(
    _In_z_ PUCHAR String,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ BOOLEAN Transparent);

VOID
NTAPI
UefiVidBitBlt(
    _In_ PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top);

/* UEFI backend provides its own top-level Vid* entrypoints; low-level arch hooks remain from VGA. */
