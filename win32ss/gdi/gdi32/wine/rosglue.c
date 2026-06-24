
#include <precomp.h>
#include "gdi_private.h"
#undef SetWorldTransform

#define NDEBUG
#include <debug.h>

static
GDILOOBJTYPE
ConvertObjectType(
    WORD wType)
{
    /* Get the GDI object type */
    switch (wType)
    {
        case OBJ_PEN: return GDILoObjType_LO_PEN_TYPE;
        case OBJ_BRUSH: return GDILoObjType_LO_BRUSH_TYPE;
        case OBJ_DC: return GDILoObjType_LO_DC_TYPE;
        case OBJ_METADC: return GDILoObjType_LO_METADC16_TYPE;
        case OBJ_PAL: return GDILoObjType_LO_PALETTE_TYPE;
        case OBJ_FONT: return GDILoObjType_LO_FONT_TYPE;
        case OBJ_BITMAP: return GDILoObjType_LO_BITMAP_TYPE;
        case OBJ_REGION: return GDILoObjType_LO_REGION_TYPE;
        case OBJ_METAFILE: return GDILoObjType_LO_METAFILE16_TYPE;
        case OBJ_MEMDC: return GDILoObjType_LO_DC_TYPE;
        case OBJ_EXTPEN: return GDILoObjType_LO_EXTPEN_TYPE;
        case OBJ_ENHMETADC: return GDILoObjType_LO_ALTDC_TYPE;
        case OBJ_ENHMETAFILE: return GDILoObjType_LO_METAFILE_TYPE;
        case OBJ_COLORSPACE: return GDILoObjType_LO_ICMLCS_TYPE;
        default: return 0;
    }
}

HGDIOBJ
alloc_gdi_handle(
    PVOID pvObject,
    WORD wType,
    const struct gdi_obj_funcs *funcs)
{
    GDILOOBJTYPE eObjType;

    /* Get the GDI object type */
    eObjType = ConvertObjectType(wType);
    if ((eObjType != GDILoObjType_LO_METAFILE_TYPE) &&
        (eObjType != GDILoObjType_LO_METAFILE16_TYPE) &&
        (eObjType != GDILoObjType_LO_METADC16_TYPE))
    {
        /* This is not supported! */
        ASSERT(FALSE);
        return NULL;
    }

    /* Insert the client object */
    return GdiCreateClientObj(pvObject, eObjType);
}

PVOID
free_gdi_handle(HGDIOBJ hobj)
{
    /* Should be a client object */
    return GdiDeleteClientObj(hobj);
}

PVOID
GDI_GetObjPtr(
    HGDIOBJ hobj,
    WORD wType)
{
    GDILOOBJTYPE eObjType;

    /* Check if the object type matches */
    eObjType = ConvertObjectType(wType);
    if ((eObjType == 0) || (GDI_HANDLE_GET_TYPE(hobj) != eObjType))
    {
        return NULL;
    }

    /* Check if we have an ALTDC */
    if (eObjType == GDILoObjType_LO_ALTDC_TYPE)
    {
        /* Object is stored as LDC */
        return GdiGetLDC(hobj);
    }

    /* Check for client objects */
    if ((eObjType == GDILoObjType_LO_METAFILE_TYPE) ||
        (eObjType == GDILoObjType_LO_METAFILE16_TYPE) ||
        (eObjType == GDILoObjType_LO_METADC16_TYPE))
    {
        return GdiGetClientObjLink(hobj);
    }

    /* This should never happen! */
    ASSERT(FALSE);
    return NULL;
}

VOID
GDI_ReleaseObj(HGDIOBJ hobj)
{
    /* We don't do any reference-counting */
}

WINEDC*
alloc_dc_ptr(WORD magic)
{
    WINEDC* pWineDc;

    /* Allocate the Wine DC */
    pWineDc = HeapAlloc(GetProcessHeap(), 0, sizeof(*pWineDc));
    if (pWineDc == NULL)
    {
        return NULL;
    }

    ZeroMemory(pWineDc, sizeof(*pWineDc));
    pWineDc->hBrush = GetStockObject(WHITE_BRUSH);
    pWineDc->hPen = GetStockObject(BLACK_PEN);

    if (magic == OBJ_ENHMETADC)
    {
        /* We create a metafile DC, but we ignore the reference DC, this is
           handled by the wine code */
        pWineDc->hdc = NtGdiCreateMetafileDC(NULL);
        if (pWineDc->hdc == NULL)
        {
            HeapFree(GetProcessHeap(), 0, pWineDc);
            return NULL;
        }

        pWineDc->iType = LDC_EMFLDC;

        /* Set the Wine DC as LDC */
        GdiSetLDC(pWineDc->hdc, pWineDc);
    }
    else
    {
        // nothing else supported!
        ASSERT(FALSE);
    }

    return pWineDc;
}

WINEDC*
get_dc_ptr(HDC hdc)
{
    /* Check for EMF DC */
    if (GDI_HANDLE_GET_TYPE(hdc) == GDILoObjType_LO_ALTDC_TYPE)
    {
        /* The Wine DC is stored as the LDC */
        return (WINEDC*)GdiGetLDC(hdc);
    }

    /* Check for METADC16 */
    if (GDI_HANDLE_GET_TYPE(hdc) == GDILoObjType_LO_METADC16_TYPE)
    {
        return GdiGetClientObjLink(hdc);
    }

    return NULL;
}

VOID
GDI_hdc_using_object(
    HGDIOBJ hobj,
    HDC hdc)
{
    /* Record that we have an object in use by a METADC. We simply link the
       object to the HDC that we use. Wine API does not give us a way to
       respond to failure, so we silently ignore it */
    if (!GdiCreateClientObjLink(hobj, hdc))
    {
        /* Ignore failure, and return */
        DPRINT1("Failed to create link for selected METADC object.\n");
        return;
    }
}

VOID
GDI_hdc_not_using_object(
    HGDIOBJ hobj,
    HDC hdc)
{
    HDC hdcLink;

    /* Remove the HDC link for the object */
    hdcLink = GdiRemoveClientObjLink(hobj);
    ASSERT(hdcLink == NULL || hdcLink == hdc);
}

/***********************************************************************
 *           bitmap_info_size
 *
 * Return the size of the bitmap info structure including color table.
 */
int
bitmap_info_size(
    const BITMAPINFO * info,
    WORD coloruse)
{
    unsigned int colors, size, masks = 0;

    if (info->bmiHeader.biSize == sizeof(BITMAPCOREHEADER))
    {
        const BITMAPCOREHEADER *core = (const BITMAPCOREHEADER *)info;
        colors = (core->bcBitCount <= 8) ? 1 << core->bcBitCount : 0;
        return sizeof(BITMAPCOREHEADER) + colors *
             ((coloruse == DIB_RGB_COLORS) ? sizeof(RGBTRIPLE) : sizeof(WORD));
    }
    else  /* assume BITMAPINFOHEADER */
    {
        if (info->bmiHeader.biClrUsed) colors = min( info->bmiHeader.biClrUsed, 256 );
        else colors = info->bmiHeader.biBitCount > 8 ? 0 : 1 << info->bmiHeader.biBitCount;
        if (info->bmiHeader.biCompression == BI_BITFIELDS) masks = 3;
        size = max( info->bmiHeader.biSize, sizeof(BITMAPINFOHEADER) + masks * sizeof(DWORD) );
        return size + colors * ((coloruse == DIB_RGB_COLORS) ? sizeof(RGBQUAD) : sizeof(WORD));
    }
}

static
DWORD
bitmap_info_header_size(
    const BITMAPINFO *info)
{
    DWORD masks = 0;

    if (info->bmiHeader.biSize == sizeof(BITMAPCOREHEADER))
        return sizeof(BITMAPCOREHEADER);

    if (info->bmiHeader.biCompression == BI_BITFIELDS)
        masks = 3;

    return max(info->bmiHeader.biSize, sizeof(BITMAPINFOHEADER) + masks * sizeof(DWORD));
}

static
const BITMAPINFO *
get_dib_brush_packed_info(
    HBRUSH hbr)
{
    LOGBRUSH logbrush;

    if (!GetObjectA(hbr, sizeof(logbrush), &logbrush) ||
        logbrush.lbStyle != BS_DIBPATTERN ||
        logbrush.lbHatch == 0)
    {
        return NULL;
    }

    return (const BITMAPINFO *)logbrush.lbHatch;
}

static BOOL
copy_dib_brush_info_table(
    HBRUSH hbr,
    BITMAPINFO *info,
    UINT usage)
{
    const BITMAPINFO *packed_info;
    DWORD dst_offset, dst_size, src_offset, src_size, table_size;
    BOOL ret = FALSE;

    packed_info = get_dib_brush_packed_info(hbr);
    if (packed_info == NULL)
        return FALSE;

    _SEH2_TRY
    {
        dst_offset = bitmap_info_header_size(info);
        dst_size = bitmap_info_size(info, usage);
        src_offset = bitmap_info_header_size(packed_info);
        src_size = bitmap_info_size(packed_info, usage);

        if (dst_size >= dst_offset &&
            src_size >= src_offset &&
            src_size - src_offset >= dst_size - dst_offset)
        {
            table_size = dst_size - dst_offset;
            CopyMemory((BYTE *)info + dst_offset, (const BYTE *)packed_info + src_offset, table_size);
            ret = TRUE;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ret = FALSE;
    }
    _SEH2_END;

    return ret;
}

static BOOL
copy_dib_brush_bits(
    HBRUSH hbr,
    const BITMAPINFO *info,
    UINT usage,
    PVOID bits)
{
    const BITMAPINFO *packed_info;
    DWORD info_size;
    BOOL ret = FALSE;

    if (bits == NULL || info->bmiHeader.biSizeImage == 0)
        return FALSE;

    packed_info = get_dib_brush_packed_info(hbr);
    if (packed_info == NULL)
        return FALSE;

    _SEH2_TRY
    {
        info_size = bitmap_info_size(packed_info, usage);
        CopyMemory(bits, (const BYTE *)packed_info + info_size, info->bmiHeader.biSizeImage);
        ret = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ret = FALSE;
    }
    _SEH2_END;

    return ret;
}

BOOL
get_brush_bitmap_info(
    HBRUSH hbr,
    PBITMAPINFO pbmi,
    PVOID pvBits,
    PUINT puUsage)
{
    BYTE bmiBuffer[FIELD_OFFSET(BITMAPINFO, bmiColors) + 256 * sizeof(RGBQUAD)];
    PBITMAPINFO pInfo = pbmi ? pbmi : (PBITMAPINFO)bmiBuffer;
    HBITMAP hbmp;
    HDC hdc;
    PVOID Bits = NULL;
    UINT usage;
    BOOL ret = FALSE;

    /* Call win32k to get the bitmap handle and color usage */
    hbmp = NtGdiGetObjectBitmapHandle(hbr, &usage);
    if (hbmp == NULL)
        return FALSE;
    if (puUsage)
        *puUsage = usage;

    hdc = GetDC(NULL);
    if (hdc == NULL)
        return FALSE;

    /* Initialize the BITMAPINFO */
    ZeroMemory(pInfo, sizeof(*pInfo));
    pInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

    /* Retrieve information about the bitmap */
    if (!GetDIBits(hdc, hbmp, 0, 0, NULL, pInfo, usage))
        goto done;
    copy_dib_brush_info_table(hbr, pInfo, usage);

    if (pvBits)
    {
        if (!copy_dib_brush_bits(hbr, pInfo, usage, pvBits))
        {
            /* Now allocate a buffer for the bits */
            Bits = HeapAlloc(GetProcessHeap(), 0, pInfo->bmiHeader.biSizeImage);
            if (Bits == NULL)
                goto done;

            /* Retrieve the bitmap bits */
            if (!GetDIBits(hdc, hbmp, 0, pInfo->bmiHeader.biHeight, Bits, pInfo, usage))
                goto done;

            CopyMemory( pvBits, Bits, pInfo->bmiHeader.biSizeImage );
        }

    }

    /* GetDIBits doesn't set biClrUsed, but wine code needs it, so we set it */
    if (pInfo->bmiHeader.biBitCount <= 8)
    {
        pInfo->bmiHeader.biClrUsed =  1 << pInfo->bmiHeader.biBitCount;
    }
    ret = TRUE;

done:
    HeapFree(GetProcessHeap(), 0, Bits);
    ReleaseDC(NULL, hdc);
    return ret;
}

BOOL
WINAPI
SetVirtualResolution(
    HDC hdc,
    DWORD cxVirtualDevicePixel,
    DWORD cyVirtualDevicePixel,
    DWORD cxVirtualDeviceMm,
    DWORD cyVirtualDeviceMm)
{
    return NtGdiSetVirtualResolution(hdc,
                                     cxVirtualDevicePixel,
                                     cyVirtualDevicePixel,
                                     cxVirtualDeviceMm,
                                     cyVirtualDeviceMm);
}

BOOL
WINAPI
DeleteColorSpace(
    HCOLORSPACE hcs)
{
    return NtGdiDeleteColorSpace(hcs);
}
void
__cdecl
_assert (
    const char *exp,
    const char *file,
    unsigned line)
{
    DbgRaiseAssertionFailure();
}

/******************************************************************************/
BOOL
WINAPI
METADC_SetD(
     _In_ HDC hdc,
     _In_ DWORD dwIn,
     _In_ USHORT usMF16Id
)
{
    switch(usMF16Id)
    {
    case META_SETMAPMODE:
        return METADC_SetMapMode(hdc, dwIn);
    case META_SETRELABS:
        return METADC_SetRelAbs(hdc, dwIn);
    default:
        return FALSE;
    }
}

BOOL
WINAPI
EMFDC_SetD(
    _In_ PLDC pldc,
    _In_ DWORD dwIn,
    _In_ ULONG ulMFId)
{
    switch(ulMFId)
    {
    case EMR_SETMAPMODE:
        return EMFDC_SetMapMode( pldc, dwIn);
    case EMR_SETARCDIRECTION:
        return EMFDC_SetArcDirection( pldc, dwIn);
    default:
        return FALSE;
    }
}

extern void METADC_DeleteObject( HDC hdc, HGDIOBJ obj );
extern void emfdc_delete_object( HDC hdc, HGDIOBJ obj );

VOID
WINAPI
METADC_RosGlueDeleteObject(HGDIOBJ hobj)
{
    GDILOOBJTYPE eObjectType;
    HDC hdc;
    BOOL found = FALSE;

    /* Check for one of the types we actually handle here */
    eObjectType = GDI_HANDLE_GET_TYPE(hobj);
    if ((eObjectType != GDILoObjType_LO_BRUSH_TYPE) &&
        (eObjectType != GDILoObjType_LO_PEN_TYPE) &&
        (eObjectType != GDILoObjType_LO_EXTPEN_TYPE) &&
        (eObjectType != GDILoObjType_LO_PALETTE_TYPE) &&
        (eObjectType != GDILoObjType_LO_FONT_TYPE))
    {
        return;
    }

    /* Check if we have client object links and remove them if found.
       The links are the HDCs that have recorded the object. */
    while ((hdc = GdiRemoveClientObjLink(hobj)) != NULL)
    {
        found = TRUE;

        if ( GDI_HANDLE_GET_TYPE(hdc) == GDILoObjType_LO_METADC16_TYPE ) METADC_DeleteObject( hdc, hobj );

        if ( GDI_HANDLE_GET_TYPE(hdc) == GDILoObjType_LO_ALTDC_TYPE )
        {
            LDC* pWineDc = GdiGetLDC(hdc);
            if ( pWineDc )
            {
                emfdc_delete_object( hdc, hobj );
            }
        }
    }

    if (!found)
    {
        DPRINT1("the link was not found\n");
    }
}

BOOL
WINAPI
METADC_RosGlueDeleteDC(
    _In_ HDC hdc)
{
    LDC* pWineDc = NULL;

    if ( GDI_HANDLE_GET_TYPE(hdc) == GDILoObjType_LO_METADC16_TYPE )
    {
        return METADC_DeleteDC(hdc);
    }

    if ( GDI_HANDLE_GET_TYPE(hdc) == GDILoObjType_LO_ALTDC_TYPE )
    {
        pWineDc = GdiGetLDC(hdc);

        if ( pWineDc )
        {
            // Handle Printer LDC
            if (pWineDc->iType != LDC_EMFLDC)
            {
               //return IntDeleteDC(hdc);
            }

            EMFDC_DeleteDC( pWineDc );

            /* Get rid of the LDC */
            ASSERT(GdiGetLDC(pWineDc->hDC) == pWineDc);
            GdiSetLDC(pWineDc->hDC, NULL);

            /* Free the Wine DC */
            HeapFree(GetProcessHeap(), 0, pWineDc);
        }
    }

    return NtGdiDeleteObjectApp(hdc);
}
