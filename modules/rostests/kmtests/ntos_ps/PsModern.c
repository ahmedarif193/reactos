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
    _Inout_ PEPROCESS Process,
    _In_ CHAR PriorityFloor);

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
    PEPROCESS Process = PsGetCurrentProcess();
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

    PreviousFloor = PsAdjustWin32kPriorityFloor(Process, 0);
    RestoredFloor = PsAdjustWin32kPriorityFloor(Process, PreviousFloor);
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

START_TEST(PsModernPolicy)
{
    TestProcessPolicyState();
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
