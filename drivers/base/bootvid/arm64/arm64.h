/*
 * PROJECT:     ReactOS Boot Video Driver for ARM64 (UEFI GOP)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 BOOTVID helpers
 */

#pragma once

VOID
InitPaletteWithTable(
    _In_ PULONG Table,
    _In_ ULONG Count);

VOID
PrepareForSetPixel(VOID);

VOID
SetPixel(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ UCHAR Color);

VOID
PreserveRow(
    _In_ ULONG CurrentTop,
    _In_ ULONG TopDelta,
    _In_ BOOLEAN Restore);

VOID
DoScroll(
    _In_ ULONG Scroll);

VOID
DisplayCharacter(
    _In_ CHAR Character,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG TextColor,
    _In_ ULONG BackColor);
