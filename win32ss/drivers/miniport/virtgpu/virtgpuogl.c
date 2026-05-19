/*
 * PROJECT:     ReactOS VirtIO GPU OpenGL ICD scaffold
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     XPDM OpenGL ICD entry points for the VirtIO GPU display driver
 */

#include <stdarg.h>
#include <stddef.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winioctl.h>

#include "virtgpu_shared.h"

#define VIRTGPU_OGL_CONTEXT_SIGNATURE 'cOgV'
#define VIRTGPU_OPENGL_ICD_DRIVER_VERSION 1

DECLARE_HANDLE(DHGLRC);

typedef VOID (APIENTRY *PFN_SETPROCTABLE)(const void *);

typedef struct _VIRTGPU_OGL_CONTEXT
{
    ULONG Signature;
    HDC hdc;
    ULONG ContextId;
} VIRTGPU_OGL_CONTEXT, *PVIRTGPU_OGL_CONTEXT;

static BOOL
VirtGpuOglEscapeIoControl(
    _In_ HDC hdc,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputSize) PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID OutputBuffer,
    _In_ ULONG OutputSize,
    _Out_opt_ PULONG Returned)
{
    PVIRTGPU_ESCAPE_3D_IOCTL Escape;
    ULONG HeaderSize = offsetof(VIRTGPU_ESCAPE_3D_IOCTL, Data);
    ULONG EscapeSize;
    INT Result;

    if (Returned != NULL)
        *Returned = 0;

    if ((InputSize != 0) && (InputBuffer == NULL))
        return FALSE;

    EscapeSize = HeaderSize + InputSize;
    Escape = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, EscapeSize);
    if (Escape == NULL)
        return FALSE;

    Escape->Size = EscapeSize;
    Escape->IoControlCode = IoControlCode;
    Escape->InputSize = InputSize;
    if (InputSize != 0)
        CopyMemory(Escape->Data, InputBuffer, InputSize);

    Result = ExtEscape(hdc,
                       VIRTGPU_ESCAPE_3D_IOCTL_CODE,
                       EscapeSize,
                       (LPCSTR)Escape,
                       OutputSize,
                       (LPSTR)OutputBuffer);
    HeapFree(GetProcessHeap(), 0, Escape);

    if (Result <= 0)
        return FALSE;

    if (Returned != NULL)
        *Returned = (ULONG)Result;
    return TRUE;
}

static BOOL
VirtGpuOglQueryCaps(_In_ HDC hdc, _Out_ PVIRTGPU_3D_CAPS Caps)
{
    ULONG Returned;

    ZeroMemory(Caps, sizeof(*Caps));
    if (!VirtGpuOglEscapeIoControl(hdc,
                                   IOCTL_VIDEO_VIRTGPU_QUERY_3D_CAPS,
                                   NULL,
                                   0,
                                   Caps,
                                   sizeof(*Caps),
                                   &Returned))
    {
        return FALSE;
    }

    return (Returned >= sizeof(*Caps)) &&
           (Caps->Size == sizeof(*Caps)) &&
           (Caps->Enabled != 0) &&
           (Caps->PreferredCapsetId != 0);
}

BOOL WINAPI
DllMain(
    _In_ HINSTANCE hinstDLL,
    _In_ DWORD fdwReason,
    _In_opt_ LPVOID lpvReserved)
{
    UNREFERENCED_PARAMETER(hinstDLL);
    UNREFERENCED_PARAMETER(fdwReason);
    UNREFERENCED_PARAMETER(lpvReserved);
    return TRUE;
}

BOOL WINAPI
DrvValidateVersion(_In_ DWORD DriverVersion)
{
    return DriverVersion == VIRTGPU_OPENGL_ICD_DRIVER_VERSION;
}

VOID WINAPI
DrvSetCallbackProcs(_In_ INT nProcs, _In_reads_opt_(nProcs) PROC *pProcs)
{
    UNREFERENCED_PARAMETER(nProcs);
    UNREFERENCED_PARAMETER(pProcs);
}

BOOL WINAPI
DrvCopyContext(_In_ DHGLRC hglrcSrc, _In_ DHGLRC hglrcDst, _In_ UINT mask)
{
    UNREFERENCED_PARAMETER(hglrcSrc);
    UNREFERENCED_PARAMETER(hglrcDst);
    UNREFERENCED_PARAMETER(mask);
    return FALSE;
}

DHGLRC WINAPI
DrvCreateContext(_In_ HDC hdc)
{
    VIRTGPU_3D_CAPS Caps;
    VIRTGPU_3D_CREATE_CONTEXT Create;
    PVIRTGPU_OGL_CONTEXT Context;
    ULONG Returned;
    static const CHAR DebugName[] = "ReactOS VirtGpu ICD";

    if (!VirtGpuOglQueryCaps(hdc, &Caps))
        return NULL;

    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    if (Context == NULL)
        return NULL;

    ZeroMemory(&Create, sizeof(Create));
    Create.CapsetId = Caps.PreferredCapsetId;
    CopyMemory(Create.DebugName, DebugName, sizeof(DebugName));

    if (!VirtGpuOglEscapeIoControl(hdc,
                                   IOCTL_VIDEO_VIRTGPU_3D_CREATE_CONTEXT,
                                   &Create,
                                   sizeof(Create),
                                   &Create,
                                   sizeof(Create),
                                   &Returned) ||
        (Returned < sizeof(Create)) ||
        (Create.ContextId == 0))
    {
        HeapFree(GetProcessHeap(), 0, Context);
        return NULL;
    }

    Context->Signature = VIRTGPU_OGL_CONTEXT_SIGNATURE;
    Context->hdc = hdc;
    Context->ContextId = Create.ContextId;
    return (DHGLRC)Context;
}

DHGLRC WINAPI
DrvCreateLayerContext(_In_ HDC hdc, _In_ INT iLayerPlane)
{
    if (iLayerPlane != 0)
        return NULL;
    return DrvCreateContext(hdc);
}

BOOL WINAPI
DrvDeleteContext(_In_ DHGLRC hglrc)
{
    PVIRTGPU_OGL_CONTEXT Context = (PVIRTGPU_OGL_CONTEXT)hglrc;
    VIRTGPU_3D_CONTEXT Destroy;
    BOOL Success = TRUE;

    if ((Context == NULL) ||
        (Context->Signature != VIRTGPU_OGL_CONTEXT_SIGNATURE))
    {
        return FALSE;
    }

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.ContextId = Context->ContextId;
    if (Destroy.ContextId != 0)
    {
        Success = VirtGpuOglEscapeIoControl(Context->hdc,
                                            IOCTL_VIDEO_VIRTGPU_3D_DESTROY_CONTEXT,
                                            &Destroy,
                                            sizeof(Destroy),
                                            NULL,
                                            0,
                                            NULL);
    }

    Context->Signature = 0;
    HeapFree(GetProcessHeap(), 0, Context);
    return Success;
}

BOOL WINAPI
DrvDescribeLayerPlane(
    _In_ HDC hdc,
    _In_ INT iPixelFormat,
    _In_ INT iLayerPlane,
    _In_ UINT nBytes,
    _Out_writes_bytes_(nBytes) LPLAYERPLANEDESCRIPTOR plpd)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(iPixelFormat);
    UNREFERENCED_PARAMETER(iLayerPlane);
    UNREFERENCED_PARAMETER(nBytes);
    UNREFERENCED_PARAMETER(plpd);
    return FALSE;
}

INT WINAPI
DrvDescribePixelFormat(
    _In_ HDC hdc,
    _In_ INT iPixelFormat,
    _In_ UINT nBytes,
    _Out_writes_bytes_opt_(nBytes) LPPIXELFORMATDESCRIPTOR ppfd)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(iPixelFormat);
    UNREFERENCED_PARAMETER(nBytes);
    UNREFERENCED_PARAMETER(ppfd);

    /*
     * The transport and ICD loader path are present, but there is no GL
     * command translator yet. Returning no ICD formats lets opengl32 keep
     * using its software implementation for normal applications.
     */
    return 0;
}

INT WINAPI
DrvGetLayerPaletteEntries(
    _In_ HDC hdc,
    _In_ INT iLayerPlane,
    _In_ INT iStart,
    _In_ INT cEntries,
    _Out_writes_(cEntries) COLORREF *pcr)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(iLayerPlane);
    UNREFERENCED_PARAMETER(iStart);
    UNREFERENCED_PARAMETER(cEntries);
    UNREFERENCED_PARAMETER(pcr);
    return 0;
}

PROC WINAPI
DrvGetProcAddress(_In_ LPCSTR lpProcName)
{
    UNREFERENCED_PARAMETER(lpProcName);
    return NULL;
}

VOID WINAPI
DrvReleaseContext(_In_ DHGLRC hglrc)
{
    UNREFERENCED_PARAMETER(hglrc);
}

BOOL WINAPI
DrvRealizeLayerPalette(_In_ HDC hdc, _In_ INT iLayerPlane, _In_ BOOL bRealize)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(iLayerPlane);
    UNREFERENCED_PARAMETER(bRealize);
    return FALSE;
}

const void * WINAPI
DrvSetContext(_In_ HDC hdc, _In_ DHGLRC hglrc, _In_ PFN_SETPROCTABLE callback)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(hglrc);
    UNREFERENCED_PARAMETER(callback);
    return NULL;
}

INT WINAPI
DrvSetLayerPaletteEntries(
    _In_ HDC hdc,
    _In_ INT iLayerPlane,
    _In_ INT iStart,
    _In_ INT cEntries,
    _In_reads_(cEntries) const COLORREF *pcr)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(iLayerPlane);
    UNREFERENCED_PARAMETER(iStart);
    UNREFERENCED_PARAMETER(cEntries);
    UNREFERENCED_PARAMETER(pcr);
    return 0;
}

BOOL WINAPI
DrvSetPixelFormat(_In_ HDC hdc, _In_ INT iPixelFormat)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(iPixelFormat);
    return FALSE;
}

BOOL WINAPI
DrvShareLists(_In_ DHGLRC hglrc1, _In_ DHGLRC hglrc2)
{
    UNREFERENCED_PARAMETER(hglrc1);
    UNREFERENCED_PARAMETER(hglrc2);
    return FALSE;
}

BOOL WINAPI
DrvSwapBuffers(_In_ HDC hdc)
{
    UNREFERENCED_PARAMETER(hdc);
    return FALSE;
}

BOOL WINAPI
DrvSwapLayerBuffers(_In_ HDC hdc, _In_ UINT fuPlanes)
{
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(fuPlanes);
    return FALSE;
}
