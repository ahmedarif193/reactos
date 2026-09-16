/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 ReactOS contributors
 * Transport-independent host-side Gaming Input Protocol support.
 */
#ifndef REACTOS_LIBGIP_H
#define REACTOS_LIBGIP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIP_DEVICE_COUNT 8
#define GIP_DATA_MTU 64
#define GIP_AUDIO_MTU 2048
#define GIP_MAX_MESSAGE 65535
#define GIP_FLAG_DEVICE 0x07
#define GIP_FLAG_ACK 0x10
#define GIP_FLAG_SYSTEM 0x20
#define GIP_FLAG_FIRST 0x40
#define GIP_FLAG_FRAGMENT 0x80

#define GIP_MESSAGE_ACK 0x01
#define GIP_MESSAGE_HELLO 0x02
#define GIP_MESSAGE_STATUS 0x03
#define GIP_MESSAGE_METADATA 0x04
#define GIP_MESSAGE_STATE 0x05
#define GIP_MESSAGE_SECURITY 0x06
#define GIP_MESSAGE_GUIDE 0x07
#define GIP_MESSAGE_AUDIO_CONTROL 0x08
#define GIP_MESSAGE_MOTOR 0x09
#define GIP_MESSAGE_LED 0x0a
#define GIP_MESSAGE_EXTENDED 0x1e
#define GIP_MESSAGE_INPUT 0x20
#define GIP_MESSAGE_AUDIO 0x60

#define GIP_STATE_START 0
#define GIP_STATE_STOP 1
#define GIP_STATE_FULL_POWER 3
#define GIP_STATE_OFF 4
#define GIP_STATE_QUIESCE 5
#define GIP_STATE_RESET 7

/* Decoded, transport-independent gamepad button mask. */
#define GIP_BUTTON_UP 0x0001
#define GIP_BUTTON_DOWN 0x0002
#define GIP_BUTTON_LEFT 0x0004
#define GIP_BUTTON_RIGHT 0x0008
#define GIP_BUTTON_MENU 0x0010
#define GIP_BUTTON_VIEW 0x0020
#define GIP_BUTTON_LEFT_STICK 0x0040
#define GIP_BUTTON_RIGHT_STICK 0x0080
#define GIP_BUTTON_LEFT_BUMPER 0x0100
#define GIP_BUTTON_RIGHT_BUMPER 0x0200
#define GIP_BUTTON_A 0x1000
#define GIP_BUTTON_B 0x2000
#define GIP_BUTTON_X 0x4000
#define GIP_BUTTON_Y 0x8000

typedef enum GIP_RESULT
{
    GipSuccess,
    GipInvalidParameter,
    GipInvalidPacket,
    GipBufferTooSmall,
    GipNotReady,
    GipTransportError
} GIP_RESULT;

typedef struct GIP_PACKET
{
    uint8_t Type;
    uint8_t Flags;
    uint8_t Sequence;
    uint32_t Length;
    uint32_t TotalOrOffset;
    const uint8_t *Data;
} GIP_PACKET;

typedef struct GIP_HELLO
{
    uint8_t DeviceId[8];
    uint16_t VendorId;
    uint16_t ProductId;
    uint16_t Firmware[4];
} GIP_HELLO;

typedef struct GIP_GAMEPAD
{
    uint16_t Buttons;
    uint16_t LeftTrigger;
    uint16_t RightTrigger;
    int16_t LeftX, LeftY, RightX, RightY;
} GIP_GAMEPAD;

typedef GIP_RESULT (*GIP_SEND)(void *Context, const uint8_t *Data, size_t Length);
typedef GIP_RESULT (*GIP_MESSAGE)(void *Context, const GIP_PACKET *Message);

typedef struct GIP_RECEIVER
{
    uint8_t *Buffer;
    uint32_t Capacity;
    uint32_t Total;
    uint32_t Received;
    uint32_t LastDataTime;
    uint32_t LastAckTime;
    uint8_t Type, Flags, Sequence, AckCount;
    uint8_t Active, Delivered;
} GIP_RECEIVER;

typedef struct GIP_DEVICE
{
    uint8_t Present;
    uint8_t Sequence[256];
    GIP_RECEIVER Receiver;
} GIP_DEVICE;

typedef struct GIP_CONTEXT
{
    GIP_SEND Send;
    GIP_MESSAGE Message;
    void *CallbackContext;
    uint8_t SendBuffer[GIP_AUDIO_MTU];
    GIP_DEVICE Devices[GIP_DEVICE_COUNT];
} GIP_CONTEXT;

/*
 * Caller serializes all context operations. Callbacks are synchronous and may
 * send commands, but must not recursively receive/reset the same context.
 * Send must copy/consume its buffer before returning and must not reenter this
 * context; successful submission
 * does not imply delivery. Message buffers are valid only during the callback.
 * No allocation, OS calls, locks, or background threads are used by the library.
 */
void GipInitialize(GIP_CONTEXT *Context, GIP_SEND Send, GIP_MESSAGE Message, void *CallbackContext);
void GipReset(GIP_CONTEXT *Context);
GIP_RESULT GipSetReceiveBuffer(GIP_CONTEXT *Context, uint8_t Device, uint8_t *Buffer, size_t Capacity);
GIP_RESULT GipDecodePacket(const uint8_t *Buffer, size_t Length, GIP_PACKET *Packet, size_t *Consumed);
GIP_RESULT GipEncodePacket(const GIP_PACKET *Packet, uint8_t *Buffer, size_t Capacity, size_t *Written);
GIP_RESULT GipReceive(GIP_CONTEXT *Context, const uint8_t *Buffer, size_t Length, uint32_t TimeMs);
/* Call at roughly 8 ms intervals while receiving reliable fragmented messages. */
GIP_RESULT GipPoll(GIP_CONTEXT *Context, uint32_t TimeMs);
/* Reserve a nonzero sequence for transport-specific/custom packet encoders. */
GIP_RESULT GipAllocateSequence(GIP_CONTEXT *Context, uint8_t Device, uint8_t Type,
                              uint8_t Flags, uint8_t *Sequence);
/* Single-message send, limited to the data-class MTU. For custom fragmented
 * senders, GipEncodePacket exposes the complete wire framing. */
GIP_RESULT GipSendMessage(GIP_CONTEXT *Context, uint8_t Device, uint8_t Type,
                         uint8_t Flags, const uint8_t *Data, size_t Length);
GIP_RESULT GipRequestMetadata(GIP_CONTEXT *Context, uint8_t Device);
GIP_RESULT GipSetDeviceState(GIP_CONTEXT *Context, uint8_t Device, uint8_t State);
GIP_RESULT GipSetLed(GIP_CONTEXT *Context, uint8_t Device, uint8_t Pattern, uint8_t Intensity);
GIP_RESULT GipSetMotors(GIP_CONTEXT *Context, uint8_t Device, uint8_t Mask,
                       uint16_t LeftImpulse, uint16_t RightImpulse,
                       uint16_t LeftMotor, uint16_t RightMotor,
                       uint8_t Duration, uint8_t Delay, uint8_t Repeat);
GIP_RESULT GipSetAudioFormat(GIP_CONTEXT *Context, uint8_t Device, uint8_t Capture, uint8_t Render);
GIP_RESULT GipDecodeHello(const GIP_PACKET *Packet, GIP_HELLO *Hello);
GIP_RESULT GipDecodeGamepad(const GIP_PACKET *Packet, GIP_GAMEPAD *Gamepad);
GIP_RESULT GipDecodeGuide(const GIP_PACKET *Packet, uint8_t *Pressed);

#ifdef __cplusplus
}
#endif
#endif
