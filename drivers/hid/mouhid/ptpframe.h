/*
 * PROJECT:     ReactOS HID Mouse Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows Precision Touchpad input-frame assembly
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define MOUHID_PTP_MAX_CONTACTS             5
#define MOUHID_PTP_BUTTON_INTEGRATED        0x01
#define MOUHID_PTP_BUTTON_EXTERNAL_PRIMARY  0x02
#define MOUHID_PTP_BUTTON_EXTERNAL_SECONDARY 0x04

typedef struct _MOUHID_PTP_CONTACT
{
    ULONG Id;
    ULONG X;
    ULONG Y;
    BOOLEAN Tip;
    BOOLEAN Confidence;
} MOUHID_PTP_CONTACT, *PMOUHID_PTP_CONTACT;

typedef struct _MOUHID_PTP_PACKET
{
    ULONG ContactCount;
    ULONG SlotCount;
    ULONG ScanTime;
    UCHAR Buttons;
    MOUHID_PTP_CONTACT Contacts[MOUHID_PTP_MAX_CONTACTS];
} MOUHID_PTP_PACKET, *PMOUHID_PTP_PACKET;

typedef struct _MOUHID_PTP_FRAME
{
    ULONG ContactCount;
    USHORT ScanTime;
    UCHAR Buttons;
    MOUHID_PTP_CONTACT Contacts[MOUHID_PTP_MAX_CONTACTS];
} MOUHID_PTP_FRAME, *PMOUHID_PTP_FRAME;

typedef struct _MOUHID_PTP_FRAME_ASSEMBLER
{
    BOOLEAN Active;
    ULONG ExpectedContactCount;
    ULONG ContactCount;
    USHORT ScanTime;
    UCHAR Buttons;
    MOUHID_PTP_CONTACT Contacts[MOUHID_PTP_MAX_CONTACTS];
} MOUHID_PTP_FRAME_ASSEMBLER, *PMOUHID_PTP_FRAME_ASSEMBLER;

typedef enum _MOUHID_PTP_FRAME_RESULT
{
    MouHidPtpFrameInvalid,
    MouHidPtpFramePending,
    MouHidPtpFrameComplete
} MOUHID_PTP_FRAME_RESULT;

static __inline
USHORT
MouHidPtpGetButtonFlags(
    _In_ UCHAR PreviousButtons,
    _In_ UCHAR Buttons)
{
    BOOLEAN CurrentLeft, CurrentRight, PreviousLeft, PreviousRight;
    USHORT ButtonFlags = 0;

    CurrentLeft = (Buttons & (MOUHID_PTP_BUTTON_INTEGRATED |
                              MOUHID_PTP_BUTTON_EXTERNAL_PRIMARY)) != 0;
    PreviousLeft =
        (PreviousButtons &
         (MOUHID_PTP_BUTTON_INTEGRATED |
          MOUHID_PTP_BUTTON_EXTERNAL_PRIMARY)) != 0;
    CurrentRight =
        (Buttons & MOUHID_PTP_BUTTON_EXTERNAL_SECONDARY) != 0;
    PreviousRight =
        (PreviousButtons & MOUHID_PTP_BUTTON_EXTERNAL_SECONDARY) != 0;

    if (CurrentLeft != PreviousLeft)
    {
        ButtonFlags |= CurrentLeft ? MOUSE_LEFT_BUTTON_DOWN :
                                     MOUSE_LEFT_BUTTON_UP;
    }
    if (CurrentRight != PreviousRight)
    {
        ButtonFlags |= CurrentRight ? MOUSE_RIGHT_BUTTON_DOWN :
                                      MOUSE_RIGHT_BUTTON_UP;
    }

    return ButtonFlags;
}

static __inline
VOID
MouHidPtpResetFrameAssembler(
    _Out_ PMOUHID_PTP_FRAME_ASSEMBLER Assembler)
{
    RtlZeroMemory(Assembler, sizeof(*Assembler));
}

static __inline
MOUHID_PTP_FRAME_RESULT
MouHidPtpAssembleFrame(
    _Inout_ PMOUHID_PTP_FRAME_ASSEMBLER Assembler,
    _In_ PMOUHID_PTP_PACKET Packet,
    _Out_ PMOUHID_PTP_FRAME Frame)
{
    ULONG ContactIndex, PacketContactCount, RemainingCount;

    RtlZeroMemory(Frame, sizeof(*Frame));

    if (Packet->SlotCount == 0 ||
        Packet->SlotCount > MOUHID_PTP_MAX_CONTACTS ||
        Packet->ContactCount > MOUHID_PTP_MAX_CONTACTS ||
        Packet->ScanTime > MAXUSHORT)
    {
        MouHidPtpResetFrameAssembler(Assembler);
        return MouHidPtpFrameInvalid;
    }

    if (!Assembler->Active)
    {
        if (Packet->ContactCount == 0)
        {
            Frame->ScanTime = (USHORT)Packet->ScanTime;
            Frame->Buttons = Packet->Buttons;
            return MouHidPtpFrameComplete;
        }

        Assembler->Active = TRUE;
        Assembler->ExpectedContactCount = Packet->ContactCount;
        Assembler->ContactCount = 0;
        Assembler->ScanTime = (USHORT)Packet->ScanTime;
        Assembler->Buttons = Packet->Buttons;
    }
    else if (Packet->ContactCount != 0 ||
             Packet->ScanTime != Assembler->ScanTime)
    {
        MouHidPtpResetFrameAssembler(Assembler);
        return MouHidPtpFrameInvalid;
    }

    if (Assembler->ContactCount > Assembler->ExpectedContactCount)
    {
        MouHidPtpResetFrameAssembler(Assembler);
        return MouHidPtpFrameInvalid;
    }

    RemainingCount = Assembler->ExpectedContactCount -
                     Assembler->ContactCount;
    PacketContactCount = Packet->SlotCount;
    if (PacketContactCount > RemainingCount)
        PacketContactCount = RemainingCount;

    for (ContactIndex = 0;
         ContactIndex < PacketContactCount;
         ContactIndex++)
    {
        ULONG ExistingIndex;

        for (ExistingIndex = 0;
             ExistingIndex < Assembler->ContactCount;
             ExistingIndex++)
        {
            if (Assembler->Contacts[ExistingIndex].Id ==
                Packet->Contacts[ContactIndex].Id)
            {
                MouHidPtpResetFrameAssembler(Assembler);
                return MouHidPtpFrameInvalid;
            }
        }

        Assembler->Contacts[Assembler->ContactCount++] =
            Packet->Contacts[ContactIndex];
    }

    if (Assembler->ContactCount != Assembler->ExpectedContactCount)
        return MouHidPtpFramePending;

    Frame->ContactCount = Assembler->ContactCount;
    Frame->ScanTime = Assembler->ScanTime;
    Frame->Buttons = Assembler->Buttons;
    RtlCopyMemory(Frame->Contacts,
                  Assembler->Contacts,
                  Frame->ContactCount * sizeof(Frame->Contacts[0]));
    MouHidPtpResetFrameAssembler(Assembler);
    return MouHidPtpFrameComplete;
}
