/*
 * PROJECT:     ReactOS Boot Video Driver for ARM64 (UEFI GOP)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 BOOTVID entry points backed by the UEFI implementation
 */

#include "precomp.h"

BOOLEAN g_BootvidUseUefi = FALSE;

VOID
InitPaletteWithTable(
    _In_ PULONG Table,
    _In_ ULONG Count)
{
    UNREFERENCED_PARAMETER(Table);
    UNREFERENCED_PARAMETER(Count);
}

VOID
PrepareForSetPixel(VOID)
{
}

VOID
SetPixel(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ UCHAR Color)
{
    /* Minimal bridge to the UEFI backend */
    UefiVidSolidColorFill(Left, Top, Left, Top, Color);
}

VOID
DisplayCharacter(
    _In_ CHAR Character,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG TextColor,
    _In_ ULONG BackColor)
{
    UCHAR Str[2] = {(UCHAR)Character, 0};

    UefiVidSetTextColor(TextColor);
    if (BackColor < BV_COLOR_NONE)
        UefiVidSolidColorFill(Left, Top, Left + BOOTCHAR_WIDTH - 1, Top + BOOTCHAR_HEIGHT - 1, (UCHAR)BackColor);
    UefiVidDisplayStringXY(Str, Left, Top, BackColor == BV_COLOR_NONE);
}

VOID
DoScroll(
    _In_ ULONG Scroll)
{
    UNREFERENCED_PARAMETER(Scroll);
}

VOID
PreserveRow(
    _In_ ULONG CurrentTop,
    _In_ ULONG TopDelta,
    _In_ BOOLEAN Restore)
{
    UNREFERENCED_PARAMETER(CurrentTop);
    UNREFERENCED_PARAMETER(TopDelta);
    UNREFERENCED_PARAMETER(Restore);
}

BOOLEAN
NTAPI
VidInitialize(
    _In_ BOOLEAN SetMode)
{
    UNREFERENCED_PARAMETER(SetMode);

    if (UefiVidInitialize(SetMode))
    {
        g_BootvidUseUefi = TRUE;
        return TRUE;
    }

    return FALSE;
}

VOID
NTAPI
VidResetDisplay(
    _In_ BOOLEAN HalReset)
{
    UefiVidResetDisplay(HalReset);
}

VOID
NTAPI
VidCleanUp(VOID)
{
    UefiVidCleanUp();
    g_BootvidUseUefi = FALSE;
}

VOID
NTAPI
VidScreenToBufferBlt(
    _Out_writes_bytes_(Delta * Height) PUCHAR Buffer,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Delta)
{
    UefiVidScreenToBufferBlt(Buffer, Left, Top, Width, Height, Delta);
}

VOID
NTAPI
VidSolidColorFill(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ UCHAR Color)
{
    UefiVidSolidColorFill(Left, Top, Right, Bottom, Color);
}
