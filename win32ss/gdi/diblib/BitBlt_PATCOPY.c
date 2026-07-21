
#include "DibLib.h"

#define __USES_SOURCE 0
#define __USES_PATTERN 1
#define __USES_DEST 0
#define __USES_MASK 0

#define __FUNCTIONNAME BitBlt_PATCOPY

#define _DibDoRop(pBltData, M, D, S, P) ROP_PATCOPY(D,S,P)

#include "DibLib_AllDstBPP.h"

#undef __FUNCTIONNAME
#define __FUNCTIONNAME BitBlt_PATCOPY_Solid
#define __USES_SOLID_BRUSH 1

VOID
FASTCALL
Dib_BitBlt_PATCOPY_Solid_D8(PBLTDATA pBltData)
{
    ULONG cLines = pBltData->ulHeight;
    PBYTE pjDest = pBltData->siDst.pjBase;

    while (cLines--)
    {
        memset(pjDest, (UCHAR)pBltData->ulSolidColor, pBltData->ulWidth);
        pjDest += pBltData->siDst.cjAdvanceY;
    }
}

VOID
FASTCALL
Dib_BitBlt_PATCOPY_Solid_D16(PBLTDATA pBltData)
{
    ULONG cLines = pBltData->ulHeight;
    USHORT usColor = (USHORT)pBltData->ulSolidColor;
    PBYTE pjDestBase = pBltData->siDst.pjBase;

    while (cLines--)
    {
        ULONG cPixels = pBltData->ulWidth;
        PUSHORT pusDest = (PUSHORT)pjDestBase;

        while (cPixels--)
        {
            *pusDest++ = usColor;
        }

        pjDestBase += pBltData->siDst.cjAdvanceY;
    }
}

VOID
FASTCALL
Dib_BitBlt_PATCOPY_Solid_D32(PBLTDATA pBltData)
{
    ULONG cLines = pBltData->ulHeight;
    PBYTE pjDestBase = pBltData->siDst.pjBase;

    while (cLines--)
    {
        ULONG cPixels = pBltData->ulWidth;
        PULONG pulDest = (PULONG)pjDestBase;

        while (cPixels--)
        {
            *pulDest++ = pBltData->ulSolidColor;
        }

        pjDestBase += pBltData->siDst.cjAdvanceY;
    }
}

#define Dib_BitBlt_PATCOPY_Solid_D8_manual 1
#define Dib_BitBlt_PATCOPY_Solid_D16_manual 1
#define Dib_BitBlt_PATCOPY_Solid_D32_manual 1

#include "DibLib_AllDstBPP.h"

VOID
FASTCALL
Dib_BitBlt_PATCOPY(PBLTDATA pBltData)
{
    /* Check for solid brush */
    if (pBltData->ulSolidColor != 0xFFFFFFFF)
    {
        /* Use the solid version of PATCOPY! */
        gapfnBitBlt_PATCOPY_Solid[pBltData->siDst.iFormat](pBltData);
    }
    else
    {
        /* Use the pattern version */
        gapfnBitBlt_PATCOPY[pBltData->siDst.iFormat](pBltData);
    }
}
