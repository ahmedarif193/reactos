/*
 * PROJECT:     ReactOS Raspberry Pi 3 video support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private user-mode interface to the VideoCore MMAL codecs
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include <windef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RPI3_MMAL_ABI_VERSION 4

#define RPI3_MMAL_DECODER_OUTPUT_DECODE_ORDER 0x00000001u

#define RPI3_MMAL_SUBMIT_FRAME_START    0x00000001u
#define RPI3_MMAL_SUBMIT_FRAME_END      0x00000002u
#define RPI3_MMAL_SUBMIT_CONFIG         0x00000004u
#define RPI3_MMAL_SUBMIT_KEYFRAME       0x00000008u
#define RPI3_MMAL_SUBMIT_DISCONTINUITY  0x00000010u
#define RPI3_MMAL_SUBMIT_EOS            0x00000020u

#define RPI3_MMAL_FRAME_EOS             0x00000001u
#define RPI3_MMAL_FRAME_CORRUPT         0x00000002u
#define RPI3_MMAL_FRAME_FORMAT_CHANGED  0x00000004u

#define RPI3_MMAL_PACKET_EOS            0x00000001u
#define RPI3_MMAL_PACKET_CORRUPT        0x00000002u
#define RPI3_MMAL_PACKET_KEYFRAME       0x00000004u
#define RPI3_MMAL_PACKET_CONFIG         0x00000008u
#define RPI3_MMAL_PACKET_FRAME_END      0x00000010u
#define RPI3_MMAL_PACKET_CODECSIDEINFO  0x00000020u

typedef struct RPI3_MMAL_DECODER RPI3_MMAL_DECODER;
typedef struct RPI3_MMAL_DECODER RPI3_MMAL_ENCODER;

typedef enum _RPI3_MMAL_H264_PROFILE
{
    Rpi3MmalH264ProfileBaseline,
    Rpi3MmalH264ProfileMain,
    Rpi3MmalH264ProfileHigh
} RPI3_MMAL_H264_PROFILE;

typedef enum _RPI3_MMAL_H264_LEVEL
{
    Rpi3MmalH264Level30,
    Rpi3MmalH264Level31,
    Rpi3MmalH264Level32,
    Rpi3MmalH264Level40,
    Rpi3MmalH264Level41,
    Rpi3MmalH264Level42
} RPI3_MMAL_H264_LEVEL;

typedef struct _RPI3_MMAL_ENCODER_CONFIG
{
    UINT Width;
    UINT Height;
    UINT FrameRateNumerator;
    UINT FrameRateDenominator;
    UINT Bitrate;
    UINT IntraPeriod;
    RPI3_MMAL_H264_PROFILE Profile;
    RPI3_MMAL_H264_LEVEL Level;
    BOOL InlineHeaders;
} RPI3_MMAL_ENCODER_CONFIG;

typedef struct _RPI3_MMAL_CAPS
{
    UINT AbiVersion;
    UINT MaximumWidth;
    UINT MaximumHeight;
    UINT MaximumMacroblocksPerSecond;
    UINT OutputFourCC;
    BOOL H264;
    BOOL BulkTransfer;
    BOOL SharedOutput;
    BOOL H264Encode;
} RPI3_MMAL_CAPS;

typedef struct _RPI3_MMAL_FRAME
{
    UINT Width;
    UINT Height;
    UINT VisibleWidth;
    UINT VisibleHeight;
    UINT Pitch;
    UINT StorageHeight;
    UINT Size;
    UINT Flags;
    LONGLONG Pts;
    LONGLONG Dts;
} RPI3_MMAL_FRAME;

typedef struct _RPI3_MMAL_PACKET
{
    UINT Size;
    UINT Flags;
    LONGLONG Pts;
    LONGLONG Dts;
} RPI3_MMAL_PACKET;

typedef HRESULT (CALLBACK *RPI3_MMAL_SELECT_NV12_OUTPUT)(
    void *Context,
    const RPI3_MMAL_FRAME *Frame,
    BYTE **Buffer,
    UINT *BufferSize,
    UINT *Pitch);

BOOL WINAPI Rpi3MmalQueryCaps(RPI3_MMAL_CAPS *Caps);
HRESULT WINAPI Rpi3MmalCreateH264Decoder(UINT Width, UINT Height,
                                         const BYTE *ExtraData, UINT ExtraDataSize,
                                         RPI3_MMAL_DECODER **Decoder);
HRESULT WINAPI Rpi3MmalCreateH264DecoderEx(UINT Width, UINT Height,
                                           const BYTE *ExtraData, UINT ExtraDataSize,
                                           UINT Flags,
                                           RPI3_MMAL_DECODER **Decoder);
HRESULT WINAPI Rpi3MmalSubmit(RPI3_MMAL_DECODER *Decoder,
                              const BYTE *Data, UINT DataSize, UINT Flags,
                              LONGLONG Pts, LONGLONG Dts);
HRESULT WINAPI Rpi3MmalReceiveNV12(RPI3_MMAL_DECODER *Decoder,
                                   BYTE *Buffer, UINT BufferSize, UINT Pitch,
                                   DWORD TimeoutMilliseconds,
                                   RPI3_MMAL_FRAME *Frame);
HRESULT WINAPI Rpi3MmalReceiveNV12Selected(
                                   RPI3_MMAL_DECODER *Decoder,
                                   DWORD TimeoutMilliseconds,
                                   RPI3_MMAL_SELECT_NV12_OUTPUT SelectOutput,
                                   void *Context,
                                   RPI3_MMAL_FRAME *Frame);
HRESULT WINAPI Rpi3MmalFlush(RPI3_MMAL_DECODER *Decoder);
void WINAPI Rpi3MmalDestroyDecoder(RPI3_MMAL_DECODER *Decoder);

HRESULT WINAPI Rpi3MmalCreateH264Encoder(
    const RPI3_MMAL_ENCODER_CONFIG *Config,
    RPI3_MMAL_ENCODER **Encoder);
HRESULT WINAPI Rpi3MmalSubmitNV12(RPI3_MMAL_ENCODER *Encoder,
                                  const BYTE *Data, UINT DataSize,
                                  UINT Pitch, UINT Flags,
                                  LONGLONG Pts, LONGLONG Dts);
HRESULT WINAPI Rpi3MmalReceiveH264(RPI3_MMAL_ENCODER *Encoder,
                                  BYTE *Buffer, UINT BufferSize,
                                  DWORD TimeoutMilliseconds,
                                  RPI3_MMAL_PACKET *Packet);
HRESULT WINAPI Rpi3MmalRequestKeyFrame(RPI3_MMAL_ENCODER *Encoder);
HRESULT WINAPI Rpi3MmalSetBitrate(RPI3_MMAL_ENCODER *Encoder, UINT Bitrate);
HRESULT WINAPI Rpi3MmalSetIntraPeriod(RPI3_MMAL_ENCODER *Encoder,
                                     UINT IntraPeriod);
HRESULT WINAPI Rpi3MmalFlushEncoder(RPI3_MMAL_ENCODER *Encoder);
void WINAPI Rpi3MmalDestroyEncoder(RPI3_MMAL_ENCODER *Encoder);

#ifdef __cplusplus
}
#endif
