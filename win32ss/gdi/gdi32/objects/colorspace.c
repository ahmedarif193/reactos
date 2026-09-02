#include <precomp.h>

#define NDEBUG
#include <debug.h>

BOOL
FASTCALL
IntGetLogColorSpaceW(HCOLORSPACE hCS, LPLOGCOLORSPACEW lplcpw);

BOOL
WINAPI
GetLogColorSpaceA(
    HCOLORSPACE hColorSpace,
    LPLOGCOLORSPACEA lpBuffer,
    DWORD nSize)
{
    LOGCOLORSPACEW lcsw;

    if (!lpBuffer || nSize < sizeof(LOGCOLORSPACEA))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!IntGetLogColorSpaceW(hColorSpace, &lcsw))
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    lpBuffer->lcsSignature = lcsw.lcsSignature;
    lpBuffer->lcsVersion = lcsw.lcsVersion;
    lpBuffer->lcsSize = sizeof(LOGCOLORSPACEA);
    lpBuffer->lcsCSType = lcsw.lcsCSType;
    lpBuffer->lcsIntent = lcsw.lcsIntent;
    lpBuffer->lcsEndpoints = lcsw.lcsEndpoints;
    lpBuffer->lcsGammaRed = lcsw.lcsGammaRed;
    lpBuffer->lcsGammaGreen = lcsw.lcsGammaGreen;
    lpBuffer->lcsGammaBlue = lcsw.lcsGammaBlue;
    WideCharToMultiByte(CP_ACP, 0, lcsw.lcsFilename, -1, lpBuffer->lcsFilename, MAX_PATH, NULL, NULL);
    lpBuffer->lcsFilename[MAX_PATH - 1] = 0;
    return TRUE;
}

BOOL
WINAPI
GetLogColorSpaceW(
    HCOLORSPACE hColorSpace,
    LPLOGCOLORSPACEW lpBuffer,
    DWORD nSize)
{
    LOGCOLORSPACEW lcsw;

    if (!lpBuffer || nSize < sizeof(LOGCOLORSPACEW))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!IntGetLogColorSpaceW(hColorSpace, &lcsw))
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    *lpBuffer = lcsw;
    return TRUE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
CheckColorsInGamut(
    HDC	a0,
    LPVOID	a1,
    LPVOID	a2,
    DWORD	a3
)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetDeviceGammaRamp( HDC hdc,
                    LPVOID lpGammaRamp)
{
    BOOL retValue = FALSE;
    if (lpGammaRamp == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
    }
    else
    {
        retValue = NtGdiGetDeviceGammaRamp(hdc,lpGammaRamp);
    }

    return retValue;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetDeviceGammaRamp(HDC hdc,
                   LPVOID lpGammaRamp)
{
    BOOL retValue = FALSE;

    if (lpGammaRamp)
    {
        retValue = NtGdiSetDeviceGammaRamp(hdc, lpGammaRamp);
    }
    else
    {
        SetLastError(ERROR_INVALID_PARAMETER);
    }

    return  retValue;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
ColorMatchToTarget(
    HDC	a0,
    HDC	a1,
    DWORD	a2
)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
SetColorAdjustment(
    HDC			hdc,
    CONST COLORADJUSTMENT	*a1
)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
