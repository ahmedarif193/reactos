/*
 * PROJECT:     ReactOS WFP callout test driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * FILE:        drivers/network/tests/wfpcallout/wfpcallout.c
 * PURPOSE:     Minimal WFP callout driver. Registers a classify callback
 *              at FWPS_LAYER_INBOUND_IPPACKET_V4 via FwpsCalloutRegister0.
 *              Link-tests the WFP kernel stub exports added to netio.sys
 *              on the dev-nt6-1 branch.
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors.
 */

#include <ntifs.h>
#include <ndis.h>
#include <fwpsk.h>

static UINT32 g_CalloutId = 0;

static VOID NTAPI
TestClassify(
    _In_ const FWPS_INCOMING_VALUES0* inFixedValues,
    _In_ const FWPS_INCOMING_METADATA_VALUES0* inMetaValues,
    _Inout_opt_ void* layerData,
    _In_opt_ const void* classifyContext,
    _In_ const FWPS_FILTER0* filter,
    _In_ UINT64 flowContext,
    _Inout_ FWPS_CLASSIFY_OUT0* classifyOut)
{
    UNREFERENCED_PARAMETER(inFixedValues);
    UNREFERENCED_PARAMETER(inMetaValues);
    UNREFERENCED_PARAMETER(layerData);
    UNREFERENCED_PARAMETER(classifyContext);
    UNREFERENCED_PARAMETER(filter);
    UNREFERENCED_PARAMETER(flowContext);

    /* Permit every packet */
    if (classifyOut)
    {
        classifyOut->actionType = FWP_ACTION_PERMIT;
        classifyOut->rights = 0;
    }
}

static NTSTATUS NTAPI
TestNotify(
    _In_ UINT32 notifyType,
    _In_ const GUID* filterKey,
    _Inout_ const FWPS_FILTER0* filter)
{
    UNREFERENCED_PARAMETER(notifyType);
    UNREFERENCED_PARAMETER(filterKey);
    UNREFERENCED_PARAMETER(filter);
    return STATUS_SUCCESS;
}

static VOID NTAPI
TestFlowDelete(
    _In_ UINT16 layerId,
    _In_ UINT32 calloutId,
    _In_ UINT64 flowContext)
{
    UNREFERENCED_PARAMETER(layerId);
    UNREFERENCED_PARAMETER(calloutId);
    UNREFERENCED_PARAMETER(flowContext);
}

static VOID NTAPI
TestUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    if (g_CalloutId != 0)
    {
        FwpsCalloutUnregisterById0(g_CalloutId);
        g_CalloutId = 0;
    }
}

NTSTATUS NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    FWPS_CALLOUT0 callout;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverUnload = TestUnload;

    RtlZeroMemory(&callout, sizeof(callout));
    /* Dummy callout GUID — real drivers use a stable key */
    callout.calloutKey.Data1 = 0xdeadbeef;
    callout.classifyFn = TestClassify;
    callout.notifyFn = TestNotify;
    callout.flowDeleteFn = TestFlowDelete;

    status = FwpsCalloutRegister0(DriverObject->DeviceObject, &callout, &g_CalloutId);
    return status;
}

/* EOF */
