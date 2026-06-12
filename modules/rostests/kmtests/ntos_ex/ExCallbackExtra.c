/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite extended ExCallback coverage
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static volatile LONG CallbackHits;
static PVOID ObservedArg1;
static PVOID ObservedArg2;

static
VOID
NTAPI
CallbackFunction(
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    ok_eq_pointer(CallbackContext, (PVOID)(ULONG_PTR)0xC0DE);
    ObservedArg1 = Argument1;
    ObservedArg2 = Argument2;
    InterlockedIncrement(&CallbackHits);
}

START_TEST(ExCallbackExtra)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    PCALLBACK_OBJECT CallbackObject = NULL;
    PVOID Registration1, Registration2;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, L"\\Callback\\KmtExCallbackExtra");
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_PERMANENT, NULL, NULL);

    Status = ExCreateCallback(&CallbackObject, &ObjectAttributes, TRUE, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ok(CallbackObject != NULL, "callback object NULL\n");

    CallbackHits = 0;
    Registration1 = ExRegisterCallback(CallbackObject, CallbackFunction, (PVOID)(ULONG_PTR)0xC0DE);
    ok(Registration1 != NULL, "register 1 failed\n");
    Registration2 = ExRegisterCallback(CallbackObject, CallbackFunction, (PVOID)(ULONG_PTR)0xC0DE);
    ok(Registration2 != NULL, "register 2 failed\n");

    ExNotifyCallback(CallbackObject, (PVOID)(ULONG_PTR)0x1111, (PVOID)(ULONG_PTR)0x2222);
    ok_eq_long(CallbackHits, 2L);
    ok_eq_pointer(ObservedArg1, (PVOID)(ULONG_PTR)0x1111);
    ok_eq_pointer(ObservedArg2, (PVOID)(ULONG_PTR)0x2222);

    if (Registration1) ExUnregisterCallback(Registration1);
    CallbackHits = 0;
    ExNotifyCallback(CallbackObject, NULL, NULL);
    ok_eq_long(CallbackHits, 1L);

    if (Registration2) ExUnregisterCallback(Registration2);
    CallbackHits = 0;
    ExNotifyCallback(CallbackObject, NULL, NULL);
    ok_eq_long(CallbackHits, 0L);

    ObDereferenceObject(CallbackObject);
}
