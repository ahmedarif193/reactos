/*
 * Native DXGI interface surface (dxgkrnl-backed), matching the Win11 DXGI ABI.
 *
 * ReactOS' SDK ships no DXGI COM headers, so the interface/vtable layout is
 * declared here exactly as the public dxgi.h/dxgi1_2.h/dxgi1_4.h define it.
 * COM is ABI-defined, so a consumer compiled against the public headers
 * (e.g. mesa's WSI) binds to these objects by vtable slot verbatim.
 *
 * Only enumeration is backed today (via D3DKMT); swapchain/composition entry
 * points return E_NOTIMPL until the dxgkrnl present path lands (see roadmap).
 */

#ifndef DXGI_NATIVE_H
#define DXGI_NATIVE_H

#include <windows.h>
#include <objbase.h>

typedef struct IDXGIFactory4 IDXGIFactory4;
typedef struct IDXGIAdapter IDXGIAdapter;
typedef struct IDXGIAdapter1 IDXGIAdapter1;
typedef struct IDXGIOutput IDXGIOutput;
typedef struct IDXGISwapChain IDXGISwapChain;
typedef struct IDXGISwapChain1 IDXGISwapChain1;

/* Opaque to this module — only ever forwarded as pointers into stubs. */
typedef struct DXGI_SWAP_CHAIN_DESC DXGI_SWAP_CHAIN_DESC;
typedef struct DXGI_SWAP_CHAIN_DESC1 DXGI_SWAP_CHAIN_DESC1;
typedef struct DXGI_SWAP_CHAIN_FULLSCREEN_DESC DXGI_SWAP_CHAIN_FULLSCREEN_DESC;

typedef struct DXGI_ADAPTER_DESC {
    WCHAR  Description[128];
    UINT   VendorId;
    UINT   DeviceId;
    UINT   SubSysId;
    UINT   Revision;
    SIZE_T DedicatedVideoMemory;
    SIZE_T DedicatedSystemMemory;
    SIZE_T SharedSystemMemory;
    LUID   AdapterLuid;
} DXGI_ADAPTER_DESC;

typedef struct DXGI_ADAPTER_DESC1 {
    WCHAR  Description[128];
    UINT   VendorId;
    UINT   DeviceId;
    UINT   SubSysId;
    UINT   Revision;
    SIZE_T DedicatedVideoMemory;
    SIZE_T DedicatedSystemMemory;
    SIZE_T SharedSystemMemory;
    LUID   AdapterLuid;
    UINT   Flags;
} DXGI_ADAPTER_DESC1;

#define DXGI_ADAPTER_FLAG_NONE     0
#define DXGI_ADAPTER_FLAG_REMOTE   1
#define DXGI_ADAPTER_FLAG_SOFTWARE 2

/* ---- IDXGIAdapter1 ------------------------------------------------------- */

typedef struct IDXGIAdapter1Vtbl {
    BEGIN_INTERFACE
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDXGIAdapter1 *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDXGIAdapter1 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IDXGIAdapter1 *This);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(IDXGIAdapter1 *This, REFGUID guid, UINT size, const void *data);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(IDXGIAdapter1 *This, REFGUID guid, const IUnknown *object);
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(IDXGIAdapter1 *This, REFGUID guid, UINT *size, void *data);
    HRESULT (STDMETHODCALLTYPE *GetParent)(IDXGIAdapter1 *This, REFIID riid, void **parent);
    HRESULT (STDMETHODCALLTYPE *EnumOutputs)(IDXGIAdapter1 *This, UINT output_idx, IDXGIOutput **output);
    HRESULT (STDMETHODCALLTYPE *GetDesc)(IDXGIAdapter1 *This, DXGI_ADAPTER_DESC *desc);
    HRESULT (STDMETHODCALLTYPE *CheckInterfaceSupport)(IDXGIAdapter1 *This, REFGUID guid, LARGE_INTEGER *umd_version);
    HRESULT (STDMETHODCALLTYPE *GetDesc1)(IDXGIAdapter1 *This, DXGI_ADAPTER_DESC1 *desc);
    END_INTERFACE
} IDXGIAdapter1Vtbl;

struct IDXGIAdapter1 { const IDXGIAdapter1Vtbl *lpVtbl; };

/* ---- IDXGIFactory4 ------------------------------------------------------- */

typedef struct IDXGIFactory4Vtbl {
    BEGIN_INTERFACE
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDXGIFactory4 *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDXGIFactory4 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IDXGIFactory4 *This);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(IDXGIFactory4 *This, REFGUID guid, UINT size, const void *data);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(IDXGIFactory4 *This, REFGUID guid, const IUnknown *object);
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(IDXGIFactory4 *This, REFGUID guid, UINT *size, void *data);
    HRESULT (STDMETHODCALLTYPE *GetParent)(IDXGIFactory4 *This, REFIID riid, void **parent);
    HRESULT (STDMETHODCALLTYPE *EnumAdapters)(IDXGIFactory4 *This, UINT adapter_idx, IDXGIAdapter **adapter);
    HRESULT (STDMETHODCALLTYPE *MakeWindowAssociation)(IDXGIFactory4 *This, HWND window, UINT flags);
    HRESULT (STDMETHODCALLTYPE *GetWindowAssociation)(IDXGIFactory4 *This, HWND *window);
    HRESULT (STDMETHODCALLTYPE *CreateSwapChain)(IDXGIFactory4 *This, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapchain);
    HRESULT (STDMETHODCALLTYPE *CreateSoftwareAdapter)(IDXGIFactory4 *This, HMODULE swrast, IDXGIAdapter **adapter);
    HRESULT (STDMETHODCALLTYPE *EnumAdapters1)(IDXGIFactory4 *This, UINT adapter_idx, IDXGIAdapter1 **adapter);
    WINBOOL (STDMETHODCALLTYPE *IsCurrent)(IDXGIFactory4 *This);
    WINBOOL (STDMETHODCALLTYPE *IsWindowedStereoEnabled)(IDXGIFactory4 *This);
    HRESULT (STDMETHODCALLTYPE *CreateSwapChainForHwnd)(IDXGIFactory4 *This, IUnknown *device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1 *desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fs, IDXGIOutput *restrict_out, IDXGISwapChain1 **swapchain);
    HRESULT (STDMETHODCALLTYPE *CreateSwapChainForCoreWindow)(IDXGIFactory4 *This, IUnknown *device, IUnknown *window, const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *restrict_out, IDXGISwapChain1 **swapchain);
    HRESULT (STDMETHODCALLTYPE *GetSharedResourceAdapterLuid)(IDXGIFactory4 *This, HANDLE resource, LUID *luid);
    HRESULT (STDMETHODCALLTYPE *RegisterStereoStatusWindow)(IDXGIFactory4 *This, HWND window, UINT msg, DWORD *cookie);
    HRESULT (STDMETHODCALLTYPE *RegisterStereoStatusEvent)(IDXGIFactory4 *This, HANDLE event, DWORD *cookie);
    void    (STDMETHODCALLTYPE *UnregisterStereoStatus)(IDXGIFactory4 *This, DWORD cookie);
    HRESULT (STDMETHODCALLTYPE *RegisterOcclusionStatusWindow)(IDXGIFactory4 *This, HWND window, UINT msg, DWORD *cookie);
    HRESULT (STDMETHODCALLTYPE *RegisterOcclusionStatusEvent)(IDXGIFactory4 *This, HANDLE event, DWORD *cookie);
    void    (STDMETHODCALLTYPE *UnregisterOcclusionStatus)(IDXGIFactory4 *This, DWORD cookie);
    HRESULT (STDMETHODCALLTYPE *CreateSwapChainForComposition)(IDXGIFactory4 *This, IUnknown *device, const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *restrict_out, IDXGISwapChain1 **swapchain);
    UINT    (STDMETHODCALLTYPE *GetCreationFlags)(IDXGIFactory4 *This);
    HRESULT (STDMETHODCALLTYPE *EnumAdapterByLuid)(IDXGIFactory4 *This, LUID luid, REFIID iid, void **adapter);
    HRESULT (STDMETHODCALLTYPE *EnumWarpAdapter)(IDXGIFactory4 *This, REFIID iid, void **adapter);
    END_INTERFACE
} IDXGIFactory4Vtbl;

struct IDXGIFactory4 { const IDXGIFactory4Vtbl *lpVtbl; };

#endif /* DXGI_NATIVE_H */
