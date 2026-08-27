/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Modern I/O manager compatibility tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef struct _TEST_IO_WORK_CONTEXT
{
    KEVENT Event;
    volatile LONG Calls;
    PDEVICE_OBJECT DeviceObject;
} TEST_IO_WORK_CONTEXT, *PTEST_IO_WORK_CONTEXT;

static const DEVPROPKEY TestMissingInterfacePropertyKey =
{
    {0x026e516e, 0xb814, 0x414b, {0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22}},
    2
};

NTKERNELAPI
NTSTATUS
NTAPI
IoReportRootDevice(
    _In_ PDRIVER_OBJECT DriverObject);

static
VOID
NTAPI
TestIoWorkRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PTEST_IO_WORK_CONTEXT WorkContext = Context;

    WorkContext->DeviceObject = DeviceObject;
    InterlockedIncrement(&WorkContext->Calls);
    KeSetEvent(&WorkContext->Event, IO_NO_INCREMENT, FALSE);
}

static
VOID
TestCallerOwnedWorkItem(VOID)
{
    TEST_IO_WORK_CONTEXT Context;
    PIO_WORKITEM WorkItem;
    PDEVICE_OBJECT DeviceObject;
    LARGE_INTEGER Timeout;
    ULONG WorkItemSize;
    NTSTATUS Status;

    DeviceObject = KmtDriverObject->DeviceObject;
    ok(DeviceObject != NULL, "kmtest driver had no device object\n");
    if (DeviceObject == NULL)
        return;

    WorkItemSize = IoSizeofWorkItem();
    trace("IoSizeofWorkItem returned 0x%lx\n", WorkItemSize);
#if defined(_M_ARM64)
    ok_eq_ulong(WorkItemSize, 0x58);
#else
    ok(WorkItemSize >= sizeof(PVOID) * 4,
       "work item size 0x%lx was unexpectedly small\n",
       WorkItemSize);
#endif

    WorkItem = ExAllocatePoolZero(NonPagedPoolNx,
                                  WorkItemSize,
                                  'tWIK');
    ok(WorkItem != NULL, "failed to allocate caller-owned work item\n");
    if (WorkItem == NULL)
        return;

    KeInitializeEvent(&Context.Event, NotificationEvent, FALSE);
    Context.Calls = 0;
    Context.DeviceObject = NULL;
    IoInitializeWorkItem(DeviceObject, WorkItem);
    IoQueueWorkItem(WorkItem,
                    TestIoWorkRoutine,
                    DelayedWorkQueue,
                    &Context);

    Timeout.QuadPart = -10 * 1000 * 1000;
    Status = KeWaitForSingleObject(&Context.Event,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Context.Calls, 1);
    ok_eq_pointer(Context.DeviceObject, DeviceObject);
    ExFreePoolWithTag(WorkItem, 'tWIK');
}

static
VOID
TestDriverMetadata(VOID)
{
    UNICODE_STRING FullPath;
    HANDLE DriverKey;
    NTSTATUS Status;

    RtlZeroMemory(&FullPath, sizeof(FullPath));
    Status = IoQueryFullDriverPath(KmtDriverObject, &FullPath);
    trace("IoQueryFullDriverPath returned 0x%08lx, path %wZ\n",
          Status,
          &FullPath);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(FullPath.Buffer != NULL, "full driver path had no buffer\n");
        ok(FullPath.Length != 0, "full driver path was empty\n");
        ExFreePool(FullPath.Buffer);
    }

    DriverKey = NULL;
    Status = IoOpenDriverRegistryKey(KmtDriverObject,
                                     DriverRegKeyPersistentState,
                                     KEY_READ,
                                     0,
                                     &DriverKey);
    trace("IoOpenDriverRegistryKey(PersistentState) returned 0x%08lx, handle %p\n",
          Status,
          DriverKey);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ZwClose(DriverKey);

    DriverKey = (HANDLE)(ULONG_PTR)0xA5A5A5A5;
    Status = IoOpenDriverRegistryKey(KmtDriverObject,
                                     (DRIVER_REGKEY_TYPE)MAXULONG,
                                     KEY_READ,
                                     0,
                                     &DriverKey);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_pointer(DriverKey, (HANDLE)(ULONG_PTR)0xA5A5A5A5);
}

static
VOID
TestMissingInterfaceProperty(VOID)
{
    UNICODE_STRING SymbolicLink =
        RTL_CONSTANT_STRING(L"\\??\\KmtMissingDeviceInterface");
    DEVPROPTYPE Type;
    ULONG RequiredSize;
    NTSTATUS Status;

    RequiredSize = MAXULONG;
    Type = MAXULONG;
    Status = IoGetDeviceInterfacePropertyData(&SymbolicLink,
                                              &TestMissingInterfacePropertyKey,
                                              0,
                                              0,
                                              0,
                                              NULL,
                                              &RequiredSize,
                                              &Type);
    trace("IoGetDeviceInterfacePropertyData(missing) returned 0x%08lx, size %lu, type 0x%lx\n",
          Status,
          RequiredSize,
          Type);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    ok_eq_ulong(RequiredSize, MAXULONG);
    ok_eq_ulong(Type, MAXULONG);
}

static
VOID
TestDeviceNumaNode(VOID)
{
    PDEVICE_OBJECT DeviceObject;
    USHORT NodeNumber;
    NTSTATUS Status;

    NodeNumber = MAXUSHORT;
    Status = IoGetDeviceNumaNode(NULL, &NodeNumber);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_ulong(NodeNumber, MAXUSHORT);

    DeviceObject = KmtDriverObject->DeviceObject;
    ok(DeviceObject != NULL, "kmtest driver had no device object\n");
    if (DeviceObject == NULL)
        return;

    Status = IoGetDeviceNumaNode(DeviceObject, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    NodeNumber = MAXUSHORT;
    Status = IoGetDeviceNumaNode(DeviceObject, &NodeNumber);
    trace("IoGetDeviceNumaNode returned 0x%08lx, node %u\n",
          Status,
          NodeNumber);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(NodeNumber, 0);
}

START_TEST(IoModern)
{
    TestCallerOwnedWorkItem();
    TestDriverMetadata();
    TestMissingInterfaceProperty();
    TestDeviceNumaNode();
}

START_TEST(IoReportRootDevice)
{
    NTSTATUS Status;

    trace("IoReportRootDevice resolved to %p\n", IoReportRootDevice);

#ifdef KMT_PERSISTENT_PNP_TESTS
    Status = IoReportRootDevice(KmtDriverObject);
    trace("first IoReportRootDevice returned 0x%08lx, flags 0x%lx\n",
          Status,
          KmtDriverObject->Flags);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok((KmtDriverObject->Flags & 0x00000800) != 0,
       "root-device-reported flag was not set\n");

    Status = IoReportRootDevice(KmtDriverObject);
    trace("second IoReportRootDevice returned 0x%08lx, flags 0x%lx\n",
          Status,
          KmtDriverObject->Flags);
    ok_eq_hex(Status, STATUS_SUCCESS);
#else
    UNREFERENCED_PARAMETER(Status);
    skip(FALSE,
         "persistent root-device test disabled; rebuild with "
         "KMT_PERSISTENT_PNP_TESTS to create ROOT\\kmtest_drv\\0000\n");
#endif
}
