/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 ReactOS contributors
 * Wire-vector and receive-state regression tests for the actual library.
 */
#include <gip.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct TEST
{
    GIP_CONTEXT Gip;
    unsigned int Sends, Messages;
    int FailSend, StartOnHello;
    uint8_t Sent[GIP_AUDIO_MTU], Received[GIP_MAX_MESSAGE];
    size_t SentLength;
    GIP_PACKET LastMessage;
} TEST;

static GIP_RESULT
Send(void *Opaque, const uint8_t *Data, size_t Length)
{
    TEST *Test = Opaque;
    if (Test->FailSend)
        return GipTransportError;
    assert(Length <= sizeof(Test->Sent));
    memcpy(Test->Sent, Data, Length);
    Test->SentLength = Length;
    ++Test->Sends;
    return GipSuccess;
}

static GIP_RESULT
Message(void *Opaque, const GIP_PACKET *Packet)
{
    TEST *Test = Opaque;
    ++Test->Messages;
    Test->LastMessage = *Packet;
    assert(Packet->Length <= sizeof(Test->Received));
    memcpy(Test->Received, Packet->Data, Packet->Length);
    if (Test->StartOnHello && Packet->Type == GIP_MESSAGE_HELLO)
        return GipSetDeviceState(&Test->Gip, Packet->Flags & GIP_FLAG_DEVICE, GIP_STATE_START);
    return GipSuccess;
}

static void
Initialize(TEST *Test)
{
    memset(Test, 0, sizeof(*Test));
    GipInitialize(&Test->Gip, Send, Message, Test);
}

static void
Hello(TEST *Test, uint8_t Device)
{
    /* MS-GIPUSB table 27, 28-byte Hello payload. */
    uint8_t Packet[32] = {0x02, 0x20, 0x01, 0x1c,
        0x12, 0x34, 0x56, 0x78, 0xfb, 0xff, 0, 0,
        0x5e, 0x04, 0x12, 0x0b, 0x05, 0, 0x0b, 0,
        0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0};
    Packet[1] |= Device;
    assert(GipReceive(&Test->Gip, Packet, sizeof(Packet), 0) == GipSuccess);
}

static void
TestHelloAndSequences(void)
{
    TEST Test;
    GIP_HELLO Info;
    GIP_PACKET Packet;
    size_t Consumed;
    unsigned int Index;
    const uint8_t Start[] = {0x05, 0x20, 0x01, 0x01, 0x00};
    Initialize(&Test);
    Test.StartOnHello = 1;
    assert(GipSetDeviceState(&Test.Gip, 0, GIP_STATE_START) == GipNotReady);
    assert(Test.Sends == 0);
    Hello(&Test, 0);
    assert(Test.Sends == 1 && Test.SentLength == sizeof(Start));
    assert(!memcmp(Test.Sent, Start, sizeof(Start)));
    Packet = Test.LastMessage;
    Packet.Data = Test.Received;
    assert(GipDecodeHello(&Packet, &Info) == GipSuccess);
    assert(Info.VendorId == 0x045e && Info.ProductId == 0x0b12);
    /* Repeated Hello must resend START even after a prior successful send. */
    Hello(&Test, 0);
    assert(Test.Sends == 2 && Test.Sent[2] == 2);
    for (Index = 3; Index <= 513; ++Index)
    {
        assert(GipSetDeviceState(&Test.Gip, 0, GIP_STATE_START) == GipSuccess);
        assert(Test.Sent[2] == (Index - 1) % 255 + 1);
        assert(GipDecodePacket(Test.Sent, Test.SentLength, &Packet, &Consumed) == GipSuccess);
    }
    assert(GipSetMotors(&Test.Gip, 0, 3, 0, 0, 0, 0, 0, 0, 0) == GipSuccess);
    assert(Test.Sent[2] == 1); /* Vendor motor messages have their own pool. */
    Hello(&Test, 3);
    assert(Test.Sent[1] == 0x23 && Test.Sent[2] == 1);
    assert(GipSetDeviceState(&Test.Gip, 3, 6) == GipInvalidParameter);
    GipReset(&Test.Gip);
    assert(GipSetDeviceState(&Test.Gip, 0, 0) == GipNotReady);
    Test.FailSend = 1;
    {
        uint8_t HelloPacket[32] = {2, 0x20, 1, 28};
        assert(GipReceive(&Test.Gip, HelloPacket, sizeof(HelloPacket), 0) == GipTransportError);
    }
    Test.FailSend = 0;
    Hello(&Test, 0);
    assert(Test.Sent[2] == 2); /* Failed submission does not reuse its sequence. */
}

static void
TestCommands(void)
{
    TEST Test;
    unsigned int Value;
    Initialize(&Test);
    Hello(&Test, 0);
    assert(GipSetMotors(&Test.Gip, 0, 0x0f, 0, 32768, 65535, 65535, 255, 0, 255) == GipSuccess);
    {
        const uint8_t Expected[] = {9, 0, 1, 9, 0, 15, 0, 50, 100, 100, 255, 0, 255};
        assert(Test.SentLength == sizeof(Expected));
        assert(!memcmp(Test.Sent, Expected, sizeof(Expected)));
    }
    /* Exhaust the public 16-bit range, checking range and monotonicity. */
    for (Value = 0; Value <= 65535; ++Value)
    {
        unsigned int Previous = Value ? Test.Sent[8] : 0;
        assert(GipSetMotors(&Test.Gip, 0, 3, 0, 0, (uint16_t)Value, 0, 1, 0, 0) == GipSuccess);
        assert(Test.Sent[8] >= Previous && Test.Sent[8] <= 100);
    }
    assert(Test.Sent[8] == 100);
    assert(GipSetMotors(&Test.Gip, 0, 0x80, 0, 0, 0, 0, 0, 0, 0) == GipInvalidParameter);
    assert(GipSetLed(&Test.Gip, 0, 1, 20) == GipSuccess);
    assert(Test.Sent[0] == 0x0a && Test.Sent[5] == 1 && Test.Sent[6] == 20);
    assert(GipSetLed(&Test.Gip, 0, 1, 48) == GipInvalidParameter);
    assert(GipRequestMetadata(&Test.Gip, 0) == GipSuccess);
    assert(Test.SentLength == 4 && Test.Sent[0] == 4 && Test.Sent[3] == 0);
    assert(GipSetAudioFormat(&Test.Gip, 0, 0x0f, 0x10) == GipSuccess);
    assert(Test.Sent[4] == 2 && Test.Sent[5] == 15 && Test.Sent[6] == 16);
    assert(GipSetAudioFormat(&Test.Gip, 0, 0x11, 0) == GipInvalidParameter);
}

static void
TestFramingAndInput(void)
{
    TEST Test;
    GIP_PACKET Packet, Decoded;
    GIP_GAMEPAD Gamepad;
    size_t Consumed, Written, Length;
    uint8_t Pressed;
    uint8_t Buffer[GIP_AUDIO_MTU], Payload[2000];
    const uint8_t Input[] = {0x20, 0, 1, 14, 0xfc, 0xff, 0xff, 3, 0, 0,
                            0, 0x80, 0xff, 0x7f, 0xff, 0xff, 0, 0};
    const uint8_t Coalesced[] = {7, 0x32, 42, 2, 1, 0x5b, 7, 0x22, 43, 2, 0, 0x5b};
    const uint8_t Ack[] = {1, 0x22, 42, 9, 0, 7, 0x22, 2, 0, 0, 0, 0, 0};
    Initialize(&Test);
    assert(GipDecodePacket(Input, sizeof(Input), &Packet, &Consumed) == GipSuccess);
    assert(Consumed == sizeof(Input));
    assert(GipDecodeGamepad(&Packet, &Gamepad) == GipSuccess);
    assert(Gamepad.Buttons == 0xf3ff && Gamepad.LeftTrigger == 1023 && Gamepad.RightTrigger == 0);
    assert(Gamepad.LeftX == -32768 && Gamepad.LeftY == 32767 && Gamepad.RightX == -1 && Gamepad.RightY == 0);
    for (Length = 0; Length < sizeof(Input); ++Length)
        assert(GipDecodePacket(Input, Length, &Packet, &Consumed) == GipInvalidPacket);
    assert(GipReceive(&Test.Gip, Coalesced, sizeof(Coalesced), 0) == GipSuccess);
    assert(Test.Messages == 2 && Test.Sends == 1);
    assert(Test.SentLength == sizeof(Ack) && !memcmp(Test.Sent, Ack, sizeof(Ack)));
    Packet = Test.LastMessage;
    Packet.Data = Test.Received;
    assert(GipDecodeGuide(&Packet, &Pressed) == GipSuccess && !Pressed);
    {
        const uint8_t BadLength[] = {0x20, 0, 1, 0x80, 0x80, 0x80, 0x80, 0};
        const uint8_t BadFlags[] = {0x20, 0x40, 1, 0};
        const uint8_t ZeroSequence[] = {0x20, 0, 0, 0};
        assert(GipReceive(&Test.Gip, BadLength, sizeof(BadLength), 0) == GipInvalidPacket);
        assert(GipReceive(&Test.Gip, BadFlags, sizeof(BadFlags), 0) == GipInvalidPacket);
        assert(GipReceive(&Test.Gip, ZeroSequence, sizeof(ZeroSequence), 0) == GipInvalidPacket);
    }
    memset(Payload, 0x5a, sizeof(Payload));
    memset(&Packet, 0, sizeof(Packet));
    Packet.Type = GIP_MESSAGE_AUDIO;
    Packet.Flags = GIP_FLAG_SYSTEM | 1;
    Packet.Sequence = 1;
    Packet.Data = Payload;
    Packet.Length = sizeof(Payload);
    assert(GipEncodePacket(&Packet, Buffer, sizeof(Buffer), &Written) == GipSuccess);
    assert(Written == sizeof(Payload) + 6); /* Extended, even-sized header. */
    assert(GipDecodePacket(Buffer, Written, &Decoded, &Consumed) == GipSuccess);
    assert(Decoded.Length == sizeof(Payload) && !memcmp(Decoded.Data, Payload, sizeof(Payload)));
    assert(GipEncodePacket(&Packet, Buffer, Written - 1, &Written) == GipBufferTooSmall);
    Hello(&Test, 1);
    assert(GipSendMessage(&Test.Gip, 1, GIP_MESSAGE_AUDIO, GIP_FLAG_SYSTEM,
                          Payload, sizeof(Payload)) == GipSuccess);
}

static void
TestFragments(void)
{
    TEST Test;
    uint8_t Storage[16], OtherStorage[16];
    const uint8_t First[] = {4, 0xf0, 9, 0x83, 0, 6, 'a', 'b', 'c'};
    const uint8_t Last[] = {4, 0xb0, 9, 3, 0x83, 0, 'd', 'e', 'f'};
    const uint8_t Complete[] = {4, 0xa0, 9, 0, 0x86, 0};
    const uint8_t FirstAck[] = {1, 0x20, 9, 9, 0, 4, 0x20, 3, 0, 0, 0, 3, 0};
    const uint8_t LastAck[] = {1, 0x20, 9, 9, 0, 4, 0x20, 6, 0, 0, 0, 0, 0};
    Initialize(&Test);
    assert(GipReceive(&Test.Gip, First, sizeof(First), 0) == GipBufferTooSmall);
    assert(GipSetReceiveBuffer(&Test.Gip, 0, Storage, sizeof(Storage)) == GipSuccess);
    assert(GipSetReceiveBuffer(&Test.Gip, 1, OtherStorage, sizeof(OtherStorage)) == GipSuccess);
    assert(GipReceive(&Test.Gip, First, sizeof(First), 0) == GipSuccess);
    assert(Test.Messages == 0 && !memcmp(Test.Sent, FirstAck, sizeof(FirstAck)));
    assert(GipReceive(&Test.Gip, First, sizeof(First), 1) == GipSuccess); /* Duplicate. */
    assert(Test.Gip.Devices[0].Receiver.Received == 3);
    assert(GipReceive(&Test.Gip, Complete, sizeof(Complete), 2) == GipInvalidPacket);
    assert(GipPoll(&Test.Gip, 101) == GipSuccess);
    assert(!memcmp(Test.Sent, FirstAck, sizeof(FirstAck)));
    assert(GipReceive(&Test.Gip, Last, sizeof(Last), 102) == GipSuccess);
    assert(Test.Messages == 1 && Test.LastMessage.Length == 6);
    assert(!memcmp(Test.Received, "abcdef", 6));
    assert(!memcmp(Test.Sent, LastAck, sizeof(LastAck)));
    assert(GipReceive(&Test.Gip, Last, sizeof(Last), 103) == GipSuccess);
    assert(Test.Messages == 1); /* Final retransmission does not redeliver. */
    assert(GipReceive(&Test.Gip, Complete, sizeof(Complete), 104) == GipSuccess);
    assert(!Test.Gip.Devices[0].Receiver.Active);
    assert(GipReceive(&Test.Gip, First, sizeof(First), UINT32_MAX - 50) == GipSuccess);
    assert(GipPoll(&Test.Gip, 950) == GipSuccess); /* Timeout across clock wrap. */
    assert(!Test.Gip.Devices[0].Receiver.Active);
    {
        uint8_t Gap[] = {4, 0xb0, 9, 1, 0x85, 0, 'f'};
        uint8_t Secondary[sizeof(First)];
        assert(GipReceive(&Test.Gip, First, sizeof(First), 0) == GipSuccess);
        memcpy(Secondary, First, sizeof(First));
        Secondary[1] |= 1;
        assert(GipReceive(&Test.Gip, Secondary, sizeof(Secondary), 0) == GipSuccess);
        assert(Test.Gip.Devices[1].Receiver.Received == 3);
        assert(GipReceive(&Test.Gip, Gap, sizeof(Gap), 101) == GipSuccess);
        assert(Test.Gip.Devices[0].Receiver.Received == 3);
        assert(!memcmp(Test.Sent, FirstAck, sizeof(FirstAck)));
        GipReset(&Test.Gip);
        assert(!Test.Gip.Devices[1].Receiver.Active);
        assert(Test.Gip.Devices[0].Receiver.Buffer == Storage);
    }
}

static void
TestMalformedTraffic(void)
{
    TEST Test;
    uint8_t Buffer[128], Storage[128];
    uint32_t Random = 0x61c88647;
    unsigned int Trial;
    Initialize(&Test);
    assert(GipSetReceiveBuffer(&Test.Gip, 0, Storage, sizeof(Storage)) == GipSuccess);
    for (Trial = 0; Trial < 20000; ++Trial)
    {
        GIP_PACKET Packet;
        size_t Length = Trial % sizeof(Buffer), Consumed, Index;
        for (Index = 0; Index < Length; ++Index)
        {
            Random = Random * 1664525u + 1013904223u;
            Buffer[Index] = (uint8_t)(Random >> 24);
        }
        if (GipDecodePacket(Buffer, Length, &Packet, &Consumed) == GipSuccess)
        {
            assert(Consumed <= Length);
            assert(Packet.Data >= Buffer && Packet.Data + Packet.Length == Buffer + Consumed);
        }
        GipReceive(&Test.Gip, Buffer, Length, Trial * 8);
        GipPoll(&Test.Gip, Trial * 8);
    }
}

int main(void)
{
    TestHelloAndSequences();
    TestCommands();
    TestFramingAndInput();
    TestFragments();
    TestMalformedTraffic();
    puts("GIP protocol tests passed");
    return 0;
}
