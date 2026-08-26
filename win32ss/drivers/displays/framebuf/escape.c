/*
 * ReactOS Generic Framebuffer display driver
 *
 * PURPOSE: DrvEscape bridge to the WDDM miniport GPU services.
 *
 * Under the WDDM stack GDI DCs are backed by this driver while the GPU is
 * owned by a WDDM miniport behind dxgkrnl. The OpenGL ICD contract, however,
 * still speaks ExtEscape on the DC: opengl32 probes OPENGL_GETINFO for the
 * ICD name and the RPi5 ICD (rpi5vc4ogl.dll) submits its bounded render
 * requests as RPI5VC4_ESCAPE_* codes. This file forwards those escapes to
 * the miniport by packing them behind IOCTL_VIDEO_DXGK_GPU_ESCAPE, which
 * dxgkrnl hands to the miniport's DxgkDdiEscape verbatim.
 */

#include "framebuf.h"
#include <reactos/rpi5vc4_xpdm.h>
#include <reactos/dwmframe.h>

#define FRAMEBUF_OPENGL_ICD_VERSION 1
#define FRAMEBUF_OPENGL_ICD_DRIVER_VERSION 1
#define OPENGL_GETINFO_DRVNAME 0

/* Layout dictated by opengl32's OPENGL_GETINFO reply (Drv_Opengl_Info). */
typedef struct _FRAMEBUF_OPENGL_INFO
{
    ULONG Version;
    ULONG DriverVersion;
    WCHAR DriverName[MAX_PATH + 1];
} FRAMEBUF_OPENGL_INFO, *PFRAMEBUF_OPENGL_INFO;

static ULONG
FrameBufferGpuEscape(
    _In_ PPDEV ppdev,
    _In_ ULONG Op,
    _In_reads_bytes_opt_(cjIn) PVOID pvIn,
    _In_ ULONG cjIn,
    _Out_writes_bytes_opt_(cjOut) PVOID pvOut,
    _In_ ULONG cjOut)
{
    PRPI5VC4_WDDM_GPU_ESCAPE Wrapper;
    ULONG PayloadBytes = (cjIn > cjOut) ? cjIn : cjOut;
    ULONG WrapperBytes;
    ULONG Returned = 0;
    ULONG CopyOut;

    if (PayloadBytes > ~(ULONG)0 -
                       FIELD_OFFSET(RPI5VC4_WDDM_GPU_ESCAPE, Payload))
    {
        return 0;
    }

    WrapperBytes = FIELD_OFFSET(RPI5VC4_WDDM_GPU_ESCAPE, Payload) +
                   PayloadBytes;
    Wrapper = EngAllocMem(0, WrapperBytes, 'EGbF');
    if (Wrapper == NULL)
        return 0;

    Wrapper->Magic = RPI5VC4_WDDM_GPU_ESCAPE_MAGIC;
    Wrapper->Op = Op;
    Wrapper->InputLength = cjIn;
    Wrapper->OutputLength = cjOut;
    if (cjIn != 0)
        memcpy(Wrapper->Payload, pvIn, cjIn);
    if (cjIn < PayloadBytes)
        memset(Wrapper->Payload + cjIn, 0, PayloadBytes - cjIn);

    if (EngDeviceIoControl(ppdev->hDriver,
                           IOCTL_VIDEO_DXGK_GPU_ESCAPE,
                           Wrapper,
                           WrapperBytes,
                           Wrapper,
                           WrapperBytes,
                           &Returned) != 0)
    {
        EngFreeMem(Wrapper);
        return 0;
    }

    CopyOut = Wrapper->OutputLength;
    if (CopyOut > cjOut)
        CopyOut = cjOut;
    if (CopyOut != 0 && pvOut != NULL)
        memcpy(pvOut, Wrapper->Payload, CopyOut);

    EngFreeMem(Wrapper);
    return CopyOut;
}

static BOOL
FrameBufferV3dExecutionSupported(
    _In_ PPDEV ppdev)
{
    RPI5VC4_V3D_INFO Info;

    memset(&Info, 0, sizeof(Info));
    if (FrameBufferGpuEscape(ppdev, RPI5VC4_ESCAPE_QUERY_V3D, NULL, 0, &Info, sizeof(Info)) < sizeof(Info))
        return FALSE;

    return Info.AbiVersion == RPI5VC4_XPDM_ABI_VERSION &&
           Info.Version == 71 &&
           (Info.Flags & RPI5VC4_V3D_FLAG_IDENT_VALID) != 0;
}

ULONG APIENTRY
DrvEscape(
    _In_ SURFOBJ *pso,
    _In_ ULONG iEsc,
    _In_ ULONG cjIn,
    _In_reads_bytes_(cjIn) PVOID pvIn,
    _In_ ULONG cjOut,
    _Out_writes_bytes_(cjOut) PVOID pvOut)
{
    PPDEV ppdev;
    ULONG RequestedEscape;

    if (pso == NULL || pso->dhpdev == NULL)
        return 0;

    ppdev = (PPDEV)pso->dhpdev;

    if (iEsc == QUERYESCSUPPORT)
    {
        if (pvIn == NULL || cjIn < sizeof(RequestedEscape))
            return 0;

        RequestedEscape = *(PULONG)pvIn;
        if (RequestedEscape == RPI5VC4_ESCAPE_RENDER_CLEAR ||
            RequestedEscape == RPI5VC4_ESCAPE_RENDER_TRIANGLE ||
            RequestedEscape == RPI5VC4_ESCAPE_RENDER_BATCH ||
            RequestedEscape == RPI5VC4_ESCAPE_UPLOAD_TEXTURE ||
            RequestedEscape == RPI5VC4_ESCAPE_RENDER_GRAPH ||
            RequestedEscape == RPI5VC4_ESCAPE_READ_GRAPH ||
            RequestedEscape == RPI5VC4_ESCAPE_READ_TEXTURE ||
            RequestedEscape == OPENGL_GETINFO)
            return FrameBufferV3dExecutionSupported(ppdev) ? 1 : 0;

        return RequestedEscape == RPI5VC4_ESCAPE_QUERY_PLATFORM ||
               RequestedEscape == RPI5VC4_ESCAPE_QUERY_V3D ||
               RequestedEscape == RPI5VC4_ESCAPE_RUN_V3D_SELFTEST ||
               RequestedEscape == RPI5VC4_ESCAPE_WAIT_VBLANK;
    }

    if (pvOut == NULL)
        return 0;

    if (iEsc == OPENGL_GETINFO)
    {
        static const WCHAR IcdName[] = L"Rpi5Vc4";
        ULONG Query;
        PFRAMEBUF_OPENGL_INFO Info;

        if (pvIn == NULL || cjIn < sizeof(Query) || cjOut < sizeof(*Info) || !FrameBufferV3dExecutionSupported(ppdev))
            return 0;

        Query = *(PULONG)pvIn;
        if (Query != OPENGL_GETINFO_DRVNAME)
            return 0;

        Info = pvOut;
        memset(Info, 0, sizeof(*Info));
        Info->Version = FRAMEBUF_OPENGL_ICD_VERSION;
        Info->DriverVersion = FRAMEBUF_OPENGL_ICD_DRIVER_VERSION;
        memcpy(Info->DriverName, IcdName, sizeof(IcdName));
        return sizeof(*Info);
    }

    if (iEsc == RPI5VC4_ESCAPE_QUERY_PLATFORM && cjOut >= sizeof(RPI5VC4_PLATFORM_INFO))
        return FrameBufferGpuEscape(ppdev, iEsc, NULL, 0, pvOut, cjOut);

    if (iEsc == RPI5VC4_ESCAPE_QUERY_V3D && cjOut >= sizeof(RPI5VC4_V3D_INFO))
        return FrameBufferGpuEscape(ppdev, iEsc, NULL, 0, pvOut, cjOut);

    if (iEsc == RPI5VC4_ESCAPE_RUN_V3D_SELFTEST && cjOut >= sizeof(RPI5VC4_V3D_SELFTEST))
        return FrameBufferGpuEscape(ppdev, iEsc, NULL, 0, pvOut, cjOut);

    if (iEsc == RPI5VC4_ESCAPE_RENDER_CLEAR && pvIn != NULL && cjIn >= sizeof(RPI5VC4_V3D_CLEAR_REQUEST) && cjOut >= FIELD_OFFSET(RPI5VC4_V3D_CLEAR_RESULT, Pixels))
        return FrameBufferGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);

    if (iEsc == RPI5VC4_ESCAPE_RENDER_TRIANGLE && pvIn != NULL && cjIn >= sizeof(RPI5VC4_V3D_TRIANGLE_REQUEST) && cjOut >= FIELD_OFFSET(RPI5VC4_V3D_TRIANGLE_RESULT, Pixels))
        return FrameBufferGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);

    if (iEsc == RPI5VC4_ESCAPE_RENDER_BATCH && pvIn != NULL && cjIn >= FIELD_OFFSET(RPI5VC4_V3D_BATCH_REQUEST, Vertices) && cjOut >= FIELD_OFFSET(RPI5VC4_V3D_BATCH_RESULT, Pixels))
        return FrameBufferGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);

    if (iEsc == RPI5VC4_ESCAPE_UPLOAD_TEXTURE && pvIn != NULL && cjIn >= FIELD_OFFSET(RPI5VC4_V3D_TEXTURE_UPLOAD_REQUEST, Pixels) && cjOut >= sizeof(RPI5VC4_V3D_TEXTURE_UPLOAD_RESULT))
        return FrameBufferGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);

    if (iEsc == RPI5VC4_ESCAPE_RENDER_GRAPH && pvIn != NULL && cjIn >= FIELD_OFFSET(RPI5VC4_V3D_RENDER_GRAPH_REQUEST, Pixels) && cjOut >= sizeof(RPI5VC4_V3D_RENDER_GRAPH_RESULT))
        return FrameBufferGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);

    if (iEsc == RPI5VC4_ESCAPE_READ_GRAPH && pvIn != NULL && cjIn >= sizeof(RPI5VC4_V3D_READ_GRAPH_REQUEST) && cjOut >= FIELD_OFFSET(RPI5VC4_V3D_READ_GRAPH_RESULT, Pixels))
        return FrameBufferGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);

    if (iEsc == RPI5VC4_ESCAPE_READ_TEXTURE && pvIn != NULL && cjIn >= sizeof(RPI5VC4_V3D_READ_TEXTURE_REQUEST) && cjOut >= FIELD_OFFSET(RPI5VC4_V3D_READ_TEXTURE_RESULT, Pixels))
        return FrameBufferGpuEscape(ppdev, iEsc, pvIn, cjIn, pvOut, cjOut);

    if (iEsc == RPI5VC4_ESCAPE_WAIT_VBLANK && cjOut >= sizeof(RPI5VC4_VBLANK_RESULT))
        return FrameBufferGpuEscape(ppdev, iEsc, NULL, 0, pvOut, cjOut);

    return 0;
}
