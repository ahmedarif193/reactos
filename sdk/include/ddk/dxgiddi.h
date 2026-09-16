/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DXGI shared user-mode display driver declarations
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * This is the kernel-safe subset of the public DXGI DDI contract needed by
 * display miniports when exchanging primary allocation data with a UMD.
 */

#pragma once

#include <d3dukmdt.h>
#include <dxgicommon.h>
#include <dxgiformat.h>
#include <dxgitype.h>

typedef struct DXGI_DDI_RATIONAL
{
    UINT Numerator;
    UINT Denominator;
} DXGI_DDI_RATIONAL;

typedef enum DXGI_DDI_MODE_SCANLINE_ORDER
{
    DXGI_DDI_MODE_SCANLINE_ORDER_UNSPECIFIED = 0,
    DXGI_DDI_MODE_SCANLINE_ORDER_PROGRESSIVE = 1,
    DXGI_DDI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST = 2,
    DXGI_DDI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST = 3,
} DXGI_DDI_MODE_SCANLINE_ORDER;

typedef enum DXGI_DDI_MODE_SCALING
{
    DXGI_DDI_MODE_SCALING_UNSPECIFIED = 0,
    DXGI_DDI_MODE_SCALING_STRETCHED = 1,
    DXGI_DDI_MODE_SCALING_CENTERED = 2,
} DXGI_DDI_MODE_SCALING;

typedef enum DXGI_DDI_MODE_ROTATION
{
    DXGI_DDI_MODE_ROTATION_UNSPECIFIED = 0,
    DXGI_DDI_MODE_ROTATION_IDENTITY = 1,
    DXGI_DDI_MODE_ROTATION_ROTATE90 = 2,
    DXGI_DDI_MODE_ROTATION_ROTATE180 = 3,
    DXGI_DDI_MODE_ROTATION_ROTATE270 = 4,
} DXGI_DDI_MODE_ROTATION;

typedef struct DXGI_DDI_MODE_DESC
{
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    DXGI_DDI_RATIONAL RefreshRate;
    DXGI_DDI_MODE_SCANLINE_ORDER ScanlineOrdering;
    DXGI_DDI_MODE_ROTATION Rotation;
    DXGI_DDI_MODE_SCALING Scaling;
} DXGI_DDI_MODE_DESC;

typedef struct DXGI_DDI_PRIMARY_DESC
{
    UINT Flags;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    DXGI_DDI_MODE_DESC ModeDesc;
    UINT DriverFlags;
} DXGI_DDI_PRIMARY_DESC;

/* User-mode DXGI DDI entry points and runtime callbacks. */
typedef UINT_PTR DXGI_DDI_HDEVICE;
typedef UINT_PTR DXGI_DDI_HRESOURCE;

typedef enum DXGI_DDI_RESIDENCY
{
    DXGI_DDI_RESIDENCY_FULLY_RESIDENT = 1,
    DXGI_DDI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY = 2,
    DXGI_DDI_RESIDENCY_EVICTED_TO_DISK = 3,
} DXGI_DDI_RESIDENCY;

typedef enum DXGI_DDI_FLIP_INTERVAL_TYPE
{
    DXGI_DDI_FLIP_INTERVAL_IMMEDIATE = 0,
    DXGI_DDI_FLIP_INTERVAL_ONE = 1,
    DXGI_DDI_FLIP_INTERVAL_TWO = 2,
    DXGI_DDI_FLIP_INTERVAL_THREE = 3,
    DXGI_DDI_FLIP_INTERVAL_FOUR = 4,
    DXGI_DDI_FLIP_INTERVAL_IMMEDIATE_ALLOW_TEARING = 5,
} DXGI_DDI_FLIP_INTERVAL_TYPE;

typedef struct DXGI_DDI_PRESENT_FLAGS
{
    union
    {
        struct
        {
            UINT Blt : 1;
            UINT Flip : 1;
            UINT PreferRight : 1;
            UINT TemporaryMono : 1;
            UINT AllowTearing : 1;
            UINT AllowFlexibleRefresh : 1;
            UINT Reserved : 26;
        };
        UINT Value;
    };
} DXGI_DDI_PRESENT_FLAGS;

typedef struct DXGI_DDI_ARG_PRESENT
{
    DXGI_DDI_HDEVICE hDevice;
    DXGI_DDI_HRESOURCE hSurfaceToPresent;
    UINT SrcSubResourceIndex;
    DXGI_DDI_HRESOURCE hDstResource;
    UINT DstSubResourceIndex;
    void *pDXGIContext;
    DXGI_DDI_PRESENT_FLAGS Flags;
    DXGI_DDI_FLIP_INTERVAL_TYPE FlipInterval;
} DXGI_DDI_ARG_PRESENT;

typedef struct DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES
{
    DXGI_DDI_HDEVICE hDevice;
    const DXGI_DDI_HRESOURCE *pResources;
    UINT Resources;
} DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES;

typedef struct DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS
{
    DXGI_DDI_HDEVICE hDevice;
    DXGI_GAMMA_CONTROL_CAPABILITIES *pGammaCapabilities;
} DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS;

typedef struct DXGI_DDI_ARG_SETDISPLAYMODE
{
    DXGI_DDI_HDEVICE hDevice;
    DXGI_DDI_HRESOURCE hResource;
    UINT SubResourceIndex;
} DXGI_DDI_ARG_SETDISPLAYMODE;

typedef struct DXGI_DDI_ARG_SETRESOURCEPRIORITY
{
    DXGI_DDI_HDEVICE hDevice;
    DXGI_DDI_HRESOURCE hResource;
    UINT Priority;
} DXGI_DDI_ARG_SETRESOURCEPRIORITY;

typedef struct DXGI_DDI_ARG_QUERYRESOURCERESIDENCY
{
    DXGI_DDI_HDEVICE hDevice;
    const DXGI_DDI_HRESOURCE *pResources;
    DXGI_DDI_RESIDENCY *pStatus;
    SIZE_T Resources;
} DXGI_DDI_ARG_QUERYRESOURCERESIDENCY;

typedef struct DXGI_DDI_ARG_BLT_FLAGS
{
    union
    {
        struct
        {
            UINT Resolve : 1;
            UINT Convert : 1;
            UINT Stretch : 1;
            UINT Present : 1;
            UINT Reserved : 28;
        };
        UINT Value;
    };
} DXGI_DDI_ARG_BLT_FLAGS;

typedef struct DXGI_DDI_ARG_BLT
{
    DXGI_DDI_HDEVICE hDevice;
    DXGI_DDI_HRESOURCE hDstResource;
    UINT DstSubresource;
    UINT DstLeft;
    UINT DstTop;
    UINT DstRight;
    UINT DstBottom;
    DXGI_DDI_HRESOURCE hSrcResource;
    UINT SrcSubresource;
    DXGI_DDI_ARG_BLT_FLAGS Flags;
    DXGI_DDI_MODE_ROTATION Rotate;
} DXGI_DDI_ARG_BLT;

typedef struct DXGI_DDI_ARG_RESOLVESHAREDRESOURCE
{
    DXGI_DDI_HDEVICE hDevice;
    DXGI_DDI_HRESOURCE hResource;
} DXGI_DDI_ARG_RESOLVESHAREDRESOURCE;

#define DXGI_DDI_PRIMARY_OPTIONAL 0x1
#define DXGI_DDI_PRIMARY_NONPREROTATED 0x2
#define DXGI_DDI_PRIMARY_STEREO 0x4
#define DXGI_DDI_PRIMARY_INDIRECT 0x8
#define DXGI_DDI_PRIMARY_DRIVER_FLAG_NO_SCANOUT 0x1

typedef struct DXGI_DDI_BASE_FUNCTIONS
{
    HRESULT (APIENTRY *pfnPresent)(DXGI_DDI_ARG_PRESENT *);
    HRESULT (APIENTRY *pfnGetGammaCaps)(DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS *);
    HRESULT (APIENTRY *pfnSetDisplayMode)(DXGI_DDI_ARG_SETDISPLAYMODE *);
    HRESULT (APIENTRY *pfnSetResourcePriority)(DXGI_DDI_ARG_SETRESOURCEPRIORITY *);
    HRESULT (APIENTRY *pfnQueryResourceResidency)(DXGI_DDI_ARG_QUERYRESOURCERESIDENCY *);
    HRESULT (APIENTRY *pfnRotateResourceIdentities)(DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *);
    HRESULT (APIENTRY *pfnBlt)(DXGI_DDI_ARG_BLT *);
} DXGI_DDI_BASE_FUNCTIONS;

typedef struct DXGI1_1_DDI_BASE_FUNCTIONS
{
    HRESULT (APIENTRY *pfnPresent)(DXGI_DDI_ARG_PRESENT *);
    HRESULT (APIENTRY *pfnGetGammaCaps)(DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS *);
    HRESULT (APIENTRY *pfnSetDisplayMode)(DXGI_DDI_ARG_SETDISPLAYMODE *);
    HRESULT (APIENTRY *pfnSetResourcePriority)(DXGI_DDI_ARG_SETRESOURCEPRIORITY *);
    HRESULT (APIENTRY *pfnQueryResourceResidency)(DXGI_DDI_ARG_QUERYRESOURCERESIDENCY *);
    HRESULT (APIENTRY *pfnRotateResourceIdentities)(DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *);
    HRESULT (APIENTRY *pfnBlt)(DXGI_DDI_ARG_BLT *);
    HRESULT (APIENTRY *pfnResolveSharedResource)(DXGI_DDI_ARG_RESOLVESHAREDRESOURCE *);
} DXGI1_1_DDI_BASE_FUNCTIONS;

typedef struct DXGIDDICB_PRESENT
{
    D3DKMT_HANDLE hSrcAllocation;
    D3DKMT_HANDLE hDstAllocation;
    void *pDXGIContext;
    HANDLE hContext;
    UINT BroadcastContextCount;
    HANDLE BroadcastContext[D3DDDI_MAX_BROADCAST_CONTEXT];
    D3DKMT_HANDLE *BroadcastSrcAllocation;
    D3DKMT_HANDLE *BroadcastDstAllocation;
    UINT PrivateDriverDataSize;
    void *pPrivateDriverData;
    BOOLEAN bOptimizeForComposition;
    BOOL SyncIntervalOverrideValid;
    DXGI_DDI_FLIP_INTERVAL_TYPE SyncIntervalOverride;
} DXGIDDICB_PRESENT;

typedef struct DXGI_DDI_BASE_CALLBACKS
{
    HRESULT (APIENTRY *pfnPresentCb)(HANDLE, DXGIDDICB_PRESENT *);
    HRESULT (APIENTRY *pfnPresentMultiplaneOverlayCb)(HANDLE, const void *);
    HRESULT (APIENTRY *pfnPresentMultiplaneOverlay1Cb)(HANDLE, const void *);
} DXGI_DDI_BASE_CALLBACKS;

typedef struct DXGI_DDI_BASE_ARGS
{
    DXGI_DDI_BASE_CALLBACKS *pDXGIBaseCallbacks;
    union
    {
        DXGI1_1_DDI_BASE_FUNCTIONS *pDXGIDDIBaseFunctions2;
        DXGI_DDI_BASE_FUNCTIONS *pDXGIDDIBaseFunctions;
    };
} DXGI_DDI_BASE_ARGS;
