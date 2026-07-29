/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3DKMT dxgkrnl callbacks
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 */

#include <win32k.h>
#include "drivers/wddm/wddm_bridge.h"
#include <reactos/rddm/rxgkinterface.h>
#include <debug.h>

#define IOCTL_RXGK_OPENADAPTERFROMDEVICENAME CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x112, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_RXGK_CLOSEADAPTER CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x103, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CHECKOCCLUSION CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x169, METHOD_BUFFERED, FILE_ANY_ACCESS)

#if (DXGKDDI_INTERFACE_VERSION < DXGKDDI_INTERFACE_VERSION_WDDM2_0)
typedef union _D3DKMT_DESTROYALLOCATION2FLAGS_LOCAL
{
    UINT Value;
} D3DKMT_DESTROYALLOCATION2FLAGS_LOCAL;

typedef union _D3DKMT_LOCK2FLAGS_LOCAL
{
    UINT Value;
} D3DKMT_LOCK2FLAGS_LOCAL;

struct _D3DKMT_DESTROYALLOCATION2
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hResource;
    CONST D3DKMT_HANDLE *phAllocationList;
    UINT AllocationCount;
    D3DKMT_DESTROYALLOCATION2FLAGS_LOCAL Flags;
};

struct _D3DKMT_LOCK2
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hAllocation;
    D3DKMT_LOCK2FLAGS_LOCAL Flags;
    PVOID pData;
};

struct _D3DKMT_UNLOCK2
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hAllocation;
};

#if defined(_WIN64)
C_ASSERT(sizeof(struct _D3DKMT_DESTROYALLOCATION2) == 24);
C_ASSERT(FIELD_OFFSET(struct _D3DKMT_DESTROYALLOCATION2, phAllocationList) == 8);
C_ASSERT(FIELD_OFFSET(struct _D3DKMT_DESTROYALLOCATION2, Flags) == 20);
C_ASSERT(sizeof(struct _D3DKMT_LOCK2) == 24);
C_ASSERT(FIELD_OFFSET(struct _D3DKMT_LOCK2, pData) == 16);
#else
C_ASSERT(sizeof(struct _D3DKMT_DESTROYALLOCATION2) == 20);
C_ASSERT(FIELD_OFFSET(struct _D3DKMT_DESTROYALLOCATION2, phAllocationList) == 8);
C_ASSERT(FIELD_OFFSET(struct _D3DKMT_DESTROYALLOCATION2, Flags) == 16);
C_ASSERT(sizeof(struct _D3DKMT_LOCK2) == 16);
C_ASSERT(FIELD_OFFSET(struct _D3DKMT_LOCK2, pData) == 12);
#endif
C_ASSERT(sizeof(struct _D3DKMT_UNLOCK2) == 8);
#endif

#define RETURN_STATUS_IF_NULL(Argument)            \
    do                                             \
    {                                              \
        if ((Argument) == NULL)                    \
            return STATUS_INVALID_PARAMETER;       \
    } while (0)

#define D3DKMT_CALL_CALLBACK(CallbackField, Argument)                    \
    do                                                                   \
    {                                                                    \
        NTSTATUS Status = D3dkmtValidateWddmThunk(Argument);             \
        if (!NT_SUCCESS(Status))                                         \
            return Status;                                               \
        if (DxgAdapterCallbacks.CallbackField == NULL)                   \
            return STATUS_PROCEDURE_NOT_FOUND;                           \
        return DxgAdapterCallbacks.CallbackField(Argument);              \
    } while (0)

#define D3DKMT_CALL_HANDLE_CALLBACK(CallbackField, HandleArgument)        \
    do                                                                   \
    {                                                                    \
        NTSTATUS Status = D3dkmtValidateWddmHandleThunk(HandleArgument);  \
        if (!NT_SUCCESS(Status))                                         \
            return Status;                                               \
        if (DxgAdapterCallbacks.CallbackField == NULL)                   \
            return STATUS_PROCEDURE_NOT_FOUND;                           \
        return DxgAdapterCallbacks.CallbackField(HandleArgument);         \
    } while (0)

#define D3DKMT_REQUIRE_HANDLE(Handle)                                    \
    do                                                                   \
    {                                                                    \
        NTSTATUS Status = D3dkmtValidateHandle(Handle);                  \
        if (!NT_SUCCESS(Status))                                         \
            return Status;                                               \
    } while (0)

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromDeviceName(
    _Inout_ D3DKMT_OPENADAPTERFROMDEVICENAME *pData);

static VOID
D3dkmtCloseCapturedAdapter(
    _In_ D3DKMT_HANDLE AdapterHandle)
{
    D3DKMT_CLOSEADAPTER CloseAdapter;

    if (AdapterHandle == 0)
        return;
    CloseAdapter.hAdapter = AdapterHandle;
    (VOID)WddmBridgeSendIoctl(IOCTL_RXGK_CLOSEADAPTER, &CloseAdapter, sizeof(CloseAdapter), NULL, 0);
}

static NTSTATUS
D3dkmtOpenAdapterByCapturedNtDeviceName(
    _In_ PCWSTR DeviceName,
    _Out_ D3DKMT_HANDLE *AdapterHandle,
    _Out_ LUID *AdapterLuid)
{
    D3DKMT_OPENADAPTERFROMDEVICENAME OpenByName;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (DeviceName == NULL || DeviceName[0] == L'\0' || AdapterHandle == NULL || AdapterLuid == NULL)
        return STATUS_INVALID_PARAMETER;
    *AdapterHandle = 0;
    RtlZeroMemory(AdapterLuid, sizeof(*AdapterLuid));
    RtlZeroMemory(&OpenByName, sizeof(OpenByName));
    OpenByName.pDeviceName = DeviceName;
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_RXGK_OPENADAPTERFROMDEVICENAME, &OpenByName, sizeof(OpenByName), &OpenByName, sizeof(OpenByName), &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information != sizeof(OpenByName) || OpenByName.hAdapter == 0)
    {
        D3dkmtCloseCapturedAdapter(OpenByName.hAdapter);
        return Information != sizeof(OpenByName) ? STATUS_INFO_LENGTH_MISMATCH : STATUS_INVALID_HANDLE;
    }
    *AdapterHandle = OpenByName.hAdapter;
    *AdapterLuid = OpenByName.AdapterLuid;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromGdiDisplayName(
    _Inout_ D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *pData);

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromHdc(
    _Inout_ D3DKMT_OPENADAPTERFROMHDC *pData);

/* WDDM 1.2 direct stubs (bypass callback table, like OpenAdapterFrom*) */
NTSTATUS
APIENTRY
D3DKMTEnumAdapters(
    _Inout_ CONST D3DKMT_ENUMADAPTERS *pData);

NTSTATUS
APIENTRY
D3DKMTEnumAdapters2(
    _Inout_ CONST D3DKMT_ENUMADAPTERS2 *pData);

NTSTATUS
APIENTRY
D3DKMTEnumAdapters3(
    _Inout_ struct _D3DKMT_ENUMADAPTERS3 *pData);

NTSTATUS APIENTRY D3DKMTCreateKeyedMutex(_Inout_ D3DKMT_CREATEKEYEDMUTEX *pData);
NTSTATUS APIENTRY D3DKMTQueryClockCalibration(_Inout_ struct _D3DKMT_QUERYCLOCKCALIBRATION *pData);
NTSTATUS APIENTRY D3DKMTChangeVideoMemoryReservation(_In_ CONST struct _D3DKMT_CHANGEVIDEOMMEMORYRESERVATION *pData);
NTSTATUS APIENTRY D3DKMTSetProcessSchedulingPriorityClass(_In_ HANDLE hProcess, _In_ D3DKMT_SCHEDULINGPRIORITYCLASS Class);
NTSTATUS APIENTRY D3DKMTGetProcessSchedulingPriorityClass(_In_ HANDLE hProcess, _Out_ D3DKMT_SCHEDULINGPRIORITYCLASS *pClass);
NTSTATUS APIENTRY D3DKMTCreateHwQueue(_Inout_ struct _D3DKMT_CREATEHWQUEUE *pData);
NTSTATUS APIENTRY D3DKMTDestroyHwQueue(_In_ CONST struct _D3DKMT_DESTROYHWQUEUE *pData);
NTSTATUS APIENTRY D3DKMTSubmitCommandToHwQueue(_In_ CONST struct _D3DKMT_SUBMITCOMMANDTOHWQUEUE *pData);
NTSTATUS APIENTRY D3DKMTRegisterTrimNotification(_Inout_ struct _D3DKMT_REGISTERTRIMNOTIFICATION *pData);
NTSTATUS APIENTRY D3DKMTUnregisterTrimNotification(_Inout_ struct _D3DKMT_UNREGISTERTRIMNOTIFICATION *pData);
NTSTATUS APIENTRY D3DKMTCreateKeyedMutex2(_Inout_ struct _D3DKMT_CREATEKEYEDMUTEX2 *pData);
NTSTATUS APIENTRY D3DKMTOpenKeyedMutex(_Inout_ D3DKMT_OPENKEYEDMUTEX *pData);
NTSTATUS APIENTRY D3DKMTOpenKeyedMutex2(_Inout_ struct _D3DKMT_OPENKEYEDMUTEX2 *pData);
NTSTATUS APIENTRY D3DKMTDestroyKeyedMutex(_In_ CONST D3DKMT_DESTROYKEYEDMUTEX *pData);
NTSTATUS APIENTRY D3DKMTAcquireKeyedMutex(_Inout_ D3DKMT_ACQUIREKEYEDMUTEX *pData);
NTSTATUS APIENTRY D3DKMTAcquireKeyedMutex2(_Inout_ struct _D3DKMT_ACQUIREKEYEDMUTEX2 *pData);
NTSTATUS APIENTRY D3DKMTReleaseKeyedMutex(_Inout_ D3DKMT_RELEASEKEYEDMUTEX *pData);
NTSTATUS APIENTRY D3DKMTReleaseKeyedMutex2(_Inout_ struct _D3DKMT_RELEASEKEYEDMUTEX2 *pData);


NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromLuid(
    _Inout_ CONST D3DKMT_OPENADAPTERFROMLUID *pData);

NTSTATUS
APIENTRY
D3DKMTCheckVidPnExclusiveOwnership(
    _In_ CONST D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP *pData);

NTSTATUS
APIENTRY
D3DKMTDestroyAllocation2(
    _In_ CONST struct _D3DKMT_DESTROYALLOCATION2 *pData);

NTSTATUS
APIENTRY
D3DKMTLock2(
    _Inout_ struct _D3DKMT_LOCK2 *pData);

NTSTATUS
APIENTRY
D3DKMTUnlock2(
    _In_ CONST struct _D3DKMT_UNLOCK2 *pData);

NTSTATUS
APIENTRY
D3DKMTSetVidPnSourceOwner2(
    _In_ CONST struct _D3DKMT_SETVIDPNSOURCEOWNER2 *pData);

VOID
WddmBridgeInitCallbacks(
    _Out_ PREACTOS_WIN32K_DXGKRNL_INTERFACE Interface);

/*
 * It looks like Windows saves all the function pointers globally inside win32k.
 * Instead, we're going to keep it static to this file and keep it organized in struct
 * we obtained with the IOCTL.
 */
static REACTOS_WIN32K_DXGKRNL_INTERFACE DxgAdapterCallbacks = {0};

static NTSTATUS
D3dkmtValidateWddmThunk(
    _In_opt_ const VOID *Argument)
{
    if (Argument == NULL)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeRequireReady();
}

static NTSTATUS
D3dkmtCaptureUserStructure(
    _In_reads_bytes_(Size) const VOID *Argument,
    _In_ SIZE_T Size,
    _Out_writes_bytes_(Size) VOID *Captured)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Argument == NULL || Captured == NULL || Size == 0)
        return STATUS_INVALID_PARAMETER;
    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForRead(Argument, Size, 1);
        RtlCopyMemory(Captured, Argument, Size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Status;
}

static NTSTATUS
D3dkmtValidateWddmHandleThunk(
    _In_opt_ HANDLE Handle)
{
    if (Handle == NULL)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeRequireReady();
}

static NTSTATUS
D3dkmtValidateHandle(
    _In_ D3DKMT_HANDLE Handle)
{
    return (Handle != 0) ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
}

/*
 * This looks like it's done inside DxDdStartupDxGraphics, but I'd rather keep this organized.
 * Dxg gets start inevitably anyway it seems at least on vista.
 */
VOID
APIENTRY
DxStartupDxgkInt(VOID)
{
    NTSTATUS Status;

    DPRINT("DxStartupDxgkInt: Entry\n");

#if defined(REACTOS_GRAPHICS_DRIVER_MODEL_XPDM)
    RtlZeroMemory(&DxgAdapterCallbacks, sizeof(DxgAdapterCallbacks));
    DPRINT("DxStartupDxgkInt: XPDM build, dxgkrnl bridge disabled\n");
    return;
#endif

    Status = WddmBridgeInit();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DxStartupDxgkInt: WddmBridgeInit failed with 0x%08lx\n",
                Status);
        RtlZeroMemory(&DxgAdapterCallbacks, sizeof(DxgAdapterCallbacks));
        return;
    }

    WddmBridgeInitCallbacks(&DxgAdapterCallbacks);
}

BOOLEAN
APIENTRY
NtGdiDdDDICheckExclusiveOwnership(VOID)
{
    // We don't support DWM at this time, excusive ownership is always false.
    return FALSE;
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetProcessSchedulingPriorityClass(_In_  HANDLE unnamedParam1,
                                            _Out_ D3DKMT_SCHEDULINGPRIORITYCLASS *unnamedParam2)
{
    return D3DKMTGetProcessSchedulingPriorityClass(unnamedParam1, unnamedParam2);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetProcessSchedulingPriorityClass(_In_ HANDLE unnamedParam1,
                                            _In_ D3DKMT_SCHEDULINGPRIORITYCLASS unnamedParam2)
{
    return D3DKMTSetProcessSchedulingPriorityClass(unnamedParam1, unnamedParam2);
}

NTSTATUS
APIENTRY
NtGdiDdDDISharedPrimaryLockNotification(_In_ const D3DKMT_SHAREDPRIMARYLOCKNOTIFICATION* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDISharedPrimaryUnLockNotification(_In_ const D3DKMT_SHAREDPRIMARYUNLOCKNOTIFICATION* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenAdapterFromGdiDisplayName(_Inout_ D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME* unnamedParam1)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Captured;
    WCHAR NtDeviceName[CCHDEVICENAME / 2];
    UNICODE_STRING DisplayName;
    PPDEVOBJ Pdev;
    ULONG Index;
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);

    if (!NT_SUCCESS(Status))
        return Status;
    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForRead(unnamedParam1, sizeof(Captured), 1);
        Captured = *unnamedParam1;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;
    for (Index = 0; Index < RTL_NUMBER_OF(Captured.DeviceName) && Captured.DeviceName[Index] != L'\0'; ++Index)
        NOTHING;
    if (Index == 0 || Index == RTL_NUMBER_OF(Captured.DeviceName))
        return STATUS_INVALID_PARAMETER;
    RtlInitUnicodeString(&DisplayName, Captured.DeviceName);
    Pdev = EngpGetPDEV(&DisplayName);
    if (Pdev == NULL)
    {
        /*
         * The name is well formed; there is simply no such display.  That is a
         * lookup that found nothing, not a malformed argument, and the two are
         * worth telling apart: a caller enumerating displays walks names until
         * one fails, and INVALID_PARAMETER reads as "you asked wrongly" while
         * UNSUCCESSFUL reads as "you have run off the end".  Measured on Win11:
         * STATUS_UNSUCCESSFUL.
         */
        return STATUS_UNSUCCESSFUL;
    }
    if (Pdev->pGraphicsDevice == NULL || Pdev->pGraphicsDevice->szNtDeviceName[0] == L'\0')
        Status = STATUS_UNSUCCESSFUL;
    else
    {
        RtlCopyMemory(NtDeviceName, Pdev->pGraphicsDevice->szNtDeviceName, sizeof(NtDeviceName));
        NtDeviceName[RTL_NUMBER_OF(NtDeviceName) - 1] = L'\0';
    }
    PDEVOBJ_vRelease(Pdev);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = D3dkmtOpenAdapterByCapturedNtDeviceName(NtDeviceName, &Captured.hAdapter, &Captured.AdapterLuid);
    if (!NT_SUCCESS(Status))
        return Status;
    Captured.VidPnSourceId = 0;
    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForWrite(unnamedParam1, sizeof(Captured), 1);
        *unnamedParam1 = Captured;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
        D3dkmtCloseCapturedAdapter(Captured.hAdapter);
    return Status;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenAdapterFromHdc(_Inout_ D3DKMT_OPENADAPTERFROMHDC* unnamedParam1)
{
    D3DKMT_OPENADAPTERFROMHDC Captured;
    WCHAR DeviceName[CCHDEVICENAME / 2];
    PDC Dc;
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);

    if (!NT_SUCCESS(Status))
        return Status;
    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForRead(unnamedParam1, sizeof(Captured), 1);
        Captured = *unnamedParam1;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;
    /* Windows 11 refuses a D3DKMT open with an unusable HDC as a bad
     * parameter, the same way it treats a bad D3DKMT handle. */
    if (Captured.hDc == NULL)
        return STATUS_INVALID_PARAMETER;
    Dc = DC_LockDc(Captured.hDc);
    if (Dc == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Dc->dctype != DCTYPE_DIRECT || Dc->ppdev == NULL || (Dc->ppdev->flFlags & PDEV_DISPLAY) == 0 || Dc->ppdev->pGraphicsDevice == NULL || Dc->ppdev->pGraphicsDevice->szNtDeviceName[0] == L'\0')
        Status = STATUS_INVALID_HANDLE;
    else
    {
        RtlCopyMemory(DeviceName, Dc->ppdev->pGraphicsDevice->szNtDeviceName, sizeof(DeviceName));
        DeviceName[RTL_NUMBER_OF(DeviceName) - 1] = L'\0';
    }
    DC_UnlockDc(Dc);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = D3dkmtOpenAdapterByCapturedNtDeviceName(DeviceName, &Captured.hAdapter, &Captured.AdapterLuid);
    if (!NT_SUCCESS(Status))
        return Status;
    Captured.VidPnSourceId = 0;
    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForWrite(unnamedParam1, sizeof(Captured), 1);
        *unnamedParam1 = Captured;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
        D3dkmtCloseCapturedAdapter(Captured.hAdapter);
    return Status;
}


NTSTATUS
APIENTRY
NtGdiDdDDIOpenAdapterFromDeviceName(_Inout_ D3DKMT_OPENADAPTERFROMDEVICENAME* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTOpenAdapterFromDeviceName(unnamedParam1);
}


/*
 * The following APIs all have the same idea.
 * Most of the parameters are stuffed in custom typedefs with a bunch of types inside them.
 * The idea here is this:
 * D3DKMT calls are routed only after the win32k<->dxgkrnl bridge is ready.
 * Missing dxgkrnl returns the bridge status; a missing callback returns
 * STATUS_PROCEDURE_NOT_FOUND.
 *
 * This essentially means the Dxgkrnl interface was never made as Win32k doesn't do any handling for these routines.
 */

NTSTATUS
APIENTRY
NtGdiDdDDICreateAllocation(_Inout_ D3DKMT_CREATEALLOCATION* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnCreateAllocation, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICheckMonitorPowerState(_In_ const D3DKMT_CHECKMONITORPOWERSTATE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnCheckMonitorPowerState, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICheckOcclusion(_In_ const D3DKMT_CHECKOCCLUSION* unnamedParam1)
{
    D3DKMT_CHECKOCCLUSION Captured;
    BOOL bEntered;
    PWND pWnd;
    NTSTATUS Status;

    Status = D3dkmtCaptureUserStructure(unnamedParam1, sizeof(Captured), &Captured);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.hWindow == NULL)
        return STATUS_INVALID_HANDLE;

    /*
     * Validate the window handle before forwarding: a bogus HWND must be
     * refused (the miniport cannot validate a user handle). Take the shared
     * USER lock around the lookup, then release it before the bridge call.
     */
    bEntered = UserIsEntered();
    if (!bEntered)
        UserEnterShared();
    pWnd = ValidateHwndNoErr(Captured.hWindow);
    if (!bEntered)
        UserLeave();
    if (pWnd == NULL)
        return STATUS_INVALID_HANDLE;
    Status = WddmBridgeRequireReady();
    if (!NT_SUCCESS(Status))
        return Status;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_CHECKOCCLUSION, &Captured, sizeof(Captured), NULL, 0);
}


NTSTATUS
APIENTRY
NtGdiDdDDICloseAdapter(_In_ const D3DKMT_CLOSEADAPTER* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnCloseAdapter, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateContext(_Inout_ D3DKMT_CREATECONTEXT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnCreateContext, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateDevice(_Inout_ D3DKMT_CREATEDEVICE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnCreateDevice, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateOverlay(_Inout_ D3DKMT_CREATEOVERLAY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnCreateOverlay, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateSynchronizationObject(_Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnCreateSynchronizationObject, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyAllocation(_In_ const D3DKMT_DESTROYALLOCATION* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnDestroyAllocation, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyContext(_In_ const D3DKMT_DESTROYCONTEXT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnDestroyContext, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyDevice(_In_ const D3DKMT_DESTROYDEVICE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnDestroyDevice, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyOverlay(_In_ const D3DKMT_DESTROYOVERLAY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnDestroyOverlay, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroySynchronizationObject(_In_ const D3DKMT_DESTROYSYNCHRONIZATIONOBJECT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnDestroySynchronizationObject, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIEscape(_In_ const D3DKMT_ESCAPE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnEscape, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIFlipOverlay(_In_ const D3DKMT_FLIPOVERLAY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnFlipOverlay, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetContextSchedulingPriority(_Inout_ D3DKMT_GETCONTEXTSCHEDULINGPRIORITY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnGetContextSchedulingPriority, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetDeviceState(_Inout_ D3DKMT_GETDEVICESTATE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnGetDeviceState, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetDisplayModeList(_Inout_ D3DKMT_GETDISPLAYMODELIST* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnGetDisplayModeList, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetMultisampleMethodList(_Inout_ D3DKMT_GETMULTISAMPLEMETHODLIST* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnGetMultisampleMethodList, unnamedParam1);
}

/*
 * Wrap a GDI device context around memory the caller already owns.
 *
 * NOTE ON LAYER: on Windows `D3DKMTCreateDCFromMemory` is a *user-mode* gdi32
 * function -- CreateDIBSection plus CreateCompatibleDC -- and issues no syscall
 * of its own.  ReactOS carries a slot for it in w32ksvc64.h because that table
 * mirrors the full Windows syscall numbering, not because anything dispatches
 * through it.  These kernel entries are therefore unreachable by design and are
 * kept only so the DDI has an implementation to grow from if the split ever
 * changes; the working implementation belongs in gdi32.
 *
 * The D3D runtime uses this to hand a CPU-accessible surface to code that
 * speaks GDI -- a locked allocation, say, that something wants to BitBlt out
 * of.  The bitmap is created *over* the caller's pointer rather than copying:
 * that is the entire point, since a copy would defeat having locked the
 * surface in the first place.
 *
 * Both exports existed with a syscall slot and no implementation behind them.
 */
static ULONG
D3dkmtBitmapFormatFromDdi(
    _In_ D3DDDIFORMAT Format)
{
    switch (Format)
    {
        case D3DDDIFMT_A8R8G8B8:
        case D3DDDIFMT_X8R8G8B8:
            return BMF_32BPP;
        case D3DDDIFMT_R5G6B5:
        case D3DDDIFMT_X1R5G5B5:
        case D3DDDIFMT_A1R5G5B5:
            return BMF_16BPP;
        case D3DDDIFMT_R8G8B8:
            return BMF_24BPP;
        case D3DDDIFMT_P8:
            return BMF_8BPP;
        default:
            /* Anything else has no GDI surface format to be, and guessing one
             * would have GDI read the memory with the wrong stride. */
            return 0;
    }
}

__kernel_entry
DWORD
APIENTRY
NtGdiDdDDICreateDCFromMemory(_Inout_ D3DKMT_CREATEDCFROMMEMORY* unnamedParam1)
{
    D3DKMT_CREATEDCFROMMEMORY Captured;
    ULONG BitmapFormat;
    ULONG MinimumPitch;
    HBITMAP hBitmap;
    HBITMAP hOldBitmap;
    HDC hDc;
    NTSTATUS Status;

    RETURN_STATUS_IF_NULL(unnamedParam1);

    _SEH2_TRY
    {
        ProbeForWrite(unnamedParam1, sizeof(Captured), 1);
        Captured = *unnamedParam1;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Captured.pMemory == NULL || Captured.Width == 0 || Captured.Height == 0)
        return STATUS_INVALID_PARAMETER;

    BitmapFormat = D3dkmtBitmapFormatFromDdi(Captured.Format);
    if (BitmapFormat == 0)
        return STATUS_INVALID_PARAMETER;

    /*
     * The pitch has to cover a row at this format and width.  A pitch shorter
     * than that makes every row after the first start inside the previous one,
     * so GDI would read and write memory the caller never described -- and it
     * is the caller's own buffer, so nothing else would catch it.
     */
    MinimumPitch = Captured.Width * (BitmapFormat == BMF_32BPP ? 4 :
                                     BitmapFormat == BMF_24BPP ? 3 :
                                     BitmapFormat == BMF_16BPP ? 2 : 1);
    if (Captured.Pitch < MinimumPitch)
        return STATUS_INVALID_PARAMETER;

    hDc = GreCreateCompatibleDC(Captured.hDeviceDc, FALSE);
    if (hDc == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    hBitmap = GreCreateBitmapEx(Captured.Width,
                                Captured.Height,
                                Captured.Pitch,
                                BitmapFormat,
                                0,
                                Captured.Pitch * Captured.Height,
                                Captured.pMemory,
                                0);
    if (hBitmap == NULL)
    {
        GreDeleteObject(hDc);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    hOldBitmap = NtGdiSelectBitmap(hDc, hBitmap);
    if (hOldBitmap == NULL)
    {
        GreDeleteObject(hBitmap);
        GreDeleteObject(hDc);
        return STATUS_UNSUCCESSFUL;
    }

    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        unnamedParam1->hDc = hDc;
        unnamedParam1->hBitmap = hBitmap;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        /* The caller never received the handles, so nothing else can free
         * them -- they would leak for the life of the process. */
        NtGdiSelectBitmap(hDc, hOldBitmap);
        GreDeleteObject(hBitmap);
        GreDeleteObject(hDc);
    }
    return Status;
}

__kernel_entry
DWORD
APIENTRY
NtGdiDdDDIDestroyDCFromMemory(_In_ CONST D3DKMT_DESTROYDCFROMMEMORY* unnamedParam1)
{
    D3DKMT_DESTROYDCFROMMEMORY Captured;

    RETURN_STATUS_IF_NULL(unnamedParam1);

    _SEH2_TRY
    {
        ProbeForRead(unnamedParam1, sizeof(Captured), 1);
        Captured = *unnamedParam1;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Captured.hDc == NULL || Captured.hBitmap == NULL)
        return STATUS_INVALID_PARAMETER;

    /*
     * Only the DC and the bitmap are ours to release.  The pixels belong to the
     * caller and were never copied, so freeing them here would destroy memory
     * that is still in use.
     */
    if (!GreDeleteObject(Captured.hBitmap))
        return STATUS_INVALID_PARAMETER;
    if (!GreDeleteObject(Captured.hDc))
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetPresentHistory(_Inout_ D3DKMT_GETPRESENTHISTORY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnGetPresentHistory, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetRuntimeData(_In_ const D3DKMT_GETRUNTIMEDATA* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnGetRuntimeData, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetScanLine(_In_ D3DKMT_GETSCANLINE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnGetScanLine, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetSharedPrimaryHandle(_Inout_ D3DKMT_GETSHAREDPRIMARYHANDLE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnGetSharedPrimaryHandle, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIInvalidateActiveVidPn(_In_ const D3DKMT_INVALIDATEACTIVEVIDPN* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnInvalidateActiveVidPn, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDILock(_Inout_ D3DKMT_LOCK* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnLock, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenResource(_Inout_ D3DKMT_OPENRESOURCE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnOpenResource, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIPollDisplayChildren(_In_ const D3DKMT_POLLDISPLAYCHILDREN* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnPollDisplayChildren, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIPresent(_In_ D3DKMT_PRESENT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnPresent, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryAdapterInfo(_Inout_ const D3DKMT_QUERYADAPTERINFO* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnQueryAdapterInfo, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryAllocationResidency(_In_ const D3DKMT_QUERYALLOCATIONRESIDENCY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnQueryAllocationResidency, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryResourceInfo(_Inout_ D3DKMT_QUERYRESOURCEINFO* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnQueryResourceInfo, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryStatistics(_Inout_ const D3DKMT_QUERYSTATISTICS* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnQueryStatistics, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIReleaseProcessVidPnSourceOwners(_In_ HANDLE unnamedParam1)
{
    D3DKMT_CALL_HANDLE_CALLBACK(RxgkIntPfnReleaseProcessVidPnSourceOwners, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIRender(_In_ D3DKMT_RENDER* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnRender, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetAllocationPriority(_In_ const D3DKMT_SETALLOCATIONPRIORITY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSetAllocationPriority, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetContextSchedulingPriority(_In_ const D3DKMT_SETCONTEXTSCHEDULINGPRIORITY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSetContextSchedulingPriority, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetDisplayMode(_In_ const D3DKMT_SETDISPLAYMODE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSetDisplayMode, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetDisplayPrivateDriverFormat(_In_ const D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSetDisplayPrivateDriverFormat, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetGammaRamp(_In_ const D3DKMT_SETGAMMARAMP* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSetGammaRamp, unnamedParam1);
}


NTSTATUS
APIENTRY
NtGdiDdDDISetQueuedLimit(_Inout_ const D3DKMT_SETQUEUEDLIMIT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSetQueuedLimit, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetVidPnSourceOwner(_In_ const D3DKMT_SETVIDPNSOURCEOWNER* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSetVidPnSourceOwner, unnamedParam1);
}

NTSTATUS
WINAPI
NtGdiDdDDIUnlock(_In_ const D3DKMT_UNLOCK* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnUnlock, unnamedParam1);
}

/* Win7 WDDM 1.1 additions */

NTSTATUS
APIENTRY
NtGdiDdDDICheckVidPnExclusiveOwnership(_In_ const D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP* unnamedParam1)
{
    NTSTATUS Status;

    RETURN_STATUS_IF_NULL(unnamedParam1);

    Status = WddmBridgeRequireReady();
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTCheckVidPnExclusiveOwnership(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateAllocation2(_Inout_ D3DKMT_CREATEALLOCATION* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    /* The bridge captures the complete top-level structure before validating
     * any field and preserves INFO2 identity before accessing the array. */
    D3DKMT_CALL_CALLBACK(RxgkIntPfnCreateAllocation2, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenResource2(_Inout_ D3DKMT_OPENRESOURCE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnOpenResource, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateSynchronizationObject2(_Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT2* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnCreateSynchronizationObject2, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenSynchronizationObject(_Inout_ D3DKMT_OPENSYNCHRONIZATIONOBJECT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnOpenSynchronizationObject, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIWaitForSynchronizationObject2(_In_ const D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    /* The bridge owns top-level capture and all field validation. */
    D3DKMT_CALL_CALLBACK(RxgkIntPfnWaitForSynchronizationObject2, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISignalSynchronizationObject2(_In_ const D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    /* The bridge owns top-level capture and all field validation. */
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSignalSynchronizationObject2, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateKeyedMutex(_Inout_ D3DKMT_CREATEKEYEDMUTEX* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTCreateKeyedMutex(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenKeyedMutex(_Inout_ D3DKMT_OPENKEYEDMUTEX* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTOpenKeyedMutex(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyKeyedMutex(_In_ const D3DKMT_DESTROYKEYEDMUTEX* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTDestroyKeyedMutex(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIAcquireKeyedMutex(_Inout_ D3DKMT_ACQUIREKEYEDMUTEX* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTAcquireKeyedMutex(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIReleaseKeyedMutex(_Inout_ D3DKMT_RELEASEKEYEDMUTEX* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTReleaseKeyedMutex(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetOverlayState(_Inout_ D3DKMT_GETOVERLAYSTATE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDICheckSharedResourceAccess(_In_ const D3DKMT_CHECKSHAREDRESOURCEACCESS* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIConfigureSharedResource(_In_ const D3DKMT_CONFIGURESHAREDRESOURCE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetPresentQueueEvent(_In_ D3DKMT_HANDLE hAdapter,
                               _Inout_ HANDLE* unnamedParam2)
{
    if (!unnamedParam2)
        return STATUS_INVALID_PARAMETER;
    D3DKMT_REQUIRE_HANDLE(hAdapter);
    /* Win11 refuses this to callers that do not own the present queue; not
     * implementing it is honest, answering NOT_IMPLEMENTED is not. */
    return STATUS_ACCESS_DENIED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIUpdateOverlay(_In_ const D3DKMT_UPDATEOVERLAY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnUpdateOverlay, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIWaitForIdle(_In_ const D3DKMT_WAITFORIDLE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnWaitForIdle, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIWaitForSynchronizationObject(_In_ const D3DKMT_WAITFORSYNCHRONIZATIONOBJECT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnWaitForSynchronizationObject, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIWaitForVerticalBlankEvent(_In_ const D3DKMT_WAITFORVERTICALBLANKEVENT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnWaitForVerticalBlankEvent, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISignalSynchronizationObject(_In_ const D3DKMT_SIGNALSYNCHRONIZATIONOBJECT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSignalSynchronizationObject, unnamedParam1);
}

/* ---- WDDM 1.2 additions ------------------------------------------------ */

NTSTATUS
APIENTRY
NtGdiDdDDIEnumAdapters(_Inout_ D3DKMT_ENUMADAPTERS* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTEnumAdapters(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIEnumAdapters2(_Inout_ D3DKMT_ENUMADAPTERS2* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTEnumAdapters2(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenAdapterFromLuid(_Inout_ D3DKMT_OPENADAPTERFROMLUID* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTOpenAdapterFromLuid(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIOfferAllocations(_In_ const D3DKMT_OFFERALLOCATIONS* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    /* The bridge owns top-level capture and all field validation. */
    D3DKMT_CALL_CALLBACK(RxgkIntPfnOfferAllocations, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIReclaimAllocations(_Inout_ D3DKMT_RECLAIMALLOCATIONS* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    /* The bridge owns top-level capture and all field validation. */
    D3DKMT_CALL_CALLBACK(RxgkIntPfnReclaimAllocations, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetVidPnSourceOwner1(_In_ const D3DKMT_SETVIDPNSOURCEOWNER1* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    /* The bridge owns top-level capture and all field validation. */
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSetVidPnSourceOwner1, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIWaitForVerticalBlankEvent2(_In_ const D3DKMT_WAITFORVERTICALBLANKEVENT2* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    /* The bridge owns top-level capture and all field validation. */
    D3DKMT_CALL_CALLBACK(RxgkIntPfnWaitForVerticalBlankEvent2, unnamedParam1);
}

/* ---- WDDM 2.0 additions ------------------------------------------------ */

NTSTATUS
APIENTRY
NtGdiDdDDIMakeResident(_Inout_ D3DDDI_MAKERESIDENT* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnMakeResident, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIEvict(_Inout_ D3DKMT_EVICT* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnEvict, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryVideoMemoryInfo(_Inout_ D3DKMT_QUERYVIDEOMEMORYINFO* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnQueryVideoMemoryInfo, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreatePagingQueue(_Inout_ D3DKMT_CREATEPAGINGQUEUE* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnCreatePagingQueue, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyPagingQueue(_Inout_ D3DDDI_DESTROYPAGINGQUEUE* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnDestroyPagingQueue, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIReserveGpuVirtualAddress(_Inout_ D3DDDI_RESERVEGPUVIRTUALADDRESS* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnReserveGpuVirtualAddress, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIMapGpuVirtualAddress(_Inout_ D3DDDI_MAPGPUVIRTUALADDRESS* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnMapGpuVirtualAddress, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIFreeGpuVirtualAddress(_In_ const D3DKMT_FREEGPUVIRTUALADDRESS* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnFreeGpuVirtualAddress, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIUpdateGpuVirtualAddress(_In_ const D3DKMT_UPDATEGPUVIRTUALADDRESS* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnUpdateGpuVirtualAddress, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIWaitForSynchronizationObjectFromCpu(_In_ const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnWaitForSynchronizationObjectFromCpu, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISignalSynchronizationObjectFromCpu(_In_ const D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSignalSynchronizationObjectFromCpu, unnamedParam1);
}

/* ---- WDDM 2.x contracts ------------------------------------------------- */

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyAllocation2(_In_ const struct _D3DKMT_DESTROYALLOCATION2* unnamedParam1)
{
    NTSTATUS Status;

    Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;
    return D3DKMTDestroyAllocation2(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDILock2(_Inout_ struct _D3DKMT_LOCK2* unnamedParam1)
{
    NTSTATUS Status;

    Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;
    return D3DKMTLock2(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIUnlock2(_In_ const struct _D3DKMT_UNLOCK2* unnamedParam1)
{
    NTSTATUS Status;

    Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;
    return D3DKMTUnlock2(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateHwQueue(_Inout_ struct _D3DKMT_CREATEHWQUEUE* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTCreateHwQueue(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyHwQueue(_In_ const struct _D3DKMT_DESTROYHWQUEUE* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTDestroyHwQueue(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISubmitCommandToHwQueue(_In_ const struct _D3DKMT_SUBMITCOMMANDTOHWQUEUE* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTSubmitCommandToHwQueue(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISubmitCommand(_In_ const struct _D3DKMT_SUBMITCOMMAND* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSubmitCommand, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetAllocationPriority(_In_ const struct _D3DKMT_GETALLOCATIONPRIORITY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnGetAllocationPriority, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIUpdateAllocationProperty(_Inout_ struct D3DDDI_UPDATEALLOCPROPERTY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIChangeVideoMemoryReservation(_In_ const struct _D3DKMT_CHANGEVIDEOMMEMORYRESERVATION* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTChangeVideoMemoryReservation(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetStablePowerState(_In_ const struct _D3DKMT_SETSTABLEPOWERSTATE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    /* Win11 refuses this without developer mode; not implementing it is honest, answering NOT_IMPLEMENTED is not. */
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryClockCalibration(_Inout_ struct _D3DKMT_QUERYCLOCKCALIBRATION* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTQueryClockCalibration(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetContextInProcessSchedulingPriority(_In_ const struct _D3DKMT_SETCONTEXTINPROCESSSCHEDULINGPRIORITY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetContextInProcessSchedulingPriority(_Inout_ struct _D3DKMT_GETCONTEXTINPROCESSSCHEDULINGPRIORITY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIRegisterTrimNotification(_Inout_ struct _D3DKMT_REGISTERTRIMNOTIFICATION* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTRegisterTrimNotification(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIUnregisterTrimNotification(_Inout_ struct _D3DKMT_UNREGISTERTRIMNOTIFICATION* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTUnregisterTrimNotification(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIRegisterBudgetChangeNotification(_Inout_ struct _D3DKMT_REGISTERBUDGETCHANGENOTIFICATION* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIUnregisterBudgetChangeNotification(_Inout_ struct _D3DKMT_UNREGISTERBUDGETCHANGENOTIFICATION* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIInvalidateCache(_In_ const struct _D3DKMT_INVALIDATECACHE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetSharedResourceAdapterLuid(_Inout_ struct _D3DKMT_GETSHAREDRESOURCEADAPTERLUID* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenResourceFromNtHandle(_Inout_ struct _D3DKMT_OPENRESOURCEFROMNTHANDLE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryResourceInfoFromNtHandle(_Inout_ struct _D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenSyncObjectFromNtHandle(_Inout_ struct _D3DKMT_OPENSYNCOBJECTFROMNTHANDLE* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenSyncObjectFromNtHandle2(_Inout_ struct _D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenSyncObjectNtHandleFromName(_Inout_ struct _D3DKMT_OPENSYNCOBJECTNTHANDLEFROMNAME* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenNtHandleFromName(_Inout_ struct _D3DKMT_OPENNTHANDLEFROMNAME* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateOutputDupl(_In_ const struct _D3DKMT_CREATE_OUTPUTDUPL* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyOutputDupl(_In_ const struct _D3DKMT_DESTROY_OUTPUTDUPL* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOutputDuplGetFrameInfo(_Inout_ struct _D3DKMT_OUTPUTDUPL_GET_FRAMEINFO* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOutputDuplGetMetaData(_Inout_ struct _D3DKMT_OUTPUTDUPL_METADATA* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOutputDuplGetPointerShapeData(_Inout_ struct _D3DKMT_OUTPUTDUPL_GET_POINTER_SHAPE_DATA* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOutputDuplPresent(_In_ const struct _D3DKMT_OUTPUTDUPLPRESENT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOutputDuplReleaseFrame(_In_ const struct _D3DKMT_OUTPUTDUPL_RELEASE_FRAME* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIPresentMultiPlaneOverlay(_In_ const struct _D3DKMT_PRESENT_MULTIPLANE_OVERLAY* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDICheckMultiPlaneOverlaySupport(_Inout_ struct _D3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    /* Win11 refuses the query on an adapter without MPO; not implementing it is honest, answering NOT_IMPLEMENTED is not. */
    return STATUS_INVALID_PARAMETER;
}

/* ---- WDDM 2.x extended contract stubs (v2 keyed mutex / MPO / misc) ---- */

NTSTATUS
APIENTRY
NtGdiDdDDIEnumAdapters3(_Inout_ struct _D3DKMT_ENUMADAPTERS3* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTEnumAdapters3(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIAcquireKeyedMutex2(_Inout_ struct _D3DKMT_ACQUIREKEYEDMUTEX2* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTAcquireKeyedMutex2(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateKeyedMutex2(_Inout_ struct _D3DKMT_CREATEKEYEDMUTEX2* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTCreateKeyedMutex2(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenKeyedMutex2(_Inout_ struct _D3DKMT_OPENKEYEDMUTEX2* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTOpenKeyedMutex2(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIReleaseKeyedMutex2(_Inout_ struct _D3DKMT_RELEASEKEYEDMUTEX2* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;

    return D3DKMTReleaseKeyedMutex2(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICancelPresents(_In_ const struct _D3DKMT_CANCEL_PRESENTS* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateContextVirtual(_Inout_ struct _D3DKMT_CREATECONTEXTVIRTUAL* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    D3DKMT_CALL_CALLBACK(RxgkIntPfnCreateContextVirtual, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetDWMVerticalBlankEvent(_In_ const struct _D3DKMT_GETVERTICALBLANKEVENT* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetMultiPlaneOverlayCaps(_Inout_ struct _D3DKMT_GET_MULTIPLANE_OVERLAY_CAPS* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryVidPnExclusiveOwnership(_Inout_ struct _D3DKMT_QUERYVIDPNEXCLUSIVEOWNERSHIP* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDISetSyncRefreshCountWaitTarget(_In_ const struct _D3DKMT_SETSYNCREFRESHCOUNTWAITTARGET* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDISetVidPnSourceOwner2(_In_ const struct _D3DKMT_SETVIDPNSOURCEOWNER2* unnamedParam1)
{
    NTSTATUS Status = D3dkmtValidateWddmThunk(unnamedParam1);
    if (!NT_SUCCESS(Status))
        return Status;
    return D3DKMTSetVidPnSourceOwner2(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISignalSynchronizationObjectFromGpu(_In_ const struct _D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSignalSynchronizationObjectFromGpu, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIWaitForSynchronizationObjectFromGpu(_In_ const struct _D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnWaitForSynchronizationObjectFromGpu, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISignalSynchronizationObjectFromGpu2(_In_ const struct _D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2* unnamedParam1)
{
    D3DKMT_CALL_CALLBACK(RxgkIntPfnSignalSynchronizationObjectFromGpu2, unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICheckMultiPlaneOverlaySupport2(_Inout_ struct _D3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT2* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    /* Win11 refuses the query on an adapter without MPO; not implementing it is honest, answering NOT_IMPLEMENTED is not. */
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS
APIENTRY
NtGdiDdDDICheckMultiPlaneOverlaySupport3(_Inout_ struct _D3DKMT_CHECKMULTIPLANEOVERLAYSUPPORT3* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIPresentMultiPlaneOverlay2(_In_ const struct _D3DKMT_PRESENT_MULTIPLANE_OVERLAY2* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
APIENTRY
NtGdiDdDDIPresentMultiPlaneOverlay3(_In_ const struct _D3DKMT_PRESENT_MULTIPLANE_OVERLAY3* unnamedParam1)
{
    RETURN_STATUS_IF_NULL(unnamedParam1);
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * D3DKMTShareObjects — multi-argument NT-handle sharing entry point.
 * Validate the documented argument contract (cObjects in [1,N], non-NULL
 * object list and output handle), then report the operation as unimplemented.
 */
NTSTATUS
APIENTRY
NtGdiDdDDIShareObjects(_In_ UINT cObjects,
                       _In_reads_(cObjects) const D3DKMT_HANDLE* hObjects,
                       _In_ PVOID pObjectAttributes,
                       _In_ DWORD dwDesiredAccess,
                       _Out_ HANDLE* phSharedNtHandle)
{
    UNREFERENCED_PARAMETER(pObjectAttributes);
    UNREFERENCED_PARAMETER(dwDesiredAccess);

    if (cObjects == 0 || hObjects == NULL || phSharedNtHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_NOT_IMPLEMENTED;
}
