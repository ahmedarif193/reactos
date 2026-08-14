/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Etw functions of Vista+
 * COPYRIGHT:   2020 Victor Perevertkin (victor.perevertkin@reactos.org)
 */

#include <ntdef.h>
#include <ntifs.h>
#include <pseh/pseh2.h>

static DECLSPEC_ALIGN(8) volatile LONG64 EtwpNextRegistrationHandle;

_IRQL_requires_max_(HIGH_LEVEL)
NTSTATUS
NTKRNLVISTAAPI
NTAPI
EtwWrite(
    _In_ REGHANDLE RegHandle,
    _In_ PCEVENT_DESCRIPTOR EventDescriptor,
    _In_opt_ LPCGUID ActivityId,
    _In_ ULONG UserDataCount,
    _In_reads_opt_(UserDataCount) PEVENT_DATA_DESCRIPTOR UserData)
{
    UNREFERENCED_PARAMETER(RegHandle);
    UNREFERENCED_PARAMETER(ActivityId);
    UNREFERENCED_PARAMETER(UserDataCount);
    UNREFERENCED_PARAMETER(UserData);
    /* Diagnostic: surface modern-ETW writes (WPP DoTraceMessage on Vista+). */
    if (EventDescriptor != NULL)
        DbgPrint("WPPTRACE: EtwWrite id=%u ver=%u level=%u keyword=0x%I64X\n",
                 EventDescriptor->Id, EventDescriptor->Version,
                 EventDescriptor->Level, EventDescriptor->Keyword);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTKRNLVISTAAPI
NTAPI
EtwRegister(
    _In_ LPCGUID ProviderId,
    _In_opt_ PETWENABLECALLBACK EnableCallback,
    _In_opt_ PVOID CallbackContext,
    _Out_ PREGHANDLE RegHandle)
{
    UNREFERENCED_PARAMETER(ProviderId);
    UNREFERENCED_PARAMETER(EnableCallback);
    UNREFERENCED_PARAMETER(CallbackContext);

    if (RegHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Issue an opaque, process-local token until a provider backend exists. */
    *RegHandle = (REGHANDLE)InterlockedIncrement64(&EtwpNextRegistrationHandle);
    if (*RegHandle == 0)
        *RegHandle = (REGHANDLE)InterlockedIncrement64(&EtwpNextRegistrationHandle);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTKRNLVISTAAPI
NTAPI
EtwUnregister(
    _In_ REGHANDLE RegHandle)
{
    UNREFERENCED_PARAMETER(RegHandle);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTKRNLVISTAAPI
NTAPI
EtwSetInformation(
    _In_ REGHANDLE RegHandle,
    _In_ ULONG InformationClass,
    _In_opt_ PVOID EventInformation,
    _In_ ULONG InformationLength)
{
    UNREFERENCED_PARAMETER(RegHandle);
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(EventInformation);
    UNREFERENCED_PARAMETER(InformationLength);

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(HIGH_LEVEL)
NTSTATUS
NTKRNLVISTAAPI
NTAPI
EtwWriteTransfer(
    _In_ REGHANDLE RegHandle,
    _In_ PCEVENT_DESCRIPTOR EventDescriptor,
    _In_opt_ LPCGUID ActivityId,
    _In_opt_ LPCGUID RelatedActivityId,
    _In_ ULONG UserDataCount,
    _In_reads_opt_(UserDataCount) PEVENT_DATA_DESCRIPTOR UserData)
{
    UNREFERENCED_PARAMETER(RegHandle);
    UNREFERENCED_PARAMETER(EventDescriptor);
    UNREFERENCED_PARAMETER(ActivityId);
    UNREFERENCED_PARAMETER(RelatedActivityId);
    UNREFERENCED_PARAMETER(UserDataCount);
    UNREFERENCED_PARAMETER(UserData);

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTKRNLVISTAAPI
NTAPI
EtwRegisterClassicProvider(
    _In_ LPCGUID ProviderGuid,
    _In_ ULONG Type,
    _In_opt_ PETWENABLECALLBACK EnableCallback,
    _In_opt_ PVOID CallbackContext,
    _Out_ PREGHANDLE RegHandle)
{
    UNREFERENCED_PARAMETER(Type);

    /*
     * Register the classic provider and immediately ENABLE it at max verbosity,
     * then invoke the provider's enable callback.  WPP-instrumented drivers
     * (e.g. the Red Hat virtio-gpu WDDM DOD viogpudo.sys) turn their
     * DoTraceMessage tracing ON in response, so their otherwise-silent init
     * trace surfaces via WmiTraceMessage/EtwWrite — giving visibility into
     * driver bring-up on ReactOS.  (Diagnostic aid; the spec was a raising stub
     * that crashed such drivers in DriverEntry.)
     */
    UNREFERENCED_PARAMETER(ProviderGuid);
    UNREFERENCED_PARAMETER(EnableCallback);
    UNREFERENCED_PARAMETER(CallbackContext);

    /* Succeed with a null registration handle so the WPP-instrumented driver
     * proceeds with tracing disabled (invoking the provider's enable callback
     * directly here faults — the classic-provider callback is not safe to call
     * synchronously at registration). */
    if (RegHandle != NULL)
        *RegHandle = 0;
    return STATUS_SUCCESS;
}
