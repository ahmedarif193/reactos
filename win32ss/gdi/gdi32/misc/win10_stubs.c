/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS gdi32.dll
 * FILE:            win32ss/gdi/gdi32/misc/win10_stubs.c
 * PURPOSE:         Stub implementations for Windows 10 GDI32 APIs
 * PROGRAMMERS:     ReactOS Development Team
 */

/* INCLUDES *******************************************************************/

#include <precomp.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(gdi32);

/* FUNCTIONS ******************************************************************/

/*
 * Windows 10 DirectWrite APIs
 */

HRESULT
WINAPI
DWriteCreateFactory(DWRITE_FACTORY_TYPE factoryType,
                    REFIID iid,
                    IUnknown **factory)
{
    WARN("DWriteCreateFactory stub\n");
    return E_NOTIMPL;
}

/*
 * Windows 10 Color Management APIs
 */

BOOL
WINAPI
GetColorProfileFromHandle(HPROFILE hProfile,
                          PBYTE pBuffer,
                          PDWORD pcbSize)
{
    WARN("GetColorProfileFromHandle stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
SetColorProfileFromHandle(HPROFILE hProfile,
                          PBYTE pBuffer)
{
    WARN("SetColorProfileFromHandle stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetStandardColorSpaceProfileA(DWORD dwSCS,
                               PCHAR pszFilename,
                               PCHAR pszSize,
                               PDWORD pcbSize)
{
    WARN("GetStandardColorSpaceProfileA stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetStandardColorSpaceProfileW(DWORD dwSCS,
                               PWCHAR pszFilename,
                               PWCHAR pszSize,
                               PDWORD pcbSize)
{
    WARN("GetStandardColorSpaceProfileW stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HTRANSFORM
WINAPI
CreateColorTransformA(LPLOGCOLORSPACEA pLogColorSpace,
                      HPROFILE hDestProfile,
                      HPROFILE hTargetProfile,
                      DWORD dwFlags)
{
    WARN("CreateColorTransformA stub\n");
    return NULL;
}

HTRANSFORM
WINAPI
CreateColorTransformW(LPLOGCOLORSPACEW pLogColorSpace,
                      HPROFILE hDestProfile,
                      HPROFILE hTargetProfile,
                      DWORD dwFlags)
{
    WARN("CreateColorTransformW stub\n");
    return NULL;
}

BOOL
WINAPI
DeleteColorTransform(HTRANSFORM hxform)
{
    WARN("DeleteColorTransform stub\n");
    return TRUE;
}

/*
 * Windows 10 Hardware Acceleration APIs
 */

BOOL
WINAPI
GetGraphicsAdapterInfo(PGRAPHICS_ADAPTER_INFO pAdapterInfo)
{
    WARN("GetGraphicsAdapterInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

LUID
WINAPI
GetGraphicsAdapterLuid(HMONITOR hMonitor)
{
    WARN("GetGraphicsAdapterLuid stub\n");
    LUID luid = {0, 0};
    return luid;
}

HANDLE
WINAPI
CreateGraphicsDevice(LUID AdapterLuid,
                     PGRAPHICS_DEVICE_DESC pDeviceDesc)
{
    WARN("CreateGraphicsDevice stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

BOOL
WINAPI
DeleteGraphicsDevice(HANDLE hDevice)
{
    WARN("DeleteGraphicsDevice stub\n");
    return TRUE;
}

/*
 * Windows 10 Advanced Font Rendering APIs
 */

BOOL
WINAPI
GetFontFileInfo(LPCWSTR pszFontFile,
                PFONT_FILE_INFO pFontFileInfo)
{
    WARN("GetFontFileInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetGlyphMetrics(HDC hdc,
                UINT uChar,
                PGLYPHMETRICS pGlyphMetrics)
{
    WARN("GetGlyphMetrics stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetColorGlyphInfo(HDC hdc,
                  UINT uChar,
                  PCOLOR_GLYPH_INFO pColorGlyphInfo)
{
    WARN("GetColorGlyphInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * Windows 10 Enhanced Path and Geometry APIs
 */

BOOL
WINAPI
CreateGeometryRealization(HDC hdc,
                          PGEOMETRY_DESC pGeometryDesc)
{
    WARN("CreateGeometryRealization stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
DeleteGeometryRealization(HANDLE hGeometry)
{
    WARN("DeleteGeometryRealization stub\n");
    return TRUE;
}

HANDLE
WINAPI
CreatePathGeometry(HDC hdc)
{
    WARN("CreatePathGeometry stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

BOOL
WINAPI
OpenPathGeometry(HANDLE hPathGeometry,
                 HDC hdc)
{
    WARN("OpenPathGeometry stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
ClosePathGeometry(HANDLE hPathGeometry)
{
    WARN("ClosePathGeometry stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * Windows 10 Enhanced Bitmap and Image APIs
 */

HRESULT
WINAPI
CreateWICFactory(IWICImagingFactory **ppIWICImagingFactory)
{
    WARN("CreateWICFactory stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateWICBitmapFromMemory(UINT uiWidth,
                          UINT uiHeight,
                          REFWICPixelFormatGUID pixelFormat,
                          UINT cbStride,
                          UINT cbBufferSize,
                          BYTE *pbBuffer,
                          IWICBitmap **ppIBitmap)
{
    WARN("CreateWICBitmapFromMemory stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateWICBitmapFromFile(LPCWSTR wzFilename,
                        IWICBitmap **ppIBitmap)
{
    WARN("CreateWICBitmapFromFile stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateWICBitmapFromHBITMAP(HBITMAP hBitmap,
                           HPALETTE hPalette,
                           WICBitmapAlphaChannelOption options,
                           IWICBitmap **ppIBitmap)
{
    WARN("CreateWICBitmapFromHBITMAP stub\n");
    return E_NOTIMPL;
}

/*
 * Windows 10 Enhanced Text Rendering APIs
 */

HANDLE
WINAPI
CreateTextRenderer(HDC hdc)
{
    WARN("CreateTextRenderer stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

BOOL
WINAPI
SetTextRenderingParams(HANDLE hTextRenderer,
                       PTEXT_RENDERING_PARAMS pParams)
{
    WARN("SetTextRenderingParams stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetTextRenderingParams(HANDLE hTextRenderer,
                       PTEXT_RENDERING_PARAMS pParams)
{
    WARN("GetTextRenderingParams stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
RenderText(HANDLE hTextRenderer,
           LPCWSTR pszText,
           UINT cchText,
           PRECT pRect,
           PTEXT_FORMAT pTextFormat,
           PBRUSH pBrush)
{
    WARN("RenderText stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
MeasureText(HANDLE hTextRenderer,
            LPCWSTR pszText,
            UINT cchText,
            PTEXT_FORMAT pTextFormat,
            PSIZE pSize)
{
    WARN("MeasureText stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * Windows 10 Enhanced Drawing and Effects APIs
 */

HANDLE
WINAPI
CreateDrawingStateBlock(HDC hdc,
                        PDRAWING_STATE_DESC pStateDesc)
{
    WARN("CreateDrawingStateBlock stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

BOOL
WINAPI
SaveDrawingState(HDC hdc,
                 HANDLE hStateBlock)
{
    WARN("SaveDrawingState stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
RestoreDrawingState(HDC hdc,
                    HANDLE hStateBlock)
{
    WARN("RestoreDrawingState stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HANDLE
WINAPI
CreateLayer(HDC hdc,
            PRECT pRect,
            PLAYER_PARAMS pLayerParams)
{
    WARN("CreateLayer stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

BOOL
WINAPI
PushLayer(HDC hdc,
          HANDLE hLayer,
          PRECT pRect)
{
    WARN("PushLayer stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
PopLayer(HDC hdc)
{
    WARN("PopLayer stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HANDLE
WINAPI
CreateEffect(HDC hdc,
             REFEFFECTID effectId,
             PEFFECT_PARAMS pEffectParams)
{
    WARN("CreateEffect stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

BOOL
WINAPI
SetEffectInput(HANDLE hEffect,
               UINT uInputIndex,
               HANDLE hInput)
{
    WARN("SetEffectInput stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
DrawEffect(HDC hdc,
           HANDLE hEffect,
           PRECT pRect)
{
    WARN("DrawEffect stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}