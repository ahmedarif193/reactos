/*
 * PROJECT:     ReactOS Explorer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Rasterize the Start orb at the taskbar's actual DPI
 */

#include "precomp.h"
#include <wincodec.h>

/* Average source pixel coverage, including alpha, instead of point-sampling
 * thin strokes. Coordinates are integers in units of 1 / Size source pixels.
 * Keep RGB premultiplied while filtering, then return straight BGRA: comctl32
 * premultiplies the image list pixels when drawing them.
 *
 * WIC's scaler currently implements only nearest-neighbor, even when Fant is
 * requested. Do the coverage filtering here; WIC is used only to decode PNG.
 */
static void
ScaleStartOrb(const BYTE *Source, UINT Width, UINT Height, BYTE *Dest, UINT Size)
{
    const ULONGLONG Area = (ULONGLONG)Width * Height;

    for (UINT y = 0; y < Size; ++y)
    {
        const UINT Top = y * Height, Bottom = (y + 1) * Height;
        for (UINT x = 0; x < Size; ++x)
        {
            const UINT Left = x * Width, Right = (x + 1) * Width;
            ULONGLONG Alpha = 0, Color[3] = {0, 0, 0};

            for (UINT sy = Top / Size; sy < (Bottom + Size - 1) / Size; ++sy)
            {
                const UINT dy = min(Bottom, (sy + 1) * Size) - max(Top, sy * Size);
                for (UINT sx = Left / Size; sx < (Right + Size - 1) / Size; ++sx)
                {
                    const UINT dx = min(Right, (sx + 1) * Size) - max(Left, sx * Size);
                    const BYTE *Pixel = Source + (sy * Width + sx) * 4;
                    const ULONGLONG Weight = (ULONGLONG)dx * dy * Pixel[3];
                    Alpha += Weight;
                    for (UINT c = 0; c < 3; ++c)
                        Color[c] += Weight * Pixel[c];
                }
            }

            BYTE *Pixel = Dest + (y * Size + x) * 4;
            Pixel[3] = (BYTE)((Alpha + Area / 2) / Area);
            for (UINT c = 0; c < 3; ++c)
                Pixel[c] = Pixel[3] ? (BYTE)((Color[c] + Alpha / 2) / Alpha) : 0;
        }
    }
}

HBITMAP CreateStartOrbBitmap(INT Size)
{
    /* Bound allocation sizes and the filter's coordinate arithmetic. */
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
    HRESULT hr = Factory.CoCreateInstance(CLSID_WICImagingFactory, IID_IWICImagingFactory,
                                         NULL, CLSCTX_INPROC_SERVER);
    if (FAILED(hr))
        return NULL;
    hr = Factory->CreateDecoderFromStream(Stream, NULL, WICDecodeMetadataCacheOnLoad, &Decoder);
    if (FAILED(hr) || FAILED(Decoder->GetFrame(0, &Frame)))
        return NULL;
    hr = Factory->CreateFormatConverter(&Converter);
    if (FAILED(hr))
        return NULL;
    hr = Converter->Initialize(Frame, GUID_WICPixelFormat32bppBGRA,
                               WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        return NULL;

    UINT Width, Height;
    hr = Converter->GetSize(&Width, &Height);
    if (FAILED(hr) || !Width || !Height || Width > 4096 || Height > 4096)
        return NULL;

    const UINT Stride = Width * 4, BufferSize = Stride * Height;
    BYTE *Source = (BYTE *)HeapAlloc(GetProcessHeap(), 0, BufferSize);
    if (!Source)
        return NULL;
    hr = Converter->CopyPixels(NULL, Stride, BufferSize, Source);

    HBITMAP Bitmap = NULL;
    if (SUCCEEDED(hr))
    {
        BITMAPINFO Info = {};
        Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        Info.bmiHeader.biWidth = Size;
        Info.bmiHeader.biHeight = -Size;
        Info.bmiHeader.biPlanes = 1;
        Info.bmiHeader.biBitCount = 32;
        Info.bmiHeader.biCompression = BI_RGB;
        BYTE *Bits;
        Bitmap = CreateDIBSection(NULL, &Info, DIB_RGB_COLORS, (void **)&Bits, NULL, 0);
        if (Bitmap)
            ScaleStartOrb(Source, Width, Height, Bits, Size);
    }
    HeapFree(GetProcessHeap(), 0, Source);
    return Bitmap;
}
