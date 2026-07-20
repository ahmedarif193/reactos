/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Monitored-fence CPU wait registry contract tests
 */

#include <kmt_test.h>
#include "sync_wait_core.h"

#define DXGK_SYNC_WAIT_TEST_TIMEOUT_SECONDS 10

typedef struct _DXGK_SYNC_WAIT_TEST_CONTEXT
{
    volatile LONG CompletionCount;
    NTSTATUS CompletionStatus;
    NTSTATUS RequestCompletionStatus;
    KIRQL CompletionIrql;
    NTSTATUS AdmissionStatus;
    KIRQL AdmissionIrql;
} DXGK_SYNC_WAIT_TEST_CONTEXT, *PDXGK_SYNC_WAIT_TEST_CONTEXT;

typedef struct _DXGK_SYNC_WAIT_DPC_CONTEXT
{
    KDPC Dpc;
    KEVENT DoneEvent;
    PDXGK_SYNC_WAIT_CORE_REGISTRY Registry;
    DXGK_SYNC_WAIT_CORE_UPDATE Update;
    NTSTATUS Status;
} DXGK_SYNC_WAIT_DPC_CONTEXT, *PDXGK_SYNC_WAIT_DPC_CONTEXT;

static VOID NTAPI DxgkSyncWaitTestComplete(_Inout_ PDXGK_SYNC_WAIT_CORE_REQUEST Request, _In_ NTSTATUS Status, _In_opt_ PVOID Context)
{
    PDXGK_SYNC_WAIT_TEST_CONTEXT TestContext = Context;

    TestContext->CompletionStatus = Status;
    TestContext->RequestCompletionStatus = Request->CompletionStatus;
    TestContext->CompletionIrql = KeGetCurrentIrql();
    InterlockedIncrement(&TestContext->CompletionCount);
}

static NTSTATUS NTAPI DxgkSyncWaitTestAdmission(_In_ PDXGK_SYNC_WAIT_CORE_REQUEST Request, _In_opt_ PVOID Context)
{
    PDXGK_SYNC_WAIT_TEST_CONTEXT TestContext = Context;

    UNREFERENCED_PARAMETER(Request);
    TestContext->AdmissionIrql = KeGetCurrentIrql();
    return TestContext->AdmissionStatus;
}

static NTSTATUS NTAPI DxgkSyncWaitTestPublishAdmission(_In_opt_ PVOID Context)
{
    PDXGK_SYNC_WAIT_TEST_CONTEXT TestContext = Context;

    TestContext->AdmissionIrql = KeGetCurrentIrql();
    return TestContext->AdmissionStatus;
}

static VOID NTAPI DxgkSyncWaitTestDpc(_In_ PKDPC Dpc, _In_opt_ PVOID DeferredContext, _In_opt_ PVOID SystemArgument1, _In_opt_ PVOID SystemArgument2)
{
    PDXGK_SYNC_WAIT_DPC_CONTEXT Context = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    Context->Status = DxgkSyncWaitCorePublishBatch(Context->Registry, &Context->Update, 1, FALSE, NULL, NULL);
    KeSetEvent(&Context->DoneEvent, IO_NO_INCREMENT, FALSE);
}

static VOID DxgkSyncWaitTestInitializeContext(_Out_ PDXGK_SYNC_WAIT_TEST_CONTEXT Context)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->AdmissionStatus = STATUS_SUCCESS;
}

static VOID DxgkSyncWaitTestInitializeTarget(_Out_ PDXGK_SYNC_WAIT_CORE_TARGET Target, _In_ PVOID Object, _In_ volatile LONG64 *FenceValue, _In_ UINT64 TargetValue)
{
    Target->Object = Object;
    Target->FenceValue = FenceValue;
    Target->TargetValue = TargetValue;
}

static VOID DxgkSyncWaitTestInitializeUpdate(_Out_ PDXGK_SYNC_WAIT_CORE_UPDATE Update, _In_ PVOID Object, _In_ volatile LONG64 *FenceValue, _In_opt_ volatile UINT64 *PublishedValue, _In_ UINT64 NewValue)
{
    Update->Object = Object;
    Update->FenceValue = FenceValue;
    Update->PublishedValue = PublishedValue;
    Update->NewValue = NewValue;
}

static NTSTATUS DxgkSyncWaitTestWaitForEvent(_In_ PKEVENT Event)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -10LL * 1000LL * 1000LL * DXGK_SYNC_WAIT_TEST_TIMEOUT_SECONDS;
    return KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout);
}

static VOID DxgkSyncWaitTestWaitAllAtomicBatch(VOID)
{
    DXGK_SYNC_WAIT_CORE_REGISTRY Registry;
    DXGK_SYNC_WAIT_CORE_REQUEST Request;
    DXGK_SYNC_WAIT_CORE_TARGET Targets[2];
    DXGK_SYNC_WAIT_CORE_UPDATE Updates[2];
    DXGK_SYNC_WAIT_TEST_CONTEXT Context;
    volatile LONG64 FenceValues[2] = { 0, 0 };
    volatile UINT64 PublishedValues[2] = { 0, 0 };
    ULONG Objects[2] = { 1, 2 };
    NTSTATUS Status;

    DxgkSyncWaitCoreInitializeRegistry(&Registry);
    DxgkSyncWaitTestInitializeContext(&Context);
    DxgkSyncWaitTestInitializeTarget(&Targets[0], &Objects[0], &FenceValues[0], 1);
    DxgkSyncWaitTestInitializeTarget(&Targets[1], &Objects[1], &FenceValues[1], 1);
    DxgkSyncWaitCoreInitializeRequest(&Request, Targets, RTL_NUMBER_OF(Targets), FALSE, DxgkSyncWaitTestAdmission, DxgkSyncWaitTestComplete, &Context);
    Status = DxgkSyncWaitCoreRegister(&Registry, &Request);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Context.CompletionCount, 0);
    ok_eq_ulong(Context.AdmissionIrql, DISPATCH_LEVEL);
    DxgkSyncWaitTestInitializeUpdate(&Updates[0], &Objects[0], &FenceValues[0], &PublishedValues[0], 1);
    Status = DxgkSyncWaitCorePublishBatch(&Registry, &Updates[0], 1, FALSE, NULL, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Context.CompletionCount, 0);
    ok(FenceValues[0] == 1 && PublishedValues[0] == 1, "first fence did not publish atomically\n");
    DxgkSyncWaitTestInitializeUpdate(&Updates[0], &Objects[0], &FenceValues[0], &PublishedValues[0], 2);
    DxgkSyncWaitTestInitializeUpdate(&Updates[1], &Objects[1], &FenceValues[1], &PublishedValues[1], 1);
    Status = DxgkSyncWaitCorePublishBatch(&Registry, Updates, RTL_NUMBER_OF(Updates), FALSE, NULL, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Context.CompletionCount, 1);
    ok_eq_hex(Context.CompletionStatus, STATUS_SUCCESS);
    ok_eq_hex(Context.RequestCompletionStatus, STATUS_SUCCESS);
    ok(FenceValues[0] == 2 && FenceValues[1] == 1, "batch fence values are %I64d and %I64d\n", FenceValues[0], FenceValues[1]);
    ok(PublishedValues[0] == 2 && PublishedValues[1] == 1, "batch published values are %I64u and %I64u\n", PublishedValues[0], PublishedValues[1]);
    ok_bool_true(DxgkSyncWaitCoreIsEmpty(&Registry), "WaitAll completion removes request");
}

static VOID DxgkSyncWaitTestWaitAnyAndImmediate(VOID)
{
    DXGK_SYNC_WAIT_CORE_REGISTRY Registry;
    DXGK_SYNC_WAIT_CORE_REQUEST WaitAnyRequest;
    DXGK_SYNC_WAIT_CORE_REQUEST ImmediateRequest;
    DXGK_SYNC_WAIT_CORE_TARGET Targets[2];
    DXGK_SYNC_WAIT_CORE_UPDATE Update;
    DXGK_SYNC_WAIT_TEST_CONTEXT WaitAnyContext;
    DXGK_SYNC_WAIT_TEST_CONTEXT ImmediateContext;
    volatile LONG64 FenceValues[2] = { 0, 0 };
    ULONG Objects[2] = { 3, 4 };
    NTSTATUS Status;

    DxgkSyncWaitCoreInitializeRegistry(&Registry);
    DxgkSyncWaitTestInitializeContext(&WaitAnyContext);
    DxgkSyncWaitTestInitializeTarget(&Targets[0], &Objects[0], &FenceValues[0], 10);
    DxgkSyncWaitTestInitializeTarget(&Targets[1], &Objects[1], &FenceValues[1], 20);
    DxgkSyncWaitCoreInitializeRequest(&WaitAnyRequest, Targets, RTL_NUMBER_OF(Targets), TRUE, NULL, DxgkSyncWaitTestComplete, &WaitAnyContext);
    Status = DxgkSyncWaitCoreRegister(&Registry, &WaitAnyRequest);
    ok_eq_hex(Status, STATUS_SUCCESS);
    DxgkSyncWaitTestInitializeUpdate(&Update, &Objects[1], &FenceValues[1], NULL, 20);
    Status = DxgkSyncWaitCorePublishBatch(&Registry, &Update, 1, FALSE, NULL, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(WaitAnyContext.CompletionCount, 1);
    ok_eq_hex(WaitAnyContext.CompletionStatus, STATUS_SUCCESS);

    DxgkSyncWaitTestInitializeContext(&ImmediateContext);
    DxgkSyncWaitTestInitializeTarget(&Targets[0], &Objects[0], &FenceValues[0], 0);
    DxgkSyncWaitCoreInitializeRequest(&ImmediateRequest, Targets, 1, FALSE, NULL, DxgkSyncWaitTestComplete, &ImmediateContext);
    Status = DxgkSyncWaitCoreRegister(&Registry, &ImmediateRequest);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(ImmediateContext.CompletionCount, 1);
    ok_eq_hex(ImmediateContext.CompletionStatus, STATUS_SUCCESS);
    ok_bool_true(DxgkSyncWaitCoreIsEmpty(&Registry), "WaitAny and immediate requests drain");
}

static VOID DxgkSyncWaitTestAdmissionAndRewind(VOID)
{
    DXGK_SYNC_WAIT_CORE_REGISTRY Registry;
    DXGK_SYNC_WAIT_CORE_REQUEST Request;
    DXGK_SYNC_WAIT_CORE_TARGET Target;
    DXGK_SYNC_WAIT_CORE_UPDATE Updates[2];
    DXGK_SYNC_WAIT_TEST_CONTEXT Context;
    volatile LONG64 FenceValues[2] = { 10, 20 };
    volatile UINT64 PublishedValues[2] = { 10, 20 };
    ULONG Objects[2] = { 5, 6 };
    NTSTATUS Status;

    DxgkSyncWaitCoreInitializeRegistry(&Registry);
    DxgkSyncWaitTestInitializeContext(&Context);
    Context.AdmissionStatus = STATUS_ACCESS_DENIED;
    DxgkSyncWaitTestInitializeTarget(&Target, &Objects[0], &FenceValues[0], 100);
    DxgkSyncWaitCoreInitializeRequest(&Request, &Target, 1, FALSE, DxgkSyncWaitTestAdmission, DxgkSyncWaitTestComplete, &Context);
    Status = DxgkSyncWaitCoreRegister(&Registry, &Request);
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);
    ok_eq_long(Context.CompletionCount, 0);
    ok_eq_ulong(Context.AdmissionIrql, DISPATCH_LEVEL);
    ok_bool_true(DxgkSyncWaitCoreIsEmpty(&Registry), "rejected request was not linked");

    DxgkSyncWaitTestInitializeUpdate(&Updates[0], &Objects[0], &FenceValues[0], &PublishedValues[0], 30);
    DxgkSyncWaitTestInitializeUpdate(&Updates[1], &Objects[1], &FenceValues[1], &PublishedValues[1], 40);
    Status = DxgkSyncWaitCorePublishBatch(&Registry, Updates, RTL_NUMBER_OF(Updates), FALSE, DxgkSyncWaitTestPublishAdmission, &Context);
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);
    ok(FenceValues[0] == 10 && FenceValues[1] == 20, "rejected batch mutated fences\n");
    ok(PublishedValues[0] == 10 && PublishedValues[1] == 20, "rejected batch mutated published pages\n");

    Context.AdmissionStatus = STATUS_SUCCESS;
    Updates[0].NewValue = 5;
    Updates[1].NewValue = 15;
    Status = DxgkSyncWaitCorePublishBatch(&Registry, Updates, RTL_NUMBER_OF(Updates), FALSE, DxgkSyncWaitTestPublishAdmission, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(FenceValues[0] == 10 && FenceValues[1] == 20, "monotonic publish rewound fences\n");
    Status = DxgkSyncWaitCorePublishBatch(&Registry, Updates, RTL_NUMBER_OF(Updates), TRUE, DxgkSyncWaitTestPublishAdmission, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(FenceValues[0] == 5 && FenceValues[1] == 15, "AllowFenceRewind did not lower fences\n");
    ok(PublishedValues[0] == 5 && PublishedValues[1] == 15, "AllowFenceRewind did not lower published pages\n");
}

static VOID DxgkSyncWaitTestCancelAndShutdown(VOID)
{
    DXGK_SYNC_WAIT_CORE_REGISTRY Registry;
    DXGK_SYNC_WAIT_CORE_REQUEST ObjectRequest;
    DXGK_SYNC_WAIT_CORE_REQUEST ShutdownRequest;
    DXGK_SYNC_WAIT_CORE_REQUEST RejectedRequest;
    DXGK_SYNC_WAIT_CORE_TARGET ObjectTarget;
    DXGK_SYNC_WAIT_CORE_TARGET ShutdownTarget;
    DXGK_SYNC_WAIT_CORE_TARGET RejectedTarget;
    DXGK_SYNC_WAIT_CORE_UPDATE Update;
    DXGK_SYNC_WAIT_TEST_CONTEXT ObjectContext;
    DXGK_SYNC_WAIT_TEST_CONTEXT ShutdownContext;
    DXGK_SYNC_WAIT_TEST_CONTEXT RejectedContext;
    volatile LONG64 FenceValues[3] = { 0, 0, 0 };
    ULONG Objects[3] = { 7, 8, 9 };
    NTSTATUS Status;

    DxgkSyncWaitCoreInitializeRegistry(&Registry);
    DxgkSyncWaitTestInitializeContext(&ObjectContext);
    DxgkSyncWaitTestInitializeTarget(&ObjectTarget, &Objects[0], &FenceValues[0], 1);
    DxgkSyncWaitCoreInitializeRequest(&ObjectRequest, &ObjectTarget, 1, FALSE, NULL, DxgkSyncWaitTestComplete, &ObjectContext);
    Status = DxgkSyncWaitCoreRegister(&Registry, &ObjectRequest);
    ok_eq_hex(Status, STATUS_SUCCESS);
    DxgkSyncWaitCoreCancelObject(&Registry, &Objects[0], STATUS_DELETE_PENDING);
    ok_eq_long(ObjectContext.CompletionCount, 1);
    ok_eq_hex(ObjectContext.CompletionStatus, STATUS_DELETE_PENDING);
    DxgkSyncWaitCoreCancelObject(&Registry, &Objects[0], STATUS_DEVICE_REMOVED);
    ok_eq_long(ObjectContext.CompletionCount, 1);

    DxgkSyncWaitTestInitializeContext(&ShutdownContext);
    DxgkSyncWaitTestInitializeTarget(&ShutdownTarget, &Objects[1], &FenceValues[1], 1);
    DxgkSyncWaitCoreInitializeRequest(&ShutdownRequest, &ShutdownTarget, 1, FALSE, NULL, DxgkSyncWaitTestComplete, &ShutdownContext);
    Status = DxgkSyncWaitCoreRegister(&Registry, &ShutdownRequest);
    ok_eq_hex(Status, STATUS_SUCCESS);
    DxgkSyncWaitCoreCancelAll(&Registry, STATUS_DEVICE_REMOVED, TRUE);
    ok_eq_long(ShutdownContext.CompletionCount, 1);
    ok_eq_hex(ShutdownContext.CompletionStatus, STATUS_DEVICE_REMOVED);
    DxgkSyncWaitCoreCancelAll(&Registry, STATUS_CANCELLED, TRUE);
    ok_eq_long(ShutdownContext.CompletionCount, 1);

    DxgkSyncWaitTestInitializeContext(&RejectedContext);
    DxgkSyncWaitTestInitializeTarget(&RejectedTarget, &Objects[2], &FenceValues[2], 1);
    DxgkSyncWaitCoreInitializeRequest(&RejectedRequest, &RejectedTarget, 1, FALSE, NULL, DxgkSyncWaitTestComplete, &RejectedContext);
    Status = DxgkSyncWaitCoreRegister(&Registry, &RejectedRequest);
    ok_eq_hex(Status, STATUS_DEVICE_REMOVED);
    ok_eq_long(RejectedContext.CompletionCount, 0);
    DxgkSyncWaitTestInitializeUpdate(&Update, &Objects[2], &FenceValues[2], NULL, 1);
    Status = DxgkSyncWaitCorePublishBatch(&Registry, &Update, 1, FALSE, NULL, NULL);
    ok_eq_hex(Status, STATUS_DEVICE_REMOVED);
    ok(FenceValues[2] == 0, "shutdown registry accepted a publication\n");
    ok_bool_true(DxgkSyncWaitCoreIsEmpty(&Registry), "shutdown registry empty");
}

static VOID DxgkSyncWaitTestDispatchPublish(VOID)
{
    DXGK_SYNC_WAIT_CORE_REGISTRY Registry;
    DXGK_SYNC_WAIT_CORE_REQUEST Request;
    DXGK_SYNC_WAIT_CORE_TARGET Target;
    DXGK_SYNC_WAIT_TEST_CONTEXT Context;
    DXGK_SYNC_WAIT_DPC_CONTEXT DpcContext;
    volatile LONG64 FenceValue = 0;
    volatile UINT64 PublishedValue = 0;
    ULONG Object = 10;
    BOOLEAN Queued;
    NTSTATUS Status;

    DxgkSyncWaitCoreInitializeRegistry(&Registry);
    DxgkSyncWaitTestInitializeContext(&Context);
    DxgkSyncWaitTestInitializeTarget(&Target, &Object, &FenceValue, 1);
    DxgkSyncWaitCoreInitializeRequest(&Request, &Target, 1, FALSE, NULL, DxgkSyncWaitTestComplete, &Context);
    Status = DxgkSyncWaitCoreRegister(&Registry, &Request);
    ok_eq_hex(Status, STATUS_SUCCESS);
    RtlZeroMemory(&DpcContext, sizeof(DpcContext));
    DpcContext.Registry = &Registry;
    DpcContext.Status = STATUS_PENDING;
    DxgkSyncWaitTestInitializeUpdate(&DpcContext.Update, &Object, &FenceValue, &PublishedValue, 1);
    KeInitializeEvent(&DpcContext.DoneEvent, NotificationEvent, FALSE);
    KeInitializeDpc(&DpcContext.Dpc, DxgkSyncWaitTestDpc, &DpcContext);
    Queued = KeInsertQueueDpc(&DpcContext.Dpc, NULL, NULL);
    ok_bool_true(Queued, "queue publication DPC");
    if (!Queued)
    {
        DxgkSyncWaitCoreCancelObject(&Registry, &Object, STATUS_CANCELLED);
        return;
    }
    Status = DxgkSyncWaitTestWaitForEvent(&DpcContext.DoneEvent);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS && !KeRemoveQueueDpc(&DpcContext.Dpc))
        Status = KeWaitForSingleObject(&DpcContext.DoneEvent, Executive, KernelMode, FALSE, NULL);
    if (Status != STATUS_SUCCESS)
    {
        DxgkSyncWaitCoreCancelObject(&Registry, &Object, STATUS_CANCELLED);
        return;
    }
    ok_eq_hex(DpcContext.Status, STATUS_SUCCESS);
    ok_eq_long(Context.CompletionCount, 1);
    ok_eq_ulong(Context.CompletionIrql, DISPATCH_LEVEL);
    ok_eq_hex(Context.RequestCompletionStatus, STATUS_SUCCESS);
    ok(FenceValue == 1 && PublishedValue == 1, "DPC publication did not reach both values\n");
    ok_bool_true(DxgkSyncWaitCoreIsEmpty(&Registry), "DPC completion removes request");
}

START_TEST(DxgkSyncWait)
{
    DxgkSyncWaitTestWaitAllAtomicBatch();
    DxgkSyncWaitTestWaitAnyAndImmediate();
    DxgkSyncWaitTestAdmissionAndRewind();
    DxgkSyncWaitTestCancelAndShutdown();
    DxgkSyncWaitTestDispatchPublish();
}
