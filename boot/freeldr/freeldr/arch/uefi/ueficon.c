/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Console output
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>
#include "../vidfb.h"

/* GLOBALS ********************************************************************/

UCHAR MachDefaultTextColor = COLOR_GRAY;

static unsigned CurrentCursorX = 0;
static unsigned CurrentCursorY = 0;
static UCHAR CurrentAttr = ATTR(COLOR_GRAY, COLOR_BLACK);

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
static BOOLEAN ExtendedKey = FALSE;
static int ExtendedScanCode = 0;
static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL* TextInputEx;
static BOOLEAN TextInputInitialized;

/* FUNCTIONS ******************************************************************/

VOID
UefiConsPutChar(int c)
{
    ULONG Width, Height, Unused;
    BOOLEAN NeedScroll;

    UefiVideoGetDisplaySize(&Width, &Height, &Unused);

    NeedScroll = (CurrentCursorY >= Height);
    if (NeedScroll)
    {
        UefiVideoScrollUp(CurrentAttr, 1);
        --CurrentCursorY;
    }
    if (c == '\r')
    {
        CurrentCursorX = 0;
    }
    else if (c == '\n')
    {
        CurrentCursorX = 0;
        if (!NeedScroll)
            ++CurrentCursorY;
    }
    else if (c == '\t')
    {
        CurrentCursorX = (CurrentCursorX + 8) & ~7;
    }
    else
    {
        UefiVideoPutChar(c, CurrentAttr, CurrentCursorX, CurrentCursorY);
        CurrentCursorX++;
    }
    if (CurrentCursorX >= Width)
    {
        CurrentCursorX = 0;
        CurrentCursorY++;
    }
}

static
int
ConvertToBiosExtValue(USHORT KeyIn)
{
    switch (KeyIn)
    {
        case SCAN_VOLUME_UP:
            return KEY_VOLUME_UP;
        case SCAN_VOLUME_DOWN:
            return KEY_VOLUME_DOWN;
        case SCAN_UP:
            return KEY_UP;
        case SCAN_DOWN:
            return KEY_DOWN;
        case SCAN_RIGHT:
            return KEY_RIGHT;
        case SCAN_LEFT:
            return KEY_LEFT;
        case SCAN_HOME:
            return KEY_HOME;
        case SCAN_END:
            return KEY_END;

        // case SCAN_INSERT:
        //     break;

        case SCAN_DELETE:
            return KEY_DELETE;

        // case SCAN_PAGE_UP:
        // case SCAN_PAGE_DOWN:
        //     break;

        case SCAN_F1:
            return KEY_F1;
        case SCAN_F2:
            return KEY_F2;
        case SCAN_F3:
            return KEY_F3;
        case SCAN_F4:
            return KEY_F4;
        case SCAN_F5:
            return KEY_F5;
        case SCAN_F6:
            return KEY_F6;
        case SCAN_F7:
            return KEY_F7;
        case SCAN_F8:
            return KEY_F8;
        case SCAN_F9:
            return KEY_F9;
        case SCAN_F10:
            return KEY_F10;
        case SCAN_ESC:
            return KEY_ESC;
    }
    return 0;
}

BOOLEAN
UefiConsKbHit(VOID)
{
    static EFI_GUID TextInputExGuid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
    if (!TextInputInitialized)
    {
        TextInputInitialized = TRUE;
        if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(GlobalSystemTable->ConsoleInHandle,
                                                                      &TextInputExGuid, (PVOID*)&TextInputEx)))
            TextInputEx = NULL;
    }
    if (ExtendedKey)
        return TRUE;
    return (GlobalSystemTable->BootServices->CheckEvent(
                TextInputEx ? TextInputEx->WaitForKeyEx :
                              GlobalSystemTable->ConIn->WaitForKey) == EFI_SUCCESS);
}

int
UefiConsGetCh(VOID)
{
    EFI_INPUT_KEY Key;
    EFI_KEY_DATA KeyData;
    EFI_STATUS Status;
    UCHAR KeyOutput = 0;

    /* If an extended key press was detected the last time we were called
     * then return the scan code of that key. */
    if (ExtendedKey)
    {
        ExtendedKey = FALSE;
        return ExtendedScanCode;
    }

    if (TextInputEx)
    {
        Status = TextInputEx->ReadKeyStrokeEx(TextInputEx, &KeyData);
        if (EFI_ERROR(Status))
            return 0;
        Key = KeyData.Key;
    }
    else
        Status = GlobalSystemTable->ConIn->ReadKeyStroke(GlobalSystemTable->ConIn, &Key);
    if (EFI_ERROR(Status))
        return 0;

    if (Key.UnicodeChar != 0)
    {
        KeyOutput = Key.UnicodeChar;
    }
    else
    {
        ExtendedKey = TRUE;
        ExtendedScanCode = ConvertToBiosExtValue(Key.ScanCode);
        KeyOutput = KEY_EXTENDED;
    }
    return KeyOutput;
}
