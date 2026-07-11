/*
 * PROJECT:         ReactOS win32 kernel mode subsystem
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            win32ss/gdi/ntgdi/wingl.c
 * PURPOSE:         WinGL API
 * PROGRAMMER:
 */

#include <win32k.h>

#define NDEBUG
#include <debug.h>

static
INT
FASTCALL
IntGetipfdDevMax(PDC pdc)
{
    INT Ret = 0;
    PPDEVOBJ ppdev = pdc->ppdev;

    if (ppdev->flFlags & PDEV_META_DEVICE)
    {
        return 0;
    }

    if (ppdev->DriverFunctions.DescribePixelFormat)
    {
        Ret = ppdev->DriverFunctions.DescribePixelFormat(
                                                ppdev->dhpdev,
                                                1,
                                                0,
                                                NULL);
    }

    if (Ret) pdc->ipfdDevMax = Ret;

    return Ret;
}

_Success_(return != 0)
__kernel_entry
INT
APIENTRY
NtGdiDescribePixelFormat(
    _In_ HDC hdc,
    _In_ INT ipfd,
    _In_ UINT cjpfd,
    _Out_writes_bytes_(cjpfd) PPIXELFORMATDESCRIPTOR ppfd)
{
    PDC pdc;
    PPDEVOBJ ppdev;
    INT Ret = 0;
    PIXELFORMATDESCRIPTOR pfdSafe;

    if ((ppfd == NULL) && (cjpfd != 0)) return 0;

    pdc = DC_LockDc(hdc);
    if (!pdc)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }

    if (!pdc->ipfdDevMax)
    {
        if (!IntGetipfdDevMax(pdc))
        {
            /* EngSetLastError ? */
            goto Exit;
        }
    }

    if (!ppfd)
    {
        Ret = pdc->ipfdDevMax;
        goto Exit;
    }

    if ((ipfd < 1) || (ipfd > pdc->ipfdDevMax))
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        goto Exit;
    }

    ppdev = pdc->ppdev;

    if (ppdev->flFlags & PDEV_META_DEVICE)
    {
        UNIMPLEMENTED;
        goto Exit;
    }

    if (ppdev->DriverFunctions.DescribePixelFormat)
    {
        Ret = ppdev->DriverFunctions.DescribePixelFormat(
                                                    ppdev->dhpdev,
                                                    ipfd,
                                                    sizeof(pfdSafe),
                                                    &pfdSafe);
    }

    if (Ret && cjpfd)
    {
        _SEH2_TRY
        {
            cjpfd = min(cjpfd, sizeof(PIXELFORMATDESCRIPTOR));
            ProbeForWrite(ppfd, cjpfd, 1);
            RtlCopyMemory(ppfd, &pfdSafe, cjpfd);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            SetLastNtError(_SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

Exit:
    DC_UnlockDc(pdc);
    return Ret;
}


BOOL
APIENTRY
NtGdiSetPixelFormat(
    _In_ HDC hdc,
    _In_ INT ipfd)
{
    PDC pdc;
    PPDEVOBJ ppdev;
    HWND hWnd;
    PWNDOBJ pWndObj;
    SURFOBJ *pso = NULL;
    BOOL Ret = FALSE;

    DPRINT1("Setting pixel format from win32k!\n");

    pdc = DC_LockDc(hdc);
    if (!pdc)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (!pdc->ipfdDevMax)
        IntGetipfdDevMax(pdc);

    /* The USER lock may not be acquired while a GDI exclusive lock is held
     * (ASSERT_NOGDILOCKS in UserEnterExclusive) — drop the DC around the
     * window work and re-lock it afterwards. */
    DC_UnlockDc(pdc);

    UserEnterExclusive();
    hWnd = UserGethWnd(hdc, &pWndObj);
    if (hWnd != NULL)
    {
        /* This window uses OpenGL — tell the compositor. Must happen BEFORE
         * the native-format validation: with a custom ICD (Mesa) the format
         * index belongs to the ICD, not the display driver, and a KMDOD
         * display exposes no native GDI formats at all (ipfdDevMax == 0) —
         * yet the ICD still creates a real GL context on this window. */
        PWND pWnd = UserGetWindowObject(hWnd);
        if (pWnd != NULL)
            IntCompositionMarkOpenGL(pWnd);
    }
    UserLeave();

    pdc = DC_LockDc(hdc);
    if (!pdc)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if ( ipfd < 1 ||
        ipfd > pdc->ipfdDevMax )
    {
        EngSetLastError(ERROR_INVALID_PARAMETER);
        goto Exit;
    }

    if (!hWnd)
    {
        EngSetLastError(ERROR_INVALID_WINDOW_STYLE);
        goto Exit;
    }

    ppdev = pdc->ppdev;

    /*
        WndObj is needed so exit on NULL pointer.
    */
    if (pWndObj)
        pso = pWndObj->psoOwner;
    else
    {
        EngSetLastError(ERROR_INVALID_PIXEL_FORMAT);
        goto Exit;
    }

    if (ppdev->flFlags & PDEV_META_DEVICE)
    {
        UNIMPLEMENTED;
        goto Exit;
    }

    if (ppdev->DriverFunctions.SetPixelFormat)
    {
        Ret = ppdev->DriverFunctions.SetPixelFormat(
                                                pso,
                                                ipfd,
                                                hWnd);
    }

Exit:
    DC_UnlockDc(pdc);
    return Ret;
}

BOOL
APIENTRY
NtGdiSwapBuffers(
    _In_ HDC hdc)
{
    PDC pdc;
    PPDEVOBJ ppdev;
    HWND hWnd;
    PWNDOBJ pWndObj;
    SURFOBJ *pso = NULL;
    BOOL Ret = FALSE;

    pdc = DC_LockDc(hdc);
    if (!pdc)
    {
        EngSetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    UserEnterExclusive();
    hWnd = UserGethWnd(hdc, &pWndObj);
    UserLeave();

    if (!hWnd)
    {
        EngSetLastError(ERROR_INVALID_WINDOW_STYLE);
        goto Exit;
    }

    ppdev = pdc->ppdev;

    /*
        WndObj is needed so exit on NULL pointer.
    */
    if (pWndObj)
        pso = pWndObj->psoOwner;
    else
    {
        EngSetLastError(ERROR_INVALID_PIXEL_FORMAT);
        goto Exit;
    }

    if (ppdev->flFlags & PDEV_META_DEVICE)
    {
        UNIMPLEMENTED;
        goto Exit;
    }

    if (ppdev->DriverFunctions.SwapBuffers)
    {
        Ret = ppdev->DriverFunctions.SwapBuffers(pso, pWndObj);
    }

Exit:
    DC_UnlockDc(pdc);
    return Ret;
}

/* EOF */
