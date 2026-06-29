/*
 * PROJECT:     ReactOS Direct3D 9 WDDM Bridge
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     WDDM UMD DDI types and bridge declarations for d3d9.dll
 * COPYRIGHT:   Copyright 2026 ReactOS Contributors
 *
 * This header defines the D3D9 User-Mode Driver (UMD) DDI types needed for
 * the WDDM code path in d3d9.dll. On Windows, these come from d3dumddi.h
 * which is part of the WDK. We define clean-room equivalents here based on
 * publicly documented MSDN structures and the binary analysis report.
 *
 * Architecture:
 *   App -> d3d9.dll -> UMD (via OpenAdapter DDI) -> D3DKMT (gdi32) -> dxgkrnl
 *
 * The runtime (d3d9.dll) provides callback functions TO the UMD:
 *   AllocateCb, DeallocateCb, LockCb, UnlockCb, RenderCb, PresentCb, EscapeCb
 *
 * The UMD provides DDI functions TO the runtime via OpenAdapter:
 *   CreateDevice, DestroyDevice, plus per-device DDI function table
 */

#ifndef __D3D9_WDDM_H
#define __D3D9_WDDM_H

#include <d3dkmthk.h>

/* ========================================================================
 * Forward declarations
 * ====================================================================== */

typedef struct _D3DDDIARG_OPENADAPTER D3DDDIARG_OPENADAPTER;
typedef struct _D3DDDI_ADAPTERFUNCS D3DDDI_ADAPTERFUNCS;
typedef struct _D3DDDI_ADAPTERCALLBACKS D3DDDI_ADAPTERCALLBACKS;
typedef struct _D3DDDI_DEVICEFUNCS D3DDDI_DEVICEFUNCS;
typedef struct _D3DDDI_DEVICECALLBACKS D3DDDI_DEVICECALLBACKS;
typedef struct _D3DDDIARG_CREATEDEVICE D3DDDIARG_CREATEDEVICE;

/* ========================================================================
 * D3D9 UMD DDI Version
 *
 * The runtime passes its interface version to the UMD in OpenAdapter.
 * WDDM 1.0 (Vista) D3D9 DDI version.
 * ====================================================================== */

#define D3D_UMD_INTERFACE_VERSION_VISTA  0x000C

/* ========================================================================
 * Runtime Callback Types (d3d9 -> kernel, provided TO the UMD)
 *
 * These are the "CB" callbacks that the UMD calls back into d3d9 when
 * it needs kernel services. d3d9 then forwards to D3DKMT*.
 * ====================================================================== */

/* pfnAllocateCb - wraps D3DKMTCreateAllocation */
typedef struct _D3DDDICB_ALLOCATE
{
    const void             *pPrivateDriverData;
    UINT                    PrivateDriverDataSize;
    UINT                    NumAllocations;
    /* Followed by array of D3DDDI_ALLOCATIONINFO */
    void                   *pAllocationInfo;
    D3DKMT_HANDLE           hResource;
    D3DKMT_HANDLE           hKMResource;
} D3DDDICB_ALLOCATE;

typedef HRESULT (APIENTRY *PFND3DDDI_ALLOCATECB)(
    HANDLE hDevice, D3DDDICB_ALLOCATE *pData);

/* pfnDeallocateCb - wraps D3DKMTDestroyAllocation */
typedef struct _D3DDDICB_DEALLOCATE
{
    HANDLE                  hResource;
    UINT                    NumAllocations;
    const D3DKMT_HANDLE    *HandleList;
} D3DDDICB_DEALLOCATE;

typedef HRESULT (APIENTRY *PFND3DDDI_DEALLOCATECB)(
    HANDLE hDevice, const D3DDDICB_DEALLOCATE *pData);

/* pfnLockCb - wraps D3DKMTLock */
typedef struct _D3DDDICB_LOCK
{
    D3DKMT_HANDLE           hAllocation;
    UINT                    PrivateDriverData;
    UINT                    NumPages;
    const UINT             *pPages;
    void                   *pData;      /* out: CPU pointer */
    D3DDDICB_LOCKFLAGS      Flags;
} D3DDDICB_LOCK;

typedef HRESULT (APIENTRY *PFND3DDDI_LOCKCB)(
    HANDLE hDevice, D3DDDICB_LOCK *pData);

/* pfnUnlockCb - wraps D3DKMTUnlock */
typedef struct _D3DDDICB_UNLOCK
{
    UINT                    NumAllocations;
    const D3DKMT_HANDLE   *phAllocations;
} D3DDDICB_UNLOCK;

typedef HRESULT (APIENTRY *PFND3DDDI_UNLOCKCB)(
    HANDLE hDevice, const D3DDDICB_UNLOCK *pData);

typedef D3DKMT_RENDERFLAGS D3DDDICB_RENDERFLAGS;

/* pfnRenderCb - wraps D3DKMTRender */
typedef struct _D3DDDICB_RENDER
{
    UINT                    CommandLength;
    UINT                    CommandOffset;
    UINT                    NumAllocations;
    UINT                    NumPatchLocations;
    void                   *pNewCommandBuffer;      /* out */
    UINT                    NewCommandBufferSize;    /* out */
    void                   *pNewAllocationList;      /* out */
    UINT                    NewAllocationListSize;   /* out */
    void                   *pNewPatchLocationList;   /* out */
    UINT                    NewPatchLocationListSize; /* out */
    D3DDDICB_RENDERFLAGS    Flags;
    HANDLE                  hContext;
    UINT                    BroadcastContextCount;
    HANDLE                  BroadcastContext[D3DDDI_MAX_BROADCAST_CONTEXT];
    ULONG                   QueuedBufferCount;
    D3DGPU_VIRTUAL_ADDRESS  NewCommandBuffer;
    void                   *pPrivateDriverData;
    UINT                    PrivateDriverDataSize;
} D3DDDICB_RENDER;

typedef HRESULT (APIENTRY *PFND3DDDI_RENDERCB)(
    HANDLE hDevice, D3DDDICB_RENDER *pData);

/* pfnPresentCb - wraps D3DKMTPresent */
typedef struct _D3DDDICB_PRESENT
{
    D3DKMT_HANDLE           hSrcAllocation;
    D3DKMT_HANDLE           hDstAllocation;
    HANDLE                  hContext;
    UINT                    BroadcastContextCount;
    HANDLE                  BroadcastContext[64];
} D3DDDICB_PRESENT;

typedef HRESULT (APIENTRY *PFND3DDDI_PRESENTCB)(
    HANDLE hDevice, const D3DDDICB_PRESENT *pData);

/* pfnEscapeCb - wraps D3DKMTEscape */
typedef struct _D3DDDICB_ESCAPE
{
    HANDLE                  hDevice;
    D3DDDI_ESCAPEFLAGS      Flags;
    void                   *pPrivateDriverData;
    UINT                    PrivateDriverDataSize;
    HANDLE                  hContext;
} D3DDDICB_ESCAPE;

typedef HRESULT (APIENTRY *PFND3DDDI_ESCAPECB)(
    HANDLE hAdapter, const D3DDDICB_ESCAPE *pData);

typedef HRESULT (APIENTRY *PFND3DDDI_SETPRIORITYCB)(
    HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_QUERYRESIDENCYCB)(
    HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_SETDISPLAYMODECB)(
    HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_SETDISPLAYPRIVATEDRIVERFORMATCB)(
    HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_CREATEOVERLAYCB)(
    HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_UPDATEOVERLAYCB)(
    HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_FLIPOVERLAYCB)(
    HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_DESTROYOVERLAYCB)(
    HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_CREATECONTEXTCB)(
    HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_DESTROYCONTEXTCB)(
    HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_CREATESYNCHRONIZATIONOBJECTCB)(
    HANDLE hDevice, void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_DESTROYSYNCHRONIZATIONOBJECTCB)(
    HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_WAITFORSYNCHRONIZATIONOBJECTCB)(
    HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_SIGNALSYNCHRONIZATIONOBJECTCB)(
    HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_SETASYNCCALLBACKSCB)(
    HANDLE hDevice, BOOL Enable);

/*
 * D3DDDI_DEVICECALLBACKS - Runtime callback table passed TO the UMD
 * during CreateDevice. The UMD stores this and calls back for kernel ops.
 */
typedef struct _D3DDDI_DEVICECALLBACKS
{
    PFND3DDDI_ALLOCATECB        pfnAllocateCb;
    PFND3DDDI_DEALLOCATECB      pfnDeallocateCb;
    PFND3DDDI_SETPRIORITYCB     pfnSetPriorityCb;
    PFND3DDDI_QUERYRESIDENCYCB  pfnQueryResidencyCb;
    PFND3DDDI_SETDISPLAYMODECB  pfnSetDisplayModeCb;
    PFND3DDDI_PRESENTCB         pfnPresentCb;
    PFND3DDDI_RENDERCB          pfnRenderCb;
    PFND3DDDI_LOCKCB            pfnLockCb;
    PFND3DDDI_UNLOCKCB          pfnUnlockCb;
    PFND3DDDI_ESCAPECB          pfnEscapeCb;
    PFND3DDDI_CREATEOVERLAYCB   pfnCreateOverlayCb;
    PFND3DDDI_UPDATEOVERLAYCB   pfnUpdateOverlayCb;
    PFND3DDDI_FLIPOVERLAYCB     pfnFlipOverlayCb;
    PFND3DDDI_DESTROYOVERLAYCB  pfnDestroyOverlayCb;
    PFND3DDDI_CREATECONTEXTCB   pfnCreateContextCb;
    PFND3DDDI_DESTROYCONTEXTCB  pfnDestroyContextCb;
    PFND3DDDI_CREATESYNCHRONIZATIONOBJECTCB pfnCreateSynchronizationObjectCb;
    PFND3DDDI_DESTROYSYNCHRONIZATIONOBJECTCB pfnDestroySynchronizationObjectCb;
    PFND3DDDI_WAITFORSYNCHRONIZATIONOBJECTCB pfnWaitForSynchronizationObjectCb;
    PFND3DDDI_SIGNALSYNCHRONIZATIONOBJECTCB pfnSignalSynchronizationObjectCb;
    PFND3DDDI_SETASYNCCALLBACKSCB pfnSetAsyncCallbacksCb;
    PFND3DDDI_SETDISPLAYPRIVATEDRIVERFORMATCB pfnSetDisplayPrivateDriverFormatCb;
} D3DDDI_DEVICECALLBACKS;

/* ========================================================================
 * UMD Adapter-Level DDI (returned by OpenAdapter)
 * ====================================================================== */

typedef HRESULT (APIENTRY *PFND3DDDI_CREATEDEVICE)(
    HANDLE hAdapter, D3DDDIARG_CREATEDEVICE *pCreateData);
typedef HRESULT (APIENTRY *PFND3DDDI_CLOSEADAPTER)(
    HANDLE hAdapter);
typedef HRESULT (APIENTRY *PFND3DDDI_GETCAPS)(
    HANDLE hAdapter, const void *pData);

typedef struct _D3DDDI_ADAPTERFUNCS
{
    PFND3DDDI_GETCAPS           pfnGetCaps;
    PFND3DDDI_CREATEDEVICE      pfnCreateDevice;
    PFND3DDDI_CLOSEADAPTER      pfnCloseAdapter;
} D3DDDI_ADAPTERFUNCS;

/* ========================================================================
 * UMD Device-Level DDI (returned by CreateDevice)
 *
 * This is a large table (~100+ function pointers on Win7). We define
 * the minimum set needed for basic rendering.
 * ====================================================================== */

typedef HRESULT (APIENTRY *PFND3DDDI_DESTROYDEVICE)(HANDLE hDevice);
typedef HRESULT (APIENTRY *PFND3DDDI_PRESENT)(HANDLE hDevice, const void *pData);
typedef HRESULT (APIENTRY *PFND3DDDI_FLUSH)(HANDLE hDevice);

typedef struct _D3DDDI_DEVICEFUNCS
{
    /* Slot 0: SetRenderState */
    void *pfnSetRenderState;
    /* Slot 1: UpdateWInfo */
    void *pfnUpdateWInfo;
    /* Slot 2: ValidateDevice */
    void *pfnValidateDevice;
    /* Slot 3: SetTextureStageState */
    void *pfnSetTextureStageState;
    /* Slot 4: SetTexture */
    void *pfnSetTexture;
    /* Slot 5: SetPixelShader */
    void *pfnSetPixelShader;
    /* Slot 6: SetPixelShaderConst */
    void *pfnSetPixelShaderConst;
    /* Slot 7: SetStreamSourceUm */
    void *pfnSetStreamSourceUm;
    /* Slot 8: SetIndices */
    void *pfnSetIndices;
    /* Slot 9: SetIndicesUm */
    void *pfnSetIndicesUm;
    /* Slot 10: DrawPrimitive */
    void *pfnDrawPrimitive;
    /* Slot 11: DrawIndexedPrimitive */
    void *pfnDrawIndexedPrimitive;
    /* Slot 12: DrawRectPatch */
    void *pfnDrawRectPatch;
    /* Slot 13: DrawTriPatch */
    void *pfnDrawTriPatch;
    /* Slot 14: DrawPrimitive2 */
    void *pfnDrawPrimitive2;
    /* Slot 15: DrawIndexedPrimitive2 */
    void *pfnDrawIndexedPrimitive2;
    /* Slot 16: VolBlt */
    void *pfnVolBlt;
    /* Slot 17: BufBlt */
    void *pfnBufBlt;
    /* Slot 18: TexBlt */
    void *pfnTexBlt;
    /* Slot 19: StateSet */
    void *pfnStateSet;
    /* Slot 20: SetPriority */
    void *pfnSetPriority;
    /* Slot 21: Clear */
    void *pfnClear;
    /* Slot 22: UpdatePalette */
    void *pfnUpdatePalette;
    /* Slot 23: SetPalette */
    void *pfnSetPalette;
    /* Slot 24: SetVertexShaderConst */
    void *pfnSetVertexShaderConst;
    /* Slot 25: MultiplyTransform */
    void *pfnMultiplyTransform;
    /* Slot 26: SetTransform */
    void *pfnSetTransform;
    /* Slot 27: SetViewport */
    void *pfnSetViewport;
    /* Slot 28: SetZRange */
    void *pfnSetZRange;
    /* Slot 29: SetMaterial */
    void *pfnSetMaterial;
    /* Slot 30: SetLight */
    void *pfnSetLight;
    /* Slot 31: CreateLight */
    void *pfnCreateLight;
    /* Slot 32: DestroyLight */
    void *pfnDestroyLight;
    /* Slot 33: SetClipPlane */
    void *pfnSetClipPlane;
    /* Slot 34: GetInfo */
    void *pfnGetInfo;
    /* Slot 35: Lock */
    void *pfnLock;
    /* Slot 36: Unlock */
    void *pfnUnlock;
    /* Slot 37: CreateResource */
    void *pfnCreateResource;
    /* Slot 38: DestroyResource */
    void *pfnDestroyResource;
    /* Slot 39: SetDisplayMode */
    void *pfnSetDisplayMode;
    /* Slot 40: Present */
    PFND3DDDI_PRESENT pfnPresent;
    /* Slot 41: Flush */
    PFND3DDDI_FLUSH pfnFlush;
    /* Slot 42: CreateVertexShaderFunc */
    void *pfnCreateVertexShaderFunc;
    /* Slot 43: DeleteVertexShaderFunc */
    void *pfnDeleteVertexShaderFunc;
    /* Slot 44: SetVertexShaderFunc */
    void *pfnSetVertexShaderFunc;
    /* Slot 45: CreateVertexShaderDecl */
    void *pfnCreateVertexShaderDecl;
    /* Slot 46: DeleteVertexShaderDecl */
    void *pfnDeleteVertexShaderDecl;
    /* Slot 47: SetVertexShaderDecl */
    void *pfnSetVertexShaderDecl;
    /* Slot 48: SetVertexShaderConstI */
    void *pfnSetVertexShaderConstI;
    /* Slot 49: SetVertexShaderConstB */
    void *pfnSetVertexShaderConstB;
    /* Slot 50: SetScissorRect */
    void *pfnSetScissorRect;
    /* Slot 51: SetStreamSource */
    void *pfnSetStreamSource;
    /* Slot 52: SetStreamSourceFreq */
    void *pfnSetStreamSourceFreq;
    /* Slot 53: SetConvolutionKernelMono */
    void *pfnSetConvolutionKernelMono;
    /* Slot 54: ComposeRects */
    void *pfnComposeRects;
    /* Slot 55: ColorFill */
    void *pfnColorFill;
    /* Slot 56: DepthFill */
    void *pfnDepthFill;
    /* Slot 57: CreateQuery */
    void *pfnCreateQuery;
    /* Slot 58: DestroyQuery */
    void *pfnDestroyQuery;
    /* Slot 59: IssueQuery */
    void *pfnIssueQuery;
    /* Slot 60: GetQueryData */
    void *pfnGetQueryData;
    /* Slot 61: SetRenderTarget */
    void *pfnSetRenderTarget;
    /* Slot 62: SetDepthStencil */
    void *pfnSetDepthStencil;
    /* Slot 63: GenerateMipSubLevels */
    void *pfnGenerateMipSubLevels;
    /* Slot 64: SetPixelShaderConstI */
    void *pfnSetPixelShaderConstI;
    /* Slot 65: SetPixelShaderConstB */
    void *pfnSetPixelShaderConstB;
    /* Slot 66: CreatePixelShader */
    void *pfnCreatePixelShader;
    /* Slot 67: DeletePixelShader */
    void *pfnDeletePixelShader;
    /* Slot 68: CreateDecodeDevice */
    void *pfnCreateDecodeDevice;
    /* Slot 69: DestroyDecodeDevice */
    void *pfnDestroyDecodeDevice;
    /* Slot 70: SetDecodeRenderTarget */
    void *pfnSetDecodeRenderTarget;
    /* Slot 71: DecodeBeginFrame */
    void *pfnDecodeBeginFrame;
    /* Slot 72: DecodeEndFrame */
    void *pfnDecodeEndFrame;
    /* Slot 73: DecodeExecute */
    void *pfnDecodeExecute;
    /* Slot 74: DecodeExtensionExecute */
    void *pfnDecodeExtensionExecute;
    /* Slot 75: CreateVideoProcessDevice */
    void *pfnCreateVideoProcessDevice;
    /* Slot 76: DestroyVideoProcessDevice */
    void *pfnDestroyVideoProcessDevice;
    /* Slot 77: VideoProcessBeginFrame */
    void *pfnVideoProcessBeginFrame;
    /* Slot 78: VideoProcessEndFrame */
    void *pfnVideoProcessEndFrame;
    /* Slot 79: SetVideoProcessRenderTarget */
    void *pfnSetVideoProcessRenderTarget;
    /* Slot 80: VideoProcessBlt */
    void *pfnVideoProcessBlt;
    /* Slot 81: CreateExtensionDevice */
    void *pfnCreateExtensionDevice;
    /* Slot 82: DestroyExtensionDevice */
    void *pfnDestroyExtensionDevice;
    /* Slot 83: ExtensionExecute */
    void *pfnExtensionExecute;
    /* Slot 84: CreateOverlay */
    void *pfnCreateOverlay;
    /* Slot 85: UpdateOverlay */
    void *pfnUpdateOverlay;
    /* Slot 86: FlipOverlay */
    void *pfnFlipOverlay;
    /* Slot 87: GetOverlayColorControls */
    void *pfnGetOverlayColorControls;
    /* Slot 88: SetOverlayColorControls */
    void *pfnSetOverlayColorControls;
    /* Slot 89: DestroyOverlay */
    void *pfnDestroyOverlay;
    /* Slot 90: DestroyDevice */
    PFND3DDDI_DESTROYDEVICE pfnDestroyDevice;
    /* Slot 91: QueryAdapterInfo (per-device) */
    void *pfnQueryAdapterInfo;
    /* Slot 92: Blt */
    void *pfnBlt;
} D3DDDI_DEVICEFUNCS;

/* ========================================================================
 * Adapter-Level Callback Types (provided TO the UMD at OpenAdapter time)
 * ====================================================================== */

typedef struct _D3DDDICB_QUERYADAPTERINFO
{
    void *pPrivateDriverData;
    UINT PrivateDriverDataSize;
} D3DDDICB_QUERYADAPTERINFO;

typedef HRESULT (APIENTRY *PFND3DDDI_QUERYADAPTERINFOCB)(
    HANDLE hAdapter, const D3DDDICB_QUERYADAPTERINFO *pData);

typedef struct _D3DDDICB_GETMULTISAMPLEMETHODLIST
{
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT Width;
    UINT Height;
    D3DDDIFORMAT Format;
    D3DDDI_MULTISAMPLINGMETHOD *pMethodList;
    UINT MethodCount;
} D3DDDICB_GETMULTISAMPLEMETHODLIST;

typedef HRESULT (APIENTRY *PFND3DDDI_GETMULTISAMPLEMETHODLISTCB)(
    HANDLE hAdapter, D3DDDICB_GETMULTISAMPLEMETHODLIST *pData);

typedef struct _D3DDDI_ADAPTERCALLBACKS
{
    PFND3DDDI_QUERYADAPTERINFOCB pfnQueryAdapterInfoCb;
    PFND3DDDI_GETMULTISAMPLEMETHODLISTCB pfnGetMultisampleMethodListCb;
} D3DDDI_ADAPTERCALLBACKS;

/* ========================================================================
 * OpenAdapter Argument Structure
 * ====================================================================== */

typedef struct _D3DDDIARG_OPENADAPTER
{
    HANDLE                      hAdapter;           /* in:  RT adapter handle */
    UINT                        Interface;          /* in:  DDI version */
    UINT                        Version;            /* in:  Runtime version */
    const D3DDDI_ADAPTERCALLBACKS *pAdapterCallbacks; /* in:  RT callbacks */
    D3DDDI_ADAPTERFUNCS        *pAdapterFuncs;      /* out: UMD adapter DDI */
    UINT                        DriverVersion;      /* out: UMD version */
} D3DDDIARG_OPENADAPTER;

/* OpenAdapter export signature */
typedef HRESULT (APIENTRY *PFND3DDDI_OPENADAPTER)(
    D3DDDIARG_OPENADAPTER *pOpenData);

/* ========================================================================
 * CreateDevice Argument Structure
 * ====================================================================== */

typedef struct _D3DDDIARG_CREATEDEVICE
{
    HANDLE                      hDevice;            /* in/out: RT device handle */
    UINT                        Interface;          /* in:  DDI version */
    UINT                        Version;            /* in:  Runtime version */
    const D3DDDI_DEVICECALLBACKS *pCallbacks;       /* in:  RT device callbacks */
    void                       *pCommandBuffer;     /* out: Command buffer ptr */
    UINT                        CommandBufferSize;  /* out: Command buffer size */
    void                       *pAllocationList;    /* out: Allocation list */
    UINT                        AllocationListSize; /* out: Allocation list size */
    void                       *pPatchLocationList; /* out: Patch location list */
    UINT                        PatchLocationListSize; /* out */
    D3DDDI_DEVICEFUNCS         *pDeviceFuncs;       /* out: UMD device DDI */
} D3DDDIARG_CREATEDEVICE;

/* ========================================================================
 * WDDM Bridge Context - Per-adapter and per-device state
 * ====================================================================== */

/*
 * D3D9_WDDM_ADAPTER - State for one WDDM adapter opened by d3d9
 */
typedef struct _D3D9_WDDM_ADAPTER
{
    D3DKMT_HANDLE               hAdapter;           /* D3DKMT adapter handle */
    LUID                        AdapterLuid;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;

    /* UMD module */
    HMODULE                     hUmdModule;          /* LoadLibrary handle */
    PFND3DDDI_OPENADAPTER       pfnOpenAdapter;      /* UMD entry point */

    /* UMD adapter-level DDI (from OpenAdapter) */
    HANDLE                      hUmdAdapter;         /* UMD's adapter handle */
    D3DDDI_ADAPTERFUNCS         AdapterFuncs;
} D3D9_WDDM_ADAPTER;

/*
 * D3D9_WDDM_DEVICE - State for one WDDM device created by d3d9
 */
typedef struct _D3D9_WDDM_DEVICE
{
    D3D9_WDDM_ADAPTER         *pAdapter;

    /* Kernel device/context handles */
    D3DKMT_HANDLE               hDevice;
    D3DKMT_HANDLE               hContext;

    /* Command buffer from kernel */
    void                       *pCommandBuffer;
    UINT                        CommandBufferSize;
    D3DGPU_VIRTUAL_ADDRESS      CommandBuffer;
    D3DDDI_ALLOCATIONLIST      *pAllocationList;
    UINT                        AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST   *pPatchLocationList;
    UINT                        PatchLocationListSize;

    /* UMD device-level DDI (from CreateDevice) */
    HANDLE                      hUmdDevice;
    D3DDDI_DEVICEFUNCS          DeviceFuncs;

    /* Runtime callbacks table (stored for UMD to call back into) */
    D3DDDI_DEVICECALLBACKS      DeviceCallbacks;
} D3D9_WDDM_DEVICE;

/* ========================================================================
 * Public API - Called from d3d9_main.c / directx.c
 * ====================================================================== */

/* Check if WDDM driver model is available */
BOOL d3d9_wddm_is_available(void);

/* Open a WDDM adapter (UMD discovery + loading) */
HRESULT d3d9_wddm_open_adapter(HDC hdc, D3D9_WDDM_ADAPTER *adapter);
HRESULT d3d9_wddm_open_adapter_by_name(const WCHAR *device_name,
                                        D3D9_WDDM_ADAPTER *adapter);

/* Close a WDDM adapter */
void d3d9_wddm_close_adapter(D3D9_WDDM_ADAPTER *adapter);

/* Create/destroy a WDDM device */
HRESULT d3d9_wddm_create_device(D3D9_WDDM_ADAPTER *adapter,
                                 D3D9_WDDM_DEVICE *device);
void d3d9_wddm_destroy_device(D3D9_WDDM_DEVICE *device);

/* Present through D3DKMT */
HRESULT d3d9_wddm_present(D3D9_WDDM_DEVICE *device,
                           D3DKMT_HANDLE hSrcAlloc,
                           D3DKMT_HANDLE hDstAlloc);

/* ========================================================================
 * DWM Composition Support (dwmapi ordinal imports)
 *
 * d3d9.dll uses these undocumented dwmapi ordinals for windowed present:
 *   100: DwmpDxGetWindowSharedSurface
 *   101: DwmpDxUpdateWindowSharedSurface
 *   125: DwmpDxBindSwapChain
 *   126: DwmpDxUnbindSwapChain
 *   128: DwmpDxgiIsThreadDesktopComposited
 *   129: DwmpDxgiDisableRedirection
 *   130: DwmpDxgiEnableRedirection
 * ====================================================================== */

/* Ordinal 100: Get the DWM shared surface for a window */
typedef HANDLE (WINAPI *PFN_DwmpDxGetWindowSharedSurface)(
    HWND hwnd, LUID *pAdapterLuid, HANDLE hDevice,
    ULONG *pFmtWindow, HANDLE *phDxSurface, ULONGLONG *pUpdateId);

/* Ordinal 101: Notify DWM that the shared surface has been updated */
typedef HRESULT (WINAPI *PFN_DwmpDxUpdateWindowSharedSurface)(
    HWND hwnd, ULONGLONG *pUpdateId, DWORD dwFlags,
    HANDLE hDxSurface, RECT *pDirtyRect);

/* Ordinal 125: Bind a swap chain to DWM composition */
typedef HRESULT (WINAPI *PFN_DwmpDxBindSwapChain)(
    HANDLE hSwapChain, BOOL bBind);

/* Ordinal 126: Unbind a swap chain from DWM composition */
typedef HRESULT (WINAPI *PFN_DwmpDxUnbindSwapChain)(
    HANDLE hSwapChain);

/* Ordinal 128: Check if the current thread's desktop is composited */
typedef BOOL (WINAPI *PFN_DwmpDxgiIsThreadDesktopComposited)(void);

/* Ordinal 129: Disable DWM redirection for a process */
typedef HRESULT (WINAPI *PFN_DwmpDxgiDisableRedirection)(BOOL bDisable);

/* Ordinal 130: Enable DWM redirection for a process */
typedef HRESULT (WINAPI *PFN_DwmpDxgiEnableRedirection)(BOOL bEnable);

/*
 * D3D9_DWM_FUNCS - Resolved dwmapi ordinal function pointers
 */
typedef struct _D3D9_DWM_FUNCS
{
    BOOL                                    Resolved;
    HMODULE                                 hDwmapi;
    PFN_DwmpDxGetWindowSharedSurface        pfnGetWindowSharedSurface;
    PFN_DwmpDxUpdateWindowSharedSurface     pfnUpdateWindowSharedSurface;
    PFN_DwmpDxBindSwapChain                 pfnBindSwapChain;
    PFN_DwmpDxUnbindSwapChain              pfnUnbindSwapChain;
    PFN_DwmpDxgiIsThreadDesktopComposited   pfnIsThreadDesktopComposited;
    PFN_DwmpDxgiDisableRedirection          pfnDisableRedirection;
    PFN_DwmpDxgiEnableRedirection           pfnEnableRedirection;
} D3D9_DWM_FUNCS;

/* Resolve dwmapi ordinal imports */
BOOL d3d9_wddm_resolve_dwm(D3D9_DWM_FUNCS *dwm);

#endif /* __D3D9_WDDM_H */
