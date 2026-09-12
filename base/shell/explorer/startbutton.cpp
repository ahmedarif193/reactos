/*
 * PROJECT:     ReactOS Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Create the Start button's state images at the taskbar's actual DPI
 */

#include "precomp.h"
#include <wincodec.h>

static HBITMAP CreateStartOrbBitmap(INT Size, UINT ResourceId)
{
    /* Bound the destination DIB allocation. */
    if (Size <= 0 || Size > 4096)
        return NULL;

    HRSRC hResource = FindResourceW(hExplorerInstance, MAKEINTRESOURCEW(ResourceId), RT_RCDATA);
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

HIMAGELIST CreateStartOrbImageList(INT Size)
{
    if (Size <= 0 || Size > 4096)
        return NULL;

    /* BUTTON_IMAGELIST uses the PBS_* state order, including disabled/focused. */
    static const UINT Resources[] =
    {
        IDI_STARTORB,
        IDI_STARTORB_HOVER,
        IDI_STARTORB_PRESSED,
        IDI_STARTORB,
        IDI_STARTORB
    };
    HIMAGELIST ImageList = ImageList_Create(Size, Size, ILC_COLOR32 | ILC_MASK,
                                          _countof(Resources), 1);
    if (!ImageList)
        return NULL;

    UINT State;
    for (State = 0; State < _countof(Resources); ++State)
    {
        HBITMAP Bitmap = CreateStartOrbBitmap(Size, Resources[State]);
        if (!Bitmap)
            break;
        INT Index = ImageList_Add(ImageList, Bitmap, NULL);
        DeleteObject(Bitmap);
        if (Index != (INT)State)
            break;
    }

    if (State == _countof(Resources))
        return ImageList;

    /* A single image is valid for every button state; a partial list is not. */
    if (State > 0 && ImageList_SetImageCount(ImageList, 1))
        return ImageList;

    /* Keep the resource icon as a fallback if the PNG decoder is unavailable. */
    if (State == 0)
    {
        HICON Icon = (HICON)LoadImageW(hExplorerInstance, MAKEINTRESOURCEW(IDI_STARTORB),
                                     IMAGE_ICON, Size, Size, 0);
        if (Icon)
        {
            INT Index = ImageList_AddIcon(ImageList, Icon);
            DestroyIcon(Icon);
            if (Index >= 0)
                return ImageList;
        }
    }

    ImageList_Destroy(ImageList);
    return NULL;
}
