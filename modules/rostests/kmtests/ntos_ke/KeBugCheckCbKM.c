/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite bugcheck callback registration API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static KBUGCHECK_CALLBACK_RECORD CallbackRecord;
static KBUGCHECK_REASON_CALLBACK_RECORD ReasonRecord;

static
VOID
NTAPI
TestBugCheckCallback(
    _In_opt_ PVOID Buffer,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
}

static
VOID
NTAPI
TestReasonCallback(
    _In_ KBUGCHECK_CALLBACK_REASON Reason,
    _In_ PKBUGCHECK_REASON_CALLBACK_RECORD Record,
    _Inout_ PVOID ReasonSpecificData,
    _In_ ULONG ReasonSpecificDataLength)
{
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Record);
    UNREFERENCED_PARAMETER(ReasonSpecificData);
    UNREFERENCED_PARAMETER(ReasonSpecificDataLength);
}

START_TEST(KeBugCheckCbKM)
{
    BOOLEAN Result;
    static UCHAR Buffer[16];

    KeInitializeCallbackRecord(&CallbackRecord);
    Result = KeRegisterBugCheckCallback(&CallbackRecord, TestBugCheckCallback, Buffer, sizeof(Buffer), (PUCHAR)"KmtBugCheckCb");
    ok_bool_true(Result, "register bugcheck callback");

    Result = KeRegisterBugCheckCallback(&CallbackRecord, TestBugCheckCallback, Buffer, sizeof(Buffer), (PUCHAR)"KmtBugCheckCb");
    ok_bool_false(Result, "double register");

    Result = KeDeregisterBugCheckCallback(&CallbackRecord);
    ok_bool_true(Result, "deregister bugcheck callback");

    Result = KeDeregisterBugCheckCallback(&CallbackRecord);
    ok_bool_false(Result, "double deregister");

    KeInitializeCallbackRecord(&ReasonRecord);
    Result = KeRegisterBugCheckReasonCallback(&ReasonRecord, TestReasonCallback, KbCallbackSecondaryDumpData, (PUCHAR)"KmtReasonCb");
    ok_bool_true(Result, "register reason callback");

    Result = KeDeregisterBugCheckReasonCallback(&ReasonRecord);
    ok_bool_true(Result, "deregister reason callback");

    Result = KeDeregisterBugCheckReasonCallback(&ReasonRecord);
    ok_bool_false(Result, "double deregister reason");
}
