/*
* COPYRIGHT:         See COPYING in the top level directory
* PROJECT:           ReactOS win32 subsystem
* PURPOSE:           Flood filling support
* FILE:              win32ss/gdi/dib/floodfill.c
* PROGRAMMER:        Gregor Schneider <grschneider AT gmail DOT com>
*/

#include <win32k.h>

#define NDEBUG
#include <debug.h>

/*
*  This floodfill algorithm is an iterative four neighbors version. It works with an internal stack like data structure.
*  The stack is kept in an array, sized for the worst case scenario of having to add all pixels of the surface.
*  This avoids having to allocate and free memory blocks  all the time. The stack grows from the end of the array towards the start.
*  All pixels are checked before being added, against belonging to the fill rule (FLOODFILLBORDER or FLOODFILLSURFACE)
*  and the position in respect to the clip region. This guarantees all pixels lying on the stack belong to the filled surface.
*  Further optimisations of the algorithm are possible.
*/

/* Floodfil helper structures and functions */
typedef struct _FLOODITEM
{
    LONG x;
    LONG y;
} FLOODITEM;

static __inline BOOL
FloodCanFill(SURFOBJ *DstSurf,
             const RECTL *DstRect,
             PREGION prgnClip,
             LONG x,
             LONG y,
             ULONG Color,
             BOOL isSurf)
{
    ULONG Pixel;

    if (x < DstRect->left || x >= DstRect->right || y < DstRect->top || y >= DstRect->bottom)
        return FALSE;
    if (prgnClip && !REGION_PtInRegion(prgnClip, x, y))
        return FALSE;
    Pixel = DibFunctionsForBitmapFormat[DstSurf->iBitmapFormat].DIB_GetPixel(DstSurf, x, y);
    return isSurf ? (Pixel == Color) : (Pixel != Color);
}

BOOLEAN DIB_XXBPP_FloodFillSolid(SURFOBJ *DstSurf,
                                 BRUSHOBJ *Brush,
                                 RECTL *DstRect,
                                 POINTL *Origin,
                                 ULONG ConvColor,
                                 UINT FillType,
                                 PVOID pvClip)
{
    PREGION prgnClip = (PREGION)pvClip;
    LONG width = DstRect->right - DstRect->left;
    LONG height = DstRect->bottom - DstRect->top;
    ULONG BrushColor = Brush->iSolidColor;
    BOOL isSurf;
    FLOODITEM *pStack;
    BYTE *pVisited;
    SIZE_T cbVisited, cItems = 0;
    LONG x, y, i;
    static const LONG dx[4] = { 0, 0, 1, -1 };
    static const LONG dy[4] = { 1, -1, 0, 0 };

    if (FillType == FLOODFILLBORDER)
        isSurf = FALSE;
    else if (FillType == FLOODFILLSURFACE)
        isSurf = TRUE;
    else
    {
        DPRINT1("Unsupported FloodFill type!\n");
        return FALSE;
    }

    if (width <= 0 || height <= 0)
        return FALSE;

    if (!FloodCanFill(DstSurf, DstRect, prgnClip, Origin->x, Origin->y, ConvColor, isSurf))
        return FALSE;

    cbVisited = ((SIZE_T)width * height + 7) / 8;
    pVisited = ExAllocatePoolWithTag(PagedPool, cbVisited, TAG_DIB);
    if (!pVisited)
        return FALSE;
    RtlZeroMemory(pVisited, cbVisited);

    pStack = ExAllocatePoolWithTag(PagedPool, (SIZE_T)width * height * sizeof(FLOODITEM), TAG_DIB);
    if (!pStack)
    {
        ExFreePoolWithTag(pVisited, TAG_DIB);
        return FALSE;
    }

#define FLOOD_BIT(px, py) ((SIZE_T)((py) - DstRect->top) * width + ((px) - DstRect->left))
#define FLOOD_VISITED(px, py) (pVisited[FLOOD_BIT(px, py) >> 3] & (1 << (FLOOD_BIT(px, py) & 7)))
#define FLOOD_MARK(px, py) (pVisited[FLOOD_BIT(px, py) >> 3] |= (1 << (FLOOD_BIT(px, py) & 7)))

    pStack[cItems].x = Origin->x;
    pStack[cItems].y = Origin->y;
    cItems++;
    FLOOD_MARK(Origin->x, Origin->y);

    while (cItems != 0)
    {
        cItems--;
        x = pStack[cItems].x;
        y = pStack[cItems].y;

        DibFunctionsForBitmapFormat[DstSurf->iBitmapFormat].DIB_PutPixel(DstSurf, x, y, BrushColor);

        for (i = 0; i < 4; i++)
        {
            LONG nx = x + dx[i], ny = y + dy[i];

            if (nx < DstRect->left || nx >= DstRect->right || ny < DstRect->top || ny >= DstRect->bottom)
                continue;
            if (FLOOD_VISITED(nx, ny))
                continue;
            if (!FloodCanFill(DstSurf, DstRect, prgnClip, nx, ny, ConvColor, isSurf))
                continue;
            FLOOD_MARK(nx, ny);
            pStack[cItems].x = nx;
            pStack[cItems].y = ny;
            cItems++;
        }
    }

#undef FLOOD_BIT
#undef FLOOD_VISITED
#undef FLOOD_MARK

    ExFreePoolWithTag(pStack, TAG_DIB);
    ExFreePoolWithTag(pVisited, TAG_DIB);
    return TRUE;
}
