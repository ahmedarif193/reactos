/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Public PoFx callback ordering and teardown
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <kmt_test.h>
#include "PoFxState_pnp.h"

enum
{
    TestIdleCondition = 0,
    TestIdleF1,
    TestPowerNotRequired,
    TestPowerRequired,
    TestIdleF0,
    TestActive,
    TestEventCount
};

typedef struct _TEST_PO_FX_CONTEXT
{
    POHANDLE Handle;
    KEVENT Events[TestEventCount];
    KEVENT UnregisterEntered;
    KEVENT UnregisterReturned;
    volatile LONG Counts[TestEventCount];
    volatile LONG Held;
    volatile LONG Pending;
    volatile LONG LastIdleState;
    volatile LONG PowerControlCount;
} TEST_PO_FX_CONTEXT, *PTEST_PO_FX_CONTEXT;

static const GUID TestControl = {0x30d22499, 0xf635, 0x4b61, {0x88, 0xc5, 0x4f, 0x62, 0x73, 0x7a, 0xc9, 0x9d}};

static VOID TestComplete(PTEST_PO_FX_CONTEXT Context, ULONG Event)
{
    LONG Bit = 1L << Event;

    if (!(InterlockedAnd(&Context->Pending, ~Bit) & Bit))
        return;
    switch (Event)
    {
        case TestIdleCondition:
            PoFxCompleteIdleCondition(Context->Handle, 0);
            break;
        case TestIdleF0:
        case TestIdleF1:
            PoFxCompleteIdleState(Context->Handle, 0);
            break;
        case TestPowerRequired:
            PoFxReportDevicePoweredOn(Context->Handle);
            break;
        case TestPowerNotRequired:
            PoFxCompleteDevicePowerNotRequired(Context->Handle);
            break;
    }
}

static VOID TestCallback(PTEST_PO_FX_CONTEXT Context, ULONG Event)
{
    InterlockedIncrement(&Context->Counts[Event]);
    if (Event != TestActive)
    {
        InterlockedOr(&Context->Pending, 1L << Event);
        if (!(Context->Held & (1L << Event)))
            TestComplete(Context, Event);
    }
    KeSetEvent(&Context->Events[Event], IO_NO_INCREMENT, FALSE);
}

static VOID NTAPI TestActiveCallback(PVOID Context, ULONG Component)
{
    ok_eq_ulong(Component, 0);
    TestCallback(Context, TestActive);
}

static VOID NTAPI TestIdleConditionCallback(PVOID Context, ULONG Component)
{
    ok_eq_ulong(Component, 0);
    TestCallback(Context, TestIdleCondition);
}

static VOID NTAPI TestIdleStateCallback(PVOID Parameter, ULONG Component, ULONG State)
{
    PTEST_PO_FX_CONTEXT Context = Parameter;

    ok_eq_ulong(Component, 0);
    ok(State < 2, "Unexpected F-state %lu\n", State);
    Context->LastIdleState = State;
    TestCallback(Context, State == 0 ? TestIdleF0 : TestIdleF1);
}

static VOID NTAPI TestPowerRequiredCallback(PVOID Context)
{
    TestCallback(Context, TestPowerRequired);
}

static VOID NTAPI TestPowerNotRequiredCallback(PVOID Context)
{
    TestCallback(Context, TestPowerNotRequired);
}

static NTSTATUS NTAPI TestPowerControlCallback(PVOID Parameter, LPCGUID Code,
    PVOID Input, SIZE_T InputSize, PVOID Output, SIZE_T OutputSize, PSIZE_T Returned)
{
    PTEST_PO_FX_CONTEXT Context = Parameter;
    UNREFERENCED_PARAMETER(Code);
    UNREFERENCED_PARAMETER(Input);
    UNREFERENCED_PARAMETER(InputSize);
    UNREFERENCED_PARAMETER(Output);
    UNREFERENCED_PARAMETER(OutputSize);
    UNREFERENCED_PARAMETER(Returned);
    InterlockedIncrement(&Context->PowerControlCount);
    return STATUS_NOT_IMPLEMENTED;
}

static BOOLEAN TestWait(PKEVENT Event, PCSTR Name)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    Timeout.QuadPart = -100000000;
    Status = KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout);
    ok(Status == STATUS_SUCCESS, "Waiting for %s returned %lx\n", Name, Status);
    return Status == STATUS_SUCCESS;
}

static BOOLEAN TestRegister(PDEVICE_OBJECT Pdo, PTEST_PO_FX_CONTEXT Context)
{
    PO_FX_DEVICE Registration;
    PO_FX_COMPONENT_IDLE_STATE IdleStates[2];
    NTSTATUS Status;
    ULONG Index;

    RtlZeroMemory(Context, sizeof(*Context));
    for (Index = 0; Index < TestEventCount; Index++)
        KeInitializeEvent(&Context->Events[Index], NotificationEvent, FALSE);
    KeInitializeEvent(&Context->UnregisterEntered, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->UnregisterReturned, NotificationEvent, FALSE);
    RtlZeroMemory(&Registration, sizeof(Registration));
    RtlZeroMemory(IdleStates, sizeof(IdleStates));
    Registration.Version = PO_FX_VERSION;
    Registration.DeviceContext = Context;
    Registration.ComponentCount = 1;
    Registration.Components[0].IdleStateCount = RTL_NUMBER_OF(IdleStates);
    Registration.Components[0].DeepestWakeableIdleState = 1;
    Registration.Components[0].IdleStates = IdleStates;
    Registration.PowerControlCallback = TestPowerControlCallback;

    Context->Handle = (POHANDLE)(ULONG_PTR)0xa5a5a5a5;
    Status = PoFxRegisterDevice(Pdo, &Registration, &Context->Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_pointer(Context->Handle, NULL);

    Registration.ComponentActiveConditionCallback = TestActiveCallback;
    Registration.ComponentIdleConditionCallback = TestIdleConditionCallback;
    Registration.ComponentIdleStateCallback = TestIdleStateCallback;
    Registration.DevicePowerRequiredCallback = TestPowerRequiredCallback;
    Registration.DevicePowerNotRequiredCallback = TestPowerNotRequiredCallback;
    Status = PoFxRegisterDevice(Pdo, &Registration, &Context->Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return FALSE;
    ok(Context->Handle != NULL, "No handle after successful registration\n");
    if (!Context->Handle)
        return FALSE;

    /* The framework must capture the caller's temporary registration tables. */
    RtlFillMemory(&Registration, sizeof(Registration), 0xa5);
    RtlFillMemory(IdleStates, sizeof(IdleStates), 0xa5);
    PoFxSetComponentLatency(Context->Handle, 0, PO_FX_UNKNOWN_TIME);
    PoFxSetComponentResidency(Context->Handle, 0, PO_FX_UNKNOWN_TIME);
    PoFxActivateComponent(Context->Handle, 0, 0);
    ok_eq_long(Context->Counts[TestActive], 0);
    ok_eq_long(Context->Counts[TestIdleF0], 0);
    PoFxStartDevicePowerManagement(Context->Handle);
    return TRUE;
}

static VOID TestReleaseHeld(PTEST_PO_FX_CONTEXT Context)
{
    ULONG Event;

    InterlockedExchange(&Context->Held, 0);
    for (Event = 0; Event < TestActive; Event++)
        TestComplete(Context, Event);
}

static VOID NTAPI TestUnregisterThread(PVOID Parameter)
{
    PTEST_PO_FX_CONTEXT Context = Parameter;

    KeSetEvent(&Context->UnregisterEntered, IO_NO_INCREMENT, FALSE);
    PoFxUnregisterDevice(Context->Handle);
    KeSetEvent(&Context->UnregisterReturned, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID TestDeferredUnregister(PDEVICE_OBJECT Pdo)
{
    TEST_PO_FX_CONTEXT Context;
    PKTHREAD Thread;
    NTSTATUS Status;
    ULONG Input = 41, Output = 0;
    SIZE_T Returned = 0;

    trace("PoFx deferred unregister\n");
    if (!TestRegister(Pdo, &Context))
        return;
    Status = PoFxPowerControl(Context.Handle, &TestControl, &Input, sizeof(Input), &Output, sizeof(Output), &Returned);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    ok_eq_ulong(Output, 0);
    ok_eq_size(Returned, 0);
    ok_eq_long(Context.PowerControlCount, 0);

    PoFxIdleComponent(Context.Handle, 0, 0);
    if (!TestWait(&Context.Events[TestPowerNotRequired], "device idle"))
        goto Cleanup;
    ok_eq_long(Context.Counts[TestIdleCondition], 1);
    ok_eq_long(Context.Counts[TestIdleF1], 1);
    ok_eq_long(Context.LastIdleState, 1);

    Context.Held = (1L << TestPowerRequired) | (1L << TestIdleF0);
    Thread = KmtStartThread(TestUnregisterThread, &Context);
    if (!Thread)
        goto Cleanup;
    TestWait(&Context.UnregisterEntered, "unregister entered");
    if (TestWait(&Context.Events[TestPowerRequired], "device power required"))
    {
        ok_eq_long(KeReadStateEvent(&Context.UnregisterReturned), 0);
        ok_eq_long(Context.Counts[TestIdleF0], 0);
        ok_eq_long(Context.Counts[TestActive], 0);
        TestComplete(&Context, TestPowerRequired);
        if (TestWait(&Context.Events[TestIdleF0], "component F0 request"))
        {
            ok_eq_long(KeReadStateEvent(&Context.UnregisterReturned), 0);
            ok_eq_long(Context.Counts[TestActive], 0);
            TestComplete(&Context, TestIdleF0);
        }
    }
    TestReleaseHeld(&Context);
    KmtFinishThread(Thread, NULL);
    ok_eq_long(KeReadStateEvent(&Context.UnregisterReturned), 1);
    ok_eq_long(Context.Counts[TestActive], 1);
    ok_eq_long(Context.Counts[TestIdleF0], 1);
    ok_eq_long(Context.LastIdleState, 0);
    ok_eq_long(Context.Pending, 0);
    return;

Cleanup:
    TestReleaseHeld(&Context);
    PoFxUnregisterDevice(Context.Handle);
}

static VOID TestActivateDuringIdle(PDEVICE_OBJECT Pdo, ULONG HeldEvent)
{
    TEST_PO_FX_CONTEXT Context;

    trace("PoFx activation while callback %lu is pending\n", HeldEvent);
    if (!TestRegister(Pdo, &Context))
        return;
    Context.Held = 1L << HeldEvent;
    PoFxIdleComponent(Context.Handle, 0, 0);
    if (!TestWait(&Context.Events[HeldEvent], "held idle callback"))
        goto Cleanup;
    PoFxActivateComponent(Context.Handle, 0, 0);
    ok_eq_long(Context.Counts[TestActive], 0);
    ok_eq_long(Context.Counts[TestPowerRequired], 0);
    ok_eq_long(Context.Counts[TestIdleF0], 0);
    TestComplete(&Context, HeldEvent);
    if (!TestWait(&Context.Events[TestActive], "active condition"))
        goto Cleanup;
    ok_eq_long(Context.Counts[TestActive], 1);
    ok_eq_long(Context.LastIdleState, 0);
    ok_eq_long(Context.Counts[TestIdleF1], HeldEvent == TestIdleCondition ? 0 : 1);
    ok_eq_long(Context.Counts[TestIdleF0], HeldEvent == TestIdleCondition ? 0 : 1);
    ok_eq_long(Context.Counts[TestPowerRequired], HeldEvent == TestPowerNotRequired ? 1 : 0);
    ok_eq_long(Context.Pending, 0);

Cleanup:
    TestReleaseHeld(&Context);
    PoFxUnregisterDevice(Context.Handle);
}

static VOID TestPoFxBody(PDEVICE_OBJECT Pdo)
{
    TestDeferredUnregister(Pdo);
    TestActivateDuringIdle(Pdo, TestIdleCondition);
    TestActivateDuringIdle(Pdo, TestIdleF1);
    TestActivateDuringIdle(Pdo, TestPowerNotRequired);
}

START_TEST(PoFxState)
{
    PDEVICE_OBJECT Pdo, Fdo;

    Fdo = KmtPoFxAcquireDevice(&Pdo);
    ok(Fdo != NULL, "No started PoFx test device; run through kmtest.exe with its signed INF package\n");
    if (!Fdo)
        return;
    TestPoFxBody(Pdo);
    KmtPoFxReleaseDevice(Fdo);
}
