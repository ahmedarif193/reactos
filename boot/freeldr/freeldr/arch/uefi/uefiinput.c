/*
 * PROJECT:     LiberNT Boot Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI pointer input and menu timers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <uefildr.h>

typedef struct _EFI_SIMPLE_POINTER_PROTOCOL EFI_SIMPLE_POINTER_PROTOCOL;
typedef struct _EFI_ABSOLUTE_POINTER_PROTOCOL EFI_ABSOLUTE_POINTER_PROTOCOL;

typedef struct
{
    INT32 RelativeMovementX;
    INT32 RelativeMovementY;
    INT32 RelativeMovementZ;
    BOOLEAN LeftButton;
    BOOLEAN RightButton;
} EFI_SIMPLE_POINTER_STATE;

typedef struct
{
    UINT64 ResolutionX;
    UINT64 ResolutionY;
    UINT64 ResolutionZ;
    BOOLEAN LeftButton;
    BOOLEAN RightButton;
} EFI_SIMPLE_POINTER_MODE;

struct _EFI_SIMPLE_POINTER_PROTOCOL
{
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_POINTER_PROTOCOL*, BOOLEAN);
    EFI_STATUS (EFIAPI *GetState)(EFI_SIMPLE_POINTER_PROTOCOL*, EFI_SIMPLE_POINTER_STATE*);
    EFI_EVENT WaitForInput;
    EFI_SIMPLE_POINTER_MODE* Mode;
};

typedef struct
{
    UINT64 AbsoluteMinX;
    UINT64 AbsoluteMinY;
    UINT64 AbsoluteMinZ;
    UINT64 AbsoluteMaxX;
    UINT64 AbsoluteMaxY;
    UINT64 AbsoluteMaxZ;
    UINT32 Attributes;
} EFI_ABSOLUTE_POINTER_MODE;

typedef struct
{
    UINT64 CurrentX;
    UINT64 CurrentY;
    UINT64 CurrentZ;
    UINT32 ActiveButtons;
} EFI_ABSOLUTE_POINTER_STATE;

struct _EFI_ABSOLUTE_POINTER_PROTOCOL
{
    EFI_STATUS (EFIAPI *Reset)(EFI_ABSOLUTE_POINTER_PROTOCOL*, BOOLEAN);
    EFI_STATUS (EFIAPI *GetState)(EFI_ABSOLUTE_POINTER_PROTOCOL*, EFI_ABSOLUTE_POINTER_STATE*);
    EFI_EVENT WaitForInput;
    EFI_ABSOLUTE_POINTER_MODE* Mode;
};

#define EFI_ABSP_TouchActive 0x00000001
#define EFI_ABS_AltActive 0x00000002

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
static EFI_SIMPLE_POINTER_PROTOCOL** SimplePointers;
static EFI_ABSOLUTE_POINTER_PROTOCOL** AbsolutePointers;
static UINTN SimplePointerCount, AbsolutePointerCount;
static EFI_EVENT InputTimer;
static BOOLEAN PointerInitialized;
static ULONG PointerX, PointerY;
static INT64 RemainderX, RemainderY;

static UINTN
UefiLocatePointers(EFI_GUID* Guid, PVOID** Protocols)
{
    EFI_BOOT_SERVICES* Services = GlobalSystemTable->BootServices;
    EFI_HANDLE* Handles;
    PVOID Protocol;
    UINTN Count, I, Found = 0;
    if (EFI_ERROR(Services->LocateHandleBuffer(ByProtocol, Guid, NULL, &Count, &Handles)))
        return 0;
    for (I = 0; I < Count; ++I)
    {
        if (Count > 1 && Handles[I] == GlobalSystemTable->ConsoleInHandle)
            continue;
        if (!EFI_ERROR(Services->HandleProtocol(Handles[I], Guid, &Protocol)))
            Handles[Found++] = Protocol;
    }
    if (Found)
        *Protocols = (PVOID*)Handles;
    else
        Services->FreePool(Handles);
    return Found;
}

static ULONG
UefiPointerPosition(UINT64 Value, UINT64 Minimum, UINT64 Maximum, ULONG Extent)
{
    UINT64 Range = Maximum - Minimum;
    if (Value <= Minimum)
        return 0;
    if (Value >= Maximum)
        return Extent - 1;
    Value -= Minimum;
    while (Range > MAXULONG)
    {
        Range >>= 1;
        Value >>= 1;
    }
    return (ULONG)(Value * (Extent - 1) / Range);
}

static ULONG
UefiPointerMove(ULONG Position, INT32 Movement, UINT64 Resolution,
                ULONG Extent, INT64* Remainder)
{
    INT64 Total, Divisor, Next;
    if (!Resolution)
        return Position;
    Divisor = min(Resolution, MAXLONG);
    Total = (INT64)Movement * 16 + *Remainder;
    Next = (INT64)Position + Total / Divisor;
    *Remainder = Total % Divisor;
    return (ULONG)max(0, min(Next, (INT64)Extent - 1));
}

BOOLEAN
UefiGetPointerState(ULONG Width, ULONG Height, PMACH_POINTER_STATE State)
{
    static EFI_GUID SimpleGuid = {0x31878c87, 0x0b75, 0x11d5, {0x9a, 0x4f, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};
    static EFI_GUID AbsoluteGuid = {0x8d59d32b, 0xc655, 0x4ae9, {0x9b, 0x15, 0xf2, 0x59, 0x04, 0x99, 0x2a, 0x43}};
    EFI_ABSOLUTE_POINTER_STATE Absolute;
    EFI_SIMPLE_POINTER_STATE Relative;
    EFI_ABSOLUTE_POINTER_MODE* Mode;
    EFI_ABSOLUTE_POINTER_PROTOCOL* AbsolutePointer;
    EFI_SIMPLE_POINTER_PROTOCOL* SimplePointer;
    UINTN I;
    if (!Width || !Height)
        return FALSE;
    if (!PointerInitialized)
    {
        PointerInitialized = TRUE;
        PointerX = Width / 2;
        PointerY = Height / 2;
        SimplePointerCount = UefiLocatePointers(&SimpleGuid, (PVOID**)&SimplePointers);
        AbsolutePointerCount = UefiLocatePointers(&AbsoluteGuid, (PVOID**)&AbsolutePointers);
    }
    RtlZeroMemory(State, sizeof(*State));
    for (I = 0; I < AbsolutePointerCount; ++I)
    {
        AbsolutePointer = AbsolutePointers[I];
        Mode = AbsolutePointer->Mode;
        if (!Mode || Mode->AbsoluteMaxX <= Mode->AbsoluteMinX || Mode->AbsoluteMaxY <= Mode->AbsoluteMinY)
            continue;
        if (EFI_ERROR(AbsolutePointer->GetState(AbsolutePointer, &Absolute)))
            continue;
        PointerX = UefiPointerPosition(Absolute.CurrentX, Mode->AbsoluteMinX, Mode->AbsoluteMaxX, Width);
        PointerY = UefiPointerPosition(Absolute.CurrentY, Mode->AbsoluteMinY, Mode->AbsoluteMaxY, Height);
        State->LeftButton = !!(Absolute.ActiveButtons & EFI_ABSP_TouchActive);
        State->RightButton = !!(Absolute.ActiveButtons & EFI_ABS_AltActive);
        State->Absolute = TRUE;
        State->X = PointerX;
        State->Y = PointerY;
        return TRUE;
    }
    for (I = 0; I < SimplePointerCount; ++I)
    {
        SimplePointer = SimplePointers[I];
        if (!SimplePointer->Mode || EFI_ERROR(SimplePointer->GetState(SimplePointer, &Relative)))
            continue;
        PointerX = UefiPointerMove(PointerX, Relative.RelativeMovementX, SimplePointer->Mode->ResolutionX,
                                   Width, &RemainderX);
        PointerY = UefiPointerMove(PointerY, Relative.RelativeMovementY, SimplePointer->Mode->ResolutionY,
                                   Height, &RemainderY);
        State->LeftButton = Relative.LeftButton;
        State->RightButton = Relative.RightButton;
        State->Wheel = Relative.RelativeMovementZ;
        State->X = PointerX;
        State->Y = PointerY;
        return TRUE;
    }
    return FALSE;
}

BOOLEAN
UefiSetInputTimer(ULONG Milliseconds)
{
    EFI_BOOT_SERVICES* Services = GlobalSystemTable->BootServices;
    if (!Milliseconds)
    {
        if (InputTimer)
        {
            Services->CloseEvent(InputTimer);
            InputTimer = NULL;
        }
        return TRUE;
    }
    if (!InputTimer && EFI_ERROR(Services->CreateEvent(EVT_TIMER, TPL_APPLICATION, NULL, NULL, &InputTimer)))
        return FALSE;
    Services->CheckEvent(InputTimer);
    return !EFI_ERROR(Services->SetTimer(InputTimer, TimerRelative, (UINT64)Milliseconds * 10000));
}

BOOLEAN
UefiInputTimerExpired(VOID)
{
    return InputTimer && GlobalSystemTable->BootServices->CheckEvent(InputTimer) == EFI_SUCCESS;
}
