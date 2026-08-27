/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Power framework component-state tests
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef struct _TEST_PO_FX_CONTEXT
{
    POHANDLE Handle;
    volatile LONG ActiveCount;
    volatile LONG IdleConditionCount;
    volatile LONG IdleStateCount;
    volatile LONG PowerRequiredCount;
    volatile LONG PowerNotRequiredCount;
    volatile LONG PowerControlCount;
    volatile LONG LastIdleState;
} TEST_PO_FX_CONTEXT, *PTEST_PO_FX_CONTEXT;

typedef struct _TEST_PO_FX_DEVICE
{
    PO_FX_DEVICE Device;
} TEST_PO_FX_DEVICE, *PTEST_PO_FX_DEVICE;

static const GUID TestPoFxPowerControlGuid = {0x30d22499, 0xf635, 0x4b61, {0x88, 0xc5, 0x4f, 0x62, 0x73, 0x7a, 0xc9, 0x9d}};

NTSTATUS
NTAPI
PoFxAddComponentRelation(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ PDEVICE_OBJECT RelatedDevice,
    _In_opt_ PVOID RelationContext);

NTSTATUS
NTAPI
PoFxRemoveComponentRelation(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ PDEVICE_OBJECT RelatedDevice,
    _In_opt_ PVOID RelationContext);

VOID
NTAPI
PoFxCompleteDirectedPowerDown(
    _In_ POHANDLE Handle);

static
VOID
NTAPI
TestPoFxActiveCallback(
    _In_ PVOID Context,
    _In_ ULONG Component)
{
    PTEST_PO_FX_CONTEXT TestContext = Context;

    TestContext->LastIdleState = Component;
    InterlockedIncrement(&TestContext->ActiveCount);
}

static
VOID
NTAPI
TestPoFxIdleConditionCallback(
    _In_ PVOID Context,
    _In_ ULONG Component)
{
    PTEST_PO_FX_CONTEXT TestContext = Context;

    InterlockedIncrement(&TestContext->IdleConditionCount);
    PoFxCompleteIdleCondition(TestContext->Handle, Component);
}

static
VOID
NTAPI
TestPoFxIdleStateCallback(
    _In_ PVOID Context,
    _In_ ULONG Component,
    _In_ ULONG State)
{
    PTEST_PO_FX_CONTEXT TestContext = Context;

    TestContext->LastIdleState = State;
    InterlockedIncrement(&TestContext->IdleStateCount);
    PoFxCompleteIdleState(TestContext->Handle, Component);
}

static
VOID
NTAPI
TestPoFxPowerRequiredCallback(
    _In_ PVOID Context)
{
    PTEST_PO_FX_CONTEXT TestContext = Context;

    InterlockedIncrement(&TestContext->PowerRequiredCount);
    PoFxReportDevicePoweredOn(TestContext->Handle);
}

static
VOID
NTAPI
TestPoFxPowerNotRequiredCallback(
    _In_ PVOID Context)
{
    PTEST_PO_FX_CONTEXT TestContext = Context;

    InterlockedIncrement(&TestContext->PowerNotRequiredCount);
    PoFxCompleteDevicePowerNotRequired(TestContext->Handle);
}

static
NTSTATUS
NTAPI
TestPoFxPowerControlCallback(
    _In_ PVOID Context,
    _In_ LPCGUID PowerControlCode,
    _In_opt_ PVOID InBuffer,
    _In_ SIZE_T InBufferSize,
    _Out_opt_ PVOID OutBuffer,
    _In_ SIZE_T OutBufferSize,
    _Out_opt_ PSIZE_T BytesReturned)
{
    PTEST_PO_FX_CONTEXT TestContext = Context;

    if (!IsEqualGUID(PowerControlCode, &TestPoFxPowerControlGuid) || (InBuffer == NULL) || (InBufferSize != sizeof(ULONG)) || (OutBuffer == NULL) || (OutBufferSize < sizeof(ULONG)))
        return STATUS_INVALID_PARAMETER;

    *(PULONG)OutBuffer = *(PULONG)InBuffer + 1;
    if (BytesReturned != NULL)
        *BytesReturned = sizeof(ULONG);
    InterlockedIncrement(&TestContext->PowerControlCount);
    return STATUS_SUCCESS;
}

START_TEST(PoFxState)
{
    TEST_PO_FX_DEVICE Registration;
    PO_FX_COMPONENT_IDLE_STATE IdleStates[2];
    TEST_PO_FX_CONTEXT Context;
    PDEVICE_OBJECT DeviceObject;
    SIZE_T BytesReturned;
    ULONG Input;
    ULONG Output;
    NTSTATUS Status;

    DeviceObject = KmtDriverObject->DeviceObject;
    if (skip(DeviceObject != NULL, "kmtest driver has no device object for PoFx registration\n"))
        return;

    RtlZeroMemory(&Registration, sizeof(Registration));
    RtlZeroMemory(&IdleStates, sizeof(IdleStates));
    RtlZeroMemory(&Context, sizeof(Context));
    Registration.Device.Version = PO_FX_VERSION;
    Registration.Device.ComponentCount = 1;
    Registration.Device.Components[0].IdleStateCount = RTL_NUMBER_OF(IdleStates);
    Registration.Device.Components[0].DeepestWakeableIdleState = 1;
    Registration.Device.Components[0].IdleStates = IdleStates;
    Registration.Device.PowerControlCallback = TestPoFxPowerControlCallback;
    Registration.Device.DeviceContext = &Context;
    Context.Handle = (POHANDLE)(ULONG_PTR)0xA5A5A5A5;
    Status = PoFxRegisterDevice(DeviceObject, &Registration.Device, &Context.Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_pointer(Context.Handle, (POHANDLE)(ULONG_PTR)0xA5A5A5A5);

    Registration.Device.ComponentActiveConditionCallback = TestPoFxActiveCallback;
    Registration.Device.ComponentIdleConditionCallback = TestPoFxIdleConditionCallback;
    Registration.Device.ComponentIdleStateCallback = TestPoFxIdleStateCallback;
    Registration.Device.DevicePowerRequiredCallback = TestPoFxPowerRequiredCallback;
    Registration.Device.DevicePowerNotRequiredCallback = TestPoFxPowerNotRequiredCallback;
    Status = PoFxRegisterDevice(DeviceObject, &Registration.Device, &Context.Handle);
    trace("PoFxRegisterDevice returned 0x%08lx, handle %p\n", Status, Context.Handle);
    if (skip(NT_SUCCESS(Status), "PoFx rejected the kmtest device object\n"))
        return;

    Registration.Device.PowerControlCallback = NULL;
    Registration.Device.DeviceContext = NULL;
    Registration.Device.Components[0].IdleStates = NULL;
    RtlFillMemory(IdleStates, sizeof(IdleStates), 0xA5);
    Input = 41;
    Output = 0;
    BytesReturned = 0;
    Status = PoFxPowerControl(Context.Handle, &TestPoFxPowerControlGuid, &Input, sizeof(Input), &Output, sizeof(Output), &BytesReturned);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Output, 42);
    ok_eq_size(BytesReturned, sizeof(Output));
    ok_eq_long(Context.PowerControlCount, 1);

    Status = PoFxAddComponentRelation(Context.Handle, 0, DeviceObject, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = PoFxRemoveComponentRelation(Context.Handle, 0, DeviceObject, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);

    PoFxSetComponentLatency(Context.Handle, 0, PO_FX_UNKNOWN_TIME);
    PoFxSetComponentResidency(Context.Handle, 0, PO_FX_UNKNOWN_TIME);
    PoFxActivateComponent(Context.Handle, 0, 0);
    PoFxStartDevicePowerManagement(Context.Handle);
    PoFxIdleComponent(Context.Handle, 0, 0);
    if (*(volatile ULONG *)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) == 0x8eac705)
    {
        ok_eq_long(Context.IdleConditionCount, 1);
        ok_eq_long(Context.IdleStateCount, 1);
        ok_eq_long(Context.LastIdleState, 1);
        ok_eq_long(Context.PowerNotRequiredCount, 1);
        PoFxActivateComponent(Context.Handle, 0, 0);
        ok_eq_long(Context.PowerRequiredCount, 1);
        ok_eq_long(Context.ActiveCount, 1);
        PoFxIdleComponent(Context.Handle, 0, 0);
    }

    PoFxCompleteDirectedPowerDown(Context.Handle);
    PoFxUnregisterDevice(Context.Handle);
}
