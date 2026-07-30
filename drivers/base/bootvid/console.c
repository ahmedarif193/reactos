/*
 * PROJECT:     ReactOS Boot Video Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Platform-independent console functionality
 * COPYRIGHT:   Copyright 2010 Gregor Schneider <gregor.schneider@reactos.org>
 *              Copyright 2011 Rafal Harabien <rafalh@reactos.org>
 *              Copyright 2020 Stanislav Motylkov <x86corez@gmail.com>
 */

#include "precomp.h"

/* GLOBALS ********************************************************************/

UCHAR VidpTextColor = BV_COLOR_WHITE;
ULONG VidpCurrentX = 0;
ULONG VidpCurrentY = 0;
ULONG VidpDisplayWidth = SCREEN_WIDTH;
ULONG VidpDisplayHeight = SCREEN_HEIGHT;
ULONG VidpPhysicalWidth = SCREEN_WIDTH;
ULONG VidpPhysicalHeight = SCREEN_HEIGHT;
ULONG VidpDisplayDpi = 96;
ULONG VidpCharacterWidth = BOOTCHAR_WIDTH;
ULONG VidpCharacterHeight = BOOTCHAR_HEIGHT + 1;
URECT VidpScrollRegion = {0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1};

static BOOLEAN ClearRow = FALSE;

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
VidQueryDisplayInfo(_Out_ PVID_DISPLAY_INFO DisplayInfo)
{
    if (!DisplayInfo)
        return FALSE;

    DisplayInfo->Width = VidpDisplayWidth;
    DisplayInfo->Height = VidpDisplayHeight;
    DisplayInfo->PhysicalWidth = VidpPhysicalWidth;
    DisplayInfo->PhysicalHeight = VidpPhysicalHeight;
    DisplayInfo->CharacterWidth = VidpCharacterWidth;
    DisplayInfo->CharacterHeight = VidpCharacterHeight;
    DisplayInfo->Dpi = VidpDisplayDpi;
    return TRUE;
}

VOID
NTAPI
VidResetDisplay(
    _In_ BOOLEAN SetMode)
{
    /* Clear the current position */
    VidpCurrentX = 0;
    VidpCurrentY = 0;
    ClearRow = FALSE;

    /* Invoke the hardware-specific routine */
    ResetDisplay(SetMode);
}

ULONG
NTAPI
VidSetTextColor(
    _In_ ULONG Color)
{
    ULONG OldColor;

    /* Save the old color and set the new one */
    OldColor = VidpTextColor;
    VidpTextColor = Color;
    return OldColor;
}

VOID
NTAPI
VidSetScrollRegion(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom)
{
    /* Assert alignment */
    ASSERT((Left % VidpCharacterWidth) == 0);
    ASSERT((Right % VidpCharacterWidth) == VidpCharacterWidth - 1);
    ASSERT(Left <= Right);
    ASSERT(Top <= Bottom);
    ASSERT(Right < VidpDisplayWidth);
    ASSERT(Bottom < VidpDisplayHeight);

    if ((Left > Right) || (Top > Bottom) || (Right >= VidpDisplayWidth) || (Bottom >= VidpDisplayHeight))
    {
        return;
    }

    /* Set the scroll region */
    VidpScrollRegion.Left = Left;
    VidpScrollRegion.Top  = Top;
    VidpScrollRegion.Right  = Right;
    VidpScrollRegion.Bottom = Bottom;

    /* Set the current X and Y */
    VidpCurrentX = Left;
    VidpCurrentY = Top;
}

VOID
NTAPI
VidDisplayStringXY(
    _In_ PCSTR String,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ BOOLEAN Transparent)
{
    ULONG BackColor;

    /*
     * If the caller wanted transparent, then send the special value (16),
     * else use our default and call the helper routine.
     */
    BackColor = Transparent ? BV_COLOR_NONE : BV_COLOR_LIGHT_CYAN;

    /* Loop every character and adjust the position */
    if (Top >= VidpDisplayHeight)
        return;

    for (; *String && (Left + VidpCharacterWidth <= VidpDisplayWidth); ++String, Left += VidpCharacterWidth)
    {
        /* Display a character */
        DisplayCharacter(*String, Left, Top, BV_COLOR_LIGHT_BLUE, BackColor);
    }
}

VOID
NTAPI
VidDisplayString(
    _In_ PCSTR String)
{
    /* Start looping the string */
    for (; *String; ++String)
    {
        /* Treat new-line separately */
        if (*String == '\n')
        {
            /* Modify Y position */
            VidpCurrentY += VidpCharacterHeight;
            if (VidpCurrentY + VidpCharacterHeight - 1 > VidpScrollRegion.Bottom)
            {
                /* Scroll the view and clear the current row */
                DoScroll(VidpCharacterHeight);
                VidpCurrentY -= VidpCharacterHeight;
                PreserveRow(VidpCurrentY, VidpCharacterHeight, TRUE);
            }
            else
            {
                /* Preserve the current row */
                PreserveRow(VidpCurrentY, VidpCharacterHeight, FALSE);
            }

            /* Update current X */
            VidpCurrentX = VidpScrollRegion.Left;

            /* No need to clear this row */
            ClearRow = FALSE;
        }
        else if (*String == '\r')
        {
            /* Update current X */
            VidpCurrentX = VidpScrollRegion.Left;

            /* If a new-line does not follow we will clear the current row */
            if (String[1] != '\n')
                ClearRow = TRUE;
        }
        else
        {
            /* Clear the current row if we had a return-carriage without a new-line */
            if (ClearRow)
            {
                PreserveRow(VidpCurrentY, VidpCharacterHeight, TRUE);
                ClearRow = FALSE;
            }

            /* Display this character */
            DisplayCharacter(*String, VidpCurrentX, VidpCurrentY, VidpTextColor, BV_COLOR_NONE);
            VidpCurrentX += VidpCharacterWidth;

            /* Check if we should scroll */
            if (VidpCurrentX + VidpCharacterWidth - 1 > VidpScrollRegion.Right)
            {
                /* Update Y position and check if we should scroll it */
                VidpCurrentY += VidpCharacterHeight;
                if (VidpCurrentY + VidpCharacterHeight - 1 > VidpScrollRegion.Bottom)
                {
                    /* Scroll the view and clear the current row */
                    DoScroll(VidpCharacterHeight);
                    VidpCurrentY -= VidpCharacterHeight;
                    PreserveRow(VidpCurrentY, VidpCharacterHeight, TRUE);
                }
                else
                {
                    /* Preserve the current row */
                    PreserveRow(VidpCurrentY, VidpCharacterHeight, FALSE);
                }

                /* Update current X */
                VidpCurrentX = VidpScrollRegion.Left;
            }
        }
    }
}
