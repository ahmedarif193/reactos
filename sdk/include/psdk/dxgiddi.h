/*
 * PROJECT:     ReactOS Software Development Kit
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXGI user-mode DDI contract shared by DXGI-aware UMDs.
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * This header intentionally exposes only the DXGI DDI surface currently
 * needed by the in-tree WDDM/OpenGL/virgl enablement work.  Layout-sensitive
 * structures keep the Windows ABI, but the file is reauthored in ReactOS
 * style rather than copied from the Windows SDK.
 *
 * REFERENCES:
 *   - Windows 10 SDK, dxgiddi.h
 */

#ifndef _DXGIDDI_H_
#define _DXGIDDI_H_

#include <windef.h>
#include <winerror.h>
#include <d3dkmthk.h>
#include <dxgicommon.h>
#include <dxgitype.h>

#pragma warning(push)
#pragma warning(disable:4201) /* nameless struct/union */

typedef UINT_PTR DXGI_DDI_HDEVICE;
typedef UINT_PTR DXGI_DDI_HRESOURCE;

typedef enum _DXGI_DDI_RESIDENCY
{
    DXGI_DDI_RESIDENCY_FULLY_RESIDENT             = 1,
    DXGI_DDI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY  = 2,
    DXGI_DDI_RESIDENCY_EVICTED_TO_DISK            = 3,
} DXGI_DDI_RESIDENCY;

typedef enum _DXGI_DDI_FLIP_INTERVAL_TYPE
{
    DXGI_DDI_FLIP_INTERVAL_IMMEDIATE                  = 0,
    DXGI_DDI_FLIP_INTERVAL_ONE                        = 1,
    DXGI_DDI_FLIP_INTERVAL_TWO                        = 2,
    DXGI_DDI_FLIP_INTERVAL_THREE                      = 3,
    DXGI_DDI_FLIP_INTERVAL_FOUR                       = 4,
    DXGI_DDI_FLIP_INTERVAL_IMMEDIATE_ALLOW_TEARING    = 5,
} DXGI_DDI_FLIP_INTERVAL_TYPE;

typedef struct _DXGI_DDI_PRESENT_FLAGS
{
    union
    {
        struct
        {
            UINT Blt                  : 1;
            UINT Flip                 : 1;
            UINT PreferRight          : 1;
            UINT TemporaryMono        : 1;
            UINT AllowTearing         : 1;
            UINT AllowFlexibleRefresh : 1;
            UINT Reserved             : 26;
        };
        UINT Value;
    };
} DXGI_DDI_PRESENT_FLAGS;

typedef struct _DXGI_DDI_ARG_PRESENT
{
    DXGI_DDI_HDEVICE            hDevice;
    DXGI_DDI_HRESOURCE          hSurfaceToPresent;
    UINT                        SrcSubResourceIndex;
    DXGI_DDI_HRESOURCE          hDstResource;
    UINT                        DstSubResourceIndex;
    VOID*                       pDXGIContext;
    DXGI_DDI_PRESENT_FLAGS      Flags;
    DXGI_DDI_FLIP_INTERVAL_TYPE FlipInterval;
} DXGI_DDI_ARG_PRESENT;

typedef struct _DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES
{
    DXGI_DDI_HDEVICE          hDevice;
    CONST DXGI_DDI_HRESOURCE* pResources;
    UINT                      Resources;
} DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES;

typedef struct _DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS
{
    DXGI_DDI_HDEVICE                 hDevice;
    DXGI_GAMMA_CONTROL_CAPABILITIES* pGammaCapabilities;
} DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS;

typedef struct _DXGI_DDI_ARG_SET_GAMMA_CONTROL
{
    DXGI_DDI_HDEVICE    hDevice;
    DXGI_GAMMA_CONTROL  GammaControl;
} DXGI_DDI_ARG_SET_GAMMA_CONTROL;

typedef struct _DXGI_DDI_ARG_SETDISPLAYMODE
{
    DXGI_DDI_HDEVICE    hDevice;
    DXGI_DDI_HRESOURCE  hResource;
    UINT                SubResourceIndex;
} DXGI_DDI_ARG_SETDISPLAYMODE;

typedef struct _DXGI_DDI_ARG_SETRESOURCEPRIORITY
{
    DXGI_DDI_HDEVICE    hDevice;
    DXGI_DDI_HRESOURCE  hResource;
    UINT                Priority;
} DXGI_DDI_ARG_SETRESOURCEPRIORITY;

typedef struct _DXGI_DDI_ARG_QUERYRESOURCERESIDENCY
{
    DXGI_DDI_HDEVICE           hDevice;
    CONST DXGI_DDI_HRESOURCE*  pResources;
    DXGI_DDI_RESIDENCY*        pStatus;
    SIZE_T                     Resources;
} DXGI_DDI_ARG_QUERYRESOURCERESIDENCY;

typedef struct _DXGI_DDI_RATIONAL
{
    UINT Numerator;
    UINT Denominator;
} DXGI_DDI_RATIONAL;

typedef enum _DXGI_DDI_MODE_SCANLINE_ORDER
{
    DXGI_DDI_MODE_SCANLINE_ORDER_UNSPECIFIED        = 0,
    DXGI_DDI_MODE_SCANLINE_ORDER_PROGRESSIVE        = 1,
    DXGI_DDI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST  = 2,
    DXGI_DDI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST  = 3,
} DXGI_DDI_MODE_SCANLINE_ORDER;

typedef enum _DXGI_DDI_MODE_SCALING
{
    DXGI_DDI_MODE_SCALING_UNSPECIFIED   = 0,
    DXGI_DDI_MODE_SCALING_STRETCHED     = 1,
    DXGI_DDI_MODE_SCALING_CENTERED      = 2,
} DXGI_DDI_MODE_SCALING;

typedef enum _DXGI_DDI_MODE_ROTATION
{
    DXGI_DDI_MODE_ROTATION_UNSPECIFIED  = 0,
    DXGI_DDI_MODE_ROTATION_IDENTITY     = 1,
    DXGI_DDI_MODE_ROTATION_ROTATE90     = 2,
    DXGI_DDI_MODE_ROTATION_ROTATE180    = 3,
    DXGI_DDI_MODE_ROTATION_ROTATE270    = 4,
} DXGI_DDI_MODE_ROTATION;

typedef struct _DXGI_DDI_MODE_DESC
{
    UINT                          Width;
    UINT                          Height;
    DXGI_FORMAT                   Format;
    DXGI_DDI_RATIONAL             RefreshRate;
    DXGI_DDI_MODE_SCANLINE_ORDER  ScanlineOrdering;
    DXGI_DDI_MODE_ROTATION        Rotation;
    DXGI_DDI_MODE_SCALING         Scaling;
} DXGI_DDI_MODE_DESC;

#define DXGI_DDI_PRIMARY_OPTIONAL           0x00000001u
#define DXGI_DDI_PRIMARY_NONPREROTATED      0x00000002u
#define DXGI_DDI_PRIMARY_STEREO             0x00000004u
#define DXGI_DDI_PRIMARY_INDIRECT           0x00000008u
#define DXGI_DDI_PRIMARY_DRIVER_FLAG_NO_SCANOUT 0x00000001u

typedef struct _DXGI_DDI_PRIMARY_DESC
{
    UINT                           Flags;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    DXGI_DDI_MODE_DESC             ModeDesc;
    UINT                           DriverFlags;
} DXGI_DDI_PRIMARY_DESC;

typedef struct _DXGI_DDI_ARG_BLT_FLAGS
{
    union
    {
        struct
        {
            UINT Resolve   : 1;
            UINT Convert   : 1;
            UINT Stretch   : 1;
            UINT Present   : 1;
            UINT Reserved  : 28;
        };
        UINT Value;
    };
} DXGI_DDI_ARG_BLT_FLAGS;

typedef struct _DXGI_DDI_ARG_BLT
{
    DXGI_DDI_HDEVICE        hDevice;
    DXGI_DDI_HRESOURCE      hDstResource;
    UINT                    DstSubresource;
    UINT                    DstLeft;
    UINT                    DstTop;
    UINT                    DstRight;
    UINT                    DstBottom;
    DXGI_DDI_HRESOURCE      hSrcResource;
    UINT                    SrcSubresource;
    DXGI_DDI_ARG_BLT_FLAGS  Flags;
    DXGI_DDI_MODE_ROTATION  Rotate;
} DXGI_DDI_ARG_BLT;

typedef struct _DXGI_DDI_ARG_RESOLVESHAREDRESOURCE
{
    DXGI_DDI_HDEVICE    hDevice;
    DXGI_DDI_HRESOURCE  hResource;
} DXGI_DDI_ARG_RESOLVESHAREDRESOURCE;

typedef struct _DXGI_DDI_BASE_FUNCTIONS DXGI_DDI_BASE_FUNCTIONS;
typedef struct _DXGI1_1_DDI_BASE_FUNCTIONS DXGI1_1_DDI_BASE_FUNCTIONS;
typedef struct _DXGI_DDI_BASE_CALLBACKS DXGI_DDI_BASE_CALLBACKS;
typedef struct _DXGI_DDI_BASE_ARGS DXGI_DDI_BASE_ARGS;

typedef HRESULT (APIENTRY *PFNDXGI_DDI_PRESENT)(
    DXGI_DDI_ARG_PRESENT* pPresent);
typedef HRESULT (APIENTRY *PFNDXGI_DDI_GET_GAMMA_CAPS)(
    DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS* pCaps);
typedef HRESULT (APIENTRY *PFNDXGI_DDI_SET_DISPLAY_MODE)(
    DXGI_DDI_ARG_SETDISPLAYMODE* pSetMode);
typedef HRESULT (APIENTRY *PFNDXGI_DDI_SET_RESOURCE_PRIORITY)(
    DXGI_DDI_ARG_SETRESOURCEPRIORITY* pPriority);
typedef HRESULT (APIENTRY *PFNDXGI_DDI_QUERY_RESOURCE_RESIDENCY)(
    DXGI_DDI_ARG_QUERYRESOURCERESIDENCY* pQuery);
typedef HRESULT (APIENTRY *PFNDXGI_DDI_ROTATE_RESOURCE_IDENTITIES)(
    DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES* pRotate);
typedef HRESULT (APIENTRY *PFNDXGI_DDI_BLT)(
    DXGI_DDI_ARG_BLT* pBlt);
typedef HRESULT (APIENTRY *PFNDXGI_DDI_RESOLVE_SHARED_RESOURCE)(
    DXGI_DDI_ARG_RESOLVESHAREDRESOURCE* pResolve);

struct _DXGI_DDI_BASE_FUNCTIONS
{
    PFNDXGI_DDI_PRESENT                     pfnPresent;
    PFNDXGI_DDI_GET_GAMMA_CAPS             pfnGetGammaCaps;
    PFNDXGI_DDI_SET_DISPLAY_MODE           pfnSetDisplayMode;
    PFNDXGI_DDI_SET_RESOURCE_PRIORITY      pfnSetResourcePriority;
    PFNDXGI_DDI_QUERY_RESOURCE_RESIDENCY   pfnQueryResourceResidency;
    PFNDXGI_DDI_ROTATE_RESOURCE_IDENTITIES pfnRotateResourceIdentities;
    PFNDXGI_DDI_BLT                        pfnBlt;
};

struct _DXGI1_1_DDI_BASE_FUNCTIONS
{
    PFNDXGI_DDI_PRESENT                     pfnPresent;
    PFNDXGI_DDI_GET_GAMMA_CAPS             pfnGetGammaCaps;
    PFNDXGI_DDI_SET_DISPLAY_MODE           pfnSetDisplayMode;
    PFNDXGI_DDI_SET_RESOURCE_PRIORITY      pfnSetResourcePriority;
    PFNDXGI_DDI_QUERY_RESOURCE_RESIDENCY   pfnQueryResourceResidency;
    PFNDXGI_DDI_ROTATE_RESOURCE_IDENTITIES pfnRotateResourceIdentities;
    PFNDXGI_DDI_BLT                        pfnBlt;
    PFNDXGI_DDI_RESOLVE_SHARED_RESOURCE    pfnResolveSharedResource;
};

typedef struct _DXGIDDICB_PRESENT
{
    D3DKMT_HANDLE   hSrcAllocation;
    D3DKMT_HANDLE   hDstAllocation;
    VOID*           pDXGIContext;
    HANDLE          hContext;
    UINT            BroadcastContextCount;
    HANDLE          BroadcastContext[D3DDDI_MAX_BROADCAST_CONTEXT];
} DXGIDDICB_PRESENT;

typedef HRESULT (APIENTRY *PFNDDXGIDDI_PRESENTCB)(
    HANDLE hDevice,
    DXGIDDICB_PRESENT* pPresent);

struct _DXGI_DDI_BASE_CALLBACKS
{
    PFNDDXGIDDI_PRESENTCB pfnPresentCb;
};

struct _DXGI_DDI_BASE_ARGS
{
    DXGI_DDI_BASE_CALLBACKS* pDXGIBaseCallbacks;
    union
    {
        DXGI1_1_DDI_BASE_FUNCTIONS* pDXGIDDIBaseFunctions2;
        DXGI_DDI_BASE_FUNCTIONS*    pDXGIDDIBaseFunctions;
    };
};

#pragma warning(pop)

#endif /* _DXGIDDI_H_ */
