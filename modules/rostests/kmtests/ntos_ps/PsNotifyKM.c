/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite process/thread notify registration API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static volatile LONG ProcessNotifications;
static volatile LONG ProcessNotificationsEx;
static volatile LONG ThreadNotifications;
static volatile LONG ImageNotifications;

static
VOID
NTAPI
ProcessNotify(
    _In_ HANDLE ParentId,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN Create)
{
    UNREFERENCED_PARAMETER(ParentId);
    UNREFERENCED_PARAMETER(ProcessId);
    UNREFERENCED_PARAMETER(Create);
    InterlockedIncrement(&ProcessNotifications);
}

#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
static
VOID
NTAPI
ProcessNotifyEx(
    _Inout_ PEPROCESS Process,
    _In_ HANDLE ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ProcessId);
    UNREFERENCED_PARAMETER(CreateInfo);
    InterlockedIncrement(&ProcessNotificationsEx);
}

static
VOID
TestCreateProcessNotifyEx2(VOID)
{
    NTSTATUS Status;

    Status = PsSetCreateProcessNotifyRoutineEx2((PSCREATEPROCESSNOTIFYTYPE)1, ProcessNotifyEx, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = PsSetCreateProcessNotifyRoutineEx2(PsCreateProcessNotifySubsystems, NULL, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = PsSetCreateProcessNotifyRoutineEx2(PsCreateProcessNotifySubsystems, ProcessNotifyEx, TRUE);
    ok_eq_hex(Status, STATUS_PROCEDURE_NOT_FOUND);

    Status = PsSetCreateProcessNotifyRoutineEx2(PsCreateProcessNotifySubsystems, ProcessNotifyEx, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = PsSetCreateProcessNotifyRoutineEx2(PsCreateProcessNotifySubsystems, ProcessNotifyEx, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = PsSetCreateProcessNotifyRoutineEx2(PsCreateProcessNotifySubsystems, ProcessNotifyEx, TRUE);
    ok_eq_hex(Status, STATUS_PROCEDURE_NOT_FOUND);
}
#endif

static
VOID
NTAPI
ThreadNotify(
    _In_ HANDLE ProcessId,
    _In_ HANDLE ThreadId,
    _In_ BOOLEAN Create)
{
    UNREFERENCED_PARAMETER(ProcessId);
    UNREFERENCED_PARAMETER(ThreadId);
    UNREFERENCED_PARAMETER(Create);
    InterlockedIncrement(&ThreadNotifications);
}

static
VOID
NTAPI
ImageNotify(
    _In_opt_ PUNICODE_STRING FullImageName,
    _In_ HANDLE ProcessId,
    _In_ PIMAGE_INFO ImageInfo)
{
    UNREFERENCED_PARAMETER(FullImageName);
    UNREFERENCED_PARAMETER(ProcessId);
    UNREFERENCED_PARAMETER(ImageInfo);
    InterlockedIncrement(&ImageNotifications);
}

static KEVENT NotifyThreadDone;

static
VOID
NTAPI
NotifyTestThread(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    KeSetEvent(&NotifyThreadDone, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

START_TEST(PsNotifyKM)
{
    NTSTATUS Status;
    HANDLE ThreadHandle;
    PVOID Thread;
    LARGE_INTEGER Timeout;

    ProcessNotifications = 0;
    ProcessNotificationsEx = 0;
    ThreadNotifications = 0;
    ImageNotifications = 0;

#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
    TestCreateProcessNotifyEx2();
#endif

    Status = PsSetCreateProcessNotifyRoutine(ProcessNotify, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = PsSetCreateThreadNotifyRoutine(ThreadNotify);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = PsSetLoadImageNotifyRoutine(ImageNotify);
    ok_eq_hex(Status, STATUS_SUCCESS);

    KeInitializeEvent(&NotifyThreadDone, NotificationEvent, FALSE);
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, NotifyTestThread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &Thread, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ObCloseHandle(ThreadHandle, KernelMode);

        Timeout.QuadPart = -10 * 1000 * 1000 * 10;
        Status = KeWaitForSingleObject(&NotifyThreadDone, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, &Timeout);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }
        ObDereferenceObject(Thread);

        ok(ThreadNotifications >= 2, "thread notifications %ld\n", ThreadNotifications);
    }

    Status = PsSetCreateProcessNotifyRoutine(ProcessNotify, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = PsRemoveCreateThreadNotifyRoutine(ThreadNotify);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = PsRemoveLoadImageNotifyRoutine(ImageNotify);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = PsRemoveCreateThreadNotifyRoutine(ThreadNotify);
    ok_eq_hex(Status, STATUS_PROCEDURE_NOT_FOUND);

    Status = PsRemoveLoadImageNotifyRoutine(ImageNotify);
    ok_eq_hex(Status, STATUS_PROCEDURE_NOT_FOUND);
}
