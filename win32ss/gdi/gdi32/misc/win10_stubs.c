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

/* Missing type definitions for Windows 10 APIs */
#ifndef _DWRITE_INCLUDED

/* DirectWrite Factory Types */
typedef enum DWRITE_FACTORY_TYPE
{
    DWRITE_FACTORY_TYPE_SHARED,
    DWRITE_FACTORY_TYPE_ISOLATED
} DWRITE_FACTORY_TYPE;

#endif /* _DWRITE_INCLUDED */

/* Color Management Profile Handle */
#ifndef HPROFILE
typedef HANDLE HPROFILE;
#endif

/* Forward declarations for COM interfaces */
#ifndef __IUnknown_FWD_DEFINED__
#define __IUnknown_FWD_DEFINED__
typedef interface IUnknown IUnknown;
#endif

/* Additional Windows 10 GDI types */
typedef HANDLE HTRANSFORM;
typedef DWORD TAGTYPE;
typedef TAGTYPE *PTAGTYPE;
typedef DWORD BMFORMAT;
typedef DWORD COLORTYPE;
typedef struct _COLOR
{
    DWORD dwIndex;
    COLORREF rgb;
} COLOR, *PCOLOR;
typedef struct _COLOR_NAME
{
    DWORD dwIndex;
    WCHAR szName[32];
} COLOR_NAME, *PCOLOR_NAME;
typedef struct _NAMED_PROFILE_INFO
{
    DWORD dwFlags;
    DWORD dwCount;
    DWORD dwCountDevCoordinates;
    WCHAR szPrefix[32];
    WCHAR szSuffix[32];
} NAMED_PROFILE_INFO, *PNAMED_PROFILE_INFO;
typedef HPROFILE *LPHPROFILE;
typedef BOOL (CALLBACK *PBMCALLBACKFN)(ULONG ulMax, ULONG ulCurrent, LPARAM lParam);
/* FONT_FILE_INFO already defined in ntgdi.h */
typedef struct _GRAPHICS_ADAPTER_INFO
{
    DWORD Size;
    DWORD Flags;
    WCHAR Description[128];
} GRAPHICS_ADAPTER_INFO, *PGRAPHICS_ADAPTER_INFO;

typedef struct _GRAPHICS_DEVICE_DESC
{
    DWORD Size;
    DWORD Flags;
    WCHAR DeviceName[32];
} GRAPHICS_DEVICE_DESC, *PGRAPHICS_DEVICE_DESC;

typedef GLYPHMETRICS *PGLYPHMETRICS;

typedef struct _COLOR_GLYPH_INFO
{
    DWORD Size;
    DWORD Flags;
    COLORREF Color;
} COLOR_GLYPH_INFO, *PCOLOR_GLYPH_INFO;

typedef struct _GEOMETRY_DESC
{
    DWORD Size;
    DWORD Type;
    PVOID Data;
} GEOMETRY_DESC, *PGEOMETRY_DESC;

typedef struct _TEXT_RENDERING_PARAMS
{
    DWORD Size;
    DWORD Flags;
    FLOAT GammaLevel;
} TEXT_RENDERING_PARAMS, *PTEXT_RENDERING_PARAMS;

typedef struct _TEXT_FORMAT
{
    DWORD Size;
    DWORD Flags;
    WCHAR FontFamily[32];
} TEXT_FORMAT, *PTEXT_FORMAT;

typedef struct _BRUSH
{
    DWORD Type;
    COLORREF Color;
} BRUSH, *PBRUSH;

typedef struct _DRAWING_STATE_DESC
{
    DWORD Size;
    DWORD Flags;
} DRAWING_STATE_DESC, *PDRAWING_STATE_DESC;

typedef struct _LAYER_PARAMS
{
    DWORD Size;
    DWORD Flags;
    FLOAT Opacity;
} LAYER_PARAMS, *PLAYER_PARAMS;

typedef GUID EFFECTID, *PEFFECTID, *REFEFFECTID;

typedef struct _EFFECT_PARAMS
{
    DWORD Size;
    DWORD Flags;
} EFFECT_PARAMS, *PEFFECT_PARAMS;

/* WIC (Windows Imaging Component) types */
typedef interface IWICImagingFactory IWICImagingFactory;
typedef interface IWICBitmap IWICBitmap;
typedef GUID WICPixelFormatGUID;
typedef const WICPixelFormatGUID* REFWICPixelFormatGUID;

typedef enum WICBitmapAlphaChannelOption
{
    WICBitmapUseAlpha = 0,
    WICBitmapUsePremultipliedAlpha = 1,
    WICBitmapIgnoreAlpha = 2
} WICBitmapAlphaChannelOption;

/* E_NOTIMPL constant */
#ifndef E_NOTIMPL
#define E_NOTIMPL 0x80004001
#endif

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

/*
 * Additional Windows 10 Direct2D Path Functions
 */

BOOL
WINAPI
AddBezierToPath(HDC hdc, LPPOINT lppt)
{
    WARN("AddBezierToPath stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
AddLineToPath(HDC hdc, LPPOINT lppt)
{
    WARN("AddLineToPath stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
AddArcToPath(HDC hdc, LPPOINT lppt)
{
    WARN("AddArcToPath stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
ApplyFontFeatures(HDC hdc, LPCWSTR lpFeatures)
{
    WARN("ApplyFontFeatures stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
CheckBitmapBits(HBITMAP hBitmap, PVOID pBits, DWORD cbBits)
{
    WARN("CheckBitmapBits stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/* Additional Direct2D/DirectWrite stubs */
HRESULT
WINAPI
CreateCompatibleRenderTarget(PVOID pTarget, PVOID *ppCompatibleTarget)
{
    WARN("CreateCompatibleRenderTarget stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateDWriteFactory(DWORD factoryType, REFIID riid, PVOID *ppFactory)
{
    WARN("CreateDWriteFactory stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateFontCollectionFromFontSet(PVOID pFontSet, PVOID *ppFontCollection)
{
    WARN("CreateFontCollectionFromFontSet stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateFontSetBuilder(PVOID *ppBuilder)
{
    WARN("CreateFontSetBuilder stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
D2D1CreateDevice(PVOID pDevice, PVOID pCreationProperties, PVOID *ppD2DDevice)
{
    WARN("D2D1CreateDevice stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
D2D1CreateDeviceContext(PVOID pDevice, PVOID pOptions, PVOID *ppDeviceContext)
{
    WARN("D2D1CreateDeviceContext stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
D2D1GetGradientMeshInteriorPointsFromCoonsPatch(PVOID pPoint, PVOID pTensorData, PVOID pOutputPoints)
{
    WARN("D2D1GetGradientMeshInteriorPointsFromCoonsPatch stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
DrawBitmap(HDC hdc, HBITMAP hBitmap, PRECT prcDst, PRECT prcSrc, DWORD dwRop)
{
    WARN("DrawBitmap stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
DrawGlyph(HDC hdc, PVOID pGlyph, PVOID pMatrix)
{
    WARN("DrawGlyph stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
DrawRectangle(HDC hdc, PRECT pRect)
{
    WARN("DrawRectangle stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
DrawRoundedRectangle(HDC hdc, PRECT pRect, FLOAT radiusX, FLOAT radiusY)
{
    WARN("DrawRoundedRectangle stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
FillMesh(HDC hdc, PVOID pMesh, PVOID pBrush)
{
    WARN("FillMesh stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
FillOpacityMask(HDC hdc, PVOID pBitmap, PVOID pBrush, PVOID pContent, PVOID pDest)
{
    WARN("FillOpacityMask stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
SetStandardColorSpaceProfileA(LPCSTR pMachineName, DWORD dwSCS, LPCSTR pProfileName)
{
    WARN("SetStandardColorSpaceProfileA stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
SetStandardColorSpaceProfileW(LPCWSTR pMachineName, DWORD dwSCS, LPCWSTR pProfileName)
{
    WARN("SetStandardColorSpaceProfileW stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * Additional Windows 10 GDI32 API stubs for missing symbols
 */

BOOL
WINAPI
CheckColors(HDC hdc, LPVOID lpBuffer, DWORD cbSize, DWORD dwFlags, LPVOID lpResult, DWORD cbResult)
{
    WARN("CheckColors stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
ClearGraphicsRenderTarget(HANDLE hRenderTarget, LPVOID pColor)
{
    WARN("ClearGraphicsRenderTarget stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
CombineGeometry(HANDLE hGeometry1, HANDLE hGeometry2, HANDLE hGeometryResult, DWORD dwCombineMode, LPVOID pTransform)
{
    WARN("CombineGeometry stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
ConvertWICBitmapFormat(IWICBitmap *pBitmap, REFWICPixelFormatGUID pixelFormat, IWICBitmap **ppConvertedBitmap)
{
    WARN("ConvertWICBitmapFormat stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
CopyGraphicsBuffer(HANDLE hSrcBuffer, HANDLE hDstBuffer)
{
    WARN("CopyGraphicsBuffer stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
CopyWICBitmap(IWICBitmap *pSrcBitmap, IWICBitmap **ppDstBitmap)
{
    WARN("CopyWICBitmap stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateBitmapBrush(HANDLE hRenderTarget, HANDLE hBitmap, LPVOID pBrushProperties, LPVOID pBitmapBrushProperties, LPHANDLE phBrush)
{
    WARN("CreateBitmapBrush stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateCustomTextRenderer(LPVOID pRenderingParams, LPVOID ppTextRenderer)
{
    WARN("CreateCustomTextRenderer stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
CreateDeviceLinkProfile(LPCWSTR *pProfileNames, DWORD nProfiles, PDWORD padwIntent, DWORD nIntents, DWORD dwFlags, LPBYTE *pProfileData, DWORD indexPreferredCMM)
{
    WARN("CreateDeviceLinkProfile stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
CreateEllipseGeometry(LPVOID pCenter, LPVOID pRadius, LPHANDLE phGeometry)
{
    WARN("CreateEllipseGeometry stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateGradientStopCollection(HANDLE hRenderTarget, LPVOID pGradientStops, UINT cGradientStops, LPVOID pExtendMode, LPHANDLE phGradientStopCollection)
{
    WARN("CreateGradientStopCollection stub\n");
    return E_NOTIMPL;
}

HANDLE
WINAPI
CreateGraphicsBuffer(DWORD dwSize, DWORD dwFlags)
{
    WARN("CreateGraphicsBuffer stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

HANDLE
WINAPI
CreateGraphicsContext(HDC hdc, DWORD dwFlags)
{
    WARN("CreateGraphicsContext stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

HANDLE
WINAPI
CreateGraphicsRenderTarget(HANDLE hDevice, LPVOID pDesc)
{
    WARN("CreateGraphicsRenderTarget stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

HANDLE
WINAPI
CreateGraphicsTexture(HANDLE hDevice, LPVOID pDesc)
{
    WARN("CreateGraphicsTexture stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

HRESULT
WINAPI
CreateInlineObject(LPVOID pTextFormat, LPVOID ppInlineObject)
{
    WARN("CreateInlineObject stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateLinearGradientBrush(HANDLE hRenderTarget, LPVOID pLinearGradientBrushProperties, LPVOID pBrushProperties, HANDLE hGradientStopCollection, LPHANDLE phBrush)
{
    WARN("CreateLinearGradientBrush stub\n");
    return E_NOTIMPL;
}

HANDLE
WINAPI
CreateMask(HDC hdc, DWORD dwWidth, DWORD dwHeight)
{
    WARN("CreateMask stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

HTRANSFORM
WINAPI
CreateMultiProfileTransform(LPHPROFILE pahProfiles, DWORD nProfiles, PDWORD padwIntent, DWORD nIntents, DWORD dwFlags, DWORD indexPreferredCMM)
{
    WARN("CreateMultiProfileTransform stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

BOOL
WINAPI
CreateProfileFromLogColorSpaceA(LPLOGCOLORSPACEA pLogColorSpace, LPBYTE *pBuffer)
{
    WARN("CreateProfileFromLogColorSpaceA stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
CreateProfileFromLogColorSpaceW(LPLOGCOLORSPACEW pLogColorSpace, LPBYTE *pBuffer)
{
    WARN("CreateProfileFromLogColorSpaceW stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
CreateRadialGradientBrush(HANDLE hRenderTarget, LPVOID pRadialGradientBrushProperties, LPVOID pBrushProperties, HANDLE hGradientStopCollection, LPHANDLE phBrush)
{
    WARN("CreateRadialGradientBrush stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateRectangleGeometry(LPVOID pRect, LPHANDLE phGeometry)
{
    WARN("CreateRectangleGeometry stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateRoundedRectangleGeometry(LPVOID pRoundedRect, LPHANDLE phGeometry)
{
    WARN("CreateRoundedRectangleGeometry stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateTextFormat(LPCWSTR fontFamilyName, LPVOID pFontCollection, DWORD fontWeight, DWORD fontStyle, DWORD fontStretch, FLOAT fontSize, LPCWSTR localeName, LPVOID ppTextFormat)
{
    WARN("CreateTextFormat stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateTextLayout(LPCWSTR string, UINT stringLength, LPVOID pTextFormat, FLOAT maxWidth, FLOAT maxHeight, LPVOID ppTextLayout)
{
    WARN("CreateTextLayout stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateTypography(LPVOID ppTypography)
{
    WARN("CreateTypography stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateWICBitmapFromHICON(HICON hIcon, IWICBitmap **ppBitmap)
{
    WARN("CreateWICBitmapFromHICON stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateWICBitmapFromIDirect2D1Image(LPVOID pImage, IWICBitmap **ppBitmap)
{
    WARN("CreateWICBitmapFromIDirect2D1Image stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
CropWICBitmap(IWICBitmap *pBitmap, LPVOID pRect, IWICBitmap **ppCroppedBitmap)
{
    WARN("CropWICBitmap stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
DWriteCreateCustomFontFileReference(LPVOID fontFileReferenceKey, UINT fontFileReferenceKeySize, LPVOID pLoader, LPVOID ppFontFile)
{
    WARN("DWriteCreateCustomFontFileReference stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
DWriteCreateEllipsisTrimmingSign(LPVOID pTextFormat, LPVOID ppInlineObject)
{
    WARN("DWriteCreateEllipsisTrimmingSign stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
DWriteCreateFontFace(DWORD fontFaceType, UINT numberOfFiles, LPVOID fontFiles, UINT faceIndex, DWORD fontFaceSimulationFlags, LPVOID ppFontFace)
{
    WARN("DWriteCreateFontFace stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
DWriteCreateFontFileReference(LPCWSTR filePath, LPVOID pLastWriteTime, LPVOID ppFontFile)
{
    WARN("DWriteCreateFontFileReference stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
DWriteCreateGdiCompatibleTextLayout(LPCWSTR string, UINT stringLength, LPVOID pTextFormat, FLOAT layoutWidth, FLOAT layoutHeight, FLOAT pixelsPerDip, LPVOID pTransform, BOOL useGdiNatural, LPVOID ppTextLayout)
{
    WARN("DWriteCreateGdiCompatibleTextLayout stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
DWriteCreateGlyphRunAnalysis(LPVOID pGlyphRun, FLOAT pixelsPerDip, LPVOID pTransform, DWORD renderingMode, DWORD measuringMode, FLOAT baselineOriginX, FLOAT baselineOriginY, LPVOID ppGlyphRunAnalysis)
{
    WARN("DWriteCreateGlyphRunAnalysis stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
DWriteCreateNumberSubstitution(DWORD method, LPCWSTR localeName, BOOL ignoreUserOverride, LPVOID ppNumberSubstitution)
{
    WARN("DWriteCreateNumberSubstitution stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
DWriteCreateTextAnalyzer(LPVOID ppAnalyzer)
{
    WARN("DWriteCreateTextAnalyzer stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
DWriteCreateTextFormat(LPCWSTR fontFamilyName, LPVOID pFontCollection, DWORD fontWeight, DWORD fontStyle, DWORD fontStretch, FLOAT fontSize, LPCWSTR localeName, LPVOID ppTextFormat)
{
    WARN("DWriteCreateTextFormat stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
DWriteCreateTextLayout(LPCWSTR string, UINT stringLength, LPVOID pTextFormat, FLOAT maxWidth, FLOAT maxHeight, LPVOID ppTextLayout)
{
    WARN("DWriteCreateTextLayout stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
DeleteGraphicsBuffer(HANDLE hBuffer)
{
    WARN("DeleteGraphicsBuffer stub\n");
    return TRUE;
}

BOOL
WINAPI
DeleteGraphicsContext(HANDLE hContext)
{
    WARN("DeleteGraphicsContext stub\n");
    return TRUE;
}

BOOL
WINAPI
DeleteGraphicsRenderTarget(HANDLE hRenderTarget)
{
    WARN("DeleteGraphicsRenderTarget stub\n");
    return TRUE;
}

BOOL
WINAPI
DeleteGraphicsTexture(HANDLE hTexture)
{
    WARN("DeleteGraphicsTexture stub\n");
    return TRUE;
}

HRESULT
WINAPI
DrawTextLayout(HANDLE hRenderTarget, LPVOID pOrigin, LPVOID pTextLayout, LPVOID pBrush, DWORD options)
{
    WARN("DrawTextLayout stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
FlipWICBitmap(IWICBitmap *pBitmap, DWORD flipOptions, IWICBitmap **ppFlippedBitmap)
{
    WARN("FlipWICBitmap stub\n");
    return E_NOTIMPL;
}

FLOAT
WINAPI
GetBrushOpacity(HANDLE hBrush)
{
    WARN("GetBrushOpacity stub\n");
    return 1.0f;
}

BOOL
WINAPI
GetBrushTransform(HANDLE hBrush, LPVOID pTransform)
{
    WARN("GetBrushTransform stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetCMMInfo(HTRANSFORM hTransform, DWORD dwInfo)
{
    WARN("GetCMMInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetCharacterWidths(HDC hdc, UINT uFirstChar, UINT uLastChar, PFLOAT lpBuffer)
{
    WARN("GetCharacterWidths stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetColorProfileElement(HPROFILE hProfile, TAGTYPE tag, DWORD dwOffset, PDWORD pcbSize, PVOID pBuffer, PBOOL pbReference)
{
    WARN("GetColorProfileElement stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetColorProfileElementTag(HPROFILE hProfile, DWORD dwIndex, PTAGTYPE pTag)
{
    WARN("GetColorProfileElementTag stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetCountColorProfileElements(HPROFILE hProfile, PDWORD pnElements)
{
    WARN("GetCountColorProfileElements stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
GetEffectInput(HANDLE hEffect, UINT uIndex, LPHANDLE phInput)
{
    WARN("GetEffectInput stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
GetEffectProperty(HANDLE hEffect, UINT uPropertyId, LPVOID pData, UINT cbData)
{
    WARN("GetEffectProperty stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
GetFontFeatures(HDC hdc, LPWSTR lpFeatures, DWORD cchFeatures)
{
    WARN("GetFontFeatures stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetFontMetrics(HDC hdc, LPVOID pMetrics)
{
    WARN("GetFontMetrics stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetFontVariations(HDC hdc, LPVOID pVariations, DWORD cVariations)
{
    WARN("GetFontVariations stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
GetGeometryBounds(HANDLE hGeometry, LPVOID pBounds)
{
    WARN("GetGeometryBounds stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
GetGeometryLength(HANDLE hGeometry, LPVOID pTransform, FLOAT flatteningTolerance, PFLOAT pLength)
{
    WARN("GetGeometryLength stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
GetGeometryRelation(HANDLE hGeometry1, HANDLE hGeometry2, LPVOID pRelation)
{
    WARN("GetGeometryRelation stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
GetGlyphBitmap(HDC hdc, UINT uChar, LPVOID pBitmap, DWORD cbBitmap)
{
    WARN("GetGlyphBitmap stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

DWORD
WINAPI
GetGlyphIndicesEx(HDC hdc, LPCWSTR lpstr, INT cch, LPWORD pgi, DWORD dwFlags, DWORD dwUnknown)
{
    WARN("GetGlyphIndicesEx stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return GDI_ERROR;
}

BOOL
WINAPI
GetGlyphOutlineData(HDC hdc, UINT uChar, UINT uFormat, LPGLYPHMETRICS lpgm, DWORD cbBuffer, LPVOID lpvBuffer, CONST MAT2 *lpmat2)
{
    WARN("GetGlyphOutlineData stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetGraphicsContextProperty(HANDLE hContext, DWORD dwPropertyId, LPVOID pData, DWORD cbData)
{
    WARN("GetGraphicsContextProperty stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetGraphicsDeviceInfo(HANDLE hDevice, LPVOID pInfo)
{
    WARN("GetGraphicsDeviceInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetGraphicsDeviceProperty(HANDLE hDevice, DWORD dwPropertyId, LPVOID pData, DWORD cbData)
{
    WARN("GetGraphicsDeviceProperty stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/* GetKerningPairs Windows 10 version - different signature than classic version */
#undef GetKerningPairs
BOOL
WINAPI
GetKerningPairs(LPVOID pDC, LPVOID pPairs, LPVOID pCount)
{
    WARN("GetKerningPairs (Win10) stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetNamedColor(HPROFILE hProfile, PCOLOR_NAME pColor, DWORD dwIndex)
{
    WARN("GetNamedColor stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetNamedProfileInfo(HPROFILE hProfile, PNAMED_PROFILE_INFO pInfo)
{
    WARN("GetNamedProfileInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetOpenTypeFeatures(HDC hdc, LPVOID pFeatures, DWORD cFeatures)
{
    WARN("GetOpenTypeFeatures stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetPS2ColorRenderingDictionary(HPROFILE hProfile, DWORD dwIntent, LPBYTE pBuffer, PDWORD pcbSize, PBOOL pbBinary)
{
    WARN("GetPS2ColorRenderingDictionary stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetPS2ColorRenderingIntent(HPROFILE hProfile, DWORD dwIntent, LPBYTE pBuffer, PDWORD pcbSize)
{
    WARN("GetPS2ColorRenderingIntent stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
GetPS2ColorSpaceArray(HPROFILE hProfile, DWORD dwIntent, DWORD dwCSAType, LPBYTE pBuffer, PDWORD pcbSize, PBOOL pbBinary)
{
    WARN("GetPS2ColorSpaceArray stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

DWORD
WINAPI
GetTextAntialiasMode(HDC hdc)
{
    WARN("GetTextAntialiasMode stub\n");
    return 0;
}

/* GetTextExtentEx Windows 10 version - different signature than classic version */
#undef GetTextExtentEx
BOOL
WINAPI
GetTextExtentEx(LPVOID pDC, LPCWSTR lpString, INT cchString, LPVOID pFit, LPVOID pDx, LPSIZE lpSize)
{
    WARN("GetTextExtentEx (Win10) stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
GetTextLayoutLineMetrics(LPVOID pTextLayout, LPVOID pLineMetrics, UINT maxLineCount, PUINT actualLineCount)
{
    WARN("GetTextLayoutLineMetrics stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
GetTextLayoutMetrics(LPVOID pTextLayout, LPVOID pMetrics)
{
    WARN("GetTextLayoutMetrics stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
GetTextLayoutProperty(LPVOID pTextLayout, DWORD dwPropertyId, LPVOID pData, DWORD cbData)
{
    WARN("GetTextLayoutProperty stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
GetWICBitmapInfo(IWICBitmap *pBitmap, LPVOID pInfo)
{
    WARN("GetWICBitmapInfo stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
GetWICBitmapPixels(IWICBitmap *pBitmap, LPVOID pRect, UINT cbStride, UINT cbBufferSize, LPBYTE pbBuffer)
{
    WARN("GetWICBitmapPixels stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
HitTestText(LPVOID pTextLayout, FLOAT pointX, FLOAT pointY, PBOOL isTrailingHit, PBOOL isInside, LPVOID pHitTestMetrics)
{
    WARN("HitTestText stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
IsColorProfileTagPresent(HPROFILE hProfile, TAGTYPE tag, PBOOL pbPresent)
{
    WARN("IsColorProfileTagPresent stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
IsColorProfileValid(HPROFILE hProfile, PBOOL pbValid)
{
    WARN("IsColorProfileValid stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
LockWICBitmap(IWICBitmap *pBitmap, LPVOID pRect, DWORD flags, LPVOID ppLock)
{
    WARN("LockWICBitmap stub\n");
    return E_NOTIMPL;
}

LPVOID
WINAPI
MapGraphicsBuffer(HANDLE hBuffer, DWORD dwAccess)
{
    WARN("MapGraphicsBuffer stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

BOOL
WINAPI
PopAxisAlignedClip(HANDLE hRenderTarget)
{
    WARN("PopAxisAlignedClip stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
PresentGraphicsRenderTarget(HANDLE hRenderTarget, LPVOID pRect)
{
    WARN("PresentGraphicsRenderTarget stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
PushAxisAlignedClip(HANDLE hRenderTarget, LPVOID pRect, DWORD antialiasMode)
{
    WARN("PushAxisAlignedClip stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
RealizeGeometry(HANDLE hGeometry, FLOAT flatteningTolerance, LPVOID ppRealizedGeometry)
{
    WARN("RealizeGeometry stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
RegisterCMMA(LPCSTR pszMachineName, DWORD cmmID, LPCSTR pszDll)
{
    WARN("RegisterCMMA stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
RegisterCMMW(LPCWSTR pszMachineName, DWORD cmmID, LPCWSTR pszDll)
{
    WARN("RegisterCMMW stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
RenderColorGlyph(HDC hdc, UINT uChar, LPVOID pGlyphData, PRECT pRect)
{
    WARN("RenderColorGlyph stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
RotateWICBitmap(IWICBitmap *pBitmap, FLOAT angle, IWICBitmap **ppRotatedBitmap)
{
    WARN("RotateWICBitmap stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SaveWICBitmapToFile(IWICBitmap *pBitmap, LPCWSTR fileName, LPVOID pEncoder)
{
    WARN("SaveWICBitmapToFile stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SaveWICBitmapToStream(IWICBitmap *pBitmap, LPVOID pStream, LPVOID pEncoder)
{
    WARN("SaveWICBitmapToStream stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
ScaleWICBitmap(IWICBitmap *pBitmap, UINT uiWidth, UINT uiHeight, DWORD interpolationMode, IWICBitmap **ppScaledBitmap)
{
    WARN("ScaleWICBitmap stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
SelectCMM(DWORD cmmID)
{
    WARN("SelectCMM stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
SetBrushOpacity(HANDLE hBrush, FLOAT opacity)
{
    WARN("SetBrushOpacity stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SetBrushTransform(HANDLE hBrush, LPVOID pTransform)
{
    WARN("SetBrushTransform stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SetEffectProperty(HANDLE hEffect, UINT uPropertyId, LPVOID pData, UINT cbData)
{
    WARN("SetEffectProperty stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
SetFontVariations(HDC hdc, LPVOID pVariations, DWORD cVariations)
{
    WARN("SetFontVariations stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
SetGraphicsContextProperty(HANDLE hContext, DWORD dwPropertyId, LPVOID pData, DWORD cbData)
{
    WARN("SetGraphicsContextProperty stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
SetGraphicsDeviceProperty(HANDLE hDevice, DWORD dwPropertyId, LPVOID pData, DWORD cbData)
{
    WARN("SetGraphicsDeviceProperty stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
SetGraphicsRenderTarget(HANDLE hContext, HANDLE hRenderTarget)
{
    WARN("SetGraphicsRenderTarget stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
SetNamedProfileInfo(HPROFILE hProfile, PNAMED_PROFILE_INFO pInfo)
{
    WARN("SetNamedProfileInfo stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
SetTextAntialiasMode(HDC hdc, DWORD mode)
{
    WARN("SetTextAntialiasMode stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
SetTextLayoutInlineObject(LPVOID pTextLayout, LPVOID pInlineObject, DWORD startPosition, DWORD length)
{
    WARN("SetTextLayoutInlineObject stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SetTextLayoutProperty(LPVOID pTextLayout, DWORD dwPropertyId, LPVOID pData, DWORD cbData)
{
    WARN("SetTextLayoutProperty stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SetTextLayoutTypography(LPVOID pTextLayout, LPVOID pTypography, DWORD startPosition, DWORD length)
{
    WARN("SetTextLayoutTypography stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SetWICBitmapPixels(IWICBitmap *pBitmap, LPVOID pRect, UINT cbStride, UINT cbBufferSize, LPBYTE pbBuffer)
{
    WARN("SetWICBitmapPixels stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SimplifyGeometry(HANDLE hGeometry, DWORD simplificationOption, LPVOID pTransform, FLOAT flatteningTolerance, LPVOID pSimplifiedGeometry)
{
    WARN("SimplifyGeometry stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
TessellateGeometry(HANDLE hGeometry, LPVOID pTransform, FLOAT flatteningTolerance, LPVOID pTessellationSink)
{
    WARN("TessellateGeometry stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
TransformGeometry(HANDLE hGeometry, LPVOID pTransform, LPHANDLE phTransformedGeometry)
{
    WARN("TransformGeometry stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
TranslateBitmapBits(HTRANSFORM hColorTransform, PVOID pSrcBits, BMFORMAT bmInput, DWORD dwWidth, DWORD dwHeight, DWORD dwInputStride, PVOID pDstBits, BMFORMAT bmOutput, DWORD dwOutputStride, PBMCALLBACKFN pfnCallback, LPARAM lParam)
{
    WARN("TranslateBitmapBits stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
TranslateColors(HTRANSFORM hTransform, PCOLOR paInputColors, DWORD nColors, COLORTYPE ctInput, PCOLOR paOutputColors, COLORTYPE ctOutput)
{
    WARN("TranslateColors stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HRESULT
WINAPI
UnlockWICBitmap(LPVOID pLock)
{
    WARN("UnlockWICBitmap stub\n");
    return E_NOTIMPL;
}

BOOL
WINAPI
UnmapGraphicsBuffer(HANDLE hBuffer)
{
    WARN("UnmapGraphicsBuffer stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
UnregisterCMMA(LPCSTR pszMachineName, DWORD cmmID)
{
    WARN("UnregisterCMMA stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
UnregisterCMMW(LPCWSTR pszMachineName, DWORD cmmID)
{
    WARN("UnregisterCMMW stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL
WINAPI
UpdateGraphicsTexture(HANDLE hTexture, LPVOID pRect, LPVOID pData, DWORD cbStride)
{
    WARN("UpdateGraphicsTexture stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}