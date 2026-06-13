/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite ZwDuplicateObject API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(ZwDuplicateKM)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE EventHandle = NULL;
    HANDLE DupHandle = NULL;
    PVOID Object1 = NULL;
    PVOID Object2 = NULL;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateEvent(&EventHandle, EVENT_ALL_ACCESS, &ObjectAttributes, NotificationEvent, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ZwDuplicateObject(ZwCurrentProcess(), EventHandle, ZwCurrentProcess(), &DupHandle, EVENT_ALL_ACCESS, OBJ_KERNEL_HANDLE, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(DupHandle != NULL, "dup handle NULL\n");
    ok(DupHandle != EventHandle, "dup handle equals source\n");

    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(EventHandle, EVENT_ALL_ACCESS, *ExEventObjectType, KernelMode, &Object1, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = ObReferenceObjectByHandle(DupHandle, EVENT_ALL_ACCESS, *ExEventObjectType, KernelMode, &Object2, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(Object1, Object2);
        if (Object1) ObDereferenceObject(Object1);
        if (Object2) ObDereferenceObject(Object2);

        Status = ZwSetEvent(DupHandle, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = ZwClose(DupHandle);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    Status = ZwDuplicateObject(ZwCurrentProcess(), EventHandle, ZwCurrentProcess(), &DupHandle, 0, OBJ_KERNEL_HANDLE, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwSetEvent(EventHandle, NULL);
        ok_eq_hex(Status, GetNTVersion() >= _WIN32_WINNT_WIN8 ? STATUS_SUCCESS : STATUS_INVALID_HANDLE);
        Status = ZwSetEvent(DupHandle, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(DupHandle);
    }
    else
    {
        ZwClose(EventHandle);
    }
}
