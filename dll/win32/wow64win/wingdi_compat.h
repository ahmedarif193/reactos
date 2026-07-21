/* ReactOS compatibility definitions missing from sdk/include/psdk/wingdi.h. */

#ifndef __WOW64WIN_WINGDI_COMPAT_H
#define __WOW64WIN_WINGDI_COMPAT_H

typedef struct tagEXTLOGPEN32
{
    DWORD elpPenStyle;
    DWORD elpWidth;
    UINT elpBrushStyle;
    COLORREF elpColor;
    ULONG elpHatch;
    DWORD elpNumEntries;
    DWORD elpStyleEntry[1];
} EXTLOGPEN32, *PEXTLOGPEN32, *NPEXTLOGPEN32, *LPEXTLOGPEN32;

#endif /* __WOW64WIN_WINGDI_COMPAT_H */
