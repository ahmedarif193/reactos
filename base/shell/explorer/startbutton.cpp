/*
 * PROJECT:     ReactOS Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Rasterize the Start orb at the taskbar's actual DPI
 */

#include "precomp.h"
#include <wincodec.h>

HBITMAP CreateStartOrbBitmap(INT Size)
{
    /* Bound the destination DIB allocation. */
    if (Size <= 0 || Size > 4096)
        return NULL;

    HRSRC hResource = FindResourceW(hExplorerInstance, MAKEINTRESOURCEW(IDI_STARTORB), RT_RCDATA);
    if (!hResource)
        return NULL;
    HGLOBAL hData = LoadResource(hExplorerInstance, hResource);
    const BYTE *Data = (const BYTE *)LockResource(hData);
    DWORD DataSize = SizeofResource(hExplorerInstance, hResource);
    if (!Data || !DataSize)
        return NULL;

    CComPtr<IStream> Stream;
    Stream.Attach(SHCreateMemStream(Data, DataSize));
    if (!Stream)
        return NULL;

    CComPtr<IWICImagingFactory> Factory;
    CComPtr<IWICBitmapDecoder> Decoder;
    CComPtr<IWICBitmapFrameDecode> Frame;
    CComPtr<IWICFormatConverter> Converter;
    CComPtr<IWICBitmapScaler> Scaler;
    HRESULT hr = Factory.CoCreateInstance(CLSID_WICImagingFactory, IID_IWICImagingFactory, NULL, CLSCTX_INPROC_SERVER);
    if (FAILED(hr))
        return NULL;
    hr = Factory->CreateDecoderFromStream(Stream, NULL, WICDecodeMetadataCacheOnLoad, &Decoder);
    if (FAILED(hr) || FAILED(Decoder->GetFrame(0, &Frame)))
        return NULL;
    hr = Factory->CreateFormatConverter(&Converter);
    if (FAILED(hr))
        return NULL;
    hr = Converter->Initialize(Frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        return NULL;

    UINT Width, Height;
    hr = Converter->GetSize(&Width, &Height);
    if (FAILED(hr) || !Width || !Height || Width > 4096 || Height > 4096)
        return NULL;

    hr = Factory->CreateBitmapScaler(&Scaler);
    if (FAILED(hr))
        return NULL;
    hr = Scaler->Initialize(Converter, Size, Size, WICBitmapInterpolationModeHighQualityCubic);
    if (FAILED(hr))
        return NULL;

    BITMAPINFO Info = {};
    Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info.bmiHeader.biWidth = Size;
    Info.bmiHeader.biHeight = -Size;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    BYTE *Bits;
    HBITMAP Bitmap = CreateDIBSection(NULL, &Info, DIB_RGB_COLORS, (void **)&Bits, NULL, 0);
    if (Bitmap)
    {
        hr = Scaler->CopyPixels(NULL, Size * 4, Size * Size * 4, Bits);
        if (FAILED(hr))
        {
            DeleteObject(Bitmap);
            Bitmap = NULL;
        }
    }
    return Bitmap;
}
