/*
 * PROJECT:     ReactOS Media Foundation decoders
 * FILE:        dll/win32/msmpeg2vdec/main.c
 * PURPOSE:     Class factories and registration of the H.264 and AAC decoders
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "gst_private.h"

#include "mfapi.h"
#include "mferror.h"
#include "rpcproxy.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);

const GUID MFAudioFormat_RAW_AAC = {WAVE_FORMAT_RAW_AAC1, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
GUID MFVideoFormat_IV50 = {MAKEFOURCC('I','V','5','0'), 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID MEDIASUBTYPE_VC1S = {MAKEFOURCC('V','C','1','S'), 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID MEDIASUBTYPE_WMV_Unknown = {0x7ce12ca9, 0xbfbf, 0x43d9, {0x9d, 0x00, 0x82, 0xb8, 0xed, 0x54, 0x31, 0x6b}};

BOOL init_gstreamer(void)
{
    return TRUE;
}

HRESULT wg_transform_create_quartz(const AM_MEDIA_TYPE *input_format, const AM_MEDIA_TYPE *output_format,
        const struct wg_transform_attrs *attrs, wg_transform_t *transform)
{
    IMFMediaType *input_type, *output_type;
    HRESULT hr;

    if (FAILED(hr = MFCreateMediaTypeFromRepresentation(AM_MEDIA_TYPE_REPRESENTATION, (void *)input_format, &input_type)))
        return hr;
    if (FAILED(hr = MFCreateMediaTypeFromRepresentation(AM_MEDIA_TYPE_REPRESENTATION, (void *)output_format, &output_type)))
    {
        IMFMediaType_Release(input_type);
        return hr;
    }

    hr = wg_transform_create_mf(input_type, output_type, attrs, transform);
    IMFMediaType_Release(output_type);
    IMFMediaType_Release(input_type);
    return hr;
}

struct class_factory
{
    IClassFactory IClassFactory_iface;
    HRESULT (*create_instance)(REFIID riid, void **out);
};

static inline struct class_factory *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, struct class_factory, IClassFactory_iface);
}

static HRESULT WINAPI class_factory_QueryInterface(IClassFactory *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IClassFactory))
    {
        *out = iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }

    *out = NULL;
    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI class_factory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI class_factory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI class_factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out)
{
    struct class_factory *factory = impl_from_IClassFactory(iface);
    IUnknown *unknown;
    HRESULT hr;

    TRACE("iface %p, outer %p, iid %s, out %p.\n", iface, outer, debugstr_guid(iid), out);

    *out = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;

    if (SUCCEEDED(hr = factory->create_instance(&IID_IUnknown, (void **)&unknown)))
    {
        hr = IUnknown_QueryInterface(unknown, iid, out);
        IUnknown_Release(unknown);
    }
    return hr;
}

static HRESULT WINAPI class_factory_LockServer(IClassFactory *iface, BOOL lock)
{
    TRACE("iface %p, lock %d.\n", iface, lock);
    return S_OK;
}

static const IClassFactoryVtbl class_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_CreateInstance,
    class_factory_LockServer,
};

static struct class_factory h264_decoder_factory = {{&class_factory_vtbl}, h264_decoder_create};
static struct class_factory aac_decoder_factory = {{&class_factory_vtbl}, aac_decoder_create};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    TRACE("clsid %s, iid %s, out %p.\n", debugstr_guid(clsid), debugstr_guid(iid), out);

    *out = NULL;
    if (IsEqualGUID(clsid, &CLSID_MSH264DecoderMFT))
        return IClassFactory_QueryInterface(&h264_decoder_factory.IClassFactory_iface, iid, out);
    if (IsEqualGUID(clsid, &CLSID_MSAACDecMFT))
        return IClassFactory_QueryInterface(&aac_decoder_factory.IClassFactory_iface, iid, out);

    FIXME("Unknown class %s.\n", debugstr_guid(clsid));
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllRegisterServer(void)
{
    MFT_REGISTER_TYPE_INFO h264_decoder_inputs[] =
    {
        {MFMediaType_Video, MFVideoFormat_H264},
        {MFMediaType_Video, MFVideoFormat_H264_ES},
    };
    MFT_REGISTER_TYPE_INFO h264_decoder_outputs[] =
    {
        {MFMediaType_Video, MFVideoFormat_NV12},
        {MFMediaType_Video, MFVideoFormat_YV12},
        {MFMediaType_Video, MFVideoFormat_IYUV},
        {MFMediaType_Video, MFVideoFormat_I420},
        {MFMediaType_Video, MFVideoFormat_YUY2},
    };
    MFT_REGISTER_TYPE_INFO aac_decoder_inputs[] =
    {
        {MFMediaType_Audio, MFAudioFormat_AAC},
        {MFMediaType_Audio, MFAudioFormat_RAW_AAC},
        {MFMediaType_Audio, MFAudioFormat_ADTS},
    };
    MFT_REGISTER_TYPE_INFO aac_decoder_outputs[] =
    {
        {MFMediaType_Audio, MFAudioFormat_Float},
        {MFMediaType_Audio, MFAudioFormat_PCM},
    };
    HRESULT hr;

    TRACE(".\n");

    if (FAILED(hr = __wine_register_resources()))
        return hr;
    if (FAILED(hr = MFTRegister(CLSID_MSH264DecoderMFT, MFT_CATEGORY_VIDEO_DECODER,
            (WCHAR *)L"Microsoft H264 Video Decoder MFT", MFT_ENUM_FLAG_SYNCMFT,
            ARRAY_SIZE(h264_decoder_inputs), h264_decoder_inputs,
            ARRAY_SIZE(h264_decoder_outputs), h264_decoder_outputs, NULL)))
        return hr;
    return MFTRegister(CLSID_MSAACDecMFT, MFT_CATEGORY_AUDIO_DECODER,
            (WCHAR *)L"Microsoft AAC Audio Decoder MFT", MFT_ENUM_FLAG_SYNCMFT,
            ARRAY_SIZE(aac_decoder_inputs), aac_decoder_inputs,
            ARRAY_SIZE(aac_decoder_outputs), aac_decoder_outputs, NULL);
}

HRESULT WINAPI DllUnregisterServer(void)
{
    HRESULT hr;

    TRACE(".\n");

    if (FAILED(hr = __wine_unregister_resources()))
        return hr;
    MFTUnregister(CLSID_MSH264DecoderMFT);
    MFTUnregister(CLSID_MSAACDecMFT);
    return S_OK;
}
