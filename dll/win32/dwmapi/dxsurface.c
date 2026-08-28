/*
 * PROJECT:     ReactOS Desktop Window Manager API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     D3DKMT shared redirection-surface management
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * The public ordinal contract matches Windows 7. The implementation creates
 * one shareable, linear D3DKMT resource per client window and registers its
 * global-share handle with win32k. The OpenGL callback may drop an immediate
 * present while the previous surface update is still owned by DWM; this keeps
 * rendering asynchronous without exposing a half-written surface.
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0xF003
#endif

#include <windows.h>
#include <d3dkmthk.h>
#include <ndk/rtlfuncs.h>
#include <reactos/dwmframe.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(dwmapi);

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

DWORD_PTR NTAPI NtUserCallOneParam(DWORD_PTR Param, DWORD Routine);

#define DWM_DX_MAX_DEVICES 8
#define DWM_DX_MAX_SURFACES 128

typedef struct _DWM_DX_DEVICE
{
    LUID Luid;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
} DWM_DX_DEVICE;

typedef struct _DWM_DX_SURFACE
{
    HWND Window;
    ULONG DeviceIndex;
    ULONG Width;
    ULONG Height;
    D3DKMT_HANDLE hResource;
    D3DKMT_HANDLE hAllocation;
    D3DKMT_HANDLE hGlobalShare;
} DWM_DX_SURFACE;

static INIT_ONCE g_DxInitOnce = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_DxLock;
static DWM_DX_DEVICE g_DxDevices[DWM_DX_MAX_DEVICES];
static DWM_DX_SURFACE g_DxSurfaces[DWM_DX_MAX_SURFACES];
static LONG g_DxFailureLogged;

static VOID
DwmDxReportFailure(const char *Stage, NTSTATUS Status)
{
    if (InterlockedCompareExchange(&g_DxFailureLogged, 1, 0) == 0)
    {
        ERR("DWM shared-surface operation failed at %s, status=%#lx, error=%lu\n",
            Stage, (ULONG)Status, RtlNtStatusToDosError(Status));
    }
}

static BOOL CALLBACK
DwmDxInitialize(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context)
{
    UNREFERENCED_PARAMETER(InitOnce);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);
    InitializeCriticalSection(&g_DxLock);
    return TRUE;
}

static BOOL
DwmDxLuidEqual(const LUID *Left, const LUID *Right)
{
    return Left->LowPart == Right->LowPart &&
           Left->HighPart == Right->HighPart;
}

static HRESULT
DwmDxStatusToHresult(NTSTATUS Status)
{
    if (NT_SUCCESS(Status))
        return S_OK;
    return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));
}

static VOID
DwmDxDestroySurface(DWM_DX_SURFACE *Surface)
{
    if (Surface->hResource != 0 &&
        Surface->DeviceIndex < DWM_DX_MAX_DEVICES &&
        g_DxDevices[Surface->DeviceIndex].hDevice != 0)
    {
        D3DKMT_DESTROYALLOCATION Destroy;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = g_DxDevices[Surface->DeviceIndex].hDevice;
        Destroy.hResource = Surface->hResource;
        (void)D3DKMTDestroyAllocation(&Destroy);
    }
    RtlZeroMemory(Surface, sizeof(*Surface));
}

static NTSTATUS
DwmDxGetDevice(const LUID *Luid, ULONG *DeviceIndex)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenAdapter;
    D3DKMT_CREATEDEVICE CreateDevice;
    ULONG Index, FreeIndex = DWM_DX_MAX_DEVICES;
    NTSTATUS Status;

    for (Index = 0; Index < DWM_DX_MAX_DEVICES; ++Index)
    {
        if (g_DxDevices[Index].hDevice != 0 &&
            DwmDxLuidEqual(&g_DxDevices[Index].Luid, Luid))
        {
            *DeviceIndex = Index;
            return STATUS_SUCCESS;
        }
        if (g_DxDevices[Index].hDevice == 0 &&
            FreeIndex == DWM_DX_MAX_DEVICES)
        {
            FreeIndex = Index;
        }
    }
    if (FreeIndex == DWM_DX_MAX_DEVICES)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&OpenAdapter, sizeof(OpenAdapter));
    lstrcpynW(OpenAdapter.DeviceName, L"\\\\.\\DISPLAY1",
              ARRAYSIZE(OpenAdapter.DeviceName));
    Status = D3DKMTOpenAdapterFromGdiDisplayName(&OpenAdapter);
    if (!NT_SUCCESS(Status) || OpenAdapter.hAdapter == 0)
    {
        DwmDxReportFailure("open_adapter",
                           NT_SUCCESS(Status) ? STATUS_NOT_FOUND : Status);
        return NT_SUCCESS(Status) ? STATUS_NOT_FOUND : Status;
    }
    if (!DwmDxLuidEqual(&OpenAdapter.AdapterLuid, Luid))
    {
        D3DKMT_CLOSEADAPTER CloseAdapter;

        RtlZeroMemory(&CloseAdapter, sizeof(CloseAdapter));
        CloseAdapter.hAdapter = OpenAdapter.hAdapter;
        (void)D3DKMTCloseAdapter(&CloseAdapter);
        DwmDxReportFailure("adapter_luid", STATUS_NOT_FOUND);
        return STATUS_NOT_FOUND;
    }

    RtlZeroMemory(&CreateDevice, sizeof(CreateDevice));
    CreateDevice.hAdapter = OpenAdapter.hAdapter;
    Status = D3DKMTCreateDevice(&CreateDevice);
    if (!NT_SUCCESS(Status) || CreateDevice.hDevice == 0)
    {
        D3DKMT_CLOSEADAPTER CloseAdapter;

        RtlZeroMemory(&CloseAdapter, sizeof(CloseAdapter));
        CloseAdapter.hAdapter = OpenAdapter.hAdapter;
        (void)D3DKMTCloseAdapter(&CloseAdapter);
        DwmDxReportFailure("create_device",
                           NT_SUCCESS(Status) ?
                               STATUS_INVALID_DEVICE_STATE : Status);
        return NT_SUCCESS(Status) ? STATUS_INVALID_DEVICE_STATE : Status;
    }

    g_DxDevices[FreeIndex].Luid = *Luid;
    g_DxDevices[FreeIndex].hAdapter = OpenAdapter.hAdapter;
    g_DxDevices[FreeIndex].hDevice = CreateDevice.hDevice;
    *DeviceIndex = FreeIndex;
    return STATUS_SUCCESS;
}

static NTSTATUS
DwmDxRegisterSurface(HWND Window,
                     const LUID *Luid,
                     DWM_DX_SURFACE *Surface,
                     ULONG Width,
                     ULONG Height)
{
    DWM_DX_SHARED_SURFACE_INFO RuntimeInfo;
    DWM_DX_SURFACE_EXCHANGE Exchange;
    D3DKMT_CREATEALLOCATION Create;
    D3DDDI_ALLOCATIONINFO Allocation;
    UINT Dimensions[3];
    ULONG DeviceIndex;
    NTSTATUS Status;

    Status = DwmDxGetDevice(Luid, &DeviceIndex);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&RuntimeInfo, sizeof(RuntimeInfo));
    RuntimeInfo.Magic = DWM_DX_SURFACE_INFO_MAGIC;
    RuntimeInfo.Version = DWM_DX_SURFACE_INFO_VERSION;
    RuntimeInfo.Width = Width;
    RuntimeInfo.Height = Height;
    RuntimeInfo.Pitch = Width * sizeof(ULONG);
    RuntimeInfo.Format = DWM_DX_FORMAT_B8G8R8A8_UNORM;

    Dimensions[0] = Width;
    Dimensions[1] = Height;
    Dimensions[2] = 32;

    RtlZeroMemory(&Allocation, sizeof(Allocation));
    Allocation.pPrivateDriverData = Dimensions;
    Allocation.PrivateDriverDataSize = sizeof(Dimensions);

    RtlZeroMemory(&Create, sizeof(Create));
    Create.hDevice = g_DxDevices[DeviceIndex].hDevice;
    Create.pPrivateRuntimeData = &RuntimeInfo;
    Create.PrivateRuntimeDataSize = sizeof(RuntimeInfo);
    Create.NumAllocations = 1;
    Create.pAllocationInfo = &Allocation;
    Create.Flags.CreateResource = 1;
    Create.Flags.CreateShared = 1;
    Status = D3DKMTCreateAllocation(&Create);
    if (!NT_SUCCESS(Status))
    {
        DwmDxReportFailure("create_allocation", Status);
        return Status;
    }
    if (Create.hResource == 0 || Create.hGlobalShare == 0 ||
        Allocation.hAllocation == 0)
    {
        D3DKMT_DESTROYALLOCATION Destroy;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = g_DxDevices[DeviceIndex].hDevice;
        Destroy.hResource = Create.hResource;
        if (Create.hResource != 0)
            (void)D3DKMTDestroyAllocation(&Destroy);
        DwmDxReportFailure("create_allocation_contract",
                           STATUS_INVALID_DEVICE_STATE);
        return STATUS_INVALID_DEVICE_STATE;
    }

    RtlZeroMemory(&Exchange, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_REGISTER;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Exchange.AdapterLuid = *Luid;
    Exchange.GlobalShare = Create.hGlobalShare;
    Exchange.Info = RuntimeInfo;
    Status = (NTSTATUS)NtUserCallOneParam((DWORD_PTR)&Exchange,
                                          DWM_ROUTINE_DXSURFACE);
    if (!NT_SUCCESS(Status))
    {
        D3DKMT_DESTROYALLOCATION Destroy;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = g_DxDevices[DeviceIndex].hDevice;
        Destroy.hResource = Create.hResource;
        (void)D3DKMTDestroyAllocation(&Destroy);
        DwmDxReportFailure("register", Status);
        return Status;
    }

    Surface->Window = Window;
    Surface->DeviceIndex = DeviceIndex;
    Surface->Width = Width;
    Surface->Height = Height;
    Surface->hResource = Create.hResource;
    Surface->hAllocation = Allocation.hAllocation;
    Surface->hGlobalShare = Create.hGlobalShare;
    return STATUS_SUCCESS;
}

static NTSTATUS
DwmDxIssueSurface(const DWM_DX_SURFACE *Surface,
                  const LUID *Luid,
                  ULONGLONG *UpdateId)
{
    DWM_DX_SURFACE_EXCHANGE Exchange;
    NTSTATUS Status;

    RtlZeroMemory(&Exchange, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_ISSUE;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Surface->Window;
    Exchange.AdapterLuid = *Luid;
    Exchange.GlobalShare = Surface->hGlobalShare;
    Status = (NTSTATUS)NtUserCallOneParam((DWORD_PTR)&Exchange,
                                          DWM_ROUTINE_DXSURFACE);
    if (NT_SUCCESS(Status))
        *UpdateId = Exchange.UpdateId;
    else if (Status != STATUS_DEVICE_BUSY)
        DwmDxReportFailure("issue", Status);
    return Status;
}

HRESULT WINAPI
DwmpDxGetWindowSharedSurface(HWND Window,
                             LUID AdapterLuid,
                             HMONITOR Monitor,
                             DWORD Flags,
                             UINT *Format,
                             HANDLE *SharedSurface,
                             ULONGLONG *UpdateId)
{
    DWM_DX_SURFACE NewSurface;
    DWM_DX_SURFACE *Surface = NULL, *FreeSurface = NULL;
    RECT ClientRect;
    ULONG Width, Height, Index;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Monitor);

    if (Format == NULL || SharedSurface == NULL || UpdateId == NULL ||
        !IsWindow(Window) || (Flags & ~1u) != 0 ||
        !GetClientRect(Window, &ClientRect) ||
        ClientRect.right <= ClientRect.left ||
        ClientRect.bottom <= ClientRect.top)
    {
        return E_INVALIDARG;
    }

    Width = (ULONG)(ClientRect.right - ClientRect.left);
    Height = (ULONG)(ClientRect.bottom - ClientRect.top);
    if (Width > ~(ULONG)0 / sizeof(ULONG) ||
        Height > ~(ULONG)0 / (Width * sizeof(ULONG)))
    {
        return E_INVALIDARG;
    }

    *Format = 0;
    *SharedSurface = NULL;
    *UpdateId = 0;
    if (!InitOnceExecuteOnce(&g_DxInitOnce, DwmDxInitialize, NULL, NULL))
        return E_FAIL;

    EnterCriticalSection(&g_DxLock);
    for (Index = 0; Index < DWM_DX_MAX_SURFACES; ++Index)
    {
        if (g_DxSurfaces[Index].Window != NULL &&
            !IsWindow(g_DxSurfaces[Index].Window))
        {
            DwmDxDestroySurface(&g_DxSurfaces[Index]);
        }
        if (g_DxSurfaces[Index].Window == Window)
            Surface = &g_DxSurfaces[Index];
        if (g_DxSurfaces[Index].Window == NULL && FreeSurface == NULL)
            FreeSurface = &g_DxSurfaces[Index];
    }

    if (Surface != NULL)
    {
        DWM_DX_DEVICE *Device = &g_DxDevices[Surface->DeviceIndex];

        if (Surface->Width != Width || Surface->Height != Height ||
            !DwmDxLuidEqual(&Device->Luid, &AdapterLuid))
        {
            FreeSurface = Surface;
            Surface = NULL;
        }
    }

    if (Surface == NULL)
    {
        if (FreeSurface == NULL)
        {
            LeaveCriticalSection(&g_DxLock);
            return E_OUTOFMEMORY;
        }

        RtlZeroMemory(&NewSurface, sizeof(NewSurface));
        Status = DwmDxRegisterSurface(Window, &AdapterLuid, &NewSurface,
                                      Width, Height);
        if (!NT_SUCCESS(Status))
        {
            LeaveCriticalSection(&g_DxLock);
            return DwmDxStatusToHresult(Status);
        }

        DwmDxDestroySurface(FreeSurface);
        *FreeSurface = NewSurface;
        Surface = FreeSurface;
    }

    Status = DwmDxIssueSurface(Surface, &AdapterLuid, UpdateId);
    if (Status == STATUS_DEVICE_BUSY)
    {
        LeaveCriticalSection(&g_DxLock);
        return S_FALSE;
    }
    if (!NT_SUCCESS(Status))
    {
        LeaveCriticalSection(&g_DxLock);
        return DwmDxStatusToHresult(Status);
    }

    *Format = DWM_DX_FORMAT_B8G8R8A8_UNORM;
    *SharedSurface = (HANDLE)(ULONG_PTR)Surface->hGlobalShare;
    LeaveCriticalSection(&g_DxLock);
    return S_OK;
}

HRESULT WINAPI
DwmpDxUpdateWindowSharedSurface(HWND Window,
                                ULONGLONG UpdateId,
                                DWORD Flags,
                                HMONITOR Monitor,
                                const RECT *UpdateRect)
{
    DWM_DX_SURFACE_EXCHANGE Exchange;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Monitor);

    if (!IsWindow(Window) || UpdateId == 0 ||
        ((Flags & DWM_DX_UPDATE_CANCEL) == 0 && UpdateRect == NULL))
    {
        return E_INVALIDARG;
    }

    RtlZeroMemory(&Exchange, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_UPDATE;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Exchange.UpdateId = UpdateId;
    Exchange.Flags = Flags;
    if (UpdateRect != NULL)
        Exchange.UpdateRect = *(const RECTL *)UpdateRect;
    Status = (NTSTATUS)NtUserCallOneParam((DWORD_PTR)&Exchange,
                                          DWM_ROUTINE_DXSURFACE);
    if (!NT_SUCCESS(Status))
        DwmDxReportFailure("update", Status);
    return DwmDxStatusToHresult(Status);
}
