/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Modern process manager compatibility tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

NTKERNELAPI
NTSTATUS
NTAPI
PsAcquireProcessExitSynchronization(
    _Inout_ PEPROCESS Process);

NTKERNELAPI
VOID
NTAPI
PsReleaseProcessExitSynchronization(
    _Inout_ PEPROCESS Process);

NTKERNELAPI
VOID
NTAPI
PsEnterPriorityRegion(VOID);

NTKERNELAPI
VOID
NTAPI
PsLeavePriorityRegion(VOID);

NTKERNELAPI
PVOID
NTAPI
PsGetProcessCommonJob(
    _In_ PEPROCESS FirstProcess,
    _In_ PEPROCESS SecondProcess);

NTKERNELAPI
ULONGLONG
NTAPI
PsGetProcessSequenceNumber(
    _In_ PEPROCESS Process);

NTKERNELAPI
ULONGLONG
NTAPI
PsGetProcessStartKey(
    _In_ PEPROCESS Process);

NTKERNELAPI
BOOLEAN
NTAPI
PsIsProcessCommitRelinquished(
    _In_ PEPROCESS Process);

NTKERNELAPI
ULONG
NTAPI
PsGetWin32KFilterSet(VOID);

NTKERNELAPI
BOOLEAN
NTAPI
PsIsWin32KFilterEnabled(VOID);

NTKERNELAPI
BOOLEAN
NTAPI
PsIsWin32KFilterAuditEnabled(VOID);

NTKERNELAPI
BOOLEAN
NTAPI
PsIsWin32KFilterEnabledForProcess(
    _In_ PEPROCESS Process);

NTKERNELAPI
BOOLEAN
NTAPI
PsIsWin32KFilterAuditEnabledForProcess(
    _In_ PEPROCESS Process);

NTKERNELAPI
CHAR
NTAPI
PsAdjustWin32kPriorityFloor(
    _Inout_ PETHREAD Thread,
    _In_ LONG PriorityFloor);

NTKERNELAPI
VOID
NTAPI
PsQueryProcessAttributesByToken(
    _In_ PACCESS_TOKEN Token,
    _Out_opt_ PBOOLEAN SystemAppIdentifier,
    _Out_opt_ PBOOLEAN PackagedApplication);

NTKERNELAPI
VOID
NTAPI
PsReferenceKernelStack(
    _Inout_ PETHREAD Thread);

NTKERNELAPI
VOID
NTAPI
PsDereferenceKernelStack(
    _Inout_ PETHREAD Thread);

DECLSPEC_NORETURN
NTKERNELAPI
VOID
NTAPI
PsUnEstablishWin32Callouts(VOID);

static
VOID
TestProcessIdentity(VOID)
{
    PEPROCESS Process = PsGetCurrentProcess();
    ULONGLONG SequenceNumber;
    ULONGLONG StartKey;
    PVOID DxgProcess;
    PVOID CommonJob;
    NTSTATUS Status;

    SequenceNumber = PsGetProcessSequenceNumber(Process);
    StartKey = PsGetProcessStartKey(Process);
    trace("process sequence %I64u, start key 0x%I64x\n",
          SequenceNumber,
          StartKey);
    ok(SequenceNumber != 0, "process sequence number was zero\n");
    ok_eq_ulonglong(StartKey & 0x0000FFFFFFFFFFFFULL,
                    SequenceNumber & 0x0000FFFFFFFFFFFFULL);

    DxgProcess = PsGetProcessDxgProcess(Process);
    PsSetProcessDxgProcess(Process, DxgProcess);
    ok_eq_pointer(PsGetProcessDxgProcess(Process), DxgProcess);

    CommonJob = PsGetProcessCommonJob(Process, Process);
    trace("current process common job %p\n", CommonJob);

    Status = PsAcquireProcessExitSynchronization(Process);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        PsReleaseProcessExitSynchronization(Process);
}

static
VOID
TestPriorityAndStackReferences(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();
    BOOLEAN ApcsDisabled;
    BOOLEAN AllApcsDisabled;
    CHAR PreviousFloor;
    CHAR RestoredFloor;

    ApcsDisabled = KeAreApcsDisabled();
    AllApcsDisabled = KeAreAllApcsDisabled();
    PsEnterPriorityRegion();
    ok_bool_true(KeAreApcsDisabled(), "priority region did not disable APCs");
    ok_eq_bool(KeAreAllApcsDisabled(), AllApcsDisabled);
    PsLeavePriorityRegion();
    ok_eq_bool(KeAreApcsDisabled(), ApcsDisabled);
    ok_eq_bool(KeAreAllApcsDisabled(), AllApcsDisabled);

    PreviousFloor = PsAdjustWin32kPriorityFloor(Thread, 0);
    RestoredFloor = PsAdjustWin32kPriorityFloor(Thread, PreviousFloor);
    trace("Win32k priority floor previous %d, set-zero previous %d\n",
          PreviousFloor,
          RestoredFloor);
    ok_eq_int(RestoredFloor, 0);

    PsReferenceKernelStack(Thread);
    PsDereferenceKernelStack(Thread);
    ok(TRUE, "kernel stack reference pair completed\n");
}

static
VOID
TestProcessPolicyState(VOID)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PACCESS_TOKEN Token;
    PVOID HostSilo;
    ULONG FilterSet;
    BOOLEAN SystemAppIdentifier;
    BOOLEAN PackagedApplication;
    BOOLEAN FilterEnabled;
    BOOLEAN FilterAuditEnabled;

    HostSilo = PsGetHostSilo();
    ok_eq_pointer(HostSilo, NULL);
    ok_bool_true(PsIsHostSilo(HostSilo), "NULL was not the host silo");

    ok(PsIsProtectedProcess(Process) == FALSE ||
       PsIsProtectedProcess(Process) == TRUE,
       "PsIsProtectedProcess returned a non-LOGICAL value\n");
    ok(PsIsProtectedProcessLight(Process) == FALSE ||
       PsIsProtectedProcessLight(Process) == TRUE,
       "PsIsProtectedProcessLight returned a non-LOGICAL value\n");
    ok(PsIsProcessCommitRelinquished(Process) == FALSE ||
       PsIsProcessCommitRelinquished(Process) == TRUE,
       "PsIsProcessCommitRelinquished returned a non-BOOLEAN value\n");

    FilterSet = PsGetWin32KFilterSet();
    FilterEnabled = PsIsWin32KFilterEnabled();
    FilterAuditEnabled = PsIsWin32KFilterAuditEnabled();
    trace("Win32k filter set 0x%lx, enabled %u, audit %u\n",
          FilterSet,
          FilterEnabled,
          FilterAuditEnabled);
    ok_eq_bool(PsIsWin32KFilterEnabledForProcess(Process), FilterEnabled);
    ok_eq_bool(PsIsWin32KFilterAuditEnabledForProcess(Process),
               FilterAuditEnabled);

    Token = PsReferencePrimaryToken(Process);
    ok(Token != NULL, "current process had no primary token\n");
    if (Token != NULL)
    {
        SystemAppIdentifier = TRUE;
        PackagedApplication = TRUE;
        PsQueryProcessAttributesByToken(Token,
                                        &SystemAppIdentifier,
                                        &PackagedApplication);
        trace("process token attributes: system-app %u, packaged %u\n",
              SystemAppIdentifier,
              PackagedApplication);
        ok(SystemAppIdentifier == FALSE || SystemAppIdentifier == TRUE,
           "invalid system-app result %u\n", SystemAppIdentifier);
        ok(PackagedApplication == FALSE || PackagedApplication == TRUE,
           "invalid packaged result %u\n", PackagedApplication);
        PsDereferencePrimaryToken(Token);
    }
}

START_TEST(PsModern)
{
    TestProcessIdentity();
    TestPriorityAndStackReferences();
    TestProcessPolicyState();
}

START_TEST(PsModernIdentity)
{
    TestProcessIdentity();
}

START_TEST(PsModernPriority)
{
    TestPriorityAndStackReferences();
}

START_TEST(PsWin32kPriorityFloor)
{
    static const LONG Floors[] = {0, 4, 8, 15, 16, -1, 17, 256, MINLONG, MAXLONG, 8, 0};
    PETHREAD Thread = PsGetCurrentThread();
    CHAR OriginalFloor, ExpectedFloor, PreviousFloor, CurrentFloor;
    KPRIORITY Priority;
    ULONG Index;

    OriginalFloor = PsAdjustWin32kPriorityFloor(Thread, MAXLONG);
    ExpectedFloor = OriginalFloor;
    for (Index = 0; Index < RTL_NUMBER_OF(Floors); ++Index)
    {
        PreviousFloor = PsAdjustWin32kPriorityFloor(Thread, Floors[Index]);
        ok_eq_int(PreviousFloor, ExpectedFloor);
        if ((ULONG)Floors[Index] <= 16)
            ExpectedFloor = (CHAR)Floors[Index];
        CurrentFloor = PsAdjustWin32kPriorityFloor(Thread, MAXLONG);
        ok_eq_int(CurrentFloor, ExpectedFloor);
        Priority = KeQueryPriorityThread((PKTHREAD)Thread);
        trace("floor request %ld, previous %d, current %d, priority %ld\n",
              Floors[Index], PreviousFloor, CurrentFloor, Priority);
        ok(Priority >= CurrentFloor,
           "thread priority %ld is below its floor %d\n", Priority, CurrentFloor);
    }
    PsAdjustWin32kPriorityFloor(Thread, OriginalFloor);
}

START_TEST(PsModernPolicy)
{
    TestProcessPolicyState();
}

static VOID NTAPI
TestPriorityFloorScheduling(_In_opt_ PVOID Context)
{
    PETHREAD Thread = PsGetCurrentThread();
    PKTHREAD KernelThread = (PKTHREAD)Thread;
    LARGE_INTEGER Interval;
    KPRIORITY Priority, PreviousPriority;
    CHAR Floor, PreviousFloor;
    ULONG Index;
    NTSTATUS Status;
    ULONG DisableBoost = 1;

    UNREFERENCED_PARAMETER(Context);
    Interval.QuadPart = -50 * 10 * 1000;
    Status = ZwSetInformationThread(NtCurrentThread(), ThreadPriorityBoost, &DisableBoost, sizeof(DisableBoost));
    ok_eq_hex(Status, STATUS_SUCCESS);

    for (Index = 0; Index < 2; ++Index)
    {
        Floor = (CHAR)(15 + Index);
        KeSetBasePriorityThread(KernelThread, 0);
        KeSetPriorityThread(KernelThread, 8);
        PreviousFloor = PsAdjustWin32kPriorityFloor(Thread, Floor);
        ok_eq_int(PreviousFloor, 0);
        Priority = KeQueryPriorityThread(KernelThread);
        ok(Priority >= Floor, "initial priority %ld below floor %d\n", Priority, Floor);

        Status = KeDelayExecutionThread(KernelMode, FALSE, &Interval);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Priority = KeQueryPriorityThread(KernelThread);
        ok(Priority >= Floor, "priority %ld below floor %d after wait\n", Priority, Floor);

        KeSetBasePriorityThread(KernelThread, -4);
        Priority = KeQueryPriorityThread(KernelThread);
        trace("floor %d, reduced base: priority %ld\n", Floor, Priority);
        ok(Priority >= Floor, "base change lowered priority %ld below floor %d\n", Priority, Floor);

        PreviousFloor = PsAdjustWin32kPriorityFloor(Thread, 0);
        ok_eq_int(PreviousFloor, Floor);
        Priority = KeQueryPriorityThread(KernelThread);
        trace("floor removed after base change: priority %ld\n", Priority);
        ok_eq_long(Priority, 4);

        KeSetPriorityThread(KernelThread, 8);
        PsAdjustWin32kPriorityFloor(Thread, Floor);
        PreviousPriority = KeSetPriorityThread(KernelThread, 4);
        Priority = KeQueryPriorityThread(KernelThread);
        trace("floor %d, explicit priority 4: previous %ld, current %ld\n", Floor, PreviousPriority, Priority);
        ok(Priority >= Floor, "explicit change lowered priority %ld below floor %d\n", Priority, Floor);
        PsAdjustWin32kPriorityFloor(Thread, 0);
        Priority = KeQueryPriorityThread(KernelThread);
        trace("floor removed after explicit change: priority %ld\n", Priority);
        ok_eq_long(Priority, 4);
    }

    KeSetPriorityThread(KernelThread, 18);
    PsAdjustWin32kPriorityFloor(Thread, 15);
    Priority = KeQueryPriorityThread(KernelThread);
    ok_eq_long(Priority, 18);
    PsAdjustWin32kPriorityFloor(Thread, 0);
    Priority = KeQueryPriorityThread(KernelThread);
    ok_eq_long(Priority, 18);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(PsWin32kPriorityFloorScheduling)
{
    PKTHREAD Thread = KmtStartThread(TestPriorityFloorScheduling, NULL);
    KmtFinishThread(Thread, NULL);
}

START_TEST(PsThreadPriorityBoost)
{
    static const ULONG InvalidLengths[] = { 0, 1, 3, 5, 8 };
    ULONG Original, Value[2], Returned, Length, Index;
    NTSTATUS Status;

    Original = MAXULONG;
    Length = 0;
    Status = ZwQueryInformationThread(NtCurrentThread(), ThreadPriorityBoost, &Original, sizeof(Original), &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_eq_ulong(Length, sizeof(Original));
    ok(Original <= 1, "unexpected boost flag %lu\n", Original);

    Value[0] = Value[1] = 0;
    for (Index = 0; Index < RTL_NUMBER_OF(InvalidLengths); ++Index)
    {
        Status = ZwSetInformationThread(NtCurrentThread(), ThreadPriorityBoost, Value, InvalidLengths[Index]);
        ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    }

    for (Value[0] = 0; Value[0] <= 1; ++Value[0])
    {
        Status = ZwSetInformationThread(NtCurrentThread(), ThreadPriorityBoost, Value, sizeof(Value[0]));
        ok_eq_hex(Status, STATUS_SUCCESS);
        Returned = MAXULONG;
        Length = 0;
        Status = ZwQueryInformationThread(NtCurrentThread(), ThreadPriorityBoost, &Returned, sizeof(Returned), &Length);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Returned, Value[0]);
        ok_eq_ulong(Length, sizeof(Returned));
    }

    Status = ZwSetInformationThread(NtCurrentThread(), ThreadPriorityBoost, &Original, sizeof(Original));
    ok_eq_hex(Status, STATUS_SUCCESS);
}

START_TEST(PsUnEstablishWin32Callouts)
{
    trace("PsUnEstablishWin32Callouts resolved to %p\n",
          PsUnEstablishWin32Callouts);

#ifdef KMT_DESTRUCTIVE_BUGCHECK_TESTS
    trace("calling PsUnEstablishWin32Callouts; expected bugcheck is 0x1FC\n");
    PsUnEstablishWin32Callouts();
#else
    skip(FALSE,
         "destructive call disabled; rebuild with "
         "KMT_DESTRUCTIVE_BUGCHECK_TESTS to expect bugcheck 0x1FC\n");
#endif
}
