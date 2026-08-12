/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 processor-change callback ABI and behavior tests
 */

#include <kmt_test.h>

VOID Test_KeArm64ProcessorChange(VOID);

#ifdef _M_ARM64

#define KMT_PROCESSOR_CHANGE_MAX_RECORDS 64

typedef PVOID (NTAPI *PKMT_KE_REGISTER_PROCESSOR_CHANGE_CALLBACK)(PPROCESSOR_CALLBACK_FUNCTION, PVOID, ULONG);
typedef VOID (NTAPI *PKMT_KE_DEREGISTER_PROCESSOR_CHANGE_CALLBACK)(PVOID);

typedef struct _KMT_PROCESSOR_CHANGE_RECORD
{
    KE_PROCESSOR_CHANGE_NOTIFY_STATE State;
    ULONG NtNumber;
    NTSTATUS ContextStatus;
    NTSTATUS OperationStatus;
    PROCESSOR_NUMBER ProcNumber;
    KIRQL Irql;
    BOOLEAN InterruptsEnabled;
} KMT_PROCESSOR_CHANGE_RECORD, *PKMT_PROCESSOR_CHANGE_RECORD;

typedef struct _KMT_PROCESSOR_CHANGE_CONTEXT
{
    KMT_PROCESSOR_CHANGE_RECORD Records[KMT_PROCESSOR_CHANGE_MAX_RECORDS];
    ULONG CallCount;
    ULONG StartCount;
    ULONG CompleteCount;
    ULONG FailureCount;
    ULONG FailNtNumber;
    ULONG ChangeCompleteStatusNtNumber;
    ULONG ChangeFailureStatusNtNumber;
} KMT_PROCESSOR_CHANGE_CONTEXT, *PKMT_PROCESSOR_CHANGE_CONTEXT;

C_ASSERT(sizeof(KE_PROCESSOR_CHANGE_NOTIFY_CONTEXT) == 16);
C_ASSERT(FIELD_OFFSET(KE_PROCESSOR_CHANGE_NOTIFY_CONTEXT, State) == 0);
C_ASSERT(FIELD_OFFSET(KE_PROCESSOR_CHANGE_NOTIFY_CONTEXT, NtNumber) == 4);
C_ASSERT(FIELD_OFFSET(KE_PROCESSOR_CHANGE_NOTIFY_CONTEXT, Status) == 8);
C_ASSERT(FIELD_OFFSET(KE_PROCESSOR_CHANGE_NOTIFY_CONTEXT, ProcNumber) == 12);

static VOID NTAPI KmtProcessorChangeCallback(_In_ PVOID CallbackContext, _In_ PKE_PROCESSOR_CHANGE_NOTIFY_CONTEXT ChangeContext, _Inout_ PNTSTATUS OperationStatus)
{
    PKMT_PROCESSOR_CHANGE_CONTEXT Context = CallbackContext;
    PKMT_PROCESSOR_CHANGE_RECORD Record;
    ULONG Index;

    Index = Context->CallCount++;
    if (Index < RTL_NUMBER_OF(Context->Records))
    {
        Record = &Context->Records[Index];
        Record->State = ChangeContext->State;
        Record->NtNumber = ChangeContext->NtNumber;
        Record->ContextStatus = ChangeContext->Status;
        Record->OperationStatus = *OperationStatus;
        Record->ProcNumber = ChangeContext->ProcNumber;
        Record->Irql = KeGetCurrentIrql();
        Record->InterruptsEnabled = KmtAreInterruptsEnabled();
    }

    if (ChangeContext->State == KeProcessorAddStartNotify)
    {
        Context->StartCount++;
        if (ChangeContext->NtNumber == Context->FailNtNumber)
            *OperationStatus = STATUS_UNSUCCESSFUL;
    }
    else if (ChangeContext->State == KeProcessorAddCompleteNotify)
    {
        Context->CompleteCount++;
        if (ChangeContext->NtNumber == Context->ChangeCompleteStatusNtNumber)
            *OperationStatus = STATUS_ACCESS_DENIED;
    }
    else if (ChangeContext->State == KeProcessorAddFailureNotify)
    {
        Context->FailureCount++;
        if (ChangeContext->NtNumber == Context->ChangeFailureStatusNtNumber)
            *OperationStatus = STATUS_ACCESS_DENIED;
    }
}

static VOID KmtInitializeProcessorChangeContext(_Out_ PKMT_PROCESSOR_CHANGE_CONTEXT Context)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->FailNtNumber = MAXULONG;
    Context->ChangeCompleteStatusNtNumber = MAXULONG;
    Context->ChangeFailureStatusNtNumber = MAXULONG;
}

static VOID KmtValidateProcessorChangeRecord(_In_ PKMT_PROCESSOR_CHANGE_RECORD Record, _In_ KE_PROCESSOR_CHANGE_NOTIFY_STATE State, _In_ ULONG ProcessorIndex, _In_ NTSTATUS ContextStatus, _In_ NTSTATUS OperationStatus)
{
    PROCESSOR_NUMBER ExpectedNumber;
    NTSTATUS Status;

    Status = KeGetProcessorNumberFromIndex(ProcessorIndex, &ExpectedNumber);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Record->State, State);
    ok_eq_ulong(Record->NtNumber, ProcessorIndex);
    ok_eq_hex(Record->ContextStatus, ContextStatus);
    ok_eq_hex(Record->OperationStatus, OperationStatus);
    ok_eq_ulong(Record->ProcNumber.Group, ExpectedNumber.Group);
    ok_eq_ulong(Record->ProcNumber.Number, ExpectedNumber.Number);
    ok_eq_ulong(Record->ProcNumber.Reserved, ExpectedNumber.Reserved);
    ok_eq_uint(Record->Irql, APC_LEVEL);
    ok_bool_true(Record->InterruptsEnabled, "processor-change callback interrupts");
}

static VOID KmtTestProcessorChangeCallbacks(VOID)
{
    PKMT_KE_REGISTER_PROCESSOR_CHANGE_CALLBACK RegisterCallback;
    PKMT_KE_DEREGISTER_PROCESSOR_CHANGE_CALLBACK DeregisterCallback;
    KMT_PROCESSOR_CHANGE_CONTEXT Context;
    PVOID Handle1;
    PVOID Handle2;
    ULONG ActiveCount;
    ULONG FailureProcessor;
    ULONG Index;

    RegisterCallback = (PKMT_KE_REGISTER_PROCESSOR_CHANGE_CALLBACK)KmtGetSystemRoutineAddress(L"KeRegisterProcessorChangeCallback");
    DeregisterCallback = (PKMT_KE_DEREGISTER_PROCESSOR_CHANGE_CALLBACK)KmtGetSystemRoutineAddress(L"KeDeregisterProcessorChangeCallback");
    ok(RegisterCallback != NULL, "KeRegisterProcessorChangeCallback is not exported\n");
    ok(DeregisterCallback != NULL, "KeDeregisterProcessorChangeCallback is not exported\n");
    if (!RegisterCallback || !DeregisterCallback)
        return;

    ActiveCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    ok(ActiveCount != 0, "active processor count is zero\n");
    ok(ActiveCount * 2 <= KMT_PROCESSOR_CHANGE_MAX_RECORDS, "active processor count %lu exceeds record capacity\n", ActiveCount);
    if ((ActiveCount == 0) || (ActiveCount * 2 > KMT_PROCESSOR_CHANGE_MAX_RECORDS))
        return;

    KmtInitializeProcessorChangeContext(&Context);
    Handle1 = RegisterCallback(KmtProcessorChangeCallback, &Context, 0);
    Handle2 = RegisterCallback(KmtProcessorChangeCallback, &Context, 2);
    trace("[KeProcessorChange] passive active=%lu h0=%p h2=%p calls=%lu\n", ActiveCount, Handle1, Handle2, Context.CallCount);
    ok(Handle1 != NULL, "flags 0 registration failed\n");
    ok(Handle2 != NULL, "flags 2 registration failed\n");
    ok_eq_ulong(Context.CallCount, 0);
    if (Handle2)
        DeregisterCallback(Handle2);
    if (Handle1)
        DeregisterCallback(Handle1);

    KmtInitializeProcessorChangeContext(&Context);
    Handle1 = RegisterCallback(KmtProcessorChangeCallback, &Context, KE_PROCESSOR_CHANGE_ADD_EXISTING);
    trace("[KeProcessorChange] existing active=%lu handle=%p calls=%lu start=%lu complete=%lu failure=%lu\n", ActiveCount, Handle1, Context.CallCount, Context.StartCount, Context.CompleteCount, Context.FailureCount);
    ok(Handle1 != NULL, "existing processor registration failed\n");
    ok_eq_ulong(Context.CallCount, ActiveCount * 2);
    ok_eq_ulong(Context.StartCount, ActiveCount);
    ok_eq_ulong(Context.CompleteCount, ActiveCount);
    ok_eq_ulong(Context.FailureCount, 0);
    for (Index = 0; Index < ActiveCount; Index++)
    {
        KmtValidateProcessorChangeRecord(&Context.Records[Index], KeProcessorAddStartNotify, Index, STATUS_SUCCESS, STATUS_SUCCESS);
        KmtValidateProcessorChangeRecord(&Context.Records[ActiveCount + Index], KeProcessorAddCompleteNotify, Index, STATUS_SUCCESS, STATUS_SUCCESS);
    }
    if (Handle1)
        DeregisterCallback(Handle1);

    KmtInitializeProcessorChangeContext(&Context);
    Handle1 = RegisterCallback(KmtProcessorChangeCallback, &Context, 3);
    trace("[KeProcessorChange] flags3 active=%lu handle=%p calls=%lu start=%lu complete=%lu failure=%lu\n", ActiveCount, Handle1, Context.CallCount, Context.StartCount, Context.CompleteCount, Context.FailureCount);
    ok(Handle1 != NULL, "flags 3 registration failed\n");
    ok_eq_ulong(Context.CallCount, ActiveCount * 2);
    ok_eq_ulong(Context.StartCount, ActiveCount);
    ok_eq_ulong(Context.CompleteCount, ActiveCount);
    ok_eq_ulong(Context.FailureCount, 0);
    for (Index = 0; Index < ActiveCount; Index++)
    {
        KmtValidateProcessorChangeRecord(&Context.Records[Index], KeProcessorAddStartNotify, Index, STATUS_SUCCESS, STATUS_SUCCESS);
        KmtValidateProcessorChangeRecord(&Context.Records[ActiveCount + Index], KeProcessorAddCompleteNotify, Index, STATUS_SUCCESS, STATUS_SUCCESS);
    }
    if (Handle1)
        DeregisterCallback(Handle1);

    KmtInitializeProcessorChangeContext(&Context);
    Context.ChangeCompleteStatusNtNumber = 0;
    Handle1 = RegisterCallback(KmtProcessorChangeCallback, &Context, KE_PROCESSOR_CHANGE_ADD_EXISTING);
    trace("[KeProcessorChange] complete-status active=%lu handle=%p calls=%lu start=%lu complete=%lu failure=%lu\n", ActiveCount, Handle1, Context.CallCount, Context.StartCount, Context.CompleteCount, Context.FailureCount);
    ok(Handle1 != NULL, "completion status change rejected registration\n");
    ok_eq_ulong(Context.CallCount, ActiveCount * 2);
    ok_eq_ulong(Context.StartCount, ActiveCount);
    ok_eq_ulong(Context.CompleteCount, ActiveCount);
    ok_eq_ulong(Context.FailureCount, 0);
    for (Index = 0; Index < ActiveCount; Index++)
    {
        KmtValidateProcessorChangeRecord(&Context.Records[Index], KeProcessorAddStartNotify, Index, STATUS_SUCCESS, STATUS_SUCCESS);
        KmtValidateProcessorChangeRecord(&Context.Records[ActiveCount + Index], KeProcessorAddCompleteNotify, Index, STATUS_SUCCESS, (Index == 0) ? STATUS_SUCCESS : STATUS_ACCESS_DENIED);
    }
    if (Handle1)
        DeregisterCallback(Handle1);

    FailureProcessor = ActiveCount - 1;
    KmtInitializeProcessorChangeContext(&Context);
    Context.FailNtNumber = FailureProcessor;
    Context.ChangeFailureStatusNtNumber = 0;
    Handle1 = RegisterCallback(KmtProcessorChangeCallback, &Context, KE_PROCESSOR_CHANGE_ADD_EXISTING);
    trace("[KeProcessorChange] failure active=%lu target=%lu handle=%p calls=%lu start=%lu complete=%lu failure=%lu\n", ActiveCount, FailureProcessor, Handle1, Context.CallCount, Context.StartCount, Context.CompleteCount, Context.FailureCount);
    ok_eq_pointer(Handle1, NULL);
    ok_eq_ulong(Context.CallCount, FailureProcessor * 2 + 1);
    ok_eq_ulong(Context.StartCount, FailureProcessor + 1);
    ok_eq_ulong(Context.CompleteCount, 0);
    ok_eq_ulong(Context.FailureCount, FailureProcessor);
    for (Index = 0; Index <= FailureProcessor; Index++)
        KmtValidateProcessorChangeRecord(&Context.Records[Index], KeProcessorAddStartNotify, Index, STATUS_SUCCESS, STATUS_SUCCESS);
    for (Index = 0; Index < FailureProcessor; Index++)
        KmtValidateProcessorChangeRecord(&Context.Records[FailureProcessor + 1 + Index], KeProcessorAddFailureNotify, Index, STATUS_UNSUCCESSFUL, (Index == 0) ? STATUS_UNSUCCESSFUL : STATUS_ACCESS_DENIED);
}

#endif

START_TEST(KeArm64ProcessorChange)
{
#ifdef _M_ARM64
    KmtTestProcessorChangeCallbacks();
#else
    skip(FALSE, "ARM64-only processor-change callback test\n");
#endif
}
