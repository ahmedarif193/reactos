/*
 * PROJECT:     ReactOS Desktop Window Manager API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3DKMT shared redirection-surface management
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
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
    HANDLE ReadyEvent;
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
    if (Surface->ReadyEvent != NULL)
        CloseHandle(Surface->ReadyEvent);
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
    D3DKMT_QUERYRESOURCEINFO Query;
    UINT Dimensions[3];
    ULONG DeviceIndex;
    HANDLE ReadyEvent;
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

    /* The standard-allocation DDI is allowed to choose its CPU-visible pitch.
     * Query the resource metadata produced by dxgkrnl instead of retaining the
     * tightly packed request as if it were an adapter result. */
    RtlZeroMemory(&RuntimeInfo, sizeof(RuntimeInfo));
    RtlZeroMemory(&Query, sizeof(Query));
    Query.hDevice = g_DxDevices[DeviceIndex].hDevice;
    Query.hGlobalShare = Create.hGlobalShare;
    Query.pPrivateRuntimeData = &RuntimeInfo;
    Query.PrivateRuntimeDataSize = sizeof(RuntimeInfo);
    Status = D3DKMTQueryResourceInfo(&Query);
    if (!NT_SUCCESS(Status) ||
        Query.NumAllocations != 1 ||
        Query.PrivateRuntimeDataSize != sizeof(RuntimeInfo) ||
        RuntimeInfo.Magic != DWM_DX_SURFACE_INFO_MAGIC ||
        RuntimeInfo.Version != DWM_DX_SURFACE_INFO_VERSION ||
        RuntimeInfo.Width != Width || RuntimeInfo.Height != Height ||
        RuntimeInfo.Pitch < Width * sizeof(ULONG) ||
        RuntimeInfo.Format != DWM_DX_FORMAT_B8G8R8A8_UNORM)
    {
        D3DKMT_DESTROYALLOCATION Destroy;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = g_DxDevices[DeviceIndex].hDevice;
        Destroy.hResource = Create.hResource;
        (void)D3DKMTDestroyAllocation(&Destroy);
        if (NT_SUCCESS(Status))
            Status = STATUS_INVALID_DEVICE_STATE;
        DwmDxReportFailure("query_allocation", Status);
        return Status;
    }

    ReadyEvent = CreateEventW(NULL, TRUE, TRUE, NULL);
    if (ReadyEvent == NULL)
    {
        D3DKMT_DESTROYALLOCATION Destroy;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = g_DxDevices[DeviceIndex].hDevice;
        Destroy.hResource = Create.hResource;
        (void)D3DKMTDestroyAllocation(&Destroy);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(&Exchange, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_REGISTER;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Exchange.AdapterLuid = *Luid;
    Exchange.GlobalShare = Create.hGlobalShare;
    Exchange.Info = RuntimeInfo;
    Exchange.ReadyEvent = (ULONGLONG)(ULONG_PTR)ReadyEvent;
    Status = (NTSTATUS)NtUserCallOneParam((DWORD_PTR)&Exchange,
                                          DWM_ROUTINE_DXSURFACE);
    if (!NT_SUCCESS(Status))
    {
        D3DKMT_DESTROYALLOCATION Destroy;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = g_DxDevices[DeviceIndex].hDevice;
        Destroy.hResource = Create.hResource;
        (void)D3DKMTDestroyAllocation(&Destroy);
        CloseHandle(ReadyEvent);
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
    Surface->ReadyEvent = ReadyEvent;
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

static VOID
DwmDxCancelGdiSurface(HWND Window, ULONGLONG UpdateId)
{
    DWM_DX_SURFACE_EXCHANGE Exchange;

    RtlZeroMemory(&Exchange, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_CANCEL_GDI;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Exchange.UpdateId = UpdateId;
    (void)NtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
}

static NTSTATUS
DwmDxIssueGdiSurface(HWND Window,
                     const LUID *Luid,
                     UINT *Format,
                     HANDLE *SharedSurface,
                     ULONGLONG *UpdateId)
{
    DWM_DX_SHARED_SURFACE_INFO RuntimeInfo;
    DWM_DX_SURFACE_EXCHANGE Exchange;
    D3DKMT_QUERYRESOURCEINFO Query;
    ULONG DeviceIndex;
    NTSTATUS Status;

    RtlZeroMemory(&Exchange, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_ISSUE_GDI;
    Exchange.Window = (ULONGLONG)(ULONG_PTR)Window;
    Status = (NTSTATUS)NtUserCallOneParam((DWORD_PTR)&Exchange,
                                          DWM_ROUTINE_DXSURFACE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = DwmDxGetDevice(Luid, &DeviceIndex);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&RuntimeInfo, sizeof(RuntimeInfo));
        RtlZeroMemory(&Query, sizeof(Query));
        Query.hDevice = g_DxDevices[DeviceIndex].hDevice;
        Query.hGlobalShare = Exchange.GlobalShare;
        Query.pPrivateRuntimeData = &RuntimeInfo;
        Query.PrivateRuntimeDataSize = sizeof(RuntimeInfo);
        Status = D3DKMTQueryResourceInfo(&Query);
        if (NT_SUCCESS(Status) &&
            (Query.NumAllocations != 1 ||
             Query.PrivateRuntimeDataSize != sizeof(RuntimeInfo) ||
             !RtlEqualMemory(&RuntimeInfo,
                             &Exchange.Info,
                             sizeof(RuntimeInfo))))
        {
            Status = STATUS_INVALID_PARAMETER;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        DwmDxCancelGdiSurface(Window, Exchange.UpdateId);
        return Status;
    }

    *Format = D3DDDIFMT_A8R8G8B8;
    *SharedSurface = (HANDLE)(ULONG_PTR)Exchange.GlobalShare;
    *UpdateId = Exchange.UpdateId;
    return STATUS_SUCCESS;
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
        Window == NULL ||
        (Flags & ~(1u | DWM_DX_REDIRECTION_GDI_SURFACE)) != 0)
    {
        return E_INVALIDARG;
    }

    *Format = 0;
    *SharedSurface = NULL;
    *UpdateId = 0;
    if (!InitOnceExecuteOnce(&g_DxInitOnce, DwmDxInitialize, NULL, NULL))
        return E_FAIL;

    if ((Flags & DWM_DX_REDIRECTION_GDI_SURFACE) != 0)
    {
        EnterCriticalSection(&g_DxLock);
        Status = DwmDxIssueGdiSurface(Window, &AdapterLuid,
                                      Format, SharedSurface, UpdateId);
        LeaveCriticalSection(&g_DxLock);
        if (NT_SUCCESS(Status))
            return DWM_S_GDI_REDIRECTION_SURFACE;
        if (Status == STATUS_DEVICE_BUSY)
            return S_FALSE;
        return HRESULT_FROM_NT(Status);
    }

    EnterCriticalSection(&g_DxLock);
    for (Index = 0; Index < DWM_DX_MAX_SURFACES; ++Index)
    {
        if (g_DxSurfaces[Index].Window == Window)
        {
            Surface = &g_DxSurfaces[Index];
            break;
        }
    }
    if (Surface != NULL && Surface->ReadyEvent != NULL &&
        WaitForSingleObject(Surface->ReadyEvent, 0) == WAIT_TIMEOUT)
    {
        LeaveCriticalSection(&g_DxLock);
        return S_FALSE;
    }
    LeaveCriticalSection(&g_DxLock);

    if (!IsWindow(Window) || !GetClientRect(Window, &ClientRect) ||
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

    Surface = NULL;
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

        if (Surface->ReadyEvent != NULL &&
            WaitForSingleObject(Surface->ReadyEvent, 0) == WAIT_TIMEOUT)
        {
            LeaveCriticalSection(&g_DxLock);
            return S_FALSE;
        }

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

    *Format = D3DDDIFMT_A8R8G8B8;
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
