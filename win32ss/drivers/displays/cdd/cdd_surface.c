/*
 * ReactOS Canonical Display Driver (CDD) - Surface Management
 *
 * Copyright (C) 2026 ReactOS Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * DESCRIPTION
 * -----------
 * DrvEnableSurface / DrvDisableSurface / DrvAssertMode.
 *
 * CDD creates a STYPE_DEVICE primary surface so drawing DDIs route through
 * our Drv* hooks.  For raw framebuffer mappings it uses a separate shadow
 * STYPE_BITMAP as the backing store.  For dxgkrnl's sysmem-scanout contract
 * it reuses the mapped scanout buffer directly as the shadow bitmap and
 * flushes via dirty-rect notifications instead of VRAM memcpy.
 *
 * Phase 1 maps the framebuffer via IOCTL_VIDEO_MAP_VIDEO_MEMORY (like
 * framebuf).  For hardware VRAM mappings it creates a system-memory shadow
 * on top of it.  For dxgkrnl sysmem scanout, the mapping is already the
 * authoritative backing store.
 *
 * Phase 2 will replace the IOCTL mapping with D3DKMTCreateAllocation /
 * D3DKMTLock, giving DWM direct access to the allocation for compositing.
 */

#include "cdd.h"

#define CDD_SHADOW_GUARD_BYTES 0x1000
#define CDD_PRIMARY_SAMPLE_LOG_LIMIT 32
#define CDD_PRIMARY_LOCK_SAMPLE_LOG_LIMIT 16

static volatile LONG g_CddPrimarySampleTraceCount = 0;
static volatile LONG g_CddPrimaryPresentTraceCount = 0;
static volatile LONG g_CddPrimaryLockSampleTraceCount = 0;

static __inline BOOL
CddShouldTracePresentSample(
    _In_ LONG Sample)
{
    return (Sample <= 8 || ((Sample % 128) == 0));
}

/* D3DKMT IOCTL codes — must match dxgkrnl/d3dkmt.h */
#ifndef DXGKRNL_DEVICE_TYPE
#define DXGKRNL_DEVICE_TYPE 0x23
#endif
#define IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x111, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CLOSEADAPTER \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x103, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEDEVICE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x120, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYDEVICE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x121, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x136, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYRESOURCEINFO \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x134, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENRESOURCE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x135, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_LOCK \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x132, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_UNLOCK \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x133, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETSHADOWSURFACE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x137, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_PRESENT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x141, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETDISPLAYMODE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x160, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _DXGKMT_GETSHADOWSURFACE
{
    D3DKMT_HANDLE                  hAdapter;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    D3DKMT_HANDLE                  hShadowSurface;
    UINT                           Width;
    UINT                           Height;
    UINT                           Pitch;
    D3DDDIFORMAT                   Format;
} DXGKMT_GETSHADOWSURFACE, *PDXGKMT_GETSHADOWSURFACE;

/*
 * EngAllocSectionMem / EngFreeSectionMem -- not declared in the standard
 * winddi.h but are exported by win32k.  We provide local prototypes.
 */
PVOID APIENTRY
EngAllocSectionMem(
    _Outptr_ PVOID *ppvSection,
    _In_ ULONG fl,
    _In_ SIZE_T cjSize,
    _In_ ULONG ulTag);

BOOL APIENTRY
EngFreeSectionMem(
    _In_opt_ PVOID pvSection,
    _In_opt_ PVOID pvMappedBase);


/* ========================================================================
 * D3DKMT shared primary surface helpers
 *
 * CddInitD3DKMT opens both the shared primary scanout allocation and a
 * coherent shadow allocation. GDI renders into the coherent shadow; WDDM
 * presents copy shadow -> primary.
 * On failure, all partial state is torn down and FALSE is returned.
 * ======================================================================== */

/*
 * CddD3DKMTIoctl — send a D3DKMT IOCTL to dxgkrnl via the display handle.
 * Returns NTSTATUS; output data is written back into pData (in/out).
 */
static NTSTATUS
CddD3DKMTIoctl(
    _In_    PCDD_PDEV ppdev,
    _In_    ULONG     IoCtl,
    _Inout_ PVOID     pData,
    _In_    ULONG     DataSize)
{
    ULONG BytesReturned = 0;
    DWORD Err;

    Err = EngDeviceIoControl(ppdev->hDriver, IoCtl,
                             pData, DataSize,
                             pData, DataSize,
                             &BytesReturned);
    /* EngDeviceIoControl returns the NTSTATUS as a DWORD */
    return (NTSTATUS)Err;
}

static NTSTATUS
CddOpenSharedResource(
    _In_ PCDD_PDEV ppdev,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _Out_ PULONG phResource,
    _Out_ PULONG phAllocation)
{
    D3DKMT_QUERYRESOURCEINFO QueryRes;
    D3DDDI_OPENALLOCATIONINFO OpenAllocInfo;
    D3DKMT_OPENRESOURCE OpenRes;
    PVOID PrivateRuntimeData = NULL;
    PVOID ResourcePrivateData = NULL;
    PVOID TotalPrivateDriverData = NULL;
    NTSTATUS Status;

    if (phResource == NULL || phAllocation == NULL)
        return STATUS_INVALID_PARAMETER;

    *phResource = 0;
    *phAllocation = 0;

    RtlZeroMemory(&QueryRes, sizeof(QueryRes));
    QueryRes.hDevice = ppdev->hD3DDevice;
    QueryRes.hGlobalShare = hGlobalShare;
    Status = CddD3DKMTIoctl(ppdev,
                            IOCTL_D3DKMT_QUERYRESOURCEINFO,
                            &QueryRes,
                            sizeof(QueryRes));
    if (!NT_SUCCESS(Status) || QueryRes.NumAllocations < 1)
    {
        CDD_DBG("D3DKMT: QueryResourceInfo failed 0x%lX (share=0x%X n=%u)\n",
                (unsigned long)Status,
                hGlobalShare,
                QueryRes.NumAllocations);
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    }

    if (QueryRes.PrivateRuntimeDataSize != 0)
    {
        PrivateRuntimeData = EngAllocMem(0,
                                         QueryRes.PrivateRuntimeDataSize,
                                         ALLOC_TAG);
        if (PrivateRuntimeData == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (QueryRes.ResourcePrivateDriverDataSize != 0)
    {
        ResourcePrivateData = EngAllocMem(0,
                                          QueryRes.ResourcePrivateDriverDataSize,
                                          ALLOC_TAG);
        if (ResourcePrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    if (QueryRes.TotalPrivateDriverDataSize != 0)
    {
        TotalPrivateDriverData = EngAllocMem(0,
                                             QueryRes.TotalPrivateDriverDataSize,
                                             ALLOC_TAG);
        if (TotalPrivateDriverData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    RtlZeroMemory(&OpenAllocInfo, sizeof(OpenAllocInfo));
    RtlZeroMemory(&OpenRes, sizeof(OpenRes));
    OpenRes.hDevice = ppdev->hD3DDevice;
    OpenRes.hGlobalShare = hGlobalShare;
    OpenRes.NumAllocations = 1;
    OpenRes.pOpenAllocationInfo = &OpenAllocInfo;
    OpenRes.pPrivateRuntimeData = PrivateRuntimeData;
    OpenRes.PrivateRuntimeDataSize = QueryRes.PrivateRuntimeDataSize;
    OpenRes.pResourcePrivateDriverData = ResourcePrivateData;
    OpenRes.ResourcePrivateDriverDataSize = QueryRes.ResourcePrivateDriverDataSize;
    OpenRes.pTotalPrivateDriverDataBuffer = TotalPrivateDriverData;
    OpenRes.TotalPrivateDriverDataBufferSize = QueryRes.TotalPrivateDriverDataSize;
    Status = CddD3DKMTIoctl(ppdev,
                            IOCTL_D3DKMT_OPENRESOURCE,
                            &OpenRes,
                            sizeof(OpenRes));
    if (!NT_SUCCESS(Status))
    {
        CDD_DBG("D3DKMT: OpenResource failed 0x%lX (share=0x%X)\n",
                (unsigned long)Status,
                hGlobalShare);
        goto Cleanup;
    }

    *phResource = OpenRes.hResource;
    *phAllocation = OpenAllocInfo.hAllocation;

Cleanup:
    if (TotalPrivateDriverData != NULL)
        EngFreeMem(TotalPrivateDriverData);
    if (ResourcePrivateData != NULL)
        EngFreeMem(ResourcePrivateData);
    if (PrivateRuntimeData != NULL)
        EngFreeMem(PrivateRuntimeData);

    return Status;
}

static BOOLEAN
CddInitD3DKMT(
    _Inout_ PCDD_PDEV ppdev)
{
    NTSTATUS Status;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenAdapter;
    D3DKMT_CREATEDEVICE CreateDevice;
    D3DKMT_GETSHAREDPRIMARYHANDLE GetShared;
    DXGKMT_GETSHADOWSURFACE GetShadow;
    D3DKMT_SETDISPLAYMODE SetDisplayMode;
    D3DKMT_LOCK Lock;

    /* Step 1: Open adapter */
    RtlZeroMemory(&OpenAdapter, sizeof(OpenAdapter));
    wcscpy(OpenAdapter.DeviceName, L"\\\\.\\DISPLAY1");
    Status = CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME,
                            &OpenAdapter, sizeof(OpenAdapter));
    if (!NT_SUCCESS(Status))
    {
        CDD_DBG("D3DKMT: OpenAdapter failed 0x%lX\n", (unsigned long)Status);
        return FALSE;
    }
    ppdev->hD3DAdapter = OpenAdapter.hAdapter;
    ppdev->VidPnSourceId = OpenAdapter.VidPnSourceId;

    /* Step 2: Create device */
    RtlZeroMemory(&CreateDevice, sizeof(CreateDevice));
    CreateDevice.hAdapter = ppdev->hD3DAdapter;
    Status = CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_CREATEDEVICE,
                            &CreateDevice, sizeof(CreateDevice));
    if (!NT_SUCCESS(Status))
    {
        CDD_DBG("D3DKMT: CreateDevice failed 0x%lX\n", (unsigned long)Status);
        goto FailCloseAdapter;
    }
    ppdev->hD3DDevice = CreateDevice.hDevice;

    /* Step 3: Open the shared primary scanout allocation. */
    RtlZeroMemory(&GetShared, sizeof(GetShared));
    GetShared.hAdapter = ppdev->hD3DAdapter;
    GetShared.VidPnSourceId = ppdev->VidPnSourceId;
    Status = CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE,
                            &GetShared, sizeof(GetShared));
    if (!NT_SUCCESS(Status) || GetShared.hSharedPrimary == 0)
    {
        CDD_DBG("D3DKMT: GetSharedPrimaryHandle failed 0x%lX (h=0x%X)\n",
                (unsigned long)Status, GetShared.hSharedPrimary);
        goto FailDestroyDevice;
    }

    Status = CddOpenSharedResource(ppdev,
                                   GetShared.hSharedPrimary,
                                   &ppdev->hD3DPrimaryResource,
                                   &ppdev->hD3DPrimaryAllocation);
    if (!NT_SUCCESS(Status))
        goto FailDestroyDevice;

    /*
     * Step 4: Bind the shared primary allocation to the active source.
     *
     * The primary remains the scanout target. GDI renders into a coherent
     * shadow allocation, and D3DKMT present copies shadow -> primary.
     */
    RtlZeroMemory(&SetDisplayMode, sizeof(SetDisplayMode));
    SetDisplayMode.hDevice = ppdev->hD3DDevice;
    SetDisplayMode.hPrimaryAllocation = ppdev->hD3DPrimaryAllocation;
    SetDisplayMode.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    SetDisplayMode.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
    Status = CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_SETDISPLAYMODE,
                            &SetDisplayMode, sizeof(SetDisplayMode));
    if (!NT_SUCCESS(Status))
    {
        CDD_DBG("D3DKMT: SetDisplayMode failed 0x%lX\n",
                (unsigned long)Status);
        goto FailDestroyDevice;
    }

    /*
     * Step 5: Open the coherent shadow allocation that backs GDI drawing.
     */
    RtlZeroMemory(&GetShadow, sizeof(GetShadow));
    GetShadow.hAdapter = ppdev->hD3DAdapter;
    GetShadow.VidPnSourceId = ppdev->VidPnSourceId;
    Status = CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_GETSHADOWSURFACE,
                            &GetShadow, sizeof(GetShadow));
    if (!NT_SUCCESS(Status) || GetShadow.hShadowSurface == 0)
    {
        CDD_DBG("D3DKMT: GetShadowSurface failed 0x%lX (h=0x%X)\n",
                (unsigned long)Status, GetShadow.hShadowSurface);
        goto FailDestroyDevice;
    }

    Status = CddOpenSharedResource(ppdev,
                                   GetShadow.hShadowSurface,
                                   &ppdev->hD3DShadowResource,
                                   &ppdev->hD3DShadowAllocation);
    if (!NT_SUCCESS(Status))
        goto FailDestroyDevice;

    if (GetShadow.Pitch != 0)
        ppdev->ScreenDelta = GetShadow.Pitch;
    ppdev->ShadowPitch = GetShadow.Pitch;

    /*
     * Step 6: Lock the coherent shadow allocation for CPU-visible GDI
     * rendering. The shared primary stays as the present destination.
     */
    RtlZeroMemory(&Lock, sizeof(Lock));
    Lock.hDevice = ppdev->hD3DDevice;
    Lock.hAllocation = ppdev->hD3DShadowAllocation;
    Status = CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_LOCK,
                            &Lock, sizeof(Lock));
    if (!NT_SUCCESS(Status) || Lock.pData == NULL)
    {
        CDD_DBG("D3DKMT: Lock failed 0x%lX (pData=%p)\n",
                (unsigned long)Status, Lock.pData);
        goto FailDestroyDevice;
    }

    ppdev->ScreenPtr = Lock.pData;
    ppdev->VramPtr = NULL;
    ppdev->D3DKMTConnected = TRUE;
    ppdev->SysmemFramebuffer = FALSE;
    ppdev->LastPrimaryUpdateTick = 0;

    CDD_DBG("D3DKMT: Primary=0x%X Shadow=0x%X mapped shadow=%p "
            "(adapter=0x%X device=0x%X pitch=%u)\n",
            ppdev->hD3DPrimaryAllocation,
            ppdev->hD3DShadowAllocation,
            Lock.pData,
            ppdev->hD3DAdapter,
            ppdev->hD3DDevice,
            ppdev->ShadowPitch ? ppdev->ShadowPitch : ppdev->ScreenDelta);

    return TRUE;

FailDestroyDevice:
    {
        D3DKMT_DESTROYDEVICE dd;
        dd.hDevice = ppdev->hD3DDevice;
        CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_DESTROYDEVICE, &dd, sizeof(dd));
        ppdev->hD3DDevice = 0;
    }
FailCloseAdapter:
    {
        D3DKMT_CLOSEADAPTER ca;
        ca.hAdapter = ppdev->hD3DAdapter;
        CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_CLOSEADAPTER, &ca, sizeof(ca));
        ppdev->hD3DAdapter = 0;
    }
    ppdev->D3DKMTConnected = FALSE;
    ppdev->hD3DPrimaryAllocation = 0;
    ppdev->hD3DPrimaryResource = 0;
    ppdev->hD3DShadowAllocation = 0;
    ppdev->hD3DShadowResource = 0;
    ppdev->ShadowPitch = 0;
    return FALSE;
}

static BOOLEAN
CddBuildSampleRect(
    _In_ PCDD_PDEV ppdev,
    _In_ const RECTL *prcl,
    _Out_ PRECTL pSampleRect)
{
    if (ppdev == NULL || prcl == NULL || pSampleRect == NULL)
        return FALSE;

    *pSampleRect = *prcl;

    if (pSampleRect->left < 0)
        pSampleRect->left = 0;
    if (pSampleRect->top < 0)
        pSampleRect->top = 0;
    if (pSampleRect->right > (LONG)ppdev->ScreenWidth)
        pSampleRect->right = (LONG)ppdev->ScreenWidth;
    if (pSampleRect->bottom > (LONG)ppdev->ScreenHeight)
        pSampleRect->bottom = (LONG)ppdev->ScreenHeight;

    return (pSampleRect->left < pSampleRect->right) &&
           (pSampleRect->top < pSampleRect->bottom);
}

static VOID
CddLogRectSamples(
    _In_z_ PCSTR SurfaceTag,
    _In_ LONG TraceSeq,
    _In_ const VOID *Base,
    _In_ ULONG PitchBytes,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ const RECTL *prcl)
{
    const ULONG *Pixels = (const ULONG *)Base;
    ULONG PitchPixels;
    RECTL SampleRect;
    LONG xs[5];
    LONG ys[5];
    ULONG SampleValues[5];
    ULONG NonZeroCount = 0;
    UINT i;

    if (Base == NULL ||
        prcl == NULL ||
        PitchBytes < sizeof(ULONG) ||
        Width == 0 ||
        Height == 0)
    {
        return;
    }

    SampleRect = *prcl;
    if (SampleRect.left < 0)
        SampleRect.left = 0;
    if (SampleRect.top < 0)
        SampleRect.top = 0;
    if (SampleRect.right > (LONG)Width)
        SampleRect.right = (LONG)Width;
    if (SampleRect.bottom > (LONG)Height)
        SampleRect.bottom = (LONG)Height;
    if (SampleRect.left >= SampleRect.right ||
        SampleRect.top >= SampleRect.bottom)
    {
        return;
    }

    PitchPixels = PitchBytes / sizeof(ULONG);
    xs[0] = SampleRect.left;
    ys[0] = SampleRect.top;
    xs[1] = SampleRect.right - 1;
    ys[1] = SampleRect.top;
    xs[2] = SampleRect.left + ((SampleRect.right - SampleRect.left) / 2);
    ys[2] = SampleRect.top + ((SampleRect.bottom - SampleRect.top) / 2);
    xs[3] = SampleRect.left;
    ys[3] = SampleRect.bottom - 1;
    xs[4] = SampleRect.right - 1;
    ys[4] = SampleRect.bottom - 1;

    for (i = 0; i < RTL_NUMBER_OF(SampleValues); ++i)
    {
        SampleValues[i] = Pixels[(ys[i] * PitchPixels) + xs[i]];
        if (SampleValues[i] != 0)
            ++NonZeroCount;
    }

    CDD_DBG("%s sample seq=%ld nz=%lu pitch=%lu rect=(%ld,%ld)-(%ld,%ld) "
            "tl=%08lx tr=%08lx c=%08lx bl=%08lx br=%08lx\n",
            SurfaceTag,
            TraceSeq,
            NonZeroCount,
            PitchBytes,
            SampleRect.left,
            SampleRect.top,
            SampleRect.right,
            SampleRect.bottom,
            (unsigned long)SampleValues[0],
            (unsigned long)SampleValues[1],
            (unsigned long)SampleValues[2],
            (unsigned long)SampleValues[3],
            (unsigned long)SampleValues[4]);
}

static VOID
CddLogPresentSamples(
    _In_ PCDD_PDEV ppdev,
    _In_ const RECTL *prcl,
    _In_ NTSTATUS PresentStatus)
{
    RECTL SampleRect;
    LONG TraceSeq;
    ULONG SamplePitch;

    if (ppdev == NULL ||
        ppdev->BitsPerPixel != 32 ||
        ppdev->ScreenWidth == 0 ||
        ppdev->ScreenHeight == 0 ||
        !CddBuildSampleRect(ppdev, prcl, &SampleRect))
    {
        return;
    }

    TraceSeq = InterlockedIncrement(&g_CddPrimarySampleTraceCount);
    if (TraceSeq > CDD_PRIMARY_SAMPLE_LOG_LIMIT)
        return;

    SamplePitch = ppdev->ShadowPitch != 0 ? ppdev->ShadowPitch : ppdev->ScreenDelta;
    if (SamplePitch >= sizeof(ULONG) && ppdev->ScreenPtr != NULL)
    {
        CddLogRectSamples("Shadow",
                          TraceSeq,
                          ppdev->ScreenPtr,
                          SamplePitch,
                          ppdev->ScreenWidth,
                          ppdev->ScreenHeight,
                          &SampleRect);
    }

    if (NT_SUCCESS(PresentStatus) &&
        ppdev->hD3DPrimaryAllocation != 0 &&
        InterlockedIncrement(&g_CddPrimaryLockSampleTraceCount) <= CDD_PRIMARY_LOCK_SAMPLE_LOG_LIMIT)
    {
        D3DKMT_LOCK Lock;
        D3DKMT_UNLOCK Unlock;
        D3DKMT_HANDLE AllocationHandle = ppdev->hD3DPrimaryAllocation;
        NTSTATUS LockStatus;

        RtlZeroMemory(&Lock, sizeof(Lock));
        Lock.hDevice = ppdev->hD3DDevice;
        Lock.hAllocation = AllocationHandle;
        LockStatus = CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_LOCK, &Lock, sizeof(Lock));
        if (NT_SUCCESS(LockStatus) && Lock.pData != NULL)
        {
            CddLogRectSamples("Primary",
                              TraceSeq,
                              Lock.pData,
                              SamplePitch,
                              ppdev->ScreenWidth,
                              ppdev->ScreenHeight,
                              &SampleRect);

            RtlZeroMemory(&Unlock, sizeof(Unlock));
            Unlock.hDevice = ppdev->hD3DDevice;
            Unlock.NumAllocations = 1;
            Unlock.phAllocations = &AllocationHandle;
            CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_UNLOCK, &Unlock, sizeof(Unlock));
        }
        else
        {
            CDD_DBG("Primary lock sample seq=%ld status=0x%08lx alloc=0x%X rect=(%ld,%ld)-(%ld,%ld)\n",
                    TraceSeq,
                    (unsigned long)LockStatus,
                    ppdev->hD3DPrimaryAllocation,
                    SampleRect.left,
                    SampleRect.top,
                    SampleRect.right,
                    SampleRect.bottom);
        }
    }
}

static VOID
CddTeardownD3DKMT(
    _Inout_ PCDD_PDEV ppdev)
{
    if (!ppdev->D3DKMTConnected)
        return;

    if (ppdev->hD3DShadowAllocation && ppdev->hD3DDevice)
    {
        D3DKMT_UNLOCK Unlock;
        D3DKMT_HANDLE hAlloc = ppdev->hD3DShadowAllocation;
        RtlZeroMemory(&Unlock, sizeof(Unlock));
        Unlock.hDevice = ppdev->hD3DDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &hAlloc;
        CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_UNLOCK, &Unlock, sizeof(Unlock));
    }

    if (ppdev->hD3DDevice)
    {
        D3DKMT_DESTROYDEVICE dd;
        dd.hDevice = ppdev->hD3DDevice;
        CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_DESTROYDEVICE, &dd, sizeof(dd));
    }

    if (ppdev->hD3DAdapter)
    {
        D3DKMT_CLOSEADAPTER ca;
        ca.hAdapter = ppdev->hD3DAdapter;
        CddD3DKMTIoctl(ppdev, IOCTL_D3DKMT_CLOSEADAPTER, &ca, sizeof(ca));
    }

    ppdev->hD3DAdapter = 0;
    ppdev->hD3DDevice = 0;
    ppdev->hD3DPrimaryAllocation = 0;
    ppdev->hD3DPrimaryResource = 0;
    ppdev->hD3DShadowAllocation = 0;
    ppdev->hD3DShadowResource = 0;
    ppdev->ShadowPitch = 0;
    ppdev->D3DKMTConnected = FALSE;
    ppdev->ScreenPtr = NULL;
}

VOID
CddPresentPrimaryRect(
    _In_ PCDD_PDEV ppdev,
    _In_opt_ const RECTL *prcl)
{
    D3DKMT_PRESENT Present;
    RECTL PresentRect;
    NTSTATUS Status;
    ULONG CurrentTick;
    ULONG ElapsedTicks;

    if (ppdev == NULL ||
        !ppdev->D3DKMTConnected ||
        ppdev->hD3DDevice == 0 ||
        ppdev->hD3DShadowAllocation == 0 ||
        ppdev->hD3DPrimaryAllocation == 0)
    {
        return;
    }

    CurrentTick = EngGetTickCount32();
    ElapsedTicks = CurrentTick - ppdev->LastPrimaryUpdateTick;

    /*
     * The miniport's scanout flip thread already runs at roughly 60 Hz.
     * Coalesce repeated GDI dirty notifications to one nudge per frame
     * so boot/login rendering does not devolve into a SetDisplayMode storm.
     */
    if (ppdev->LastPrimaryUpdateTick != 0 && ElapsedTicks < 15)
        return;

    PresentRect.left = 0;
    PresentRect.top = 0;
    PresentRect.right = (LONG)ppdev->ScreenWidth;
    PresentRect.bottom = (LONG)ppdev->ScreenHeight;

    if (prcl != NULL)
    {
        PresentRect = *prcl;

        if (PresentRect.left < 0)
            PresentRect.left = 0;
        if (PresentRect.top < 0)
            PresentRect.top = 0;
        if (PresentRect.right > (LONG)ppdev->ScreenWidth)
            PresentRect.right = (LONG)ppdev->ScreenWidth;
        if (PresentRect.bottom > (LONG)ppdev->ScreenHeight)
            PresentRect.bottom = (LONG)ppdev->ScreenHeight;

        if (PresentRect.left >= PresentRect.right ||
            PresentRect.top >= PresentRect.bottom)
        {
            PresentRect.left = 0;
            PresentRect.top = 0;
            PresentRect.right = (LONG)ppdev->ScreenWidth;
            PresentRect.bottom = (LONG)ppdev->ScreenHeight;
        }
    }

    RtlZeroMemory(&Present, sizeof(Present));
    Present.hDevice = ppdev->hD3DDevice;
    Present.VidPnSourceId = ppdev->VidPnSourceId;
    Present.hSource = ppdev->hD3DShadowAllocation;
    Present.hDestination = ppdev->hD3DPrimaryAllocation;
    Present.DstRect = *(const RECT *)&PresentRect;
    Present.SrcRect = *(const RECT *)&PresentRect;
    Present.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
    Present.Flags.Blt = 1;
    Present.Flags.DstRectValid = 1;
    Present.Flags.SrcRectValid = 1;
    Present.Flags.RestrictVidPnSource = 1;

    Status = CddD3DKMTIoctl(ppdev,
                            IOCTL_D3DKMT_PRESENT,
                            &Present,
                            sizeof(Present));
    LONG TraceSample = InterlockedIncrement(&g_CddPrimaryPresentTraceCount);
    if (CddShouldTracePresentSample(TraceSample))
    {
        CDD_DBG("Primary present sample=%ld status=0x%08lx rect=(%ld,%ld)-(%ld,%ld) src=0x%X dst=0x%X\n",
                TraceSample,
                (unsigned long)Status,
                PresentRect.left,
                PresentRect.top,
                PresentRect.right,
                PresentRect.bottom,
                ppdev->hD3DShadowAllocation,
                ppdev->hD3DPrimaryAllocation);
    }
    CddLogPresentSamples(ppdev, &PresentRect, Status);

    if (!NT_SUCCESS(Status))
    {
        CDD_DBG("D3DKMT: primary present failed 0x%lX rect=(%ld,%ld)-(%ld,%ld)\n",
                (unsigned long)Status,
                PresentRect.left,
                PresentRect.top,
                PresentRect.right,
                PresentRect.bottom);
    }
    else
    {
        ppdev->LastPrimaryUpdateTick = CurrentTick;
    }
}

VOID
CddNotifyPrimaryUpdate(
    _In_ PCDD_PDEV ppdev)
{
    CddPresentPrimaryRect(ppdev, NULL);
}

/*
 * DrvEnableSurface
 *
 * Creates the GDI primary surface.  CDD always uses a shadow buffer
 * architecture:
 *
 *   Full adapters:
 *   1. Open the shared primary through D3DKMT.
 *   2. Bind it with SetDisplayMode.
 *   3. Open and lock the coherent shadow surface for GDI drawing.
 *   4. Present shadow -> primary via D3DKMT.
 *
 *   Legacy fallback:
 *   1. Set the video mode on the miniport (IOCTL_VIDEO_SET_CURRENT_MODE).
 *   2. Map the framebuffer (IOCTL_VIDEO_MAP_VIDEO_MEMORY) to get VRAM ptr.
 *   3. Reuse the mapped sysmem scanout buffer directly, or allocate a
 *      separate system-memory shadow buffer for raw VRAM mappings.
 *   4. Copy boot screen content from VRAM to shadow when using raw VRAM.
 *
 *   Shared tail:
 *   5. Create a STYPE_BITMAP surface on the backing store for Eng* to draw on.
 *   6. Create a STYPE_DEVICE primary surface so hooks dispatch to our Drv*.
 *   7. Associate the device surface with all drawing hooks.
 *
 * GDI operations flow: GDI -> Drv*(device surface) -> Eng*(shadow bitmap)
 * -> CddShadowFlushRect(shadow -> VRAM or dirty-rect present).
 */
HSURF APIENTRY
DrvEnableSurface(
    IN DHPDEV dhpdev)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)dhpdev;
    HSURF hSurface;
    ULONG BitmapType;
    SIZEL ScreenSize;
    VIDEO_MEMORY VideoMemory;
    VIDEO_MEMORY_INFORMATION VideoMemoryInfo;
    ULONG ulTemp = 0;
    VIDEO_MODE current = {0};
    FLONG hooks;
    SIZE_T shadowSize;
    SIZE_T shadowAllocSize;
    PVOID shadowMapping;

    CDD_DBG("Setting mode %lu (%ux%u %ubpp)\n",
            (unsigned long)ppdev->ModeIndex,
            (unsigned int)ppdev->ScreenWidth,
            (unsigned int)ppdev->ScreenHeight,
            (unsigned int)ppdev->BitsPerPixel);

    /*
     * Full adapters must bind the shared primary first and avoid the
     * legacy video IOCTL bootstrap entirely.
     */
    if (CddInitD3DKMT(ppdev))
    {
        CDD_DBG("D3DKMT shared primary active — skipping legacy mode programming\n");

        /* Register W32kCddInterface for WDDM notifications */
        CddRegisterW32kInterface(ppdev);

        /* Start the present worker for async display updates */
        CddStartPresentWorker(ppdev);

        goto HaveScreenPtr;
    }

    /*
     * Legacy fallback: set the video mode on the miniport.
     */
    current.RequestedMode = ppdev->ModeIndex;
    if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                           &current, sizeof(current),
                           NULL, 0, &ulTemp))
    {
        CDD_DBG("IOCTL_VIDEO_SET_CURRENT_MODE failed for mode %lu\n",
                (unsigned long)ppdev->ModeIndex);
        return NULL;
    }

    /*
     * Query the actual mode after setting (miniport may have changed it).
     */
    {
        VIDEO_MODE_INFORMATION actualMode = {0};
        ULONG actualModeLen = 0;

        if (!EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_QUERY_CURRENT_MODE,
                                NULL, 0,
                                &actualMode, sizeof(actualMode),
                                &actualModeLen) &&
            actualModeLen >= sizeof(actualMode))
        {
            if (actualMode.VisScreenWidth != ppdev->ScreenWidth ||
                actualMode.VisScreenHeight != ppdev->ScreenHeight ||
                actualMode.ScreenStride != ppdev->ScreenDelta)
            {
                CDD_DBG("Mode adjusted: requested %ux%u -> actual %ux%u\n",
                        (unsigned int)ppdev->ScreenWidth,
                        (unsigned int)ppdev->ScreenHeight,
                        (unsigned int)actualMode.VisScreenWidth,
                        (unsigned int)actualMode.VisScreenHeight);

                ppdev->ModeIndex = actualMode.ModeIndex;
                ppdev->ScreenWidth = actualMode.VisScreenWidth;
                ppdev->ScreenHeight = actualMode.VisScreenHeight;
                ppdev->ScreenDelta = actualMode.ScreenStride;
                ppdev->BitsPerPixel = (UCHAR)(actualMode.BitsPerPlane *
                                               actualMode.NumberOfPlanes);
                ppdev->RedMask = actualMode.RedMask;
                ppdev->GreenMask = actualMode.GreenMask;
                ppdev->BlueMask = actualMode.BlueMask;
            }
        }
    }

    /*
     * Legacy fallback: map the framebuffer from the miniport.
     */
    VideoMemory.RequestedVirtualAddress = NULL;
    RtlZeroMemory(&VideoMemoryInfo, sizeof(VideoMemoryInfo));

    if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY,
                           &VideoMemory, sizeof(VIDEO_MEMORY),
                           &VideoMemoryInfo, sizeof(VIDEO_MEMORY_INFORMATION),
                           &ulTemp))
    {
        CDD_DBG("IOCTL_VIDEO_MAP_VIDEO_MEMORY failed\n");
        return NULL;
    }

    ppdev->VramPtr = VideoMemoryInfo.FrameBufferBase;
    ppdev->ScreenPtr = ppdev->VramPtr;

    CDD_DBG("Mapped framebuffer @ %p (screen %ux%u delta %u bpp %u)\n",
            ppdev->VramPtr,
            (unsigned int)ppdev->ScreenWidth,
            (unsigned int)ppdev->ScreenHeight,
            (unsigned int)ppdev->ScreenDelta,
            (unsigned int)ppdev->BitsPerPixel);

HaveScreenPtr:

    /*
     * Determine bitmap format.
     */
    switch (ppdev->BitsPerPixel)
    {
        case 8:
            CddSetPaletteHw(dhpdev, ppdev->PaletteEntries, 0, 256);
            BitmapType = BMF_8BPP;
            break;
        case 16:
            BitmapType = BMF_16BPP;
            break;
        case 24:
            BitmapType = BMF_24BPP;
            break;
        case 32:
            BitmapType = BMF_32BPP;
            break;
        default:
            CDD_DBG("Unsupported BitsPerPixel: %u\n",
                    (unsigned int)ppdev->BitsPerPixel);
            return NULL;
    }

    ppdev->iDitherFormat = BitmapType;
    ScreenSize.cx = ppdev->ScreenWidth;
    ScreenSize.cy = ppdev->ScreenHeight;

    ppdev->ShadowBuffer = NULL;
    ppdev->ShadowSection = NULL;
    ppdev->hShadowBitmap = NULL;
    ppdev->psoShadow = NULL;

    shadowSize = (SIZE_T)ppdev->ScreenDelta * (SIZE_T)ppdev->ScreenHeight;

    if (ppdev->D3DKMTConnected)
    {
        ppdev->ShadowBuffer = ppdev->ScreenPtr;
        shadowMapping = ppdev->ScreenPtr;

        CDD_DBG("Using D3DKMT shadow @ %p for WDDM desktop "
                "(shadow=0x%X primary=0x%X pitch=%u)\n",
                shadowMapping,
                ppdev->hD3DShadowAllocation,
                ppdev->hD3DPrimaryAllocation,
                ppdev->ScreenDelta);
    }
    else if (ppdev->SysmemFramebuffer)
    {
        /*
         * dxgkrnl already handed us the system-memory scanout buffer that
         * PresentDisplayOnly reads from.  Reuse it directly and rely on
         * dirty-rect notifications instead of an extra shadow copy.
         */
        ppdev->ShadowBuffer = ppdev->ScreenPtr;
        ppdev->VramPtr = NULL;
        shadowMapping = ppdev->ScreenPtr;

        CDD_DBG("Using dxgkrnl sysmem framebuffer @ %p with dirty-rect presents\n",
                shadowMapping);
    }
    else
    {
        if (shadowSize > MAXULONG_PTR - CDD_SHADOW_GUARD_BYTES)
        {
            CDD_DBG("Shadow buffer size overflow (%lu bytes)\n",
                    (unsigned long)shadowSize);
            goto FallbackNoShadow;
        }

        /*
         * Leave one extra page after the shadow surface.  win32k cursor and
         * blit paths still hit memmove-based flushes during boot, and the
         * extra page keeps harmless over-reads from faulting while the
         * remaining rectangle plumbing is tightened up.
         */
        shadowAllocSize = shadowSize + CDD_SHADOW_GUARD_BYTES;
        shadowMapping = EngAllocSectionMem(&ppdev->ShadowSection,
                                           FL_ZERO_MEMORY,
                                           shadowAllocSize,
                                           ALLOC_TAG);
        if (!shadowMapping)
        {
            CDD_DBG("Shadow buffer allocation failed (%lu bytes + %lu guard)\n",
                    (unsigned long)shadowSize,
                    (unsigned long)CDD_SHADOW_GUARD_BYTES);
            goto FallbackNoShadow;
        }

        ppdev->ShadowBuffer = shadowMapping;
        ppdev->ScreenPtr = shadowMapping;

        /* Preserve boot screen content: copy VRAM to shadow */
        if (ppdev->VramPtr)
        {
            RtlCopyMemory(shadowMapping, ppdev->VramPtr, shadowSize);
        }

        CDD_DBG("Shadow buffer @ %p (%lu bytes + %lu guard), VRAM @ %p\n",
                shadowMapping,
                (unsigned long)shadowSize,
                (unsigned long)CDD_SHADOW_GUARD_BYTES,
                ppdev->VramPtr);
    }

    /*
     * Create the shadow STYPE_BITMAP for Eng* functions to draw on.
     */
    ppdev->hShadowBitmap = (HSURF)EngCreateBitmap(
        ScreenSize,
        ppdev->ScreenDelta,
        BitmapType,
        (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0,
        ppdev->ScreenPtr);

    if (ppdev->hShadowBitmap == NULL)
    {
        CDD_DBG("EngCreateBitmap for shadow failed\n");
        goto ShadowFailed;
    }

    ppdev->psoShadow = EngLockSurface(ppdev->hShadowBitmap);
    if (ppdev->psoShadow == NULL)
    {
        CDD_DBG("EngLockSurface for shadow failed\n");
        EngDeleteSurface(ppdev->hShadowBitmap);
        ppdev->hShadowBitmap = NULL;
        goto ShadowFailed;
    }

    /*
     * Create the STYPE_DEVICE primary surface so that all DDI hooks fire.
     */
    hSurface = (HSURF)EngCreateDeviceSurface((DHSURF)ppdev, ScreenSize, BitmapType);
    if (hSurface == NULL)
    {
        CDD_DBG("EngCreateDeviceSurface failed\n");
        EngUnlockSurface(ppdev->psoShadow);
        ppdev->psoShadow = NULL;
        EngDeleteSurface(ppdev->hShadowBitmap);
        ppdev->hShadowBitmap = NULL;
        goto ShadowFailed;
    }

    /*
     * Hook all drawing functions.  This is essential: without hooks, the
     * GDI engine would bypass our Drv* functions and we would never flush
     * the shadow buffer to VRAM.
     */
    hooks = HOOK_BITBLT | HOOK_COPYBITS | HOOK_TEXTOUT |
            HOOK_STROKEPATH | HOOK_FILLPATH | HOOK_STROKEANDFILLPATH |
            HOOK_LINETO | HOOK_STRETCHBLT | HOOK_STRETCHBLTROP |
            HOOK_ALPHABLEND | HOOK_TRANSPARENTBLT | HOOK_GRADIENTFILL |
            HOOK_PAINT | HOOK_PLGBLT;

    if (!EngAssociateSurface(hSurface, ppdev->hDevEng, hooks))
    {
        CDD_DBG("EngAssociateSurface failed\n");
        EngDeleteSurface(hSurface);
        EngUnlockSurface(ppdev->psoShadow);
        ppdev->psoShadow = NULL;
        EngDeleteSurface(ppdev->hShadowBitmap);
        ppdev->hShadowBitmap = NULL;
        goto ShadowFailed;
    }

    ppdev->hSurfEng = hSurface;
    return hSurface;

ShadowFailed:
    /* Clean up shadow buffer */
    if (ppdev->ShadowSection && ppdev->ShadowBuffer)
    {
        EngFreeSectionMem(ppdev->ShadowSection, ppdev->ShadowBuffer);
        ppdev->ShadowBuffer = NULL;
        ppdev->ShadowSection = NULL;
    }

    if (ppdev->D3DKMTConnected || ppdev->SysmemFramebuffer)
    {
        CDD_DBG("Shadow setup failed for active shared backing store\n");
        return NULL;
    }

FallbackNoShadow:
    /*
     * Fallback: no shadow buffer -- create a plain STYPE_BITMAP surface
     * directly on the VRAM mapping.  This is the framebuf non-shadow path.
     * GDI draws directly to VRAM (slow but functional).
     */
    CDD_DBG("Falling back to direct VRAM surface (no shadow)\n");

    ppdev->ScreenPtr = ppdev->VramPtr;
    ppdev->ShadowBuffer = NULL;
    ppdev->ShadowSection = NULL;
    ppdev->hShadowBitmap = NULL;
    ppdev->psoShadow = NULL;

    hSurface = (HSURF)EngCreateBitmap(
        ScreenSize,
        ppdev->ScreenDelta,
        BitmapType,
        (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0,
        ppdev->ScreenPtr);

    if (hSurface == NULL)
    {
        CDD_DBG("EngCreateBitmap (fallback) failed\n");
        return NULL;
    }

    if (!EngAssociateSurface(hSurface, ppdev->hDevEng, 0))
    {
        CDD_DBG("EngAssociateSurface (fallback) failed\n");
        EngDeleteSurface(hSurface);
        return NULL;
    }

    ppdev->hSurfEng = hSurface;
    return hSurface;
}


/*
 * DrvDisableSurface
 *
 * Tears down the primary surface and frees shadow buffer resources.
 */
VOID APIENTRY
DrvDisableSurface(
    IN DHPDEV dhpdev)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)dhpdev;
    DWORD ulTemp;
    VIDEO_MEMORY VideoMemory;

    /* Delete the primary GDI surface */
    if (ppdev->hSurfEng)
    {
        EngDeleteSurface(ppdev->hSurfEng);
        ppdev->hSurfEng = NULL;
    }

    /* Release shadow bitmap */
    if (ppdev->psoShadow)
    {
        EngUnlockSurface(ppdev->psoShadow);
        ppdev->psoShadow = NULL;
    }
    if (ppdev->hShadowBitmap)
    {
        EngDeleteSurface(ppdev->hShadowBitmap);
        ppdev->hShadowBitmap = NULL;
    }

    /* Free shadow memory */
    if (ppdev->ShadowSection && ppdev->ShadowBuffer)
    {
        EngFreeSectionMem(ppdev->ShadowSection, ppdev->ShadowBuffer);
        ppdev->ShadowBuffer = NULL;
        ppdev->ShadowSection = NULL;
    }

    /* Stop present worker before teardown */
    CddStopPresentWorker(ppdev);

    /* D3DKMT path cleanup — skip legacy VRAM unmap */
    if (ppdev->D3DKMTConnected)
    {
        CddTeardownD3DKMT(ppdev);
        ppdev->VramPtr = NULL;
        return;
    }

    if (!ppdev->SysmemFramebuffer)
        ppdev->ScreenPtr = ppdev->VramPtr;

    /* Unmap VRAM */
    if (ppdev->ScreenPtr)
    {
        VideoMemory.RequestedVirtualAddress = ppdev->ScreenPtr;
        EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY,
                           &VideoMemory, sizeof(VIDEO_MEMORY),
                           NULL, 0, &ulTemp);
    }

    ppdev->VramPtr = NULL;
    ppdev->ScreenPtr = NULL;
}


/*
 * DrvAssertMode
 *
 * Reasserts or resets the video mode.
 */
BOOL APIENTRY
DrvAssertMode(
    IN DHPDEV dhpdev,
    IN BOOL bEnable)
{
    PCDD_PDEV ppdev = (PCDD_PDEV)dhpdev;
    ULONG ulTemp;

    if (bEnable)
    {
        VIDEO_MODE setMode = {0};

        CDD_DBG("DrvAssertMode(TRUE) mode %lu\n",
                (unsigned long)ppdev->ModeIndex);

        if (ppdev->D3DKMTConnected)
        {
            if (ppdev->BitsPerPixel == 8)
                CddSetPaletteHw(dhpdev, ppdev->PaletteEntries, 0, 256);
            return TRUE;
        }

        setMode.RequestedMode = ppdev->ModeIndex;
        if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE,
                               &setMode, sizeof(setMode), NULL, 0, &ulTemp))
        {
            CDD_DBG("DrvAssertMode: SET_CURRENT_MODE failed\n");
            return FALSE;
        }

        if (ppdev->BitsPerPixel == 8)
        {
            CddSetPaletteHw(dhpdev, ppdev->PaletteEntries, 0, 256);
        }

        /* Flush entire shadow to VRAM after mode reassertion */
        if (ppdev->ShadowBuffer && ppdev->VramPtr)
        {
            RECTL rclScreen;
            rclScreen.left = 0;
            rclScreen.top = 0;
            rclScreen.right = ppdev->ScreenWidth;
            rclScreen.bottom = ppdev->ScreenHeight;
            CddShadowFlushRect(ppdev, &rclScreen);
        }

        return TRUE;
    }
    else
    {
        CDD_DBG("DrvAssertMode(FALSE) reset device\n");

        if (ppdev->D3DKMTConnected)
            return TRUE;

        if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_RESET_DEVICE,
                               NULL, 0, NULL, 0, &ulTemp))
        {
            CDD_DBG("DrvAssertMode: RESET_DEVICE failed\n");
            return FALSE;
        }
        return TRUE;
    }
}
