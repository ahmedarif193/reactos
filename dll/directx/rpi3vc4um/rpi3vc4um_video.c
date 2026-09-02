/*
 * PROJECT:     ReactOS Raspberry Pi 3 video support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3D9 decode DDI backend for the VideoCore MMAL decoder
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <d3d9.h>
#include <d3dkmthk.h>
#include <d3dumddi.h>
#include <dxva.h>

#include "rpi3mmal.h"
#include <reactos/drivers/directx/softgpu_2d_shared.h>

#include "../softgpuum/softgpuum_backend.h"

#define RPI3_H264_PARAMETER_SET_SIZE 1024
#define RPI3_H264_MAX_FRAME_SIZE (16u * 1024u * 1024u)
#define RPI3_H264_SLICE_BUFFER_SIZE 65534u
#define RPI3VC4UM_DEVICE_MAGIC 0x55443352u /* 'R3DU' */
#define RPI3VC4UM_DECODE_MAGIC 0x43443352u /* 'R3DC' */

static CONST GUID Rpi3Vc4UmH264VldNoFgt =
{
    0x1b81be68, 0xa0c7, 0x11d3,
    {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}
};

static CONST GUID Rpi3Vc4UmNoEncrypt =
{
    0x1b81bed0, 0xa0c7, 0x11d3,
    {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}
};

typedef struct _RPI3VC4UM_VIDEO_DEVICE RPI3VC4UM_VIDEO_DEVICE;
typedef struct _RPI3VC4UM_DECODER RPI3VC4UM_DECODER;
typedef struct _RPI3VC4UM_DECODE_TARGET RPI3VC4UM_DECODE_TARGET;

struct _RPI3VC4UM_VIDEO_DEVICE
{
    ULONG Magic;
    HANDLE CoreDevice;
    CRITICAL_SECTION Lock;
    RPI3VC4UM_DECODER *Decoders;
    ULONG DecoderCount;
};

struct _RPI3VC4UM_DECODER
{
    ULONG Magic;
    RPI3VC4UM_VIDEO_DEVICE *Device;
    RPI3VC4UM_DECODER *Next;
    CRITICAL_SECTION Lock;
    HANDLE IdleEvent;
    HANDLE OutputThread;
    HANDLE OutputWakeEvent;
    HANDLE OutputStopEvent;
    volatile LONG References;
    BOOL Destroying;
    DXVADDI_VIDEODESC VideoDesc;
    DXVADDI_CONFIGPICTUREDECODE Config;
    HANDLE RenderTarget;
    UINT RenderTargetIndex;
    BOOL FrameBegun;
    BOOL PictureValid;
    BOOL MatrixValid;
    DXVA_PicParams_H264 Picture;
    DXVA_Qmatrix_H264 Matrix;
    BYTE *Bitstream;
    UINT BitstreamSize;
    UINT BitstreamCapacity;
    BYTE ParameterSets[RPI3_H264_PARAMETER_SET_SIZE];
    UINT ParameterSetSize;
    LONGLONG NextFrameToken;
    RPI3VC4UM_DECODE_TARGET *Targets;
    RPI3VC4UM_DECODE_TARGET *CurrentTarget;
    RPI3_MMAL_DECODER *MmalDecoder;
};

struct _RPI3VC4UM_DECODE_TARGET
{
    RPI3VC4UM_DECODE_TARGET *Next;
    HANDLE Resource;
    UINT SubResourceIndex;
    HANDLE CompletionEvent;
    LONGLONG FrameToken;
    HRESULT Result;
    BOOL Pending;
};

typedef struct _RPI3VC4UM_OUTPUT_SELECTION
{
    RPI3VC4UM_DECODER *Decoder;
    RPI3VC4UM_DECODE_TARGET *Target;
    SOFTGPUUM_RESOURCE_MAPPING Mapping;
} RPI3VC4UM_OUTPUT_SELECTION;

struct bit_writer
{
    BYTE *data;
    UINT capacity;
    UINT bit_position;
    BOOL overflow;
};

struct bit_reader
{
    const BYTE *data;
    UINT size;
    UINT bit_position;
};

static void bit_writer_put_bit(struct bit_writer *writer, UINT bit)
{
    UINT byte_offset = writer->bit_position >> 3;
    UINT bit_offset = 7 - (writer->bit_position & 7);

    if (byte_offset >= writer->capacity)
    {
        writer->overflow = TRUE;
        return;
    }

    if (!(writer->bit_position & 7))
        writer->data[byte_offset] = 0;
    writer->data[byte_offset] |= (bit & 1) << bit_offset;
    ++writer->bit_position;
}

static void bit_writer_put_bits(struct bit_writer *writer, UINT32 value, UINT count)
{
    while (count)
    {
        --count;
        bit_writer_put_bit(writer, value >> count);
    }
}

static void bit_writer_put_ue(struct bit_writer *writer, UINT32 value)
{
    UINT32 encoded = value + 1;
    UINT bits = 0;
    UINT index;

    for (index = encoded; index; index >>= 1)
        ++bits;
    for (index = 1; index < bits; ++index)
        bit_writer_put_bit(writer, 0);
    bit_writer_put_bits(writer, encoded, bits);
}

static void bit_writer_put_se(struct bit_writer *writer, INT32 value)
{
    UINT32 encoded = value <= 0 ? (UINT32)(-value) * 2 : (UINT32)value * 2 - 1;
    bit_writer_put_ue(writer, encoded);
}

static UINT bit_writer_finish_rbsp(struct bit_writer *writer)
{
    bit_writer_put_bit(writer, 1);
    while (writer->bit_position & 7)
        bit_writer_put_bit(writer, 0);
    return writer->overflow ? 0 : writer->bit_position >> 3;
}

static BOOL bit_reader_get_bit(struct bit_reader *reader, UINT *bit)
{
    UINT byte_offset = reader->bit_position >> 3;

    if (byte_offset >= reader->size)
        return FALSE;
    *bit = (reader->data[byte_offset] >> (7 - (reader->bit_position & 7))) & 1;
    ++reader->bit_position;
    return TRUE;
}

static BOOL bit_reader_get_ue(struct bit_reader *reader, UINT32 *value)
{
    UINT leading_zeroes = 0;
    UINT bit = 0;
    UINT32 suffix = 0;
    UINT index;

    for (;;)
    {
        if (!bit_reader_get_bit(reader, &bit))
            return FALSE;
        if (bit)
            break;
        if (++leading_zeroes > 31)
            return FALSE;
    }
    for (index = 0; index < leading_zeroes; ++index)
    {
        if (!bit_reader_get_bit(reader, &bit))
            return FALSE;
        suffix = (suffix << 1) | bit;
    }
    *value = ((1u << leading_zeroes) - 1) + suffix;
    return TRUE;
}

static UINT rpi3_h264_escape_nal(BYTE *output, UINT output_size, BYTE nal_header,
        const BYTE *rbsp, UINT rbsp_size)
{
    UINT output_position = 0;
    UINT zero_count = 0;
    UINT index;

    if (output_size < 5)
        return 0;
    output[output_position++] = 0;
    output[output_position++] = 0;
    output[output_position++] = 0;
    output[output_position++] = 1;
    output[output_position++] = nal_header;

    for (index = 0; index < rbsp_size; ++index)
    {
        if (zero_count >= 2 && rbsp[index] <= 3)
        {
            if (output_position >= output_size)
                return 0;
            output[output_position++] = 3;
            zero_count = 0;
        }
        if (output_position >= output_size)
            return 0;
        output[output_position++] = rbsp[index];
        if (!rbsp[index])
            ++zero_count;
        else
            zero_count = 0;
    }

    return output_position;
}

static const BYTE *rpi3_h264_find_slice(const BYTE *data, UINT size, UINT *remaining)
{
    UINT index;

    for (index = 0; index + 3 < size; ++index)
    {
        UINT prefix_size;
        BYTE type;

        if (data[index] || data[index + 1])
            continue;
        if (data[index + 2] == 1)
            prefix_size = 3;
        else if (index + 4 < size && !data[index + 2] && data[index + 3] == 1)
            prefix_size = 4;
        else
            continue;

        type = data[index + prefix_size] & 0x1f;
        if (type == 1 || type == 5)
        {
            *remaining = size - index - prefix_size;
            return data + index + prefix_size;
        }
        index += prefix_size;
    }

    if (size && ((data[0] & 0x1f) == 1 || (data[0] & 0x1f) == 5))
    {
        *remaining = size;
        return data;
    }
    if (size >= 5)
    {
        UINT32 length = ((UINT32)data[0] << 24) | ((UINT32)data[1] << 16) |
                        ((UINT32)data[2] << 8) | data[3];
        if (length <= size - 4 && ((data[4] & 0x1f) == 1 || (data[4] & 0x1f) == 5))
        {
            *remaining = length;
            return data + 4;
        }
    }
    return NULL;
}

static BOOL rpi3_h264_get_pps_id(const BYTE *data, UINT size, UINT32 *pps_id)
{
    BYTE rbsp[128];
    const BYTE *nal;
    struct bit_reader reader;
    UINT remaining;
    UINT source_position;
    UINT rbsp_size = 0;
    UINT zero_count = 0;
    UINT32 ignored;

    nal = rpi3_h264_find_slice(data, size, &remaining);
    if (!nal || remaining < 2)
        return FALSE;

    for (source_position = 1; source_position < remaining && rbsp_size < sizeof(rbsp); ++source_position)
    {
        BYTE value = nal[source_position];

        if (zero_count >= 2 && value == 3)
        {
            zero_count = 0;
            continue;
        }
        rbsp[rbsp_size++] = value;
        if (!value)
            ++zero_count;
        else
            zero_count = 0;
    }

    reader.data = rbsp;
    reader.size = rbsp_size;
    reader.bit_position = 0;
    return bit_reader_get_ue(&reader, &ignored) &&
           bit_reader_get_ue(&reader, &ignored) &&
           bit_reader_get_ue(&reader, pps_id);
}

static void rpi3_h264_write_scaling_list(struct bit_writer *writer, const BYTE *values, UINT count)
{
    INT last_scale = 8;
    INT next_scale = 8;
    UINT index;

    for (index = 0; index < count; ++index)
    {
        if (next_scale)
        {
            INT delta_scale = (INT)values[index] - last_scale;
            while (delta_scale > 127)
                delta_scale -= 256;
            while (delta_scale < -128)
                delta_scale += 256;
            bit_writer_put_se(writer, delta_scale);
            next_scale = values[index];
        }
        if (next_scale)
            last_scale = next_scale;
    }
}

static UINT rpi3_h264_build_parameter_sets(const DXVA_PicParams_H264 *picture,
        const DXVA_Qmatrix_H264 *matrix, UINT32 pps_id, UINT width, UINT height,
        BYTE *output, UINT output_size)
{
    BYTE rbsp[512];
    struct bit_writer writer;
    UINT rbsp_size;
    UINT output_position;
    UINT coded_width;
    UINT coded_height;
    UINT crop_unit_x;
    UINT crop_unit_y;
    UINT crop_right;
    UINT crop_bottom;
    UINT index;
    UINT scaling_count;
    UINT chroma_format = picture->chroma_format_idc ? picture->chroma_format_idc : 1;

    if (picture->num_slice_groups_minus1)
        return 0;

    writer.data = rbsp;
    writer.capacity = sizeof(rbsp);
    writer.bit_position = 0;
    writer.overflow = FALSE;

    bit_writer_put_bits(&writer, 100, 8);
    bit_writer_put_bits(&writer, 0, 8);
    bit_writer_put_bits(&writer, 41, 8);
    bit_writer_put_ue(&writer, 0);
    bit_writer_put_ue(&writer, chroma_format);
    if (chroma_format == 3)
        bit_writer_put_bit(&writer, picture->residual_colour_transform_flag);
    bit_writer_put_ue(&writer, picture->bit_depth_luma_minus8);
    bit_writer_put_ue(&writer, picture->bit_depth_chroma_minus8);
    bit_writer_put_bit(&writer, 0);
    bit_writer_put_bit(&writer, 0);
    bit_writer_put_ue(&writer, picture->log2_max_frame_num_minus4);
    bit_writer_put_ue(&writer, picture->pic_order_cnt_type);
    if (!picture->pic_order_cnt_type)
    {
        bit_writer_put_ue(&writer, picture->log2_max_pic_order_cnt_lsb_minus4);
    }
    bit_writer_put_ue(&writer, picture->num_ref_frames);
    bit_writer_put_bit(&writer, 0);
    bit_writer_put_ue(&writer, picture->wFrameWidthInMbsMinus1);
    bit_writer_put_ue(&writer, picture->wFrameHeightInMbsMinus1);
    bit_writer_put_bit(&writer, picture->frame_mbs_only_flag);
    if (!picture->frame_mbs_only_flag)
        bit_writer_put_bit(&writer, picture->MbaffFrameFlag);
    bit_writer_put_bit(&writer, picture->direct_8x8_inference_flag);

    coded_width = (picture->wFrameWidthInMbsMinus1 + 1) * 16;
    coded_height = (picture->wFrameHeightInMbsMinus1 + 1) * 16 *
                   (2 - picture->frame_mbs_only_flag);
    crop_unit_x = chroma_format ? 2 : 1;
    crop_unit_y = (chroma_format ? 2 : 1) * (2 - picture->frame_mbs_only_flag);
    crop_right = coded_width > width ? (coded_width - width) / crop_unit_x : 0;
    crop_bottom = coded_height > height ? (coded_height - height) / crop_unit_y : 0;
    bit_writer_put_bit(&writer, crop_right || crop_bottom);
    if (crop_right || crop_bottom)
    {
        bit_writer_put_ue(&writer, 0);
        bit_writer_put_ue(&writer, crop_right);
        bit_writer_put_ue(&writer, 0);
        bit_writer_put_ue(&writer, crop_bottom);
    }
    bit_writer_put_bit(&writer, 0);
    rbsp_size = bit_writer_finish_rbsp(&writer);
    if (!rbsp_size)
        return 0;

    output_position = rpi3_h264_escape_nal(output, output_size, 0x67, rbsp, rbsp_size);
    if (!output_position)
        return 0;

    writer.bit_position = 0;
    writer.overflow = FALSE;
    bit_writer_put_ue(&writer, pps_id);
    bit_writer_put_ue(&writer, 0);
    bit_writer_put_bit(&writer, picture->entropy_coding_mode_flag);
    bit_writer_put_bit(&writer, picture->pic_order_present_flag);
    bit_writer_put_ue(&writer, 0);
    bit_writer_put_ue(&writer, picture->num_ref_idx_l0_active_minus1);
    bit_writer_put_ue(&writer, picture->num_ref_idx_l1_active_minus1);
    bit_writer_put_bit(&writer, picture->weighted_pred_flag);
    bit_writer_put_bits(&writer, picture->weighted_bipred_idc, 2);
    bit_writer_put_se(&writer, picture->pic_init_qp_minus26);
    bit_writer_put_se(&writer, picture->pic_init_qs_minus26);
    bit_writer_put_se(&writer, picture->chroma_qp_index_offset);
    bit_writer_put_bit(&writer, picture->deblocking_filter_control_present_flag);
    bit_writer_put_bit(&writer, picture->constrained_intra_pred_flag);
    bit_writer_put_bit(&writer, picture->redundant_pic_cnt_present_flag);
    bit_writer_put_bit(&writer, picture->transform_8x8_mode_flag);
    bit_writer_put_bit(&writer, matrix != NULL);
    if (matrix)
    {
        scaling_count = 6 + (picture->transform_8x8_mode_flag ? 2 : 0);
        for (index = 0; index < scaling_count; ++index)
        {
            bit_writer_put_bit(&writer, 1);
            if (index < 6)
                rpi3_h264_write_scaling_list(&writer, matrix->bScalingLists4x4[index], 16);
            else
                rpi3_h264_write_scaling_list(&writer, matrix->bScalingLists8x8[index - 6], 64);
        }
    }
    bit_writer_put_se(&writer, picture->second_chroma_qp_index_offset);
    rbsp_size = bit_writer_finish_rbsp(&writer);
    if (!rbsp_size)
        return 0;

    rbsp_size = rpi3_h264_escape_nal(output + output_position,
            output_size - output_position, 0x68, rbsp, rbsp_size);
    if (!rbsp_size)
        return 0;
    return output_position + rbsp_size;
}
static BOOL
Rpi3Vc4UmDecodeGuidValid(
    CONST GUID *Guid)
{
    return Guid != NULL &&
           IsEqualGUID(Guid, &Rpi3Vc4UmH264VldNoFgt);
}

static BOOL
Rpi3Vc4UmDecodeInputValid(
    CONST DXVADDI_DECODEINPUT *Input)
{
    return Input != NULL && Input->pGuid != NULL &&
           Rpi3Vc4UmDecodeGuidValid(Input->pGuid) &&
           Input->VideoDesc.Format == SOFTGPU_D3DDDIFMT_NV12 &&
           Input->VideoDesc.SampleWidth != 0 &&
           Input->VideoDesc.SampleHeight != 0 &&
           (Input->VideoDesc.SampleWidth & 1) == 0 &&
           (Input->VideoDesc.SampleHeight & 1) == 0 &&
           Input->VideoDesc.SampleWidth <= 1920 &&
           Input->VideoDesc.SampleHeight <= 1088;
}

static HRESULT
Rpi3Vc4UmValidatePicture(
    CONST RPI3VC4UM_DECODER *Decoder)
{
    CONST DXVA_PicParams_H264 *Picture = &Decoder->Picture;
    UINT WidthInMbs;
    UINT HeightInMbs;

    WidthInMbs = (Decoder->VideoDesc.SampleWidth + 15) / 16;
    HeightInMbs = (Decoder->VideoDesc.SampleHeight + 15) / 16;
    if ((UINT)Picture->wFrameWidthInMbsMinus1 + 1 != WidthInMbs ||
        (UINT)Picture->wFrameHeightInMbsMinus1 + 1 != HeightInMbs ||
        Picture->num_ref_frames > ARRAYSIZE(Picture->RefFrameList) ||
        Picture->log2_max_frame_num_minus4 > 12 ||
        (Picture->pic_order_cnt_type == 0 &&
         Picture->log2_max_pic_order_cnt_lsb_minus4 > 12) ||
        Picture->pic_order_cnt_type > 2 ||
        Picture->weighted_bipred_idc > 2)
    {
        return E_INVALIDARG;
    }

    /*
     * The MMAL firmware consumes an elementary stream, while DXVA provides
     * parsed picture state and slice NAL units.  Reconstruct only fields that
     * are completely represented by DXVA_PicParams_H264.  In particular, POC
     * type 1 offsets and field-picture sequencing are not present in this DDI.
     */
    if (Picture->chroma_format_idc != 1 ||
        Picture->bit_depth_luma_minus8 != 0 ||
        Picture->bit_depth_chroma_minus8 != 0 ||
        Picture->residual_colour_transform_flag ||
        Picture->field_pic_flag ||
        !Picture->frame_mbs_only_flag ||
        Picture->MbaffFrameFlag ||
        Picture->sp_for_switch_flag ||
        Picture->pic_order_cnt_type == 1 ||
        Picture->num_slice_groups_minus1 ||
        Picture->NonExistingFrameFlags)
    {
        return E_NOTIMPL;
    }

    return S_OK;
}

static HRESULT APIENTRY
Rpi3Vc4UmGetCaps(
    CONST D3DDDIARG_GETCAPS *Data)
{
    CONST DXVADDI_DECODEINPUT *Input;
    DXVADDI_DECODEBUFFERINFO *BufferInfo;
    DXVADDI_CONFIGPICTUREDECODE *Config;

    if (Data == NULL || Data->pData == NULL)
        return E_INVALIDARG;

    switch (Data->Type)
    {
        case D3DDDICAPS_GETDECODEGUIDCOUNT:
            if (Data->DataSize < sizeof(UINT))
                return E_INVALIDARG;
            *(UINT *)Data->pData = 1;
            return S_OK;

        case D3DDDICAPS_GETDECODEGUIDS:
            if (Data->DataSize < sizeof(GUID))
                return E_INVALIDARG;
            *(GUID *)Data->pData = Rpi3Vc4UmH264VldNoFgt;
            return S_OK;

        case D3DDDICAPS_GETDECODERTFORMATCOUNT:
        case D3DDDICAPS_GETDECODERTFORMATS:
            if (!Rpi3Vc4UmDecodeGuidValid((CONST GUID *)Data->pInfo))
                return E_INVALIDARG;
            break;

        case D3DDDICAPS_GETDECODECOMPRESSEDBUFFERINFOCOUNT:
        case D3DDDICAPS_GETDECODECOMPRESSEDBUFFERINFO:
        case D3DDDICAPS_GETDECODECONFIGURATIONCOUNT:
        case D3DDDICAPS_GETDECODECONFIGURATIONS:
            Input = (CONST DXVADDI_DECODEINPUT *)Data->pInfo;
            if (!Rpi3Vc4UmDecodeInputValid(Input))
                return E_INVALIDARG;
            break;

        default:
            return E_NOTIMPL;
    }

    switch (Data->Type)
    {
        case D3DDDICAPS_GETDECODERTFORMATCOUNT:
            if (Data->DataSize < sizeof(UINT))
                return E_INVALIDARG;
            *(UINT *)Data->pData = 1;
            return S_OK;

        case D3DDDICAPS_GETDECODERTFORMATS:
            if (Data->DataSize < sizeof(D3DDDIFORMAT))
                return E_INVALIDARG;
            *(D3DDDIFORMAT *)Data->pData = SOFTGPU_D3DDDIFMT_NV12;
            return S_OK;

        case D3DDDICAPS_GETDECODECOMPRESSEDBUFFERINFOCOUNT:
            if (Data->DataSize < sizeof(UINT))
                return E_INVALIDARG;
            *(UINT *)Data->pData = 4;
            return S_OK;

        case D3DDDICAPS_GETDECODECOMPRESSEDBUFFERINFO:
            if (Data->DataSize < 4 * sizeof(*BufferInfo))
                return E_INVALIDARG;
            BufferInfo = (DXVADDI_DECODEBUFFERINFO *)Data->pData;
            ZeroMemory(BufferInfo, 4 * sizeof(*BufferInfo));
            BufferInfo[0].CompressedBufferType =
                D3DDDIFMT_PICTUREPARAMSDATA;
            BufferInfo[0].CreationWidth = sizeof(DXVA_PicParams_H264);
            BufferInfo[1].CompressedBufferType =
                D3DDDIFMT_INVERSEQUANTIZATIONDATA;
            BufferInfo[1].CreationWidth = sizeof(DXVA_Qmatrix_H264);
            BufferInfo[2].CompressedBufferType =
                D3DDDIFMT_SLICECONTROLDATA;
            BufferInfo[2].CreationWidth = RPI3_H264_SLICE_BUFFER_SIZE;
            BufferInfo[3].CompressedBufferType = D3DDDIFMT_BITSTREAMDATA;
            BufferInfo[3].CreationWidth = RPI3_H264_MAX_FRAME_SIZE;
            BufferInfo[0].CreationHeight = 1;
            BufferInfo[1].CreationHeight = 1;
            BufferInfo[2].CreationHeight = 1;
            BufferInfo[3].CreationHeight = 1;
            BufferInfo[0].CreationPool = D3DDDIPOOL_VIDEOMEMORY;
            BufferInfo[1].CreationPool = D3DDDIPOOL_VIDEOMEMORY;
            BufferInfo[2].CreationPool = D3DDDIPOOL_VIDEOMEMORY;
            BufferInfo[3].CreationPool = D3DDDIPOOL_VIDEOMEMORY;
            return S_OK;

        case D3DDDICAPS_GETDECODECONFIGURATIONCOUNT:
            if (Data->DataSize < sizeof(UINT))
                return E_INVALIDARG;
            *(UINT *)Data->pData = 1;
            return S_OK;

        case D3DDDICAPS_GETDECODECONFIGURATIONS:
            if (Data->DataSize < sizeof(*Config))
                return E_INVALIDARG;
            Config = (DXVADDI_CONFIGPICTUREDECODE *)Data->pData;
            ZeroMemory(Config, sizeof(*Config));
            Config->guidConfigBitstreamEncryption = Rpi3Vc4UmNoEncrypt;
            Config->guidConfigMBcontrolEncryption = Rpi3Vc4UmNoEncrypt;
            Config->guidConfigResidDiffEncryption = Rpi3Vc4UmNoEncrypt;
            Config->ConfigBitstreamRaw = 2;
            Config->ConfigMinRenderTargetBuffCount = 4;
            return S_OK;

        default:
            return E_NOTIMPL;
    }
}

static RPI3VC4UM_VIDEO_DEVICE *
Rpi3Vc4UmVideoDevice(
    HANDLE Device)
{
    RPI3VC4UM_VIDEO_DEVICE *VideoDevice;

    VideoDevice = (RPI3VC4UM_VIDEO_DEVICE *)
        SoftGpuUmGetBackendContext(Device);
    return VideoDevice != NULL &&
           VideoDevice->Magic == RPI3VC4UM_DEVICE_MAGIC
               ? VideoDevice
               : NULL;
}

static RPI3VC4UM_DECODER *
Rpi3Vc4UmDecoderLocked(
    RPI3VC4UM_VIDEO_DEVICE *Device,
    HANDLE Decode)
{
    RPI3VC4UM_DECODER *Decoder;

    for (Decoder = Device->Decoders;
         Decoder != NULL;
         Decoder = Decoder->Next)
    {
        if ((HANDLE)Decoder == Decode)
            break;
    }
    return Decoder != NULL && Decoder->Magic == RPI3VC4UM_DECODE_MAGIC &&
           Decoder->Device == Device ? Decoder : NULL;
}

static RPI3VC4UM_DECODER *
Rpi3Vc4UmAcquireDecoder(
    RPI3VC4UM_VIDEO_DEVICE *Device,
    HANDLE Decode)
{
    RPI3VC4UM_DECODER *Decoder;

    EnterCriticalSection(&Device->Lock);
    Decoder = Rpi3Vc4UmDecoderLocked(Device, Decode);
    if (Decoder != NULL && !Decoder->Destroying)
        InterlockedIncrement(&Decoder->References);
    else
        Decoder = NULL;
    LeaveCriticalSection(&Device->Lock);
    return Decoder;
}

static VOID
Rpi3Vc4UmReleaseDecoder(
    RPI3VC4UM_DECODER *Decoder)
{
    if (InterlockedDecrement(&Decoder->References) == 0)
        SetEvent(Decoder->IdleEvent);
}

static RPI3VC4UM_DECODE_TARGET *
Rpi3Vc4UmFindTargetLocked(RPI3VC4UM_DECODER *Decoder,
                          HANDLE Resource,
                          UINT SubResourceIndex)
{
    RPI3VC4UM_DECODE_TARGET *Target;

    for (Target = Decoder->Targets;
         Target != NULL;
         Target = Target->Next)
    {
        if (Target->Resource == Resource &&
            Target->SubResourceIndex == SubResourceIndex)
        {
            return Target;
        }
    }
    return NULL;
}

static RPI3VC4UM_DECODE_TARGET *
Rpi3Vc4UmFindTokenLocked(RPI3VC4UM_DECODER *Decoder,
                         LONGLONG FrameToken)
{
    RPI3VC4UM_DECODE_TARGET *Target;

    for (Target = Decoder->Targets;
         Target != NULL;
         Target = Target->Next)
    {
        if (Target->Pending && Target->FrameToken == FrameToken)
            return Target;
    }
    return NULL;
}

static BOOL
Rpi3Vc4UmHasPendingOutputLocked(RPI3VC4UM_DECODER *Decoder)
{
    RPI3VC4UM_DECODE_TARGET *Target;

    for (Target = Decoder->Targets;
         Target != NULL;
         Target = Target->Next)
    {
        if (Target->Pending)
            return TRUE;
    }
    return FALSE;
}

static VOID
Rpi3Vc4UmCompleteTargetLocked(RPI3VC4UM_DECODE_TARGET *Target,
                              HRESULT Result)
{
    Target->Result = Result;
    Target->Pending = FALSE;
    SetEvent(Target->CompletionEvent);
}

static VOID
Rpi3Vc4UmFailPendingOutputs(RPI3VC4UM_DECODER *Decoder,
                            HRESULT Result)
{
    RPI3VC4UM_DECODE_TARGET *Target;

    EnterCriticalSection(&Decoder->Lock);
    for (Target = Decoder->Targets;
         Target != NULL;
         Target = Target->Next)
    {
        if (Target->Pending)
            Rpi3Vc4UmCompleteTargetLocked(Target, Result);
    }
    LeaveCriticalSection(&Decoder->Lock);
}

static HRESULT CALLBACK
Rpi3Vc4UmSelectMmalOutput(void *Context,
                         const RPI3_MMAL_FRAME *Frame,
                         BYTE **Buffer,
                         UINT *BufferSize,
                         UINT *Pitch)
{
    RPI3VC4UM_OUTPUT_SELECTION *Selection = Context;
    RPI3VC4UM_DECODE_TARGET *Target;
    HRESULT Result;

    if (!Selection || !Frame || !Buffer || !BufferSize || !Pitch)
        return E_INVALIDARG;

    EnterCriticalSection(&Selection->Decoder->Lock);
    Target = Rpi3Vc4UmFindTokenLocked(Selection->Decoder, Frame->Pts);
    if (Target == NULL)
    {
        LeaveCriticalSection(&Selection->Decoder->Lock);
        return E_FAIL;
    }
    Selection->Target = Target;
    Result = SoftGpuUmMapResource(
                 Selection->Decoder->Device->CoreDevice,
                 Target->Resource,
                 Target->SubResourceIndex,
                 TRUE,
                 &Selection->Mapping);
    LeaveCriticalSection(&Selection->Decoder->Lock);
    if (FAILED(Result))
        return Result;

    if (Selection->Mapping.Format != SOFTGPU_D3DDDIFMT_NV12 ||
        Selection->Mapping.Width < Frame->VisibleWidth ||
        Selection->Mapping.Height < Frame->VisibleHeight ||
        Selection->Mapping.StorageHeight < Frame->VisibleHeight ||
        Selection->Mapping.PlaneCount != 2 ||
        Selection->Mapping.PlaneOffsets[0] != 0 ||
        Selection->Mapping.PlaneOffsets[1] !=
            Selection->Mapping.Pitch * Selection->Mapping.StorageHeight ||
        Selection->Mapping.PlanePitches[0] != Frame->Pitch ||
        Selection->Mapping.PlanePitches[1] != Frame->Pitch ||
        Selection->Mapping.Pitch != Frame->Pitch ||
        Selection->Mapping.Size < Frame->Size)
    {
        SoftGpuUmUnmapResource(Selection->Decoder->Device->CoreDevice,
                               &Selection->Mapping);
        ZeroMemory(&Selection->Mapping, sizeof(Selection->Mapping));
        Selection->Target = NULL;
        return E_INVALIDARG;
    }

    *Buffer = (BYTE *)Selection->Mapping.Data;
    *BufferSize = (UINT)Selection->Mapping.Size;
    *Pitch = Selection->Mapping.Pitch;
    return S_OK;
}

static DWORD WINAPI
Rpi3Vc4UmOutputThread(void *Context)
{
    RPI3VC4UM_DECODER *Decoder = Context;
    HANDLE WaitHandles[2];

    WaitHandles[0] = Decoder->OutputWakeEvent;
    WaitHandles[1] = Decoder->OutputStopEvent;
    for (;;)
    {
        RPI3VC4UM_OUTPUT_SELECTION Selection;
        RPI3_MMAL_FRAME Frame;
        DWORD WaitStatus;
        HRESULT Result;
        BOOL Pending;

        EnterCriticalSection(&Decoder->Lock);
        Pending = Rpi3Vc4UmHasPendingOutputLocked(Decoder);
        if (!Pending)
            ResetEvent(Decoder->OutputWakeEvent);
        LeaveCriticalSection(&Decoder->Lock);

        if (!Pending)
        {
            WaitStatus = WaitForMultipleObjects(ARRAYSIZE(WaitHandles),
                                                WaitHandles,
                                                FALSE,
                                                INFINITE);
            if (WaitStatus == WAIT_OBJECT_0 + 1)
                break;
            if (WaitStatus != WAIT_OBJECT_0)
            {
                Rpi3Vc4UmFailPendingOutputs(
                    Decoder,
                    HRESULT_FROM_WIN32(GetLastError()));
                break;
            }
        }
        if (WaitForSingleObject(Decoder->OutputStopEvent, 0) == WAIT_OBJECT_0)
            break;

        ZeroMemory(&Selection, sizeof(Selection));
        Selection.Decoder = Decoder;
        ZeroMemory(&Frame, sizeof(Frame));
        Result = Rpi3MmalReceiveNV12Selected(
                     Decoder->MmalDecoder,
                     100,
                     Rpi3Vc4UmSelectMmalOutput,
                     &Selection,
                     &Frame);
        if (Result == HRESULT_FROM_WIN32(ERROR_TIMEOUT))
            continue;

        if (Selection.Mapping.Data != NULL)
        {
            HRESULT UnmapResult = SoftGpuUmUnmapResource(
                                      Decoder->Device->CoreDevice,
                                      &Selection.Mapping);
            if (FAILED(UnmapResult) && SUCCEEDED(Result))
                Result = UnmapResult;
        }

        if (Selection.Target != NULL)
        {
            EnterCriticalSection(&Decoder->Lock);
            if (Selection.Target->Pending &&
                Selection.Target->FrameToken == Frame.Pts)
            {
                Rpi3Vc4UmCompleteTargetLocked(Selection.Target, Result);
            }
            LeaveCriticalSection(&Decoder->Lock);
        }

        if (FAILED(Result))
        {
            Rpi3Vc4UmFailPendingOutputs(Decoder, Result);
            break;
        }
    }

    Rpi3Vc4UmFailPendingOutputs(
        Decoder,
        HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED));
    return 0;
}

static HRESULT
Rpi3Vc4UmReserveBitstream(
    RPI3VC4UM_DECODER *Decoder,
    UINT AdditionalSize)
{
    BYTE *NewBuffer;
    UINT Required;
    UINT Capacity;

    if (AdditionalSize > RPI3_H264_MAX_FRAME_SIZE - Decoder->BitstreamSize)
        return E_OUTOFMEMORY;
    Required = Decoder->BitstreamSize + AdditionalSize;
    if (Required <= Decoder->BitstreamCapacity)
        return S_OK;

    Capacity = Decoder->BitstreamCapacity != 0
                   ? Decoder->BitstreamCapacity
                   : 65536;
    while (Capacity < Required)
    {
        if (Capacity > RPI3_H264_MAX_FRAME_SIZE / 2)
        {
            Capacity = RPI3_H264_MAX_FRAME_SIZE;
            break;
        }
        Capacity *= 2;
    }

    NewBuffer = Decoder->Bitstream != NULL
                    ? (BYTE *)HeapReAlloc(GetProcessHeap(), 0,
                                         Decoder->Bitstream, Capacity)
                    : (BYTE *)HeapAlloc(GetProcessHeap(), 0, Capacity);
    if (NewBuffer == NULL && Decoder->Bitstream != NULL)
    {
        NewBuffer = (BYTE *)HeapAlloc(
                        GetProcessHeap(),
                        0,
                        Capacity);
        if (NewBuffer == NULL)
            return E_OUTOFMEMORY;
        if (Decoder->BitstreamSize != 0)
            CopyMemory(NewBuffer, Decoder->Bitstream, Decoder->BitstreamSize);
        if (Decoder->Bitstream != NULL)
            HeapFree(GetProcessHeap(), 0, Decoder->Bitstream);
    }
    if (NewBuffer == NULL)
        return E_OUTOFMEMORY;
    Decoder->Bitstream = NewBuffer;
    Decoder->BitstreamCapacity = Capacity;
    return S_OK;
}

static HRESULT APIENTRY
Rpi3Vc4UmCreateDecodeDevice(
    HANDLE DeviceHandle,
    D3DDDIARG_CREATEDECODEDEVICE *Data)
{
    RPI3VC4UM_VIDEO_DEVICE *Device =
        Rpi3Vc4UmVideoDevice(DeviceHandle);
    RPI3VC4UM_DECODER *Decoder;
    HRESULT Result;

    if (Device == NULL || Data == NULL || Data->pGuid == NULL ||
        Data->pConfig == NULL ||
        !IsEqualGUID(Data->pGuid, &Rpi3Vc4UmH264VldNoFgt) ||
        Data->VideoDesc.Format != SOFTGPU_D3DDDIFMT_NV12 ||
        Data->VideoDesc.SampleWidth == 0 ||
        Data->VideoDesc.SampleHeight == 0 ||
        (Data->VideoDesc.SampleWidth & 1) != 0 ||
        (Data->VideoDesc.SampleHeight & 1) != 0 ||
        Data->VideoDesc.SampleWidth > 1920 ||
        Data->VideoDesc.SampleHeight > 1088 ||
        Data->pConfig->ConfigBitstreamRaw != 2 ||
        !IsEqualGUID(&Data->pConfig->guidConfigBitstreamEncryption,
                     &Rpi3Vc4UmNoEncrypt) ||
        !IsEqualGUID(&Data->pConfig->guidConfigMBcontrolEncryption,
                     &Rpi3Vc4UmNoEncrypt) ||
        !IsEqualGUID(&Data->pConfig->guidConfigResidDiffEncryption,
                     &Rpi3Vc4UmNoEncrypt))
    {
        return E_INVALIDARG;
    }

    Decoder = (RPI3VC4UM_DECODER *)HeapAlloc(
                  GetProcessHeap(),
                  HEAP_ZERO_MEMORY,
                  sizeof(*Decoder));
    if (Decoder == NULL)
        return E_OUTOFMEMORY;

    InitializeCriticalSection(&Decoder->Lock);
    Decoder->IdleEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Decoder->OutputWakeEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Decoder->OutputStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Decoder->IdleEvent == NULL ||
        Decoder->OutputWakeEvent == NULL ||
        Decoder->OutputStopEvent == NULL)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        if (Decoder->OutputStopEvent != NULL)
            CloseHandle(Decoder->OutputStopEvent);
        if (Decoder->OutputWakeEvent != NULL)
            CloseHandle(Decoder->OutputWakeEvent);
        if (Decoder->IdleEvent != NULL)
            CloseHandle(Decoder->IdleEvent);
        DeleteCriticalSection(&Decoder->Lock);
        HeapFree(GetProcessHeap(), 0, Decoder);
        return Result;
    }
    Decoder->References = 1;
    Decoder->Magic = RPI3VC4UM_DECODE_MAGIC;
    Decoder->Device = Device;
    Decoder->VideoDesc = Data->VideoDesc;
    Decoder->Config = *Data->pConfig;

    EnterCriticalSection(&Device->Lock);
    Decoder->Next = Device->Decoders;
    Device->Decoders = Decoder;
    ++Device->DecoderCount;
    LeaveCriticalSection(&Device->Lock);

    Data->hDecode = (HANDLE)Decoder;
    return S_OK;
}

static HRESULT APIENTRY
Rpi3Vc4UmDestroyDecodeDevice(
    HANDLE DeviceHandle,
    HANDLE DecodeHandle)
{
    RPI3VC4UM_VIDEO_DEVICE *Device =
        Rpi3Vc4UmVideoDevice(DeviceHandle);
    RPI3VC4UM_DECODER *Decoder;
    RPI3VC4UM_DECODER **Link;

    if (Device == NULL || DecodeHandle == NULL)
        return E_INVALIDARG;

    EnterCriticalSection(&Device->Lock);
    Decoder = Rpi3Vc4UmDecoderLocked(Device, DecodeHandle);
    if (Decoder == NULL)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_INVALIDARG;
    }
    Decoder->Destroying = TRUE;
    for (Link = &Device->Decoders; *Link != NULL; Link = &(*Link)->Next)
    {
        if (*Link == Decoder)
        {
            *Link = Decoder->Next;
            break;
        }
    }
    --Device->DecoderCount;
    if (InterlockedDecrement(&Decoder->References) == 0)
        SetEvent(Decoder->IdleEvent);
    LeaveCriticalSection(&Device->Lock);

    if (Decoder->OutputThread != NULL)
    {
        SetEvent(Decoder->OutputStopEvent);
        SetEvent(Decoder->OutputWakeEvent);
        WaitForSingleObject(Decoder->OutputThread, INFINITE);
        CloseHandle(Decoder->OutputThread);
    }
    WaitForSingleObject(Decoder->IdleEvent, INFINITE);
    Decoder->Magic = 0;

    Rpi3MmalDestroyDecoder(Decoder->MmalDecoder);
    if (Decoder->Bitstream != NULL)
        HeapFree(GetProcessHeap(), 0, Decoder->Bitstream);
    while (Decoder->Targets != NULL)
    {
        RPI3VC4UM_DECODE_TARGET *Target = Decoder->Targets;

        Decoder->Targets = Target->Next;
        CloseHandle(Target->CompletionEvent);
        HeapFree(GetProcessHeap(), 0, Target);
    }
    CloseHandle(Decoder->OutputStopEvent);
    CloseHandle(Decoder->OutputWakeEvent);
    CloseHandle(Decoder->IdleEvent);
    DeleteCriticalSection(&Decoder->Lock);
    HeapFree(GetProcessHeap(), 0, Decoder);
    return S_OK;
}

static HRESULT APIENTRY
Rpi3Vc4UmSetDecodeRenderTarget(
    HANDLE DeviceHandle,
    CONST D3DDDIARG_SETDECODERENDERTARGET *Data)
{
    RPI3VC4UM_VIDEO_DEVICE *Device =
        Rpi3Vc4UmVideoDevice(DeviceHandle);
    RPI3VC4UM_DECODER *Decoder;
    RPI3VC4UM_DECODE_TARGET *Target;
    SOFTGPUUM_RESOURCE_MAPPING Mapping;
    HRESULT Result;

    if (Device == NULL || Data == NULL || Data->hRenderTarget == NULL)
        return E_INVALIDARG;

    Decoder = Rpi3Vc4UmAcquireDecoder(Device, Data->hDecode);
    if (Decoder == NULL)
        return E_INVALIDARG;
    EnterCriticalSection(&Decoder->Lock);
    if (Decoder->FrameBegun)
    {
        Result = E_INVALIDARG;
        goto Exit;
    }

    Result = SoftGpuUmMapResource(
                 DeviceHandle,
                 Data->hRenderTarget,
                 Data->SubResourceIndex,
                 TRUE,
                 &Mapping);
    if (FAILED(Result))
        goto Exit;
    if (Mapping.Format != SOFTGPU_D3DDDIFMT_NV12 ||
        Mapping.Width < Decoder->VideoDesc.SampleWidth ||
        Mapping.Height < Decoder->VideoDesc.SampleHeight)
    {
        Result = E_INVALIDARG;
    }
    if (FAILED(SoftGpuUmUnmapResource(DeviceHandle, &Mapping)) &&
        SUCCEEDED(Result))
    {
        Result = E_FAIL;
    }
    if (FAILED(Result))
        goto Exit;

    Target = Rpi3Vc4UmFindTargetLocked(
                 Decoder,
                 Data->hRenderTarget,
                 Data->SubResourceIndex);
    if (Target == NULL)
    {
        Target = (RPI3VC4UM_DECODE_TARGET *)HeapAlloc(
                     GetProcessHeap(),
                     HEAP_ZERO_MEMORY,
                     sizeof(*Target));
        if (Target == NULL)
        {
            Result = E_OUTOFMEMORY;
            goto Exit;
        }
        Target->CompletionEvent = CreateEventW(NULL, TRUE, TRUE, NULL);
        if (Target->CompletionEvent == NULL)
        {
            Result = HRESULT_FROM_WIN32(GetLastError());
            HeapFree(GetProcessHeap(), 0, Target);
            goto Exit;
        }
        Target->Resource = Data->hRenderTarget;
        Target->SubResourceIndex = Data->SubResourceIndex;
        Target->Result = S_OK;
        Target->Next = Decoder->Targets;
        Decoder->Targets = Target;
    }
    if (Target->Pending)
    {
        Result = E_PENDING;
        goto Exit;
    }

    Decoder->RenderTarget = Data->hRenderTarget;
    Decoder->RenderTargetIndex = Data->SubResourceIndex;
    Decoder->CurrentTarget = Target;
    Result = S_OK;

Exit:
    LeaveCriticalSection(&Decoder->Lock);
    Rpi3Vc4UmReleaseDecoder(Decoder);
    return Result;
}

static HRESULT APIENTRY
Rpi3Vc4UmDecodeBeginFrame(
    HANDLE DeviceHandle,
    D3DDDIARG_DECODEBEGINFRAME *Data)
{
    RPI3VC4UM_VIDEO_DEVICE *Device =
        Rpi3Vc4UmVideoDevice(DeviceHandle);
    RPI3VC4UM_DECODER *Decoder;

    if (Device == NULL || Data == NULL || Data->pPVPSetKey != NULL)
        return E_INVALIDARG;

    Decoder = Rpi3Vc4UmAcquireDecoder(Device, Data->hDecode);
    if (Decoder == NULL)
        return E_INVALIDARG;
    EnterCriticalSection(&Decoder->Lock);
    if (Decoder->FrameBegun ||
        Decoder->RenderTarget == NULL ||
        Decoder->CurrentTarget == NULL ||
        Decoder->CurrentTarget->Pending)
    {
        LeaveCriticalSection(&Decoder->Lock);
        Rpi3Vc4UmReleaseDecoder(Decoder);
        return E_INVALIDARG;
    }
    Decoder->FrameBegun = TRUE;
    Decoder->PictureValid = FALSE;
    Decoder->MatrixValid = FALSE;
    Decoder->BitstreamSize = 0;
    LeaveCriticalSection(&Decoder->Lock);
    Rpi3Vc4UmReleaseDecoder(Decoder);
    return S_OK;
}

static HRESULT
Rpi3Vc4UmValidateSlices(
    CONST BYTE *SliceData,
    UINT SliceDataSize,
    UINT BitstreamSize,
    UINT *PayloadSize)
{
    CONST DXVA_Slice_H264_Short *Slices;
    UINT SliceCount;
    UINT MaximumEnd = 0;
    UINT Index;

    if (SliceData == NULL || PayloadSize == NULL ||
        SliceDataSize == 0 ||
        SliceDataSize % sizeof(*Slices) != 0)
    {
        return E_INVALIDARG;
    }

    Slices = (CONST DXVA_Slice_H264_Short *)SliceData;
    SliceCount = SliceDataSize / sizeof(*Slices);
    for (Index = 0; Index < SliceCount; ++Index)
    {
        UINT End;

        if (Slices[Index].SliceBytesInBuffer == 0 ||
            Slices[Index].wBadSliceChopping != 0 ||
            Slices[Index].BSNALunitDataLocation > BitstreamSize ||
            Slices[Index].SliceBytesInBuffer >
                BitstreamSize - Slices[Index].BSNALunitDataLocation)
        {
            return E_INVALIDARG;
        }
        End = Slices[Index].BSNALunitDataLocation +
              Slices[Index].SliceBytesInBuffer;
        if (End > MaximumEnd)
            MaximumEnd = End;
    }
    *PayloadSize = MaximumEnd;
    return S_OK;
}

static HRESULT APIENTRY
Rpi3Vc4UmDecodeExecute(
    HANDLE DeviceHandle,
    CONST D3DDDIARG_DECODEEXECUTE *Data)
{
    RPI3VC4UM_VIDEO_DEVICE *Device =
        Rpi3Vc4UmVideoDevice(DeviceHandle);
    RPI3VC4UM_DECODER *Decoder;
    SOFTGPUUM_RESOURCE_MAPPING *Mappings;
    CONST BYTE *Bitstream = NULL;
    CONST BYTE *SliceData = NULL;
    UINT BitstreamSize = 0;
    UINT SliceDataSize = 0;
    UINT PayloadSize;
    UINT Index;
    UINT MappedCount = 0;
    HRESULT Result = S_OK;

    if (Device == NULL || Data == NULL || Data->NumCompBuffers == 0 ||
        Data->NumCompBuffers > 64 || Data->pCompressedBuffers == NULL)
    {
        return E_INVALIDARG;
    }

    Decoder = Rpi3Vc4UmAcquireDecoder(Device, Data->hDecode);
    if (Decoder == NULL)
        return E_INVALIDARG;
    EnterCriticalSection(&Decoder->Lock);
    if (!Decoder->FrameBegun)
    {
        LeaveCriticalSection(&Decoder->Lock);
        Rpi3Vc4UmReleaseDecoder(Decoder);
        return E_INVALIDARG;
    }

    Mappings = (SOFTGPUUM_RESOURCE_MAPPING *)HeapAlloc(
                   GetProcessHeap(),
                   HEAP_ZERO_MEMORY,
                   Data->NumCompBuffers * sizeof(*Mappings));
    if (Mappings == NULL)
    {
        LeaveCriticalSection(&Decoder->Lock);
        Rpi3Vc4UmReleaseDecoder(Decoder);
        return E_OUTOFMEMORY;
    }

    for (Index = 0; Index < Data->NumCompBuffers; ++Index)
    {
        CONST DXVADDI_DECODEBUFFERDESC *Description =
            &Data->pCompressedBuffers[Index];
        CONST BYTE *Buffer;

        if (Description->hBuffer == NULL ||
            Description->pCipherCounter != NULL)
        {
            Result = E_INVALIDARG;
            break;
        }
        Result = SoftGpuUmMapResource(
                     DeviceHandle,
                     Description->hBuffer,
                     0,
                     FALSE,
                     &Mappings[Index]);
        if (FAILED(Result))
            break;
        ++MappedCount;
        if (Mappings[Index].Format != Description->CompressedBufferType ||
            Description->DataOffset > Mappings[Index].Size ||
            Description->DataSize >
                Mappings[Index].Size - Description->DataOffset)
        {
            Result = E_INVALIDARG;
            break;
        }
        Buffer = (CONST BYTE *)Mappings[Index].Data +
                 Description->DataOffset;

        switch (Description->CompressedBufferType)
        {
            case D3DDDIFMT_PICTUREPARAMSDATA:
                if (Description->DataSize < sizeof(Decoder->Picture))
                {
                    Result = E_INVALIDARG;
                    break;
                }
                CopyMemory(&Decoder->Picture,
                           Buffer,
                           sizeof(Decoder->Picture));
                Decoder->PictureValid = TRUE;
                break;

            case D3DDDIFMT_INVERSEQUANTIZATIONDATA:
                if (Description->DataSize < sizeof(Decoder->Matrix))
                {
                    Result = E_INVALIDARG;
                    break;
                }
                CopyMemory(&Decoder->Matrix,
                           Buffer,
                           sizeof(Decoder->Matrix));
                Decoder->MatrixValid = TRUE;
                break;

            case D3DDDIFMT_SLICECONTROLDATA:
                if (SliceData != NULL)
                {
                    Result = E_INVALIDARG;
                    break;
                }
                SliceData = Buffer;
                SliceDataSize = Description->DataSize;
                break;

            case D3DDDIFMT_BITSTREAMDATA:
                if (Bitstream != NULL)
                {
                    Result = E_INVALIDARG;
                    break;
                }
                Bitstream = Buffer;
                BitstreamSize = Description->DataSize;
                break;

            default:
                Result = E_NOTIMPL;
                break;
        }
        if (FAILED(Result))
            break;
    }

    if (SUCCEEDED(Result) && Bitstream != NULL)
    {
        Result = Rpi3Vc4UmValidateSlices(
                     SliceData,
                     SliceDataSize,
                     BitstreamSize,
                     &PayloadSize);
        if (SUCCEEDED(Result))
            Result = Rpi3Vc4UmReserveBitstream(Decoder, PayloadSize);
        if (SUCCEEDED(Result))
        {
            CopyMemory(Decoder->Bitstream + Decoder->BitstreamSize,
                       Bitstream,
                       PayloadSize);
            Decoder->BitstreamSize += PayloadSize;
        }
    }
    else if (SUCCEEDED(Result) && SliceData != NULL)
    {
        Result = E_INVALIDARG;
    }

    while (MappedCount != 0)
    {
        --MappedCount;
        if (FAILED(SoftGpuUmUnmapResource(
                       DeviceHandle,
                       &Mappings[MappedCount])) &&
            SUCCEEDED(Result))
        {
            Result = E_FAIL;
        }
    }
    HeapFree(GetProcessHeap(), 0, Mappings);
    LeaveCriticalSection(&Decoder->Lock);
    Rpi3Vc4UmReleaseDecoder(Decoder);
    return Result;
}

static HRESULT APIENTRY
Rpi3Vc4UmDecodeEndFrame(
    HANDLE DeviceHandle,
    D3DDDIARG_DECODEENDFRAME *Data)
{
    RPI3VC4UM_VIDEO_DEVICE *Device =
        Rpi3Vc4UmVideoDevice(DeviceHandle);
    RPI3VC4UM_DECODER *Decoder;
    RPI3VC4UM_DECODE_TARGET *Target;
    BYTE ParameterSets[RPI3_H264_PARAMETER_SET_SIZE];
    BYTE *SubmitData;
    CONST BYTE *SubmitBitstream;
    UINT SubmitSize;
    UINT ParameterSetSize;
    UINT32 PpsId;
    UINT Flags;
    LONGLONG FrameToken;
    BOOL ParameterSetsChanged;
    HRESULT Result;

    if (Device == NULL || Data == NULL || Data->pHandleComplete != NULL)
        return E_INVALIDARG;

    Decoder = Rpi3Vc4UmAcquireDecoder(Device, Data->hDecode);
    if (Decoder == NULL)
        return E_INVALIDARG;
    EnterCriticalSection(&Decoder->Lock);
    if (!Decoder->FrameBegun)
    {
        Result = E_INVALIDARG;
        goto Exit;
    }
    Decoder->FrameBegun = FALSE;
    if (!Decoder->PictureValid || Decoder->BitstreamSize == 0)
    {
        Result = E_INVALIDARG;
        goto Exit;
    }
    Result = Rpi3Vc4UmValidatePicture(Decoder);
    if (FAILED(Result))
        goto Exit;

    if (!rpi3_h264_get_pps_id(
             Decoder->Bitstream,
             Decoder->BitstreamSize,
             &PpsId) || PpsId > 255)
    {
        Result = E_INVALIDARG;
        goto Exit;
    }
    ParameterSetSize = rpi3_h264_build_parameter_sets(
                           &Decoder->Picture,
                           Decoder->MatrixValid ? &Decoder->Matrix : NULL,
                           PpsId,
                           Decoder->VideoDesc.SampleWidth,
                           Decoder->VideoDesc.SampleHeight,
                           ParameterSets,
                           sizeof(ParameterSets));
    if (ParameterSetSize == 0)
    {
        Result = E_NOTIMPL;
        goto Exit;
    }

    ParameterSetsChanged = Decoder->ParameterSetSize != ParameterSetSize ||
                           memcmp(Decoder->ParameterSets,
                                  ParameterSets,
                                  ParameterSetSize) != 0;

    if (Decoder->MmalDecoder == NULL)
    {
        Result = Rpi3MmalCreateH264DecoderEx(
                     Decoder->VideoDesc.SampleWidth,
                     Decoder->VideoDesc.SampleHeight,
                     NULL,
                     0,
                     RPI3_MMAL_DECODER_OUTPUT_DECODE_ORDER,
                     &Decoder->MmalDecoder);
        if (FAILED(Result))
            goto Exit;

        Decoder->OutputThread = CreateThread(NULL,
                                             0,
                                             Rpi3Vc4UmOutputThread,
                                             Decoder,
                                             0,
                                             NULL);
        if (Decoder->OutputThread == NULL)
        {
            Result = HRESULT_FROM_WIN32(GetLastError());
            Rpi3MmalDestroyDecoder(Decoder->MmalDecoder);
            Decoder->MmalDecoder = NULL;
            goto Exit;
        }
    }

    Target = Decoder->CurrentTarget;
    if (Target == NULL || Target->Pending)
    {
        Result = E_PENDING;
        goto Exit;
    }
    FrameToken = ++Decoder->NextFrameToken;
    if (FrameToken <= 0)
    {
        Decoder->NextFrameToken = 1;
        FrameToken = 1;
    }
    Target->FrameToken = FrameToken;
    Target->Result = E_PENDING;
    Target->Pending = TRUE;
    ResetEvent(Target->CompletionEvent);

    Flags = RPI3_MMAL_SUBMIT_FRAME_START |
            RPI3_MMAL_SUBMIT_FRAME_END;
    if (Decoder->Picture.IntraPicFlag)
        Flags |= RPI3_MMAL_SUBMIT_KEYFRAME;

    SubmitData = NULL;
    SubmitBitstream = Decoder->Bitstream;
    SubmitSize = Decoder->BitstreamSize;
    if (ParameterSetsChanged)
    {
        if (ParameterSetSize > MAXUINT - Decoder->BitstreamSize)
        {
            Result = E_OUTOFMEMORY;
            Rpi3Vc4UmCompleteTargetLocked(Target, Result);
            goto Exit;
        }
        SubmitSize = ParameterSetSize + Decoder->BitstreamSize;
        SubmitData = HeapAlloc(GetProcessHeap(), 0, SubmitSize);
        if (SubmitData == NULL)
        {
            Result = E_OUTOFMEMORY;
            Rpi3Vc4UmCompleteTargetLocked(Target, Result);
            goto Exit;
        }
        CopyMemory(SubmitData, ParameterSets, ParameterSetSize);
        CopyMemory(SubmitData + ParameterSetSize,
                   Decoder->Bitstream,
                   Decoder->BitstreamSize);
        SubmitBitstream = SubmitData;
    }
    Result = Rpi3MmalSubmit(
                 Decoder->MmalDecoder,
                 SubmitBitstream,
                 SubmitSize,
                 Flags,
                 FrameToken,
                 _I64_MIN);
    if (SubmitData != NULL)
        HeapFree(GetProcessHeap(), 0, SubmitData);
    if (FAILED(Result))
    {
        Rpi3Vc4UmCompleteTargetLocked(Target, Result);
        goto Exit;
    }
    if (ParameterSetsChanged)
    {
        CopyMemory(Decoder->ParameterSets, ParameterSets, ParameterSetSize);
        Decoder->ParameterSetSize = ParameterSetSize;
    }
    SetEvent(Decoder->OutputWakeEvent);
    Result = S_OK;

Exit:
    LeaveCriticalSection(&Decoder->Lock);
    Rpi3Vc4UmReleaseDecoder(Decoder);
    return Result;
}

static HRESULT APIENTRY
Rpi3Vc4UmDecodeExtensionExecute(
    HANDLE DeviceHandle,
    CONST D3DDDIARG_DECODEEXTENSIONEXECUTE *Data)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Data);
    return E_NOTIMPL;
}

static HRESULT APIENTRY
Rpi3Vc4UmCreateVideoDevice(
    HANDLE Device,
    D3DDDI_DEVICEFUNCS *Functions,
    VOID **Context)
{
    RPI3VC4UM_VIDEO_DEVICE *VideoDevice;

    if (Device == NULL || Functions == NULL || Context == NULL)
        return E_INVALIDARG;
    *Context = NULL;
    VideoDevice = (RPI3VC4UM_VIDEO_DEVICE *)HeapAlloc(
                      GetProcessHeap(),
                      HEAP_ZERO_MEMORY,
                      sizeof(*VideoDevice));
    if (VideoDevice == NULL)
        return E_OUTOFMEMORY;

    VideoDevice->Magic = RPI3VC4UM_DEVICE_MAGIC;
    VideoDevice->CoreDevice = Device;
    InitializeCriticalSection(&VideoDevice->Lock);

    Functions->pfnCreateDecodeDevice = Rpi3Vc4UmCreateDecodeDevice;
    Functions->pfnDestroyDecodeDevice = Rpi3Vc4UmDestroyDecodeDevice;
    Functions->pfnSetDecodeRenderTarget = Rpi3Vc4UmSetDecodeRenderTarget;
    Functions->pfnDecodeBeginFrame = Rpi3Vc4UmDecodeBeginFrame;
    Functions->pfnDecodeEndFrame = Rpi3Vc4UmDecodeEndFrame;
    Functions->pfnDecodeExecute = Rpi3Vc4UmDecodeExecute;
    Functions->pfnDecodeExtensionExecute =
        Rpi3Vc4UmDecodeExtensionExecute;
    *Context = VideoDevice;
    return S_OK;
}

static HRESULT APIENTRY
Rpi3Vc4UmCanDestroyVideoDevice(
    HANDLE Device,
    VOID *Context)
{
    RPI3VC4UM_VIDEO_DEVICE *VideoDevice =
        (RPI3VC4UM_VIDEO_DEVICE *)Context;
    HRESULT Result;

    UNREFERENCED_PARAMETER(Device);
    if (VideoDevice == NULL ||
        VideoDevice->Magic != RPI3VC4UM_DEVICE_MAGIC)
    {
        return E_INVALIDARG;
    }
    EnterCriticalSection(&VideoDevice->Lock);
    Result = VideoDevice->DecoderCount == 0 ? S_OK : E_FAIL;
    LeaveCriticalSection(&VideoDevice->Lock);
    return Result;
}

static VOID APIENTRY
Rpi3Vc4UmDestroyVideoDevice(
    HANDLE Device,
    VOID *Context)
{
    RPI3VC4UM_VIDEO_DEVICE *VideoDevice =
        (RPI3VC4UM_VIDEO_DEVICE *)Context;

    UNREFERENCED_PARAMETER(Device);
    if (VideoDevice == NULL)
        return;
    VideoDevice->Magic = 0;
    DeleteCriticalSection(&VideoDevice->Lock);
    HeapFree(GetProcessHeap(), 0, VideoDevice);
}

static HRESULT APIENTRY
Rpi3Vc4UmGetSurfaceLayout(
    CONST D3DDDIARG_CREATERESOURCE *Resource,
    CONST D3DDDI_SURFACEINFO *Surface,
    SOFTGPUUM_SURFACE_LAYOUT *Layout)
{
    ULONGLONG Size;

    if (Resource == NULL || Surface == NULL || Layout == NULL ||
        Surface->Width == 0 || Surface->Height == 0 ||
        Surface->Depth > 1 || Surface->pSysMem != NULL ||
        Surface->SysMemPitch != 0 || Surface->SysMemSlicePitch != 0)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(Layout, sizeof(*Layout));
    if (Resource->Format == SOFTGPU_D3DDDIFMT_NV12)
    {
        if (!Resource->Flags.DecodeRenderTarget ||
            Resource->Flags.DecodeCompressedBuffer ||
            (Surface->Width & 1) != 0 || (Surface->Height & 1) != 0 ||
            Surface->Width > 1920 || Surface->Height > 1088)
        {
            return E_INVALIDARG;
        }
        Layout->Pitch = (Surface->Width + 31) & ~31u;
        Layout->StorageHeight = (Surface->Height + 15) & ~15u;
        Size = (ULONGLONG)Layout->Pitch *
               (Layout->StorageHeight + Layout->StorageHeight / 2);
        Layout->BitsPerPixel = 12;
        Layout->PlaneCount = 2;
        Layout->PlaneOffsets[0] = 0;
        Layout->PlaneOffsets[1] =
            Layout->Pitch * Layout->StorageHeight;
        Layout->PlanePitches[0] = Layout->Pitch;
        Layout->PlanePitches[1] = Layout->Pitch;
    }
    else if (Resource->Format == D3DDDIFMT_PICTUREPARAMSDATA ||
             Resource->Format == D3DDDIFMT_INVERSEQUANTIZATIONDATA ||
             Resource->Format == D3DDDIFMT_SLICECONTROLDATA ||
             Resource->Format == D3DDDIFMT_BITSTREAMDATA)
    {
        if (!Resource->Flags.DecodeCompressedBuffer ||
            Resource->Flags.DecodeRenderTarget ||
            Surface->Width > RPI3_H264_MAX_FRAME_SIZE)
        {
            return E_INVALIDARG;
        }
        Layout->Pitch = (Surface->Width + 15) & ~15u;
        Layout->StorageHeight = Surface->Height;
        Size = (ULONGLONG)Layout->Pitch * Surface->Height;
        Layout->BitsPerPixel = 8;
        Layout->PlaneCount = 1;
        Layout->PlaneOffsets[0] = 0;
        Layout->PlanePitches[0] = Layout->Pitch;
    }
    else
    {
        return E_NOTIMPL;
    }

    if (Size == 0 || Size > RPI3_H264_MAX_FRAME_SIZE)
        return E_INVALIDARG;
    Layout->Size = (SIZE_T)Size;
    return S_OK;
}

static HRESULT APIENTRY
Rpi3Vc4UmWaitResource(HANDLE Device,
                      VOID *Context,
                      HANDLE Resource,
                      UINT SubResourceIndex,
                      BOOL WriteAccess,
                      BOOL DoNotWait)
{
    RPI3VC4UM_VIDEO_DEVICE *VideoDevice = Context;
    RPI3VC4UM_DECODE_TARGET *Target;
    RPI3VC4UM_DECODER *Decoder = NULL;
    HANDLE WaitHandles[2];
    DWORD WaitStatus;
    HRESULT Result = S_OK;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(WriteAccess);
    if (VideoDevice == NULL ||
        VideoDevice->Magic != RPI3VC4UM_DEVICE_MAGIC ||
        Resource == NULL)
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&VideoDevice->Lock);
    for (Decoder = VideoDevice->Decoders;
         Decoder != NULL;
         Decoder = Decoder->Next)
    {
        EnterCriticalSection(&Decoder->Lock);
        Target = Rpi3Vc4UmFindTargetLocked(
                     Decoder,
                     Resource,
                     SubResourceIndex);
        if (Target != NULL)
        {
            InterlockedIncrement(&Decoder->References);
            LeaveCriticalSection(&Decoder->Lock);
            break;
        }
        LeaveCriticalSection(&Decoder->Lock);
    }
    LeaveCriticalSection(&VideoDevice->Lock);
    if (Decoder == NULL)
        return S_OK;

    EnterCriticalSection(&Decoder->Lock);
    Target = Rpi3Vc4UmFindTargetLocked(
                 Decoder,
                 Resource,
                 SubResourceIndex);
    if (Target == NULL)
    {
        Result = E_INVALIDARG;
        LeaveCriticalSection(&Decoder->Lock);
        goto Exit;
    }
    if (!Target->Pending)
    {
        Result = Target->Result;
        LeaveCriticalSection(&Decoder->Lock);
        goto Exit;
    }
    WaitHandles[0] = Target->CompletionEvent;
    WaitHandles[1] = Decoder->OutputStopEvent;
    LeaveCriticalSection(&Decoder->Lock);

    WaitStatus = WaitForMultipleObjects(ARRAYSIZE(WaitHandles),
                                        WaitHandles,
                                        FALSE,
                                        DoNotWait ? 0 : INFINITE);
    if (WaitStatus == WAIT_TIMEOUT)
    {
        Result = D3DERR_WASSTILLDRAWING;
        goto Exit;
    }
    if (WaitStatus == WAIT_OBJECT_0 + 1)
    {
        Result = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
        goto Exit;
    }
    if (WaitStatus != WAIT_OBJECT_0)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Exit;
    }

    EnterCriticalSection(&Decoder->Lock);
    Target = Rpi3Vc4UmFindTargetLocked(
                 Decoder,
                 Resource,
                 SubResourceIndex);
    Result = Target != NULL ? Target->Result : E_INVALIDARG;
    LeaveCriticalSection(&Decoder->Lock);

Exit:
    Rpi3Vc4UmReleaseDecoder(Decoder);
    return Result;
}

static HRESULT APIENTRY
Rpi3Vc4UmReleaseResource(HANDLE Device,
                         VOID *Context,
                         HANDLE Resource)
{
    RPI3VC4UM_VIDEO_DEVICE *VideoDevice = Context;
    RPI3VC4UM_DECODE_TARGET **Link;
    RPI3VC4UM_DECODE_TARGET *Target;
    RPI3VC4UM_DECODER *Decoder;

    UNREFERENCED_PARAMETER(Device);
    if (VideoDevice == NULL ||
        VideoDevice->Magic != RPI3VC4UM_DEVICE_MAGIC ||
        Resource == NULL)
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&VideoDevice->Lock);
    for (Decoder = VideoDevice->Decoders;
         Decoder != NULL;
         Decoder = Decoder->Next)
    {
        EnterCriticalSection(&Decoder->Lock);
        for (Link = &Decoder->Targets; *Link != NULL;)
        {
            Target = *Link;
            if (Target->Resource != Resource)
            {
                Link = &Target->Next;
                continue;
            }
            if (Target->Pending)
            {
                LeaveCriticalSection(&Decoder->Lock);
                LeaveCriticalSection(&VideoDevice->Lock);
                return E_PENDING;
            }

            *Link = Target->Next;
            if (Decoder->CurrentTarget == Target)
            {
                Decoder->CurrentTarget = NULL;
                Decoder->RenderTarget = NULL;
                Decoder->RenderTargetIndex = 0;
            }
            CloseHandle(Target->CompletionEvent);
            HeapFree(GetProcessHeap(), 0, Target);
        }
        LeaveCriticalSection(&Decoder->Lock);
    }
    LeaveCriticalSection(&VideoDevice->Lock);
    return S_OK;
}

static CONST SOFTGPUUM_BACKEND Rpi3Vc4UmBackend =
{
    Rpi3Vc4UmGetCaps,
    Rpi3Vc4UmCreateVideoDevice,
    Rpi3Vc4UmCanDestroyVideoDevice,
    Rpi3Vc4UmDestroyVideoDevice,
    Rpi3Vc4UmGetSurfaceLayout,
    Rpi3Vc4UmWaitResource,
    Rpi3Vc4UmReleaseResource
};

CONST SOFTGPUUM_BACKEND *APIENTRY
SoftGpuUmGetBackend(VOID)
{
    return &Rpi3Vc4UmBackend;
}
