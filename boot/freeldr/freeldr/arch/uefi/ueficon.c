/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Console output
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16

/* GLOBALS ********************************************************************/

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
static unsigned CurrentCursorX = 0;
static unsigned CurrentCursorY = 0;
static unsigned CurrentAttr = 0x0f;
#define KEY_QUEUE_CAPACITY 32

static INT KeyQueue[KEY_QUEUE_CAPACITY];
static UINT8 KeyQueueHead = 0;
static UINT8 KeyQueueTail = 0;

/* GOP console entry points provided by the GOP console module. */
extern VOID UefiGopConsolePutChar(CHAR Ch);
extern VOID UefiGopConsolePutString(PCSTR String);
extern VOID UefiGopConsoleClear(VOID);
extern VOID UefiGopConsoleSetCursor(UINT32 X, UINT32 Y);
extern BOOLEAN UefiGopConsoleIsInitialized(VOID);
static BOOLEAN BootServicesExited = FALSE;

/* Forward declarations ******************************************************/
static BOOLEAN BootServicesAvailable(VOID);
static VOID UefiConsSyncCursorFromFirmware(VOID);
static BOOLEAN UefiConsFirmwarePutChar(int c);
static BOOLEAN KeyQueueIsEmpty(VOID);
static BOOLEAN KeyQueuePush(INT Value);
static BOOLEAN KeyQueuePop(INT *Value);
static VOID UefiConsPumpKeys(BOOLEAN WaitForKey);

/* FUNCTIONS ******************************************************************/

VOID
UefiConsPutChar(int c)
{
    /* Use firmware text output while boot services are still available. */
    if (UefiConsFirmwarePutChar(c))
        return;

    if (UefiGopConsoleIsInitialized())
    {
        UefiGopConsolePutChar((CHAR)c);
        return;
    }

    ULONG Width, Height, Unused;
    BOOLEAN NeedScroll;

    UefiVideoGetDisplaySize(&Width, &Height, &Unused);

    NeedScroll = (CurrentCursorY >= Height);
    if (NeedScroll)
    {
        UefiVideoScrollUp();
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
UCHAR
ConvertToBiosExtValue(UCHAR KeyIn)
{
    switch (KeyIn)
    {
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

static BOOLEAN
KeyQueueIsFull(VOID)
{
    return ((KeyQueueTail + 1) % KEY_QUEUE_CAPACITY) == KeyQueueHead;
}

static BOOLEAN
KeyQueueIsEmpty(VOID)
{
    return KeyQueueHead == KeyQueueTail;
}

static BOOLEAN
KeyQueuePush(INT Value)
{
    UINT8 NextTail;

    NextTail = (KeyQueueTail + 1) % KEY_QUEUE_CAPACITY;
    if (NextTail == KeyQueueHead)
    {
        /* Queue full, drop newest key. */
        return FALSE;
    }

    KeyQueue[KeyQueueTail] = Value;
    KeyQueueTail = NextTail;
    return TRUE;
}

static BOOLEAN
KeyQueuePop(INT *Value)
{
    if (KeyQueueIsEmpty())
        return FALSE;

    *Value = KeyQueue[KeyQueueHead];
    KeyQueueHead = (KeyQueueHead + 1) % KEY_QUEUE_CAPACITY;
    return TRUE;
}

static VOID
KeyQueueStoreKey(EFI_INPUT_KEY *InputKey)
{
    if (InputKey->UnicodeChar != 0)
    {
        KeyQueuePush((INT)InputKey->UnicodeChar);
    }
    else if (InputKey->ScanCode != 0)
    {
        UCHAR Ext;

        Ext = ConvertToBiosExtValue(InputKey->ScanCode);
        if (Ext != 0)
        {
            if (!KeyQueuePush(KEY_EXTENDED))
                return;

            KeyQueuePush((INT)Ext);
        }
    }
}

static BOOLEAN
BootServicesAvailable(VOID)
{
    return (!BootServicesExited &&
            GlobalSystemTable != NULL &&
            GlobalSystemTable->BootServices != NULL);
}

static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*
UefiGetConOut(VOID)
{
    if (GlobalSystemTable == NULL)
        return NULL;

    return GlobalSystemTable->ConOut;
}

static VOID
UefiConsSyncCursorFromFirmware(VOID)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;

    if (!BootServicesAvailable())
        return;

    ConOut = UefiGetConOut();
    if (ConOut == NULL || ConOut->Mode == NULL)
        return;

    CurrentCursorX = ConOut->Mode->CursorColumn;
    CurrentCursorY = ConOut->Mode->CursorRow;
}

static EFI_STATUS
UefiConsFirmwareWriteSpaces(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut, UINTN Count)
{
    static const CHAR16 Space[] = { L' ', 0 };
    EFI_STATUS Status = EFI_SUCCESS;

    while (Count-- > 0 && !EFI_ERROR(Status))
    {
        Status = ConOut->OutputString(ConOut, (CHAR16*)Space);
    }

    return Status;
}

static EFI_STATUS
UefiConsFirmwareHandleTab(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut)
{
    UINTN Column, Spaces;

    if (ConOut->Mode == NULL)
        return EFI_DEVICE_ERROR;

    Column = ConOut->Mode->CursorColumn;
    Spaces = 8 - (Column & 7);
    if (Spaces == 0)
        Spaces = 8;

    return UefiConsFirmwareWriteSpaces(ConOut, Spaces);
}

static BOOLEAN
UefiConsFirmwarePutChar(int c)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_STATUS Status;
    CHAR16 Buffer[2];

    if (!BootServicesAvailable())
        return FALSE;

    /* Skip firmware console when no display is attached -- some firmware
     * implementations divide by zero in OutputString on headless systems.
     * Once the GOP console is initialised we render text ourselves. */
    if (UefiGopConsoleIsInitialized() || !UefiIsFramebufferReady())
        return FALSE;

    ConOut = UefiGetConOut();
    if (ConOut == NULL || ConOut->OutputString == NULL)
        return FALSE;

    switch (c)
    {
        case '\n':
        {
            static const CHAR16 NewLine[] = { L'\r', L'\n', 0 };
            Status = ConOut->OutputString(ConOut, (CHAR16*)NewLine);
            break;
        }
        case '\r':
        {
            static const CHAR16 CarriageReturn[] = { L'\r', 0 };
            Status = ConOut->OutputString(ConOut, (CHAR16*)CarriageReturn);
            break;
        }
        case '\b':
        {
            static const CHAR16 Backspace[] = { L'\b', 0 };
            Status = ConOut->OutputString(ConOut, (CHAR16*)Backspace);
            break;
        }
        case '\t':
            Status = UefiConsFirmwareHandleTab(ConOut);
            break;
        default:
            Buffer[0] = (CHAR16)(UINT8)c;
            Buffer[1] = 0;
            Status = ConOut->OutputString(ConOut, Buffer);
            break;
    }

    if (EFI_ERROR(Status))
        return FALSE;

    UefiConsSyncCursorFromFirmware();
    return TRUE;
}

static VOID
UefiConsPumpKeys(BOOLEAN WaitForKey)
{
    EFI_STATUS Status;
    EFI_INPUT_KEY TempKey;
    BOOLEAN ReceivedAny = FALSE;
    BOOLEAN BootServicesReady;

    if (GlobalSystemTable == NULL || GlobalSystemTable->ConIn == NULL)
        return;

    BootServicesReady = BootServicesAvailable();

    while (!KeyQueueIsFull())
    {
        if (BootServicesReady)
        {
            EFI_EVENT WaitEvent;
            UINTN EventIndex;

            WaitEvent = GlobalSystemTable->ConIn->WaitForKey;

            if (WaitForKey && !ReceivedAny)
            {
                Status = GlobalSystemTable->BootServices->WaitForEvent(1, &WaitEvent, &EventIndex);
                if (EFI_ERROR(Status))
                    break;
            }
            else
            {
                Status = GlobalSystemTable->BootServices->CheckEvent(WaitEvent);
                if (Status == EFI_NOT_READY)
                    break;
                if (EFI_ERROR(Status))
                    break;
            }

            Status = GlobalSystemTable->ConIn->ReadKeyStroke(GlobalSystemTable->ConIn, &TempKey);
            if (Status == EFI_NOT_READY)
            {
                if (WaitForKey && !ReceivedAny)
                    continue;
                break;
            }

            if (EFI_ERROR(Status))
                break;
        }
        else
        {
            Status = GlobalSystemTable->ConIn->ReadKeyStroke(GlobalSystemTable->ConIn, &TempKey);
            if (Status == EFI_NOT_READY)
            {
                if (WaitForKey && !ReceivedAny)
                    continue;
                break;
            }

            if (EFI_ERROR(Status))
                break;
        }

        KeyQueueStoreKey(&TempKey);
        ReceivedAny = TRUE;
        WaitForKey = FALSE; /* Drain additional keys without blocking */
    }
}

BOOLEAN
UefiConsKbHit(VOID)
{
    UefiConsPumpKeys(FALSE);

    return !KeyQueueIsEmpty();
}

int
UefiConsGetCh(VOID)
{
    INT KeyOutput;

    while (!KeyQueuePop(&KeyOutput))
    {
        UefiConsPumpKeys(TRUE);
    }

    return KeyOutput;
}

/* Record that ExitBootServices has been called so we can switch to GOP output. */
VOID
UefiConsMarkBootServicesExited(VOID)
{
    BootServicesExited = TRUE;
    
    /* Reposition the cursor if the GOP console is already initialised. */
    if (UefiGopConsoleIsInitialized())
    {
        UefiGopConsoleSetCursor(CurrentCursorX, CurrentCursorY);
    }
}
