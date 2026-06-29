/*
 * PROJECT:     ReactOS DXGI Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Internal header for DXGI implementation
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#ifndef _DXGI_PRIVATE_H_
#define _DXGI_PRIVATE_H_

#include <stdarg.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#define COBJMACROS
#define CONST_VTABLE

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <objbase.h>
#include <d3dkmthk.h>

#include <dxgi.h>

#undef WIN32_NO_STATUS
#include <ntstatus.h>

/* Debug output macro */
#include <wine/debug.h>

/*
 * ========================================================================
 *  Internal structures
 * ========================================================================
 */

/* Maximum adapters and outputs we cache */
#define DXGI_MAX_ADAPTERS   16
#define DXGI_MAX_OUTPUTS    16

/* Private data entry for IDXGIObject::SetPrivateData */
typedef struct _DXGI_PRIVATE_DATA_ENTRY
{
    GUID Guid;
    UINT DataSize;
    void *pData;
    IUnknown *pInterface;
    struct _DXGI_PRIVATE_DATA_ENTRY *pNext;
} DXGI_PRIVATE_DATA_ENTRY;

/*
 * DxgiObject -- base for all DXGI objects.  Provides private data storage
 * and parent chain.  Does NOT own a COM reference count (subclasses do).
 */
typedef struct _DxgiObject
{
    DXGI_PRIVATE_DATA_ENTRY *PrivateDataHead;
} DxgiObject;

/* Forward declarations */
typedef struct _DxgiFactory  DxgiFactory;
typedef struct _DxgiAdapter  DxgiAdapter;
typedef struct _DxgiOutput   DxgiOutput;
typedef struct _DxgiSwapChain DxgiSwapChain;

/*
 * DxgiFactory -- implements IDXGIFactory1.
 */
struct _DxgiFactory
{
    IDXGIFactory1 IDXGIFactory1_iface;
    LONG RefCount;
    DxgiObject Base;
    HWND AssociatedWindow;
    UINT AssociationFlags;
    UINT AdapterCount;

    /* Cached adapter information from D3DKMT enumeration */
    struct
    {
        D3DKMT_HANDLE hAdapter;
        WCHAR DeviceName[32];
        LUID AdapterLuid;
        UINT OutputCount;
        struct
        {
            WCHAR DeviceName[32];
            D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
        } Outputs[DXGI_MAX_OUTPUTS];
        BOOL Valid;
    } AdapterCache[DXGI_MAX_ADAPTERS];

    BOOL Enumerated;
};

/*
 * DxgiAdapter -- implements IDXGIAdapter1.
 */
struct _DxgiAdapter
{
    IDXGIAdapter1 IDXGIAdapter1_iface;
    LONG RefCount;
    DxgiObject Base;
    DxgiFactory *pFactory;
    UINT AdapterOrdinal;
    D3DKMT_HANDLE hAdapter;
    LUID AdapterLuid;
    WCHAR DeviceName[32];
    DXGI_ADAPTER_DESC1 Desc;
    BOOL DescValid;
};

/*
 * DxgiOutput -- implements IDXGIOutput.
 */
struct _DxgiOutput
{
    IDXGIOutput IDXGIOutput_iface;
    LONG RefCount;
    DxgiObject Base;
    DxgiAdapter *pAdapter;
    UINT OutputOrdinal;
    D3DKMT_HANDLE hAdapter;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    WCHAR DeviceName[32];
    DXGI_OUTPUT_DESC Desc;
    BOOL DescValid;
};

/*
 * DxgiSwapChain -- implements IDXGISwapChain.
 */
struct _DxgiSwapChain
{
    IDXGISwapChain IDXGISwapChain_iface;
    LONG RefCount;
    DxgiObject Base;
    DxgiFactory *pFactory;
    IUnknown *pDevice;
    DXGI_SWAP_CHAIN_DESC Desc;
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hAdapter;
    BOOL Fullscreen;
    IDXGIOutput *pFullscreenOutput;
    IDXGISurface *pBackBuffer;
    UINT PresentCount;
    UINT SyncInterval;
};

/*
 * ========================================================================
 *  Inline helpers for interface casts
 * ========================================================================
 */

static inline DxgiFactory *impl_from_IDXGIFactory1(IDXGIFactory1 *iface)
{
    return CONTAINING_RECORD(iface, DxgiFactory, IDXGIFactory1_iface);
}

static inline DxgiAdapter *impl_from_IDXGIAdapter1(IDXGIAdapter1 *iface)
{
    return CONTAINING_RECORD(iface, DxgiAdapter, IDXGIAdapter1_iface);
}

static inline DxgiOutput *impl_from_IDXGIOutput(IDXGIOutput *iface)
{
    return CONTAINING_RECORD(iface, DxgiOutput, IDXGIOutput_iface);
}

static inline DxgiSwapChain *impl_from_IDXGISwapChain(IDXGISwapChain *iface)
{
    return CONTAINING_RECORD(iface, DxgiSwapChain, IDXGISwapChain_iface);
}

/*
 * ========================================================================
 *  Internal function prototypes
 * ========================================================================
 */

/* factory.c */
HRESULT DxgiFactory_Create(REFIID riid, void **ppFactory);

/* adapter.c */
HRESULT DxgiAdapter_Create(DxgiFactory *pFactory, UINT ordinal,
                            D3DKMT_HANDLE hAdapter, const WCHAR *DeviceName,
                            LUID AdapterLuid, IDXGIAdapter1 **ppAdapter);

/* output.c */
HRESULT DxgiOutput_Create(DxgiAdapter *pAdapter, UINT ordinal,
                           D3DKMT_HANDLE hAdapter,
                           D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
                           const WCHAR *DeviceName,
                           IDXGIOutput **ppOutput);

/* swapchain.c */
HRESULT DxgiSwapChain_Create(DxgiFactory *pFactory, IUnknown *pDevice,
                              DXGI_SWAP_CHAIN_DESC *pDesc,
                              IDXGISwapChain **ppSwapChain);

/* utils.c */
void DxgiObject_Init(DxgiObject *obj);
void DxgiObject_Destroy(DxgiObject *obj);
HRESULT DxgiObject_SetPrivateData(DxgiObject *obj, REFGUID Name,
                                   UINT DataSize, const void *pData);
HRESULT DxgiObject_SetPrivateDataInterface(DxgiObject *obj, REFGUID Name,
                                            const IUnknown *pUnknown);
HRESULT DxgiObject_GetPrivateData(DxgiObject *obj, REFGUID Name,
                                   UINT *pDataSize, void *pData);

#endif /* _DXGI_PRIVATE_H_ */
