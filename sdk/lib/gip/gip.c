/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 ReactOS contributors
 * MS-GIPUSB framing, host receive engine and device command codecs.
 */
#include <gip.h>
#include <string.h>

static uint16_t
GipRead16(const uint8_t *Data)
{
    return (uint16_t)(Data[0] | ((uint16_t)Data[1] << 8));
}

static void
GipWrite16(uint8_t *Data, uint32_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8);
}

static GIP_RESULT
GipReadLength(const uint8_t *Buffer, size_t Length, size_t *Offset, uint32_t *Value)
{
    unsigned int Index;
    *Value = 0;
    for (Index = 0; Index < 4; ++Index)
    {
        uint8_t Byte;
        if (*Offset == Length)
            return GipInvalidPacket;
        Byte = Buffer[(*Offset)++];
        *Value |= (uint32_t)(Byte & 0x7f) << (Index * 7);
        if (!(Byte & 0x80))
            return GipSuccess;
    }
    return GipInvalidPacket;
}

static size_t
GipWriteLength(uint8_t *Buffer, uint32_t Value)
{
    size_t Count = 0;
    do
    {
        Buffer[Count] = (uint8_t)(Value & 0x7f);
        Value >>= 7;
        if (Value)
            Buffer[Count] |= 0x80;
        ++Count;
    } while (Value);
    return Count;
}

static size_t
GipMtu(uint8_t Type)
{
    return (Type & 0xe0) == 0x60 ? GIP_AUDIO_MTU : GIP_DATA_MTU;
}

static int
GipValidHeader(const GIP_PACKET *Packet)
{
    return Packet->Type < 0x80 && Packet->Sequence != 0 &&
           !(Packet->Flags & 0x08) &&
           (!(Packet->Flags & GIP_FLAG_FIRST) || (Packet->Flags & GIP_FLAG_FRAGMENT));
}

GIP_RESULT
GipDecodePacket(const uint8_t *Buffer, size_t Length, GIP_PACKET *Packet, size_t *Consumed)
{
    GIP_PACKET Result;
    size_t Offset = 3;
    if (!Buffer || !Packet || !Consumed)
        return GipInvalidParameter;
    *Consumed = 0;
    if (Length < 4)
        return GipInvalidPacket;
    memset(&Result, 0, sizeof(Result));
    Result.Type = Buffer[0];
    Result.Flags = Buffer[1];
    Result.Sequence = Buffer[2];
    if (!GipValidHeader(&Result) ||
        GipReadLength(Buffer, Length, &Offset, &Result.Length) != GipSuccess)
        return GipInvalidPacket;
    if ((Result.Flags & GIP_FLAG_FRAGMENT) &&
        GipReadLength(Buffer, Length, &Offset, &Result.TotalOrOffset) != GipSuccess)
        return GipInvalidPacket;
    if (Result.Length > Length - Offset || Offset + Result.Length > GipMtu(Result.Type))
        return GipInvalidPacket;
    Result.Data = Buffer + Offset;
    *Packet = Result;
    *Consumed = Offset + Result.Length;
    return GipSuccess;
}

GIP_RESULT
GipEncodePacket(const GIP_PACKET *Packet, uint8_t *Buffer, size_t Capacity, size_t *Written)
{
    uint8_t Header[12];
    size_t Offset = 3, LengthEnd;
    if (!Packet || !Buffer || !Written)
        return GipInvalidParameter;
    *Written = 0;
    if (!GipValidHeader(Packet) || Packet->Length > GIP_MAX_MESSAGE ||
        Packet->TotalOrOffset > GIP_MAX_MESSAGE || (Packet->Length && !Packet->Data))
        return GipInvalidParameter;
    Header[0] = Packet->Type;
    Header[1] = Packet->Flags;
    Header[2] = Packet->Sequence;
    Offset += GipWriteLength(Header + Offset, Packet->Length);
    LengthEnd = Offset;
    if (Packet->Flags & GIP_FLAG_FRAGMENT)
        Offset += GipWriteLength(Header + Offset, Packet->TotalOrOffset);
    /* Downstream headers must be even-sized. Extend the length field by one
     * zero group rather than adding a byte outside the header's fields. */
    if (Offset & 1)
    {
        memmove(Header + LengthEnd + 1, Header + LengthEnd, Offset - LengthEnd);
        Header[LengthEnd - 1] |= 0x80;
        Header[LengthEnd] = 0;
        ++Offset;
    }
    if (Offset + Packet->Length > GipMtu(Packet->Type))
        return GipBufferTooSmall;
    if (Offset + Packet->Length > Capacity)
        return GipBufferTooSmall;
    memcpy(Buffer, Header, Offset);
    if (Packet->Length)
        memcpy(Buffer + Offset, Packet->Data, Packet->Length);
    *Written = Offset + Packet->Length;
    return GipSuccess;
}

void
GipInitialize(GIP_CONTEXT *Context, GIP_SEND Send, GIP_MESSAGE Message, void *CallbackContext)
{
    memset(Context, 0, sizeof(*Context));
    Context->Send = Send;
    Context->Message = Message;
    Context->CallbackContext = CallbackContext;
}

static void
GipClearReceiver(GIP_RECEIVER *Receiver)
{
    uint8_t *Buffer = Receiver->Buffer;
    uint32_t Capacity = Receiver->Capacity;
    memset(Receiver, 0, sizeof(*Receiver));
    Receiver->Buffer = Buffer;
    Receiver->Capacity = Capacity;
}

void
GipReset(GIP_CONTEXT *Context)
{
    unsigned int Index;
    for (Index = 0; Index < GIP_DEVICE_COUNT; ++Index)
    {
        Context->Devices[Index].Present = 0;
        memset(Context->Devices[Index].Sequence, 0, sizeof(Context->Devices[Index].Sequence));
        GipClearReceiver(&Context->Devices[Index].Receiver);
    }
}

GIP_RESULT
GipSetReceiveBuffer(GIP_CONTEXT *Context, uint8_t Device, uint8_t *Buffer, size_t Capacity)
{
    GIP_RECEIVER *Receiver;
    if (!Context || Device >= GIP_DEVICE_COUNT || Capacity > GIP_MAX_MESSAGE || (Capacity && !Buffer))
        return GipInvalidParameter;
    Receiver = &Context->Devices[Device].Receiver;
    GipClearReceiver(Receiver);
    Receiver->Buffer = Buffer;
    Receiver->Capacity = (uint32_t)Capacity;
    return GipSuccess;
}

static GIP_RESULT
GipTransmit(GIP_CONTEXT *Context, const GIP_PACKET *Packet)
{
    size_t Length;
    GIP_RESULT Result;
    if (!Context->Send)
        return GipTransportError;
    Result = GipEncodePacket(Packet, Context->SendBuffer, sizeof(Context->SendBuffer), &Length);
    if (Result != GipSuccess)
        return Result;
    return Context->Send(Context->CallbackContext, Context->SendBuffer, Length);
}

GIP_RESULT
GipAllocateSequence(GIP_CONTEXT *Context, uint8_t Device, uint8_t Type,
                    uint8_t Flags, uint8_t *Sequence)
{
    uint8_t Pool;
    if (!Context || !Sequence || Device >= GIP_DEVICE_COUNT || Type >= 0x80 ||
        (Flags & ~GIP_FLAG_SYSTEM))
        return GipInvalidParameter;
    if (!Context->Devices[Device].Present)
        return GipNotReady;
    /* MS-GIPUSB table 15: ordinary system commands share a pool; security,
     * extended commands, audio data and vendor messages have individual pools. */
    Pool = Type;
    if (Flags & GIP_FLAG_SYSTEM)
    {
        Pool = (Type == GIP_MESSAGE_SECURITY || Type == GIP_MESSAGE_EXTENDED || Type >= 0x60)
                   ? (uint8_t)(Type | 0x80) : 0x80;
    }
    *Sequence = ++Context->Devices[Device].Sequence[Pool];
    if (!*Sequence)
        *Sequence = ++Context->Devices[Device].Sequence[Pool];
    return GipSuccess;
}

GIP_RESULT
GipSendMessage(GIP_CONTEXT *Context, uint8_t Device, uint8_t Type,
               uint8_t Flags, const uint8_t *Data, size_t Length)
{
    GIP_PACKET Packet;
    GIP_RESULT Result;
    uint8_t Sequence;
    if (Length > GipMtu(Type) - 4 || (Length && !Data))
        return GipInvalidParameter;
    Result = GipAllocateSequence(Context, Device, Type, Flags, &Sequence);
    if (Result != GipSuccess)
        return Result;
    memset(&Packet, 0, sizeof(Packet));
    Packet.Type = Type;
    Packet.Flags = Flags | Device;
    Packet.Sequence = Sequence;
    Packet.Length = (uint32_t)Length;
    Packet.Data = Data;
    return GipTransmit(Context, &Packet);
}

static GIP_RESULT
GipAcknowledge(GIP_CONTEXT *Context, const GIP_PACKET *Packet, uint32_t Received, uint32_t Remaining)
{
    uint8_t Payload[9] = {0};
    GIP_PACKET Ack;
    Payload[1] = Packet->Type;
    Payload[2] = GIP_FLAG_SYSTEM | (Packet->Flags & GIP_FLAG_DEVICE);
    GipWrite16(Payload + 3, Received);
    GipWrite16(Payload + 7, Remaining);
    memset(&Ack, 0, sizeof(Ack));
    Ack.Type = GIP_MESSAGE_ACK;
    Ack.Flags = Payload[2];
    Ack.Sequence = Packet->Sequence;
    Ack.Length = sizeof(Payload);
    Ack.Data = Payload;
    return GipTransmit(Context, &Ack);
}

static GIP_RESULT
GipDeliver(GIP_CONTEXT *Context, const GIP_PACKET *Packet)
{
    if ((Packet->Flags & GIP_FLAG_SYSTEM) && Packet->Type == GIP_MESSAGE_HELLO)
    {
        GIP_HELLO Hello;
        GIP_DEVICE *Device = &Context->Devices[Packet->Flags & GIP_FLAG_DEVICE];
        if (GipDecodeHello(Packet, &Hello) != GipSuccess)
            return GipInvalidPacket;
        GipClearReceiver(&Device->Receiver);
        Device->Present = 1;
        /* Deliver repeated Hellos too: a lost START must not strand a device. */
    }
    if (Context->Message)
        return Context->Message(Context->CallbackContext, Packet);
    return GipSuccess;
}

static GIP_RESULT
GipReceiveFragment(GIP_CONTEXT *Context, const GIP_PACKET *Packet, uint32_t TimeMs)
{
    GIP_RECEIVER *Receiver = &Context->Devices[Packet->Flags & GIP_FLAG_DEVICE].Receiver;
    GIP_PACKET Message;
    GIP_RESULT Result = GipSuccess;
    uint32_t Offset = Packet->TotalOrOffset;
    uint8_t Identity = Packet->Flags & (GIP_FLAG_DEVICE | GIP_FLAG_SYSTEM);
    int Same = Receiver->Active && Receiver->Type == Packet->Type &&
               Receiver->Sequence == Packet->Sequence && Receiver->Flags == Identity;

    if ((Packet->Flags & GIP_FLAG_SYSTEM) && Packet->Type == GIP_MESSAGE_HELLO)
        return GipInvalidPacket;

    if (Packet->Flags & GIP_FLAG_FIRST)
    {
        if (!Packet->Length || !Offset || Packet->Length > Offset)
            return GipInvalidPacket;
        if (!Same || Receiver->Total != Offset)
        {
            if (Receiver->Active)
                return GipNotReady;
            if (Offset > Receiver->Capacity)
                return GipBufferTooSmall;
            GipClearReceiver(Receiver);
            Receiver->Type = Packet->Type;
            Receiver->Flags = Identity;
            Receiver->Sequence = Packet->Sequence;
            Receiver->Total = Offset;
            Receiver->Active = 1;
        }
        Offset = 0;
    }
    else if (!Same)
    {
        return GipInvalidPacket;
    }

    if (Offset > Receiver->Total || Packet->Length > Receiver->Total - Offset)
        return GipInvalidPacket;
    /* Completion marker is a separate, empty final handshake packet. */
    if (!Packet->Length)
    {
        if (Offset != Receiver->Total || !Receiver->Delivered)
            return GipInvalidPacket;
        GipClearReceiver(Receiver);
        return GipSuccess;
    }

    Receiver->LastDataTime = TimeMs;
    Receiver->AckCount = 0;
    if (Offset == Receiver->Received)
    {
        memcpy(Receiver->Buffer + Offset, Packet->Data, Packet->Length);
        Receiver->Received += Packet->Length;
    }
    else if (Offset < Receiver->Received)
    {
        /* Retransmissions must match bytes already accepted. */
        if (Packet->Length > Receiver->Received - Offset ||
            memcmp(Receiver->Buffer + Offset, Packet->Data, Packet->Length))
            return GipInvalidPacket;
    }
    /* A gap is discarded; ACK the contiguous prefix so the sender retries. */
    if ((Packet->Flags & (GIP_FLAG_ACK | GIP_FLAG_FIRST)) ||
        Receiver->Received == Receiver->Total || TimeMs - Receiver->LastAckTime >= 100)
    {
        Result = GipAcknowledge(Context, Packet, Receiver->Received, Receiver->Total - Receiver->Received);
        if (Result != GipSuccess)
            return Result;
        Receiver->LastAckTime = TimeMs;
        ++Receiver->AckCount;
    }
    if (Receiver->Received == Receiver->Total && !Receiver->Delivered)
    {
        Message = *Packet;
        Message.Flags = Identity;
        Message.TotalOrOffset = 0;
        Message.Length = Receiver->Total;
        Message.Data = Receiver->Buffer;
        Result = GipDeliver(Context, &Message);
        if (Result == GipSuccess)
            Receiver->Delivered = 1;
    }
    return Result;
}

GIP_RESULT
GipReceive(GIP_CONTEXT *Context, const uint8_t *Buffer, size_t Length, uint32_t TimeMs)
{
    size_t Offset = 0, Consumed;
    GIP_PACKET Packet;
    GIP_RESULT Result;
    if (!Context || (!Buffer && Length))
        return GipInvalidParameter;
    while (Offset < Length)
    {
        Result = GipDecodePacket(Buffer + Offset, Length - Offset, &Packet, &Consumed);
        if (Result != GipSuccess)
            return Result;
        if (Packet.Flags & GIP_FLAG_FRAGMENT)
            Result = GipReceiveFragment(Context, &Packet, TimeMs);
        else
        {
            /* Protocol-control packets must not provoke an ACK loop. */
            if ((Packet.Flags & GIP_FLAG_ACK) &&
                !((Packet.Flags & GIP_FLAG_SYSTEM) && Packet.Type == GIP_MESSAGE_ACK))
            {
                Result = GipAcknowledge(Context, &Packet, Packet.Length, 0);
                if (Result != GipSuccess)
                    return Result;
            }
            Result = GipDeliver(Context, &Packet);
        }
        if (Result != GipSuccess)
            return Result;
        Offset += Consumed;
    }
    return GipSuccess;
}

GIP_RESULT
GipPoll(GIP_CONTEXT *Context, uint32_t TimeMs)
{
    unsigned int Index;
    if (!Context)
        return GipInvalidParameter;
    for (Index = 0; Index < GIP_DEVICE_COUNT; ++Index)
    {
        GIP_RECEIVER *Receiver = &Context->Devices[Index].Receiver;
        GIP_PACKET Packet;
        GIP_RESULT Result;
        if (!Receiver->Active)
            continue;
        if (TimeMs - Receiver->LastDataTime >= 1000)
        {
            GipClearReceiver(Receiver);
            continue;
        }
        if (Receiver->AckCount >= 8 || TimeMs - Receiver->LastAckTime < 100)
            continue;
        memset(&Packet, 0, sizeof(Packet));
        Packet.Type = Receiver->Type;
        Packet.Flags = Receiver->Flags;
        Packet.Sequence = Receiver->Sequence;
        Result = GipAcknowledge(Context, &Packet, Receiver->Received, Receiver->Total - Receiver->Received);
        if (Result != GipSuccess)
            return Result;
        Receiver->LastAckTime = TimeMs;
        ++Receiver->AckCount;
    }
    return GipSuccess;
}

GIP_RESULT
GipRequestMetadata(GIP_CONTEXT *Context, uint8_t Device)
{
    return GipSendMessage(Context, Device, GIP_MESSAGE_METADATA, GIP_FLAG_SYSTEM, NULL, 0);
}

GIP_RESULT
GipSetDeviceState(GIP_CONTEXT *Context, uint8_t Device, uint8_t State)
{
    if (State > GIP_STATE_RESET || State == 2 || State == 6)
        return GipInvalidParameter;
    return GipSendMessage(Context, Device, GIP_MESSAGE_STATE, GIP_FLAG_SYSTEM, &State, 1);
}

GIP_RESULT
GipSetLed(GIP_CONTEXT *Context, uint8_t Device, uint8_t Pattern, uint8_t Intensity)
{
    uint8_t Payload[3] = {0};
    if (Intensity > 47 || (Pattern > 4 && Pattern != 0x0d))
        return GipInvalidParameter;
    Payload[1] = Pattern;
    Payload[2] = Intensity;
    return GipSendMessage(Context, Device, GIP_MESSAGE_LED, GIP_FLAG_SYSTEM, Payload, sizeof(Payload));
}

static uint8_t
GipMotorPercent(uint16_t Intensity)
{
    return (uint8_t)(((uint32_t)Intensity * 100 + 32767) / 65535);
}

GIP_RESULT
GipSetMotors(GIP_CONTEXT *Context, uint8_t Device, uint8_t Mask,
             uint16_t LeftImpulse, uint16_t RightImpulse,
             uint16_t LeftMotor, uint16_t RightMotor,
             uint8_t Duration, uint8_t Delay, uint8_t Repeat)
{
    uint8_t Payload[9];
    if (Mask & 0xf0)
        return GipInvalidParameter;
    Payload[0] = 0;
    Payload[1] = Mask;
    Payload[2] = GipMotorPercent(LeftImpulse);
    Payload[3] = GipMotorPercent(RightImpulse);
    Payload[4] = GipMotorPercent(LeftMotor);
    Payload[5] = GipMotorPercent(RightMotor);
    Payload[6] = Duration;
    Payload[7] = Delay;
    Payload[8] = Repeat;
    return GipSendMessage(Context, Device, GIP_MESSAGE_MOTOR, 0, Payload, sizeof(Payload));
}

GIP_RESULT
GipSetAudioFormat(GIP_CONTEXT *Context, uint8_t Device, uint8_t Capture, uint8_t Render)
{
    uint8_t Payload[3] = {2, Capture, Render};
    if (Capture > 0x10 || Render > 0x10)
        return GipInvalidParameter;
    return GipSendMessage(Context, Device, GIP_MESSAGE_AUDIO_CONTROL, GIP_FLAG_SYSTEM, Payload, sizeof(Payload));
}

GIP_RESULT
GipDecodeHello(const GIP_PACKET *Packet, GIP_HELLO *Hello)
{
    unsigned int Index;
    if (!Packet || !Hello)
        return GipInvalidParameter;
    if (Packet->Type != GIP_MESSAGE_HELLO || !(Packet->Flags & GIP_FLAG_SYSTEM) ||
        Packet->Length != 28 || !Packet->Data)
        return GipInvalidPacket;
    memcpy(Hello->DeviceId, Packet->Data, sizeof(Hello->DeviceId));
    Hello->VendorId = GipRead16(Packet->Data + 8);
    Hello->ProductId = GipRead16(Packet->Data + 10);
    for (Index = 0; Index < 4; ++Index)
        Hello->Firmware[Index] = GipRead16(Packet->Data + 12 + Index * 2);
    return GipSuccess;
}

GIP_RESULT
GipDecodeGamepad(const GIP_PACKET *Packet, GIP_GAMEPAD *Gamepad)
{
    const uint8_t *Data;
    if (!Packet || !Gamepad)
        return GipInvalidParameter;
    if (Packet->Type != GIP_MESSAGE_INPUT || (Packet->Flags & GIP_FLAG_SYSTEM) ||
        Packet->Length < 14 || !Packet->Data)
        return GipInvalidPacket;
    Data = Packet->Data;
    if (GipRead16(Data + 2) > 1023 || GipRead16(Data + 4) > 1023)
        return GipInvalidPacket;
    Gamepad->Buttons = (uint16_t)((Data[1] & 0x0f) | ((Data[0] & 0x0c) << 2) |
                                 ((Data[1] & 0xc0)) | ((Data[1] & 0x30) << 4) |
                                 ((Data[0] & 0xf0) << 8));
    Gamepad->LeftTrigger = GipRead16(Data + 2);
    Gamepad->RightTrigger = GipRead16(Data + 4);
    Gamepad->LeftX = (int16_t)GipRead16(Data + 6);
    Gamepad->LeftY = (int16_t)GipRead16(Data + 8);
    Gamepad->RightX = (int16_t)GipRead16(Data + 10);
    Gamepad->RightY = (int16_t)GipRead16(Data + 12);
    return GipSuccess;
}

GIP_RESULT
GipDecodeGuide(const GIP_PACKET *Packet, uint8_t *Pressed)
{
    if (!Packet || !Pressed)
        return GipInvalidParameter;
    if (Packet->Type != GIP_MESSAGE_GUIDE || !(Packet->Flags & GIP_FLAG_SYSTEM) ||
        Packet->Length != 2 || !Packet->Data || Packet->Data[1] != 0x5b)
        return GipInvalidPacket;
    *Pressed = !!(Packet->Data[0] & 1);
    return GipSuccess;
}
