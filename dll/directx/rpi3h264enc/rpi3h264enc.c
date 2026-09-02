/*
 * PROJECT:     ReactOS Raspberry Pi 3 video support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VideoCore H.264 Media Foundation encoder
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#define COBJMACROS
#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <oleauto.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <rpcproxy.h>
#include <initguid.h>
#include <codecapi.h>
#include <icodecapi.h>

#include "../rpi3mmal/rpi3mmal.h"

static const CLSID CLSID_Rpi3H264EncoderMFT =
    {0x7fdf3f31, 0x28d2, 0x4c58, {0x8a, 0x0c, 0x47, 0x72, 0x2d, 0x6f, 0x3a, 0x13}};

#define RPI3_ENCODER_MAX_QUEUED_INPUTS 4
#define RPI3_ENCODER_BITSTREAM_MARGIN (64 * 1024)
#define RPI3_ENCODER_MAX_BITRATE 25000000u
#define RPI3_ENCODER_OUTPUT_TIMEOUT_MS 10000u

struct rpi3_encoder
{
    IMFTransform IMFTransform_iface;
    ICodecAPI ICodecAPI_iface;
    LONG RefCount;
    CRITICAL_SECTION Lock;
    IMFMediaType *InputType;
    IMFMediaType *OutputType;
    IMFAttributes *Attributes;
    RPI3_MMAL_ENCODER *Session;
    MFT_INPUT_STREAM_INFO InputInfo;
    MFT_OUTPUT_STREAM_INFO OutputInfo;
    UINT Width;
    UINT Height;
    UINT FrameRateNumerator;
    UINT FrameRateDenominator;
    UINT InputStride;
    UINT HardwarePitch;
    UINT HardwareHeight;
    UINT Bitrate;
    UINT IntraPeriod;
    UINT Profile;
    UINT Level;
    UINT QueuedInputs;
    BYTE *InputBuffer;
    UINT InputBufferSize;
    BYTE *PacketBuffer;
    UINT PacketBufferSize;
    BYTE *SequenceHeader;
    UINT SequenceHeaderSize;
    BOOL SequenceHeaderOpen;
    BYTE *PendingOutput;
    UINT PendingOutputSize;
    UINT PendingOutputFlags;
    LONGLONG PendingPts;
    LONGLONG PendingDts;
    BOOL PendingHasFrame;
    BOOL Draining;
    BOOL EosSent;
    BOOL EosReceived;
};

static LONG ObjectCount;
static LONG ServerLocks;

static inline struct rpi3_encoder *
impl_from_IMFTransform(IMFTransform *Interface)
{
    return CONTAINING_RECORD(Interface, struct rpi3_encoder, IMFTransform_iface);
}

static inline struct rpi3_encoder *
impl_from_ICodecAPI(ICodecAPI *Interface)
{
    return CONTAINING_RECORD(Interface, struct rpi3_encoder, ICodecAPI_iface);
}

static HRESULT
rpi3_copy_media_type(IMFMediaType *Source, IMFMediaType **Destination)
{
    IMFMediaType *Copy;
    HRESULT Result;

    if (FAILED(Result = MFCreateMediaType(&Copy)))
        return Result;
    Result = IMFMediaType_CopyAllItems(Source, (IMFAttributes *)Copy);
    if (FAILED(Result))
    {
        IMFMediaType_Release(Copy);
        return Result;
    }
    *Destination = Copy;
    return S_OK;
}

static HRESULT
rpi3_get_attribute_pair(IMFAttributes *Attributes,
                        REFGUID Key,
                        UINT32 *High,
                        UINT32 *Low)
{
    UINT64 Value;
    HRESULT Result;

    if (FAILED(Result = IMFAttributes_GetUINT64(Attributes, Key, &Value)))
        return Result;
    *High = (UINT32)(Value >> 32);
    *Low = (UINT32)Value;
    return S_OK;
}

static HRESULT
rpi3_set_attribute_pair(IMFAttributes *Attributes,
                        REFGUID Key,
                        UINT32 High,
                        UINT32 Low)
{
    return IMFAttributes_SetUINT64(Attributes,
                                   Key,
                                   ((UINT64)High << 32) | Low);
}

static void
rpi3_reset_pending_output(struct rpi3_encoder *Encoder)
{
    Encoder->PendingOutputSize = 0;
    Encoder->PendingOutputFlags = 0;
    Encoder->PendingPts = 0;
    Encoder->PendingDts = 0;
    Encoder->PendingHasFrame = FALSE;
}

static void
rpi3_destroy_session(struct rpi3_encoder *Encoder)
{
    if (Encoder->Session)
        Rpi3MmalDestroyEncoder(Encoder->Session);
    Encoder->Session = NULL;
    if (Encoder->InputBuffer)
        HeapFree(GetProcessHeap(), 0, Encoder->InputBuffer);
    if (Encoder->PacketBuffer)
        HeapFree(GetProcessHeap(), 0, Encoder->PacketBuffer);
    if (Encoder->SequenceHeader)
        HeapFree(GetProcessHeap(), 0, Encoder->SequenceHeader);
    if (Encoder->PendingOutput)
        HeapFree(GetProcessHeap(), 0, Encoder->PendingOutput);
    Encoder->InputBuffer = NULL;
    Encoder->PacketBuffer = NULL;
    Encoder->SequenceHeader = NULL;
    Encoder->PendingOutput = NULL;
    Encoder->InputBufferSize = 0;
    Encoder->PacketBufferSize = 0;
    Encoder->SequenceHeaderSize = 0;
    Encoder->SequenceHeaderOpen = FALSE;
    Encoder->QueuedInputs = 0;
    Encoder->Draining = FALSE;
    Encoder->EosSent = FALSE;
    Encoder->EosReceived = FALSE;
    rpi3_reset_pending_output(Encoder);
}

static RPI3_MMAL_H264_PROFILE
rpi3_map_profile(UINT Profile)
{
    switch (Profile)
    {
        case eAVEncH264VProfile_Base:
        case eAVEncH264VProfile_ConstrainedBase:
            return Rpi3MmalH264ProfileBaseline;
        case eAVEncH264VProfile_High:
            return Rpi3MmalH264ProfileHigh;
        default:
            return Rpi3MmalH264ProfileMain;
    }
}

static RPI3_MMAL_H264_LEVEL
rpi3_map_level(UINT Level)
{
    switch (Level)
    {
        case eAVEncH264VLevel3:
            return Rpi3MmalH264Level30;
        case eAVEncH264VLevel3_1:
            return Rpi3MmalH264Level31;
        case eAVEncH264VLevel3_2:
            return Rpi3MmalH264Level32;
        case eAVEncH264VLevel4_1:
            return Rpi3MmalH264Level41;
        case eAVEncH264VLevel4_2:
            return Rpi3MmalH264Level42;
        default:
            return Rpi3MmalH264Level40;
    }
}

static BOOL
rpi3_profile_supported(UINT Profile)
{
    return Profile == eAVEncH264VProfile_Base ||
           Profile == eAVEncH264VProfile_ConstrainedBase ||
           Profile == eAVEncH264VProfile_Main ||
           Profile == eAVEncH264VProfile_High;
}

static BOOL
rpi3_level_supported(UINT Level)
{
    return Level == eAVEncH264VLevel3 ||
           Level == eAVEncH264VLevel3_1 ||
           Level == eAVEncH264VLevel3_2 ||
           Level == eAVEncH264VLevel4 ||
           Level == eAVEncH264VLevel4_1 ||
           Level == eAVEncH264VLevel4_2;
}

struct rpi3_h264_level_limit
{
    UINT Level;
    UINT MaximumFrameMacroblocks;
    UINT MaximumMacroblocksPerSecond;
    UINT MaximumBitrate;
};

static BOOL
rpi3_select_level(UINT RequestedLevel,
                  BOOL LevelSpecified,
                  UINT Profile,
                  UINT Width,
                  UINT Height,
                  UINT RateNumerator,
                  UINT RateDenominator,
                  UINT Bitrate,
                  UINT *SelectedLevel)
{
    static const struct rpi3_h264_level_limit Limits[] =
    {
        {eAVEncH264VLevel3,   1620,  40500, 10000000},
        {eAVEncH264VLevel3_1, 3600, 108000, 14000000},
        {eAVEncH264VLevel3_2, 5120, 216000, 20000000},
        {eAVEncH264VLevel4,   8192, 245760, 20000000},
        {eAVEncH264VLevel4_1, 8192, 245760, 50000000},
        {eAVEncH264VLevel4_2, 8704, 522240, 50000000}
    };
    ULONGLONG FrameMacroblocks;
    ULONGLONG MacroblocksPerSecond;
    ULONGLONG MaximumBitrate;
    UINT Index;

    FrameMacroblocks = (ULONGLONG)((Width + 15) / 16) *
                       ((Height + 15) / 16);
    MacroblocksPerSecond = FrameMacroblocks * RateNumerator /
                           RateDenominator;
    for (Index = 0; Index < ARRAYSIZE(Limits); ++Index)
    {
        if (LevelSpecified && Limits[Index].Level != RequestedLevel)
            continue;
        MaximumBitrate = Limits[Index].MaximumBitrate;
        if (Profile == eAVEncH264VProfile_High)
            MaximumBitrate = MaximumBitrate * 5 / 4;
        if (FrameMacroblocks <= Limits[Index].MaximumFrameMacroblocks &&
            MacroblocksPerSecond <= Limits[Index].MaximumMacroblocksPerSecond &&
            Bitrate <= MaximumBitrate)
        {
            *SelectedLevel = Limits[Index].Level;
            return TRUE;
        }
        if (LevelSpecified)
            break;
    }
    return FALSE;
}

static HRESULT
rpi3_create_session(struct rpi3_encoder *Encoder)
{
    RPI3_MMAL_ENCODER_CONFIG Config;
    ULONGLONG Size;
    HRESULT Result;

    if (Encoder->Session)
        return S_OK;
    if (!Encoder->InputType || !Encoder->OutputType)
        return MF_E_TRANSFORM_TYPE_NOT_SET;

    Encoder->HardwarePitch = (Encoder->Width + 31) & ~31u;
    Encoder->HardwareHeight = (Encoder->Height + 15) & ~15u;
    Size = (ULONGLONG)Encoder->HardwarePitch *
           Encoder->HardwareHeight * 3 / 2;
    if (Size > MAXUINT)
        return E_OUTOFMEMORY;
    Encoder->InputBufferSize = (UINT)Size;
    Encoder->InputBuffer = HeapAlloc(GetProcessHeap(),
                                     HEAP_ZERO_MEMORY,
                                     Encoder->InputBufferSize);
    Encoder->PacketBufferSize = Encoder->OutputInfo.cbSize;
    Encoder->PacketBuffer = HeapAlloc(GetProcessHeap(),
                                      0,
                                      Encoder->PacketBufferSize);
    Encoder->SequenceHeader = HeapAlloc(GetProcessHeap(),
                                        0,
                                        Encoder->OutputInfo.cbSize);
    Encoder->PendingOutput = HeapAlloc(GetProcessHeap(),
                                       0,
                                       Encoder->OutputInfo.cbSize);
    if (!Encoder->InputBuffer || !Encoder->PacketBuffer ||
        !Encoder->SequenceHeader || !Encoder->PendingOutput)
    {
        rpi3_destroy_session(Encoder);
        return E_OUTOFMEMORY;
    }

    ZeroMemory(&Config, sizeof(Config));
    Config.Width = Encoder->Width;
    Config.Height = Encoder->Height;
    Config.FrameRateNumerator = Encoder->FrameRateNumerator;
    Config.FrameRateDenominator = Encoder->FrameRateDenominator;
    Config.Bitrate = Encoder->Bitrate;
    Config.IntraPeriod = Encoder->IntraPeriod;
    Config.Profile = rpi3_map_profile(Encoder->Profile);
    Config.Level = rpi3_map_level(Encoder->Level);
    Config.InlineHeaders = TRUE;
    Result = Rpi3MmalCreateH264Encoder(&Config, &Encoder->Session);
    if (FAILED(Result))
        rpi3_destroy_session(Encoder);
    return Result;
}

static HRESULT WINAPI
transform_QueryInterface(IMFTransform *Interface, REFIID InterfaceId, void **Object)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);

    if (!Object)
        return E_POINTER;
    if (IsEqualGUID(InterfaceId, &IID_IUnknown) ||
        IsEqualGUID(InterfaceId, &IID_IMFTransform))
    {
        *Object = &Encoder->IMFTransform_iface;
    }
    else if (IsEqualGUID(InterfaceId, &IID_ICodecAPI))
    {
        *Object = &Encoder->ICodecAPI_iface;
    }
    else
    {
        *Object = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*Object);
    return S_OK;
}

static ULONG WINAPI
transform_AddRef(IMFTransform *Interface)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    return InterlockedIncrement(&Encoder->RefCount);
}

static ULONG WINAPI
transform_Release(IMFTransform *Interface)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    ULONG RefCount = InterlockedDecrement(&Encoder->RefCount);

    if (!RefCount)
    {
        rpi3_destroy_session(Encoder);
        if (Encoder->InputType)
            IMFMediaType_Release(Encoder->InputType);
        if (Encoder->OutputType)
            IMFMediaType_Release(Encoder->OutputType);
        if (Encoder->Attributes)
            IMFAttributes_Release(Encoder->Attributes);
        DeleteCriticalSection(&Encoder->Lock);
        HeapFree(GetProcessHeap(), 0, Encoder);
        InterlockedDecrement(&ObjectCount);
    }
    return RefCount;
}

static HRESULT WINAPI
transform_GetStreamLimits(IMFTransform *Interface,
                          DWORD *InputMinimum,
                          DWORD *InputMaximum,
                          DWORD *OutputMinimum,
                          DWORD *OutputMaximum)
{
    UNREFERENCED_PARAMETER(Interface);
    if (!InputMinimum || !InputMaximum || !OutputMinimum || !OutputMaximum)
        return E_POINTER;
    *InputMinimum = *InputMaximum = 1;
    *OutputMinimum = *OutputMaximum = 1;
    return S_OK;
}

static HRESULT WINAPI
transform_GetStreamCount(IMFTransform *Interface, DWORD *Inputs, DWORD *Outputs)
{
    UNREFERENCED_PARAMETER(Interface);
    if (!Inputs || !Outputs)
        return E_POINTER;
    *Inputs = *Outputs = 1;
    return S_OK;
}

static HRESULT WINAPI
transform_GetStreamIDs(IMFTransform *Interface,
                       DWORD InputSize,
                       DWORD *Inputs,
                       DWORD OutputSize,
                       DWORD *Outputs)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(InputSize);
    UNREFERENCED_PARAMETER(Inputs);
    UNREFERENCED_PARAMETER(OutputSize);
    UNREFERENCED_PARAMETER(Outputs);
    return E_NOTIMPL;
}

static HRESULT WINAPI
transform_GetInputStreamInfo(IMFTransform *Interface,
                             DWORD StreamId,
                             MFT_INPUT_STREAM_INFO *Info)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    if (StreamId || !Info)
        return StreamId ? MF_E_INVALIDSTREAMNUMBER : E_POINTER;
    EnterCriticalSection(&Encoder->Lock);
    *Info = Encoder->InputInfo;
    LeaveCriticalSection(&Encoder->Lock);
    return S_OK;
}

static HRESULT WINAPI
transform_GetOutputStreamInfo(IMFTransform *Interface,
                              DWORD StreamId,
                              MFT_OUTPUT_STREAM_INFO *Info)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    if (StreamId || !Info)
        return StreamId ? MF_E_INVALIDSTREAMNUMBER : E_POINTER;
    EnterCriticalSection(&Encoder->Lock);
    *Info = Encoder->OutputInfo;
    LeaveCriticalSection(&Encoder->Lock);
    return S_OK;
}

static HRESULT WINAPI
transform_GetAttributes(IMFTransform *Interface, IMFAttributes **Attributes)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    if (!Attributes)
        return E_POINTER;
    IMFAttributes_AddRef(Encoder->Attributes);
    *Attributes = Encoder->Attributes;
    return S_OK;
}

static HRESULT WINAPI
transform_GetInputStreamAttributes(IMFTransform *Interface,
                                   DWORD StreamId,
                                   IMFAttributes **Attributes)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(StreamId);
    UNREFERENCED_PARAMETER(Attributes);
    return E_NOTIMPL;
}

static HRESULT WINAPI
transform_GetOutputStreamAttributes(IMFTransform *Interface,
                                    DWORD StreamId,
                                    IMFAttributes **Attributes)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(StreamId);
    UNREFERENCED_PARAMETER(Attributes);
    return E_NOTIMPL;
}

static HRESULT WINAPI
transform_DeleteInputStream(IMFTransform *Interface, DWORD StreamId)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(StreamId);
    return E_NOTIMPL;
}

static HRESULT WINAPI
transform_AddInputStreams(IMFTransform *Interface,
                          DWORD StreamCount,
                          DWORD *StreamIds)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(StreamCount);
    UNREFERENCED_PARAMETER(StreamIds);
    return E_NOTIMPL;
}

static HRESULT
rpi3_create_input_type(struct rpi3_encoder *Encoder, IMFMediaType **Type)
{
    IMFMediaType *MediaType;
    UINT64 Value;
    UINT32 Mode;
    HRESULT Result;

    if (FAILED(Result = MFCreateMediaType(&MediaType)))
        return Result;
    if (FAILED(Result = IMFMediaType_SetGUID(MediaType,
                                             &MF_MT_MAJOR_TYPE,
                                             &MFMediaType_Video)) ||
        FAILED(Result = IMFMediaType_SetGUID(MediaType,
                                             &MF_MT_SUBTYPE,
                                             &MFVideoFormat_NV12)) ||
        FAILED(Result = IMFMediaType_GetUINT64(Encoder->OutputType,
                                               &MF_MT_FRAME_SIZE,
                                               &Value)) ||
        FAILED(Result = IMFMediaType_SetUINT64(MediaType,
                                               &MF_MT_FRAME_SIZE,
                                               Value)) ||
        FAILED(Result = IMFMediaType_GetUINT64(Encoder->OutputType,
                                               &MF_MT_FRAME_RATE,
                                               &Value)) ||
        FAILED(Result = IMFMediaType_SetUINT64(MediaType,
                                               &MF_MT_FRAME_RATE,
                                               Value)))
    {
        IMFMediaType_Release(MediaType);
        return Result;
    }
    Mode = MFVideoInterlace_Progressive;
    IMFMediaType_SetUINT32(MediaType, &MF_MT_INTERLACE_MODE, Mode);
    rpi3_set_attribute_pair((IMFAttributes *)MediaType,
                            &MF_MT_PIXEL_ASPECT_RATIO,
                            1,
                            1);
    *Type = MediaType;
    return S_OK;
}

static HRESULT WINAPI
transform_GetInputAvailableType(IMFTransform *Interface,
                                DWORD StreamId,
                                DWORD TypeIndex,
                                IMFMediaType **Type)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    HRESULT Result;

    if (StreamId || !Type)
        return StreamId ? MF_E_INVALIDSTREAMNUMBER : E_POINTER;
    *Type = NULL;
    EnterCriticalSection(&Encoder->Lock);
    if (!Encoder->OutputType)
        Result = MF_E_TRANSFORM_TYPE_NOT_SET;
    else if (TypeIndex)
        Result = MF_E_NO_MORE_TYPES;
    else
        Result = rpi3_create_input_type(Encoder, Type);
    LeaveCriticalSection(&Encoder->Lock);
    return Result;
}

static HRESULT WINAPI
transform_GetOutputAvailableType(IMFTransform *Interface,
                                 DWORD StreamId,
                                 DWORD TypeIndex,
                                 IMFMediaType **Type)
{
    IMFMediaType *MediaType;
    HRESULT Result;

    UNREFERENCED_PARAMETER(Interface);
    if (StreamId || !Type)
        return StreamId ? MF_E_INVALIDSTREAMNUMBER : E_POINTER;
    *Type = NULL;
    if (TypeIndex)
        return MF_E_NO_MORE_TYPES;
    if (FAILED(Result = MFCreateMediaType(&MediaType)))
        return Result;
    if (FAILED(Result = IMFMediaType_SetGUID(MediaType,
                                             &MF_MT_MAJOR_TYPE,
                                             &MFMediaType_Video)) ||
        FAILED(Result = IMFMediaType_SetGUID(MediaType,
                                             &MF_MT_SUBTYPE,
                                             &MFVideoFormat_H264)))
    {
        IMFMediaType_Release(MediaType);
        return Result;
    }
    *Type = MediaType;
    return S_OK;
}

static HRESULT
rpi3_validate_output_type(IMFMediaType *Type,
                          UINT *Width,
                          UINT *Height,
                          UINT *RateNumerator,
                          UINT *RateDenominator,
                          UINT *Bitrate)
{
    GUID MajorType;
    GUID Subtype;
    UINT32 InterlaceMode;
    HRESULT Result;

    if (!Type)
        return E_INVALIDARG;
    if (FAILED(IMFMediaType_GetGUID(Type, &MF_MT_MAJOR_TYPE, &MajorType)) ||
        FAILED(IMFMediaType_GetGUID(Type, &MF_MT_SUBTYPE, &Subtype)) ||
        !IsEqualGUID(&MajorType, &MFMediaType_Video) ||
        !IsEqualGUID(&Subtype, &MFVideoFormat_H264))
    {
        return MF_E_INVALIDMEDIATYPE;
    }
    if (FAILED(Result = rpi3_get_attribute_pair((IMFAttributes *)Type,
                                                &MF_MT_FRAME_SIZE,
                                                Width,
                                                Height)) ||
        FAILED(Result = rpi3_get_attribute_pair((IMFAttributes *)Type,
                                                &MF_MT_FRAME_RATE,
                                                RateNumerator,
                                                RateDenominator)) ||
        FAILED(Result = IMFMediaType_GetUINT32(Type,
                                               &MF_MT_AVG_BITRATE,
                                               Bitrate)))
    {
        return MF_E_INVALIDMEDIATYPE;
    }
    if (!*Width || !*Height || (*Width & 1) || (*Height & 1) ||
        *Width > 1920 || *Height > 1088 ||
        !*RateNumerator || !*RateDenominator ||
        !*Bitrate || *Bitrate > RPI3_ENCODER_MAX_BITRATE)
    {
        return MF_E_INVALIDMEDIATYPE;
    }
    if (FAILED(IMFMediaType_GetUINT32(Type,
                                      &MF_MT_INTERLACE_MODE,
                                      &InterlaceMode)) ||
        InterlaceMode != MFVideoInterlace_Progressive)
    {
        return MF_E_INVALIDMEDIATYPE;
    }
    return S_OK;
}

static HRESULT WINAPI
transform_SetOutputType(IMFTransform *Interface,
                        DWORD StreamId,
                        IMFMediaType *Type,
                        DWORD Flags)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    IMFMediaType *NewType = NULL;
    UINT Width, Height, RateNumerator, RateDenominator, Bitrate;
    UINT Profile = eAVEncH264VProfile_Base;
    UINT Level = 0;
    ULONGLONG MacroblocksPerSecond;
    ULONGLONG RateControlledSize;
    ULONGLONG OutputSize;
    BOOL LevelSpecified;
    HRESULT Result;

    if (StreamId)
        return MF_E_INVALIDSTREAMNUMBER;
    if (Flags & ~MFT_SET_TYPE_TEST_ONLY)
        return E_INVALIDARG;
    if (!Type)
    {
        if (Flags & MFT_SET_TYPE_TEST_ONLY)
            return S_OK;
        EnterCriticalSection(&Encoder->Lock);
        rpi3_destroy_session(Encoder);
        if (Encoder->InputType)
            IMFMediaType_Release(Encoder->InputType);
        Encoder->InputType = NULL;
        if (Encoder->OutputType)
            IMFMediaType_Release(Encoder->OutputType);
        Encoder->OutputType = NULL;
        ZeroMemory(&Encoder->InputInfo, sizeof(Encoder->InputInfo));
        ZeroMemory(&Encoder->OutputInfo, sizeof(Encoder->OutputInfo));
        LeaveCriticalSection(&Encoder->Lock);
        return S_OK;
    }

    Result = rpi3_validate_output_type(Type,
                                       &Width,
                                       &Height,
                                       &RateNumerator,
                                       &RateDenominator,
                                       &Bitrate);
    if (FAILED(Result))
        return Result;
    Result = IMFMediaType_GetUINT32(Type, &MF_MT_MPEG2_PROFILE, &Profile);
    if (FAILED(Result) && Result != MF_E_ATTRIBUTENOTFOUND)
        return MF_E_INVALIDMEDIATYPE;
    Result = IMFMediaType_GetUINT32(Type, &MF_MT_MPEG2_LEVEL, &Level);
    LevelSpecified = SUCCEEDED(Result);
    if (FAILED(Result) && Result != MF_E_ATTRIBUTENOTFOUND)
        return MF_E_INVALIDMEDIATYPE;
    if (!rpi3_profile_supported(Profile) ||
        (LevelSpecified && !rpi3_level_supported(Level)) ||
        !rpi3_select_level(Level,
                           LevelSpecified,
                           Profile,
                           Width,
                           Height,
                           RateNumerator,
                           RateDenominator,
                           Bitrate,
                           &Level))
    {
        return MF_E_INVALIDMEDIATYPE;
    }
    MacroblocksPerSecond =
        (ULONGLONG)((Width + 15) / 16) * ((Height + 15) / 16) *
        RateNumerator / RateDenominator;
    if (MacroblocksPerSecond > 244800)
        return MF_E_INVALIDMEDIATYPE;
    OutputSize = (ULONGLONG)((Width + 31) & ~31u) *
                 ((Height + 15) & ~15u) * 3 / 2;
    OutputSize += OutputSize / 4 + RPI3_ENCODER_BITSTREAM_MARGIN;
    RateControlledSize = (ULONGLONG)RPI3_ENCODER_MAX_BITRATE *
                         RateDenominator /
                         RateNumerator / 8 +
                         RPI3_ENCODER_BITSTREAM_MARGIN;
    if (OutputSize < RateControlledSize)
        OutputSize = RateControlledSize;
    if (OutputSize > MAXDWORD)
        return MF_E_INVALIDMEDIATYPE;
    if (Flags & MFT_SET_TYPE_TEST_ONLY)
        return S_OK;
    if (FAILED(Result = rpi3_copy_media_type(Type, &NewType)) ||
        FAILED(Result = IMFMediaType_SetUINT32(NewType,
                                               &MF_MT_MPEG2_PROFILE,
                                               Profile)) ||
        FAILED(Result = IMFMediaType_SetUINT32(NewType,
                                               &MF_MT_MPEG2_LEVEL,
                                               Level)))
    {
        if (NewType)
            IMFMediaType_Release(NewType);
        return Result;
    }

    EnterCriticalSection(&Encoder->Lock);
    rpi3_destroy_session(Encoder);
    if (Encoder->InputType)
        IMFMediaType_Release(Encoder->InputType);
    Encoder->InputType = NULL;
    if (Encoder->OutputType)
        IMFMediaType_Release(Encoder->OutputType);
    Encoder->OutputType = NewType;
    Encoder->Width = Width;
    Encoder->Height = Height;
    Encoder->FrameRateNumerator = RateNumerator;
    Encoder->FrameRateDenominator = RateDenominator;
    Encoder->Bitrate = Bitrate;
    Encoder->Profile = Profile;
    Encoder->Level = Level;
    Encoder->OutputInfo.dwFlags = MFT_OUTPUT_STREAM_WHOLE_SAMPLES |
                                  MFT_OUTPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER;
    Encoder->OutputInfo.cbSize = (DWORD)OutputSize;
    Encoder->OutputInfo.cbAlignment = 0;
    LeaveCriticalSection(&Encoder->Lock);
    return S_OK;
}

static HRESULT WINAPI
transform_SetInputType(IMFTransform *Interface,
                       DWORD StreamId,
                       IMFMediaType *Type,
                       DWORD Flags)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    IMFMediaType *NewType = NULL;
    GUID MajorType, Subtype;
    UINT Width, Height, RateNumerator, RateDenominator;
    UINT32 Stride;
    ULONGLONG Size;
    HRESULT Result;

    if (StreamId)
        return MF_E_INVALIDSTREAMNUMBER;
    if (Flags & ~MFT_SET_TYPE_TEST_ONLY)
        return E_INVALIDARG;
    if (!Type)
    {
        if (Flags & MFT_SET_TYPE_TEST_ONLY)
            return S_OK;
        EnterCriticalSection(&Encoder->Lock);
        rpi3_destroy_session(Encoder);
        if (Encoder->InputType)
            IMFMediaType_Release(Encoder->InputType);
        Encoder->InputType = NULL;
        ZeroMemory(&Encoder->InputInfo, sizeof(Encoder->InputInfo));
        LeaveCriticalSection(&Encoder->Lock);
        return S_OK;
    }
    EnterCriticalSection(&Encoder->Lock);
    if (!Encoder->OutputType)
    {
        Result = MF_E_TRANSFORM_TYPE_NOT_SET;
        goto Done;
    }
    if (FAILED(IMFMediaType_GetGUID(Type, &MF_MT_MAJOR_TYPE, &MajorType)) ||
        FAILED(IMFMediaType_GetGUID(Type, &MF_MT_SUBTYPE, &Subtype)) ||
        !IsEqualGUID(&MajorType, &MFMediaType_Video) ||
        !IsEqualGUID(&Subtype, &MFVideoFormat_NV12) ||
        FAILED(rpi3_get_attribute_pair((IMFAttributes *)Type,
                                       &MF_MT_FRAME_SIZE,
                                       &Width,
                                       &Height)) ||
        FAILED(rpi3_get_attribute_pair((IMFAttributes *)Type,
                                       &MF_MT_FRAME_RATE,
                                       &RateNumerator,
                                       &RateDenominator)) ||
        Width != Encoder->Width || Height != Encoder->Height ||
        RateNumerator != Encoder->FrameRateNumerator ||
        RateDenominator != Encoder->FrameRateDenominator)
    {
        Result = MF_E_INVALIDMEDIATYPE;
        goto Done;
    }
    Stride = Width;
    IMFMediaType_GetUINT32(Type, &MF_MT_DEFAULT_STRIDE, &Stride);
    if ((INT32)Stride <= 0 || Stride < Width)
    {
        Result = MF_E_INVALIDMEDIATYPE;
        goto Done;
    }
    Size = (ULONGLONG)Stride * Height +
           (ULONGLONG)Stride * ((Height + 1) / 2);
    if (Size > MAXDWORD)
    {
        Result = MF_E_INVALIDMEDIATYPE;
        goto Done;
    }
    if (Flags & MFT_SET_TYPE_TEST_ONLY)
    {
        Result = S_OK;
        goto Done;
    }
    Result = rpi3_copy_media_type(Type, &NewType);
    if (FAILED(Result))
        goto Done;

    rpi3_destroy_session(Encoder);
    if (Encoder->InputType)
        IMFMediaType_Release(Encoder->InputType);
    Encoder->InputType = NewType;
    Encoder->InputStride = Stride;
    Encoder->InputInfo.hnsMaxLatency = 0;
    Encoder->InputInfo.dwFlags = MFT_INPUT_STREAM_WHOLE_SAMPLES |
                                 MFT_INPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER |
                                 MFT_INPUT_STREAM_FIXED_SAMPLE_SIZE;
    Encoder->InputInfo.cbSize = (DWORD)Size;
    Encoder->InputInfo.cbMaxLookahead = 0;
    Encoder->InputInfo.cbAlignment = 0;
    Result = S_OK;
Done:
    if (FAILED(Result) && NewType)
        IMFMediaType_Release(NewType);
    LeaveCriticalSection(&Encoder->Lock);
    return Result;
}

static HRESULT WINAPI
transform_GetInputCurrentType(IMFTransform *Interface,
                              DWORD StreamId,
                              IMFMediaType **Type)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    HRESULT Result;

    if (StreamId || !Type)
        return StreamId ? MF_E_INVALIDSTREAMNUMBER : E_POINTER;
    *Type = NULL;
    EnterCriticalSection(&Encoder->Lock);
    if (!Encoder->InputType)
        Result = MF_E_TRANSFORM_TYPE_NOT_SET;
    else
        Result = rpi3_copy_media_type(Encoder->InputType, Type);
    LeaveCriticalSection(&Encoder->Lock);
    return Result;
}

static HRESULT WINAPI
transform_GetOutputCurrentType(IMFTransform *Interface,
                               DWORD StreamId,
                               IMFMediaType **Type)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    HRESULT Result;

    if (StreamId || !Type)
        return StreamId ? MF_E_INVALIDSTREAMNUMBER : E_POINTER;
    *Type = NULL;
    EnterCriticalSection(&Encoder->Lock);
    if (!Encoder->OutputType)
        Result = MF_E_TRANSFORM_TYPE_NOT_SET;
    else
        Result = rpi3_copy_media_type(Encoder->OutputType, Type);
    LeaveCriticalSection(&Encoder->Lock);
    return Result;
}

static HRESULT WINAPI
transform_GetInputStatus(IMFTransform *Interface, DWORD StreamId, DWORD *Flags)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    if (StreamId || !Flags)
        return StreamId ? MF_E_INVALIDSTREAMNUMBER : E_POINTER;
    EnterCriticalSection(&Encoder->Lock);
    *Flags = Encoder->InputType && Encoder->OutputType && !Encoder->Draining &&
             Encoder->QueuedInputs < RPI3_ENCODER_MAX_QUEUED_INPUTS ?
             MFT_INPUT_STATUS_ACCEPT_DATA : 0;
    LeaveCriticalSection(&Encoder->Lock);
    return S_OK;
}

static HRESULT WINAPI
transform_GetOutputStatus(IMFTransform *Interface, DWORD *Flags)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    if (!Flags)
        return E_POINTER;
    EnterCriticalSection(&Encoder->Lock);
    *Flags = Encoder->PendingOutputSize || Encoder->QueuedInputs ||
             Encoder->Draining ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
    LeaveCriticalSection(&Encoder->Lock);
    return S_OK;
}

static HRESULT WINAPI
transform_SetOutputBounds(IMFTransform *Interface, LONGLONG Lower, LONGLONG Upper)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(Lower);
    UNREFERENCED_PARAMETER(Upper);
    return E_NOTIMPL;
}

static HRESULT WINAPI
transform_ProcessEvent(IMFTransform *Interface, DWORD StreamId, IMFMediaEvent *Event)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(StreamId);
    UNREFERENCED_PARAMETER(Event);
    return E_NOTIMPL;
}

static HRESULT WINAPI
transform_ProcessMessage(IMFTransform *Interface,
                         MFT_MESSAGE_TYPE Message,
                         ULONG_PTR Parameter)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    HRESULT Result = S_OK;

    UNREFERENCED_PARAMETER(Parameter);
    EnterCriticalSection(&Encoder->Lock);
    switch (Message)
    {
        case MFT_MESSAGE_NOTIFY_BEGIN_STREAMING:
        case MFT_MESSAGE_NOTIFY_START_OF_STREAM:
            Result = rpi3_create_session(Encoder);
            break;
        case MFT_MESSAGE_COMMAND_DRAIN:
            Result = rpi3_create_session(Encoder);
            if (SUCCEEDED(Result) && !Encoder->EosSent)
            {
                Result = Rpi3MmalSubmitNV12(
                             Encoder->Session,
                             NULL,
                             0,
                             0,
                             RPI3_MMAL_SUBMIT_EOS,
                             0,
                             0);
                if (SUCCEEDED(Result))
                {
                    Encoder->EosSent = TRUE;
                    Encoder->Draining = TRUE;
                }
            }
            break;
        case MFT_MESSAGE_COMMAND_FLUSH:
            if (Encoder->Session)
            {
                Result = Rpi3MmalFlushEncoder(Encoder->Session);
                if (FAILED(Result))
                {
                    rpi3_destroy_session(Encoder);
                    break;
                }
            }
            Encoder->QueuedInputs = 0;
            Encoder->Draining = FALSE;
            Encoder->EosSent = FALSE;
            Encoder->EosReceived = FALSE;
            rpi3_reset_pending_output(Encoder);
            break;
        case MFT_MESSAGE_NOTIFY_END_STREAMING:
            rpi3_destroy_session(Encoder);
            break;
        case MFT_MESSAGE_NOTIFY_END_OF_STREAM:
        case MFT_MESSAGE_SET_D3D_MANAGER:
            break;
        default:
            break;
    }
    LeaveCriticalSection(&Encoder->Lock);
    return Result;
}

static HRESULT WINAPI
transform_ProcessInput(IMFTransform *Interface,
                       DWORD StreamId,
                       IMFSample *Sample,
                       DWORD Flags)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    IMFMediaBuffer *MediaBuffer = NULL;
    IMF2DBuffer2 *Buffer2D = NULL;
    BYTE *Source = NULL;
    BYTE *BufferStart = NULL;
    DWORD MaximumLength = 0;
    DWORD CurrentLength = 0;
    DWORD BufferCount;
    DWORD BufferLength = 0;
    ULONG_PTR SourceOffset;
    ULONGLONG RequiredSize;
    LONGLONG SampleTime;
    LONG SourcePitch;
    BOOL Buffer2DLocked = FALSE;
    BOOL MediaBufferLocked = FALSE;
    UINT Row;
    HRESULT Result;

    if (StreamId)
        return MF_E_INVALIDSTREAMNUMBER;
    if (!Sample || Flags)
        return E_INVALIDARG;

    EnterCriticalSection(&Encoder->Lock);
    Result = rpi3_create_session(Encoder);
    if (FAILED(Result))
        goto Done;
    if (Encoder->Draining ||
        Encoder->QueuedInputs >= RPI3_ENCODER_MAX_QUEUED_INPUTS)
    {
        Result = MF_E_NOTACCEPTING;
        goto Done;
    }
    if (FAILED(Result = IMFSample_GetBufferCount(Sample, &BufferCount)))
        goto Done;
    if (BufferCount == 1)
        Result = IMFSample_GetBufferByIndex(Sample, 0, &MediaBuffer);
    else
        Result = IMFSample_ConvertToContiguousBuffer(Sample, &MediaBuffer);
    if (FAILED(Result))
        goto Done;

    Result = IMFMediaBuffer_QueryInterface(MediaBuffer,
                                            &IID_IMF2DBuffer2,
                                            (void **)&Buffer2D);
    if (SUCCEEDED(Result))
    {
        Result = IMF2DBuffer2_Lock2DSize(Buffer2D,
                                         MF2DBuffer_LockFlags_Read,
                                         &Source,
                                         &SourcePitch,
                                         &BufferStart,
                                         &BufferLength);
        if (FAILED(Result))
            goto Done;
        Buffer2DLocked = TRUE;
        if (SourcePitch <= 0 || (UINT)SourcePitch < Encoder->Width ||
            (ULONG_PTR)Source < (ULONG_PTR)BufferStart)
        {
            Result = MF_E_INVALIDMEDIATYPE;
            goto Unlock;
        }
        SourceOffset = (ULONG_PTR)Source - (ULONG_PTR)BufferStart;
        RequiredSize = (ULONGLONG)(UINT)SourcePitch * Encoder->Height +
                       (ULONGLONG)(UINT)SourcePitch *
                           ((Encoder->Height + 1) / 2);
        if (SourceOffset > BufferLength ||
            RequiredSize > BufferLength - SourceOffset)
        {
            Result = MF_E_BUFFERTOOSMALL;
            goto Unlock;
        }
    }
    else if (FAILED(Result = IMFMediaBuffer_Lock(MediaBuffer,
                                                 &Source,
                                                 &MaximumLength,
                                                 &CurrentLength)))
    {
        goto Done;
    }
    else
    {
        MediaBufferLocked = TRUE;
        SourcePitch = Encoder->InputStride;
        RequiredSize = (ULONGLONG)(UINT)SourcePitch * Encoder->Height +
                       (ULONGLONG)(UINT)SourcePitch *
                           ((Encoder->Height + 1) / 2);
        if (CurrentLength < RequiredSize || MaximumLength < RequiredSize)
        {
            Result = MF_E_BUFFERTOOSMALL;
            goto Unlock;
        }
    }

    ZeroMemory(Encoder->InputBuffer, Encoder->InputBufferSize);
    for (Row = 0; Row < Encoder->Height; ++Row)
    {
        CopyMemory(Encoder->InputBuffer + Row * Encoder->HardwarePitch,
                   Source + Row * SourcePitch,
                   Encoder->Width);
    }
    for (Row = 0; Row < (Encoder->Height + 1) / 2; ++Row)
    {
        CopyMemory(Encoder->InputBuffer +
                       Encoder->HardwarePitch * Encoder->HardwareHeight +
                       Row * Encoder->HardwarePitch,
                   Source + SourcePitch * Encoder->Height +
                       Row * SourcePitch,
                   Encoder->Width);
    }
    if (FAILED(IMFSample_GetSampleTime(Sample, &SampleTime)))
        SampleTime = 0;
    Result = Rpi3MmalSubmitNV12(Encoder->Session,
                                 Encoder->InputBuffer,
                                 Encoder->InputBufferSize,
                                 Encoder->HardwarePitch,
                                 0,
                                 SampleTime / 10,
                                 SampleTime / 10);
    if (SUCCEEDED(Result))
        ++Encoder->QueuedInputs;

Unlock:
    if (Buffer2DLocked)
        IMF2DBuffer2_Unlock2D(Buffer2D);
    if (MediaBufferLocked)
        IMFMediaBuffer_Unlock(MediaBuffer);
Done:
    if (Buffer2D)
        IMF2DBuffer2_Release(Buffer2D);
    if (MediaBuffer)
        IMFMediaBuffer_Release(MediaBuffer);
    LeaveCriticalSection(&Encoder->Lock);
    return Result;
}

static HRESULT
rpi3_append_packet(struct rpi3_encoder *Encoder,
                   const RPI3_MMAL_PACKET *Packet)
{
    UINT PacketFlags = Packet->Flags;
    HRESULT Result;

    if (PacketFlags & RPI3_MMAL_PACKET_CODECSIDEINFO)
    {
        Encoder->PendingOutputFlags |=
                PacketFlags & (RPI3_MMAL_PACKET_EOS |
                               RPI3_MMAL_PACKET_CORRUPT);
        return S_OK;
    }
    if (Packet->Size > Encoder->OutputInfo.cbSize - Encoder->PendingOutputSize)
        return MF_E_BUFFERTOOSMALL;
    if (Packet->Size)
    {
        CopyMemory(Encoder->PendingOutput + Encoder->PendingOutputSize,
                   Encoder->PacketBuffer,
                   Packet->Size);
        if (Packet->Flags & RPI3_MMAL_PACKET_CONFIG)
        {
            PacketFlags &= ~(RPI3_MMAL_PACKET_FRAME_END |
                             RPI3_MMAL_PACKET_KEYFRAME);
            if (!Encoder->SequenceHeaderOpen)
            {
                Encoder->SequenceHeaderSize = 0;
                Encoder->SequenceHeaderOpen = TRUE;
            }
            if (Packet->Size > Encoder->OutputInfo.cbSize -
                               Encoder->SequenceHeaderSize)
            {
                return MF_E_BUFFERTOOSMALL;
            }
            CopyMemory(Encoder->SequenceHeader + Encoder->SequenceHeaderSize,
                       Encoder->PacketBuffer,
                       Packet->Size);
            Encoder->SequenceHeaderSize += Packet->Size;
            Result = IMFMediaType_SetBlob(Encoder->OutputType,
                                          &MF_MT_MPEG_SEQUENCE_HEADER,
                                          Encoder->SequenceHeader,
                                          Encoder->SequenceHeaderSize);
            if (FAILED(Result))
                return Result;
        }
        else
        {
            Encoder->SequenceHeaderOpen = FALSE;
            if (!Encoder->PendingHasFrame)
            {
                Encoder->PendingPts = Packet->Pts;
                Encoder->PendingDts = Packet->Dts;
                Encoder->PendingHasFrame = TRUE;
            }
        }
        Encoder->PendingOutputSize += Packet->Size;
    }
    Encoder->PendingOutputFlags |= PacketFlags;
    return S_OK;
}

static HRESULT
rpi3_write_output_sample(struct rpi3_encoder *Encoder, IMFSample *Sample)
{
    IMFMediaBuffer *MediaBuffer = NULL;
    BYTE *Destination = NULL;
    DWORD MaximumLength = 0;
    DWORD CurrentLength = 0;
    LONGLONG Duration;
    HRESULT Result;

    if (FAILED(Result = IMFSample_ConvertToContiguousBuffer(Sample,
                                                            &MediaBuffer)) ||
        FAILED(Result = IMFMediaBuffer_Lock(MediaBuffer,
                                            &Destination,
                                            &MaximumLength,
                                            &CurrentLength)))
    {
        goto Done;
    }
    if (MaximumLength < Encoder->PendingOutputSize)
    {
        Result = MF_E_BUFFERTOOSMALL;
        goto Unlock;
    }
    CopyMemory(Destination,
               Encoder->PendingOutput,
               Encoder->PendingOutputSize);
    Result = IMFMediaBuffer_SetCurrentLength(MediaBuffer,
                                             Encoder->PendingOutputSize);
    if (FAILED(Result))
        goto Unlock;

    Result = IMFSample_SetSampleTime(Sample, Encoder->PendingPts * 10);
    if (FAILED(Result))
        goto Unlock;
    Duration = 10000000LL * Encoder->FrameRateDenominator /
               Encoder->FrameRateNumerator;
    Result = IMFSample_SetSampleDuration(Sample, Duration);
    if (FAILED(Result))
        goto Unlock;
    Result = IMFSample_SetUINT32(
                 Sample,
                 &MFSampleExtension_CleanPoint,
                 !!(Encoder->PendingOutputFlags & RPI3_MMAL_PACKET_KEYFRAME));
Unlock:
    IMFMediaBuffer_Unlock(MediaBuffer);
Done:
    if (MediaBuffer)
        IMFMediaBuffer_Release(MediaBuffer);
    return Result;
}

static HRESULT WINAPI
transform_ProcessOutput(IMFTransform *Interface,
                        DWORD Flags,
                        DWORD OutputCount,
                        MFT_OUTPUT_DATA_BUFFER *Outputs,
                        DWORD *Status)
{
    struct rpi3_encoder *Encoder = impl_from_IMFTransform(Interface);
    RPI3_MMAL_PACKET Packet;
    DWORD TimeoutMilliseconds;
    HRESULT Result;

    if (Flags || OutputCount != 1 || !Outputs || !Status || !Outputs[0].pSample)
        return E_INVALIDARG;
    *Status = 0;
    Outputs[0].dwStatus = 0;
    Outputs[0].pEvents = NULL;

    EnterCriticalSection(&Encoder->Lock);
    Result = rpi3_create_session(Encoder);
    if (FAILED(Result))
        goto Done;

    for (;;)
    {
        if (Encoder->PendingHasFrame &&
            (Encoder->PendingOutputFlags & RPI3_MMAL_PACKET_FRAME_END))
        {
            if (Encoder->PendingOutputFlags & RPI3_MMAL_PACKET_CORRUPT)
            {
                if (Encoder->QueuedInputs)
                    --Encoder->QueuedInputs;
                rpi3_reset_pending_output(Encoder);
                Result = E_FAIL;
                goto Done;
            }
            Result = rpi3_write_output_sample(Encoder, Outputs[0].pSample);
            if (SUCCEEDED(Result) && Encoder->QueuedInputs)
                --Encoder->QueuedInputs;
            if (SUCCEEDED(Result))
                rpi3_reset_pending_output(Encoder);
            goto Done;
        }
        if (Encoder->EosReceived)
        {
            rpi3_destroy_session(Encoder);
            Result = MF_E_TRANSFORM_NEED_MORE_INPUT;
            goto Done;
        }

        ZeroMemory(&Packet, sizeof(Packet));
        TimeoutMilliseconds = Encoder->QueuedInputs || Encoder->Draining ?
                              RPI3_ENCODER_OUTPUT_TIMEOUT_MS : 0;
        Result = Rpi3MmalReceiveH264(Encoder->Session,
                                     Encoder->PacketBuffer,
                                     Encoder->PacketBufferSize,
                                     TimeoutMilliseconds,
                                     &Packet);
        if (Result == HRESULT_FROM_WIN32(ERROR_TIMEOUT))
        {
            if (!TimeoutMilliseconds)
                Result = MF_E_TRANSFORM_NEED_MORE_INPUT;
            goto Done;
        }
        if (FAILED(Result))
        {
            if (Packet.Flags & RPI3_MMAL_PACKET_CORRUPT)
            {
                if (Encoder->QueuedInputs)
                    --Encoder->QueuedInputs;
                rpi3_reset_pending_output(Encoder);
            }
            goto Done;
        }
        Result = rpi3_append_packet(Encoder, &Packet);
        if (FAILED(Result))
            goto Done;
        if (Packet.Flags & RPI3_MMAL_PACKET_EOS)
        {
            Encoder->EosReceived = TRUE;
            if (!Encoder->PendingHasFrame)
            {
                rpi3_destroy_session(Encoder);
                Result = MF_E_TRANSFORM_NEED_MORE_INPUT;
                goto Done;
            }
        }
    }

Done:
    LeaveCriticalSection(&Encoder->Lock);
    return Result;
}

static IMFTransformVtbl TransformVtbl =
{
    transform_QueryInterface,
    transform_AddRef,
    transform_Release,
    transform_GetStreamLimits,
    transform_GetStreamCount,
    transform_GetStreamIDs,
    transform_GetInputStreamInfo,
    transform_GetOutputStreamInfo,
    transform_GetAttributes,
    transform_GetInputStreamAttributes,
    transform_GetOutputStreamAttributes,
    transform_DeleteInputStream,
    transform_AddInputStreams,
    transform_GetInputAvailableType,
    transform_GetOutputAvailableType,
    transform_SetInputType,
    transform_SetOutputType,
    transform_GetInputCurrentType,
    transform_GetOutputCurrentType,
    transform_GetInputStatus,
    transform_GetOutputStatus,
    transform_SetOutputBounds,
    transform_ProcessEvent,
    transform_ProcessMessage,
    transform_ProcessInput,
    transform_ProcessOutput
};

static BOOL
rpi3_codec_api_supported(REFGUID Api)
{
    return IsEqualGUID(Api, &CODECAPI_AVEncCommonMeanBitRate) ||
           IsEqualGUID(Api, &CODECAPI_AVEncMPVGOPSize) ||
           IsEqualGUID(Api, &CODECAPI_AVEncVideoForceKeyFrame);
}

static HRESULT WINAPI
codec_QueryInterface(ICodecAPI *Interface, REFIID InterfaceId, void **Object)
{
    struct rpi3_encoder *Encoder = impl_from_ICodecAPI(Interface);
    return IMFTransform_QueryInterface(&Encoder->IMFTransform_iface,
                                       InterfaceId,
                                       Object);
}

static ULONG WINAPI
codec_AddRef(ICodecAPI *Interface)
{
    struct rpi3_encoder *Encoder = impl_from_ICodecAPI(Interface);
    return IMFTransform_AddRef(&Encoder->IMFTransform_iface);
}

static ULONG WINAPI
codec_Release(ICodecAPI *Interface)
{
    struct rpi3_encoder *Encoder = impl_from_ICodecAPI(Interface);
    return IMFTransform_Release(&Encoder->IMFTransform_iface);
}

static HRESULT WINAPI
codec_IsSupported(ICodecAPI *Interface, const GUID *Api)
{
    UNREFERENCED_PARAMETER(Interface);
    return Api && rpi3_codec_api_supported(Api) ? S_OK : S_FALSE;
}

static HRESULT WINAPI
codec_IsModifiable(ICodecAPI *Interface, const GUID *Api)
{
    UNREFERENCED_PARAMETER(Interface);
    return Api && rpi3_codec_api_supported(Api) ? S_OK : S_FALSE;
}

static HRESULT WINAPI
codec_GetParameterRange(ICodecAPI *Interface,
                        const GUID *Api,
                        VARIANT *Minimum,
                        VARIANT *Maximum,
                        VARIANT *Step)
{
    UNREFERENCED_PARAMETER(Interface);
    if (!Api || !Minimum || !Maximum || !Step)
        return E_POINTER;
    VariantInit(Minimum);
    VariantInit(Maximum);
    VariantInit(Step);
    Minimum->vt = Maximum->vt = Step->vt = VT_UI4;
    Step->ulVal = 1;
    if (IsEqualGUID(Api, &CODECAPI_AVEncCommonMeanBitRate))
    {
        Minimum->ulVal = 64000;
        Maximum->ulVal = RPI3_ENCODER_MAX_BITRATE;
    }
    else if (IsEqualGUID(Api, &CODECAPI_AVEncMPVGOPSize))
    {
        Minimum->ulVal = 0;
        Maximum->ulVal = 1000;
    }
    else if (IsEqualGUID(Api, &CODECAPI_AVEncVideoForceKeyFrame))
    {
        Minimum->ulVal = 0;
        Maximum->ulVal = 1;
    }
    else
    {
        return E_NOTIMPL;
    }
    return S_OK;
}

static HRESULT WINAPI
codec_GetParameterValues(ICodecAPI *Interface,
                         const GUID *Api,
                         VARIANT **Values,
                         ULONG *Count)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(Api);
    UNREFERENCED_PARAMETER(Values);
    UNREFERENCED_PARAMETER(Count);
    return E_NOTIMPL;
}

static HRESULT WINAPI
codec_GetDefaultValue(ICodecAPI *Interface, const GUID *Api, VARIANT *Value)
{
    UNREFERENCED_PARAMETER(Interface);
    if (!Api || !Value)
        return E_POINTER;
    VariantInit(Value);
    Value->vt = VT_UI4;
    if (IsEqualGUID(Api, &CODECAPI_AVEncCommonMeanBitRate))
        Value->ulVal = 8000000;
    else if (IsEqualGUID(Api, &CODECAPI_AVEncMPVGOPSize))
        Value->ulVal = 60;
    else if (IsEqualGUID(Api, &CODECAPI_AVEncVideoForceKeyFrame))
        Value->ulVal = 0;
    else
        return E_NOTIMPL;
    return S_OK;
}

static HRESULT WINAPI
codec_GetValue(ICodecAPI *Interface, const GUID *Api, VARIANT *Value)
{
    struct rpi3_encoder *Encoder = impl_from_ICodecAPI(Interface);
    HRESULT Result = S_OK;

    if (!Api || !Value)
        return E_POINTER;
    VariantInit(Value);
    EnterCriticalSection(&Encoder->Lock);
    Value->vt = VT_UI4;
    if (IsEqualGUID(Api, &CODECAPI_AVEncCommonMeanBitRate))
        Value->ulVal = Encoder->Bitrate;
    else if (IsEqualGUID(Api, &CODECAPI_AVEncMPVGOPSize))
        Value->ulVal = Encoder->IntraPeriod;
    else if (IsEqualGUID(Api, &CODECAPI_AVEncVideoForceKeyFrame))
        Value->ulVal = 0;
    else
        Result = E_NOTIMPL;
    LeaveCriticalSection(&Encoder->Lock);
    return Result;
}

static HRESULT
rpi3_variant_to_uint32(const VARIANT *Value, UINT32 *Result)
{
    if (Value->vt == VT_UI4)
        *Result = Value->ulVal;
    else
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT WINAPI
codec_SetValue(ICodecAPI *Interface, const GUID *Api, VARIANT *Value)
{
    struct rpi3_encoder *Encoder = impl_from_ICodecAPI(Interface);
    UINT32 Integer;
    HRESULT Result = S_OK;

    if (!Api || !Value)
        return E_POINTER;
    if (!rpi3_codec_api_supported(Api))
        return E_NOTIMPL;
    EnterCriticalSection(&Encoder->Lock);
    if (IsEqualGUID(Api, &CODECAPI_AVEncVideoForceKeyFrame))
    {
        Result = rpi3_variant_to_uint32(Value, &Integer);
        if (SUCCEEDED(Result) && Integer &&
            SUCCEEDED(Result = rpi3_create_session(Encoder)))
        {
            Result = Rpi3MmalRequestKeyFrame(Encoder->Session);
        }
    }
    else if (SUCCEEDED(Result = rpi3_variant_to_uint32(Value, &Integer)) &&
             IsEqualGUID(Api, &CODECAPI_AVEncCommonMeanBitRate))
    {
        if (!Integer || Integer > RPI3_ENCODER_MAX_BITRATE)
            Result = E_INVALIDARG;
        else if (Encoder->Session)
            Result = Rpi3MmalSetBitrate(Encoder->Session, Integer);
        if (SUCCEEDED(Result))
        {
            Encoder->Bitrate = Integer;
            if (Encoder->OutputType)
                IMFMediaType_SetUINT32(Encoder->OutputType,
                                       &MF_MT_AVG_BITRATE,
                                       Integer);
        }
    }
    else if (SUCCEEDED(Result) && IsEqualGUID(Api, &CODECAPI_AVEncMPVGOPSize))
    {
        if (Integer > 1000)
            Result = E_INVALIDARG;
        else if (Encoder->Session)
            Result = Rpi3MmalSetIntraPeriod(Encoder->Session, Integer);
        if (SUCCEEDED(Result))
            Encoder->IntraPeriod = Integer;
    }
    else if (SUCCEEDED(Result))
    {
        Result = E_NOTIMPL;
    }
    LeaveCriticalSection(&Encoder->Lock);
    return Result;
}

static HRESULT WINAPI
codec_RegisterForEvent(ICodecAPI *Interface, const GUID *Api, LONG_PTR UserData)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(Api);
    UNREFERENCED_PARAMETER(UserData);
    return E_NOTIMPL;
}

static HRESULT WINAPI
codec_UnregisterForEvent(ICodecAPI *Interface, const GUID *Api)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(Api);
    return E_NOTIMPL;
}

static HRESULT WINAPI codec_SetAllDefaults(ICodecAPI *Interface)
{
    struct rpi3_encoder *Encoder = impl_from_ICodecAPI(Interface);
    HRESULT Result = S_OK;

    EnterCriticalSection(&Encoder->Lock);
    if (Encoder->Session)
        Result = MF_E_INVALIDREQUEST;
    else
    {
        Encoder->Bitrate = 8000000;
        Encoder->IntraPeriod = 60;
        Encoder->Profile = eAVEncH264VProfile_Base;
        if (Encoder->OutputType)
        {
            IMFMediaType_SetUINT32(Encoder->OutputType,
                                   &MF_MT_AVG_BITRATE,
                                   Encoder->Bitrate);
            IMFMediaType_SetUINT32(Encoder->OutputType,
                                   &MF_MT_MPEG2_PROFILE,
                                   Encoder->Profile);
        }
    }
    LeaveCriticalSection(&Encoder->Lock);
    return Result;
}

static HRESULT WINAPI
codec_SetValueWithNotify(ICodecAPI *Interface,
                         const GUID *Api,
                         VARIANT *Value,
                         GUID **Changed,
                         ULONG *Count)
{
    HRESULT Result = codec_SetValue(Interface, Api, Value);
    if (Changed)
        *Changed = NULL;
    if (Count)
        *Count = 0;
    return Result;
}

static HRESULT WINAPI
codec_SetAllDefaultsWithNotify(ICodecAPI *Interface,
                               GUID **Changed,
                               ULONG *Count)
{
    HRESULT Result = codec_SetAllDefaults(Interface);
    if (Changed)
        *Changed = NULL;
    if (Count)
        *Count = 0;
    return Result;
}

static HRESULT WINAPI codec_GetAllSettings(ICodecAPI *Interface, IStream *Stream)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(Stream);
    return E_NOTIMPL;
}

static HRESULT WINAPI codec_SetAllSettings(ICodecAPI *Interface, IStream *Stream)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(Stream);
    return E_NOTIMPL;
}

static HRESULT WINAPI
codec_SetAllSettingsWithNotify(ICodecAPI *Interface,
                               IStream *Stream,
                               GUID **Changed,
                               ULONG *Count)
{
    UNREFERENCED_PARAMETER(Interface);
    UNREFERENCED_PARAMETER(Stream);
    if (Changed)
        *Changed = NULL;
    if (Count)
        *Count = 0;
    return E_NOTIMPL;
}

static ICodecAPIVtbl CodecVtbl =
{
    codec_QueryInterface,
    codec_AddRef,
    codec_Release,
    codec_IsSupported,
    codec_IsModifiable,
    codec_GetParameterRange,
    codec_GetParameterValues,
    codec_GetDefaultValue,
    codec_GetValue,
    codec_SetValue,
    codec_RegisterForEvent,
    codec_UnregisterForEvent,
    codec_SetAllDefaults,
    codec_SetValueWithNotify,
    codec_SetAllDefaultsWithNotify,
    codec_GetAllSettings,
    codec_SetAllSettings,
    codec_SetAllSettingsWithNotify
};

static HRESULT
rpi3_encoder_create(REFIID InterfaceId, void **Object)
{
    struct rpi3_encoder *Encoder;
    HRESULT Result;

    if (!Object)
        return E_POINTER;
    *Object = NULL;
    Encoder = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Encoder));
    if (!Encoder)
        return E_OUTOFMEMORY;
    Encoder->IMFTransform_iface.lpVtbl = &TransformVtbl;
    Encoder->ICodecAPI_iface.lpVtbl = &CodecVtbl;
    Encoder->RefCount = 1;
    Encoder->Bitrate = 8000000;
    Encoder->IntraPeriod = 60;
    Encoder->Profile = eAVEncH264VProfile_Base;
    Encoder->Level = eAVEncH264VLevel4;
    InitializeCriticalSection(&Encoder->Lock);
    if (FAILED(Result = MFCreateAttributes(&Encoder->Attributes, 4)))
    {
        DeleteCriticalSection(&Encoder->Lock);
        HeapFree(GetProcessHeap(), 0, Encoder);
        return Result;
    }
    InterlockedIncrement(&ObjectCount);
    Result = IMFTransform_QueryInterface(&Encoder->IMFTransform_iface,
                                         InterfaceId,
                                         Object);
    IMFTransform_Release(&Encoder->IMFTransform_iface);
    return Result;
}

struct rpi3_class_factory
{
    IClassFactory IClassFactory_iface;
};

static HRESULT WINAPI
factory_QueryInterface(IClassFactory *Interface, REFIID InterfaceId, void **Object)
{
    if (!Object)
        return E_POINTER;
    if (IsEqualGUID(InterfaceId, &IID_IUnknown) ||
        IsEqualGUID(InterfaceId, &IID_IClassFactory))
    {
        *Object = Interface;
        IClassFactory_AddRef(Interface);
        return S_OK;
    }
    *Object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IClassFactory *Interface)
{
    UNREFERENCED_PARAMETER(Interface);
    return 2;
}

static ULONG WINAPI factory_Release(IClassFactory *Interface)
{
    UNREFERENCED_PARAMETER(Interface);
    return 1;
}

static HRESULT WINAPI
factory_CreateInstance(IClassFactory *Interface,
                       IUnknown *Outer,
                       REFIID InterfaceId,
                       void **Object)
{
    UNREFERENCED_PARAMETER(Interface);
    if (Outer)
        return CLASS_E_NOAGGREGATION;
    return rpi3_encoder_create(InterfaceId, Object);
}

static HRESULT WINAPI
factory_LockServer(IClassFactory *Interface, BOOL Lock)
{
    UNREFERENCED_PARAMETER(Interface);
    if (Lock)
        InterlockedIncrement(&ServerLocks);
    else
        InterlockedDecrement(&ServerLocks);
    return S_OK;
}

static IClassFactoryVtbl FactoryVtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_CreateInstance,
    factory_LockServer
};

static struct rpi3_class_factory ClassFactory = {{&FactoryVtbl}};

HRESULT WINAPI
DllGetClassObject(REFCLSID ClassId, REFIID InterfaceId, void **Object)
{
    if (!Object)
        return E_POINTER;
    *Object = NULL;
    if (!IsEqualGUID(ClassId, &CLSID_Rpi3H264EncoderMFT))
        return CLASS_E_CLASSNOTAVAILABLE;
    return IClassFactory_QueryInterface(&ClassFactory.IClassFactory_iface,
                                        InterfaceId,
                                        Object);
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return !ObjectCount && !ServerLocks ? S_OK : S_FALSE;
}

HRESULT WINAPI DllRegisterServer(void)
{
    MFT_REGISTER_TYPE_INFO InputType = {MFMediaType_Video, MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO OutputType = {MFMediaType_Video, MFVideoFormat_H264};
    HRESULT Result;

    if (FAILED(Result = __wine_register_resources()))
        return Result;
    Result = MFTRegister(CLSID_Rpi3H264EncoderMFT,
                         MFT_CATEGORY_VIDEO_ENCODER,
                         L"Raspberry Pi 3 VideoCore H.264 Encoder",
                         MFT_ENUM_FLAG_SYNCMFT,
                         1,
                         &InputType,
                         1,
                         &OutputType,
                         NULL);
    if (FAILED(Result))
        __wine_unregister_resources();
    return Result;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    HRESULT MftResult;
    HRESULT ComResult;

    MftResult = MFTUnregister(CLSID_Rpi3H264EncoderMFT);
    ComResult = __wine_unregister_resources();
    return FAILED(MftResult) ? MftResult : ComResult;
}
