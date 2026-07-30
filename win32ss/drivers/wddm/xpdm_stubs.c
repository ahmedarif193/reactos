/*
 * PROJECT:     ReactOS Display Driver Model - Win32k/dxgkrnl Bridge
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     XPDM link stubs for the always-built win32k D3DKMT thunks
 * COPYRIGHT:   Copyright 2026 ReactOS Contributors
 */

#include <ntifs.h>
#include <windef.h>
#include <d3dkmthk.h>

/*
 * XPDM does not have a dxgkrnl bridge. Keep only the symbols referenced by
 * win32k and avoid pulling the full WDDM implementation or its selected
 * WDDM 3.x layouts into an XPDM build.
 */

typedef struct _REACTOS_WIN32K_DXGKRNL_INTERFACE
    *PREACTOS_WIN32K_DXGKRNL_INTERFACE;

/*
 * These structures belong to DDI levels newer than XPDM's header selection.
 * Their contents are deliberately unavailable here; pointer declarations are
 * sufficient to preserve the callers' C ABI without importing newer layouts.
 */
struct _D3DKMT_ACQUIREKEYEDMUTEX2;
/* The doubled "M" is the public WDK tag spelling; keep tag identity exact. */
struct _D3DKMT_CHANGEVIDEOMMEMORYRESERVATION;
struct _D3DKMT_CREATEHWQUEUE;
struct _D3DKMT_CREATEKEYEDMUTEX2;
struct _D3DKMT_DESTROYALLOCATION2;
struct _D3DKMT_DESTROYHWQUEUE;
struct _D3DKMT_ENUMADAPTERS;
struct _D3DKMT_ENUMADAPTERS2;
struct _D3DKMT_ENUMADAPTERS3;
struct _D3DKMT_ISFEATUREENABLED;
struct _D3DKMT_LOCK2;
struct _D3DKMT_OPENADAPTERFROMLUID;
struct _D3DKMT_OPENKEYEDMUTEX2;
struct _D3DKMT_QUERYCLOCKCALIBRATION;
struct _D3DKMT_REGISTERTRIMNOTIFICATION;
struct _D3DKMT_RELEASEKEYEDMUTEX2;
struct _D3DKMT_SETVIDPNSOURCEOWNER2;
struct _D3DKMT_SUBMITCOMMANDTOHWQUEUE;
struct _D3DKMT_UNLOCK2;
struct _D3DKMT_UNREGISTERTRIMNOTIFICATION;

#define DEFINE_XPDM_D3DKMT_STUB(Name, Annotation, Type) \
    NTSTATUS                                             \
    APIENTRY                                             \
    Name(                                                \
        Annotation Type *pData)                          \
    {                                                    \
        UNREFERENCED_PARAMETER(pData);                   \
        return STATUS_NOT_SUPPORTED;                     \
    }

DEFINE_XPDM_D3DKMT_STUB(D3DKMTAcquireKeyedMutex,
                        _Inout_,
                        struct _D3DKMT_ACQUIREKEYEDMUTEX)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTAcquireKeyedMutex2,
                        _Inout_,
                        struct _D3DKMT_ACQUIREKEYEDMUTEX2)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTChangeVideoMemoryReservation,
                        _In_,
                        CONST struct _D3DKMT_CHANGEVIDEOMMEMORYRESERVATION)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTCheckVidPnExclusiveOwnership,
                        _In_,
                        CONST struct _D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTCreateHwQueue,
                        _Inout_,
                        struct _D3DKMT_CREATEHWQUEUE)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTCreateKeyedMutex,
                        _Inout_,
                        struct _D3DKMT_CREATEKEYEDMUTEX)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTCreateKeyedMutex2,
                        _Inout_,
                        struct _D3DKMT_CREATEKEYEDMUTEX2)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTDestroyAllocation2,
                        _In_,
                        CONST struct _D3DKMT_DESTROYALLOCATION2)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTDestroyHwQueue,
                        _In_,
                        CONST struct _D3DKMT_DESTROYHWQUEUE)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTDestroyKeyedMutex,
                        _In_,
                        CONST struct _D3DKMT_DESTROYKEYEDMUTEX)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTEnumAdapters,
                        _Inout_,
                        CONST struct _D3DKMT_ENUMADAPTERS)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTEnumAdapters2,
                        _Inout_,
                        CONST struct _D3DKMT_ENUMADAPTERS2)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTEnumAdapters3,
                        _Inout_,
                        struct _D3DKMT_ENUMADAPTERS3)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTIsFeatureEnabled,
                        _Inout_,
                        struct _D3DKMT_ISFEATUREENABLED)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTLock2,
                        _Inout_,
                        struct _D3DKMT_LOCK2)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTOpenAdapterFromDeviceName,
                        _Inout_,
                        struct _D3DKMT_OPENADAPTERFROMDEVICENAME)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTOpenAdapterFromLuid,
                        _Inout_,
                        CONST struct _D3DKMT_OPENADAPTERFROMLUID)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTOpenKeyedMutex,
                        _Inout_,
                        struct _D3DKMT_OPENKEYEDMUTEX)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTOpenKeyedMutex2,
                        _Inout_,
                        struct _D3DKMT_OPENKEYEDMUTEX2)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTQueryClockCalibration,
                        _Inout_,
                        struct _D3DKMT_QUERYCLOCKCALIBRATION)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTRegisterTrimNotification,
                        _Inout_,
                        struct _D3DKMT_REGISTERTRIMNOTIFICATION)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTReleaseKeyedMutex,
                        _Inout_,
                        struct _D3DKMT_RELEASEKEYEDMUTEX)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTReleaseKeyedMutex2,
                        _Inout_,
                        struct _D3DKMT_RELEASEKEYEDMUTEX2)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTSetVidPnSourceOwner2,
                        _In_,
                        CONST struct _D3DKMT_SETVIDPNSOURCEOWNER2)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTSubmitCommandToHwQueue,
                        _In_,
                        CONST struct _D3DKMT_SUBMITCOMMANDTOHWQUEUE)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTUnlock2,
                        _In_,
                        CONST struct _D3DKMT_UNLOCK2)
DEFINE_XPDM_D3DKMT_STUB(D3DKMTUnregisterTrimNotification,
                        _Inout_,
                        struct _D3DKMT_UNREGISTERTRIMNOTIFICATION)

NTSTATUS
APIENTRY
D3DKMTSetProcessSchedulingPriorityClass(
    _In_ HANDLE Process,
    _In_ D3DKMT_SCHEDULINGPRIORITYCLASS PriorityClass)
{
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(PriorityClass);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
APIENTRY
D3DKMTGetProcessSchedulingPriorityClass(
    _In_ HANDLE Process,
    _Out_ D3DKMT_SCHEDULINGPRIORITYCLASS *PriorityClass)
{
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(PriorityClass);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
WddmBridgeInit(VOID)
{
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
WddmBridgeRequireReady(VOID)
{
    return STATUS_NOT_SUPPORTED;
}

VOID
WddmBridgeInitCallbacks(
    _Out_ PREACTOS_WIN32K_DXGKRNL_INTERFACE Interface)
{
    UNREFERENCED_PARAMETER(Interface);
}

NTSTATUS
WddmBridgeSendIoctl(
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputSize)
{
    UNREFERENCED_PARAMETER(IoControlCode);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputSize);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputSize);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
WddmBridgeSendIoctlWithInformation(
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputSize,
    _Out_opt_ PULONG_PTR Information)
{
    UNREFERENCED_PARAMETER(IoControlCode);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputSize);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputSize);
    UNREFERENCED_PARAMETER(Information);
    return STATUS_NOT_SUPPORTED;
}
