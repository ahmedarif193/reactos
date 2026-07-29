/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * PURPOSE:          Native DirectDraw implementation
 * FILE:             win32ss/reactx/ntddraw/d3dkmt.c
 * PROGRAMER:        Sebastian Gasiorek (sebastian.gasiorek@reactos.com)
 */

#include <win32k.h>

__kernel_entry
DWORD
APIENTRY
NtGdiDdDDICreateDCFromMemory(
    _Inout_ D3DKMT_CREATEDCFROMMEMORY *desc)
{
    D3DKMT_CREATEDCFROMMEMORY Captured;
    PALETTEENTRY PaletteEntries[256];
    ULONG BufferSize, MinimumPitch;
    ULONGLONG RowBits;
    PPALETTE ppal;
    PSURFACE psurf;
    HBITMAP hBitmap, hOldBitmap;
    HDC hDC;
    NTSTATUS Status;

    const struct d3dddi_format_info
    {
        D3DDDIFORMAT format;
        unsigned int bit_count;
        DWORD compression;
        unsigned int palette_size;
        DWORD mask_r, mask_g, mask_b;
    } *format = NULL;
    unsigned int i;

    static const struct d3dddi_format_info format_info[] =
    {
        { D3DDDIFMT_R8G8B8,   24, BI_RGB,       0,   0x00000000, 0x00000000, 0x00000000 },
        { D3DDDIFMT_A8R8G8B8, 32, BI_RGB,       0,   0x00000000, 0x00000000, 0x00000000 },
        { D3DDDIFMT_X8R8G8B8, 32, BI_RGB,       0,   0x00000000, 0x00000000, 0x00000000 },
        { D3DDDIFMT_R5G6B5,   16, BI_BITFIELDS, 0,   0x0000f800, 0x000007e0, 0x0000001f },
        { D3DDDIFMT_X1R5G5B5, 16, BI_BITFIELDS, 0,   0x00007c00, 0x000003e0, 0x0000001f },
        { D3DDDIFMT_A1R5G5B5, 16, BI_BITFIELDS, 0,   0x00007c00, 0x000003e0, 0x0000001f },
        { D3DDDIFMT_A4R4G4B4, 16, BI_BITFIELDS, 0,   0x00000f00, 0x000000f0, 0x0000000f },
        { D3DDDIFMT_X4R4G4B4, 16, BI_BITFIELDS, 0,   0x00000f00, 0x000000f0, 0x0000000f },
        { D3DDDIFMT_P8,       8,  BI_RGB,       256, 0x00000000, 0x00000000, 0x00000000 },
    };

    if (desc == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        ProbeForWrite(desc, sizeof(Captured), 1);
        Captured = *desc;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Captured.pMemory == NULL ||
        Captured.Width == 0 || Captured.Width > MAXLONG ||
        Captured.Height == 0 || Captured.Height > MAXLONG ||
        Captured.Pitch == 0 || Captured.Pitch > MAXLONG)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < sizeof(format_info) / sizeof(*format_info); ++i)
    {
        if (format_info[i].format == Captured.Format)
        {
            format = &format_info[i];
            break;
        }
    }

    if (!format)
        return STATUS_INVALID_PARAMETER;

    RowBits = (ULONGLONG)Captured.Width * format->bit_count;
    if (RowBits > MAXULONG - 31)
        return STATUS_INVALID_PARAMETER;

    MinimumPitch = (ULONG)(((RowBits + 31) >> 5) << 2);
    if (Captured.Pitch < MinimumPitch ||
        !NT_SUCCESS(RtlULongMult(Captured.Pitch,
                                Captured.Height,
                                &BufferSize)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Captured.pColorTable != NULL && format->palette_size != 0)
    {
        _SEH2_TRY
        {
            ProbeForRead(Captured.pColorTable,
                         format->palette_size * sizeof(PALETTEENTRY),
                         1);
            RtlCopyMemory(PaletteEntries,
                          Captured.pColorTable,
                          format->palette_size * sizeof(PALETTEENTRY));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    _SEH2_TRY
    {
        ProbeForWrite(Captured.pMemory, BufferSize, 1);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Captured.hDeviceDc == NULL ||
        !(hDC = NtGdiCreateCompatibleDC(Captured.hDeviceDc)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Allocate a surface */
    psurf = SURFACE_AllocSurface(STYPE_BITMAP,
                                 Captured.Width,
                                 Captured.Height,
                                 BitmapFormat(format->bit_count, format->compression),
                                 BMF_TOPDOWN | BMF_NOZEROINIT,
                                 Captured.Pitch,
                                 BufferSize,
                                 Captured.pMemory);
    if (psurf == NULL)
    {
        NtGdiDeleteObjectApp(hDC);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * SURFACE_AllocSurface normalizes a supplied stride to a DWORD-aligned
     * bitmap width.  This API's Pitch is the caller's actual row spacing, so
     * retain it exactly after the allocation has validated the buffer size.
     */
    psurf->SurfObj.lDelta = Captured.Pitch;
    psurf->SurfObj.cjBits = BufferSize;

    /* Mark as API bitmap */
    psurf->flags |= (DDB_SURFACE | API_BITMAP);

    /* Get the handle for the bitmap */
    hBitmap = (HBITMAP)psurf->SurfObj.hsurf;

    /* Allocate a palette for this surface */
    ppal = NULL;
    if (format->palette_size != 0)
    {
        ppal = PALETTE_AllocPalette(
            PAL_INDEXED | PAL_DIBSECTION,
            format->palette_size,
            Captured.pColorTable != NULL ? PaletteEntries : NULL,
            0,
            0,
            0);
    }
    else if (format->compression == BI_BITFIELDS)
    {
        ppal = PALETTE_AllocPalette(
            PAL_BITFIELDS | PAL_DIBSECTION,
            0,
            NULL,
            format->mask_r,
            format->mask_g,
            format->mask_b);
    }

    if ((format->palette_size != 0 ||
         format->compression == BI_BITFIELDS) &&
        ppal == NULL)
    {
        SURFACE_UnlockSurface(psurf);
        NtGdiDeleteObjectApp(hBitmap);
        NtGdiDeleteObjectApp(hDC);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (ppal != NULL)
    {
        SURFACE_vSetPalette(psurf, ppal);
        PALETTE_ShareUnlockPalette(ppal);
    }

    /* Unlock the surface and return */
    SURFACE_UnlockSurface(psurf);

    hOldBitmap = NtGdiSelectBitmap(hDC, hBitmap);
    if (hOldBitmap == NULL)
    {
        NtGdiDeleteObjectApp(hBitmap);
        NtGdiDeleteObjectApp(hDC);
        return STATUS_UNSUCCESSFUL;
    }

    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        desc->hDc = hDC;
        desc->hBitmap = hBitmap;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        NtGdiSelectBitmap(hDC, hOldBitmap);
        NtGdiDeleteObjectApp(hBitmap);
        NtGdiDeleteObjectApp(hDC);
    }

    return Status;
}

__kernel_entry
DWORD
APIENTRY
NtGdiDdDDIDestroyDCFromMemory(
    _In_ CONST D3DKMT_DESTROYDCFROMMEMORY *desc)
{
    D3DKMT_DESTROYDCFROMMEMORY Captured;
    PDC Dc;
    BOOL ValidPair;

    if (desc == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        ProbeForRead(desc, sizeof(Captured), 1);
        Captured = *desc;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (GDI_HANDLE_GET_TYPE(Captured.hDc) != GDI_OBJECT_TYPE_DC ||
        GDI_HANDLE_GET_TYPE(Captured.hBitmap) != GDI_OBJECT_TYPE_BITMAP)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Dc = DC_LockDc(Captured.hDc);
    if (Dc == NULL)
        return STATUS_INVALID_PARAMETER;

    ValidPair = (Dc->dctype == DCTYPE_MEMORY &&
                 Dc->dclevel.pSurface != NULL &&
                 Dc->dclevel.pSurface->SurfObj.hsurf == Captured.hBitmap);
    DC_UnlockDc(Dc);

    if (!ValidPair ||
        !NtGdiDeleteObjectApp(Captured.hBitmap) ||
        !NtGdiDeleteObjectApp(Captured.hDc))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}
