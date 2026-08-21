/*
 * PROJECT:     ReactOS boot storage read benchmark driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Read-only benchmark immediately after a USB disk stack starts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ntddstor.h>
#include <wdmguid.h>
#include <reactos/storage_read_benchmark.h>

#define USBREAD_TAG 'BRsU'

typedef struct _USBREAD_WORK_CONTEXT
{
    WORK_QUEUE_ITEM WorkItem;
    UNICODE_STRING InterfaceName;
    WCHAR NameBuffer[ANYSIZE_ARRAY];
} USBREAD_WORK_CONTEXT, *PUSBREAD_WORK_CONTEXT;

static PVOID NotificationEntry;
static volatile LONG BenchmarkState;

static BOOLEAN
UsbReadUnicodeContains(
    _In_ PCUNICODE_STRING String,
    _In_ PCWSTR Needle)
{
    SIZE_T NeedleChars = wcslen(Needle);
    SIZE_T StringChars = String->Length / sizeof(WCHAR);
    SIZE_T Index;

    if (NeedleChars > StringChars)
        return FALSE;

    for (Index = 0; Index + NeedleChars <= StringChars; Index++)
    {
        if (RtlCompareMemory(&String->Buffer[Index], Needle,
                             NeedleChars * sizeof(WCHAR)) ==
            NeedleChars * sizeof(WCHAR))
        {
            return TRUE;
        }
    }
    return FALSE;
}

static VOID NTAPI
UsbReadBenchmarkWorker(
    _In_ PVOID Parameter)
{
    PUSBREAD_WORK_CONTEXT Context = Parameter;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    LARGE_INTEGER Start;
    LARGE_INTEGER End;
    LARGE_INTEGER Frequency;
    LARGE_INTEGER IoStart;
    LARGE_INTEGER IoEnd;
    HANDLE Handle = NULL;
    PUCHAR Buffer;
    ULONGLONG Done = 0;
    ULONGLONG MaxIoOffset = 0;
    ULONGLONG MiBps100;
    ULONGLONG MaxIoUs;
    LONGLONG IoTicks;
    LONGLONG MaxIoTicks = 0;
    ULONG Ops = 0;
    ULONG Over10Ms = 0;
    ULONG Over100Ms = 0;
    ULONG Over1S = 0;
    NTSTATUS Status;

    DbgPrint("USBREADBENCH KERNEL BEGIN interface=%wZ offset=%I64u bytes=%I64u block=%lu qd=1\n",
             &Context->InterfaceName,
             STORAGE_READ_BENCHMARK_OFFSET,
             STORAGE_READ_BENCHMARK_LENGTH,
             STORAGE_READ_BENCHMARK_BLOCK_SIZE);

    InitializeObjectAttributes(&ObjectAttributes,
                               &Context->InterfaceName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwCreateFile(&Handle,
                          FILE_GENERIC_READ | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatus,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE |
                          FILE_NO_INTERMEDIATE_BUFFERING |
                          FILE_SEQUENTIAL_ONLY |
                          FILE_SYNCHRONOUS_IO_NONALERT,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("USBREADBENCH KERNEL FAILED open status=%08lx\n", Status);
        goto Finished;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   STORAGE_READ_BENCHMARK_BLOCK_SIZE,
                                   USBREAD_TAG);
    if (Buffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        DbgPrint("USBREADBENCH KERNEL FAILED allocation status=%08lx\n", Status);
        goto Finished;
    }

    Start = KeQueryPerformanceCounter(&Frequency);
    while (Done < STORAGE_READ_BENCHMARK_LENGTH)
    {
        Offset.QuadPart = STORAGE_READ_BENCHMARK_OFFSET + Done;
        IoStart = KeQueryPerformanceCounter(NULL);
        Status = ZwReadFile(Handle,
                            NULL,
                            NULL,
                            NULL,
                            &IoStatus,
                            Buffer,
                            STORAGE_READ_BENCHMARK_BLOCK_SIZE,
                            &Offset,
                            NULL);
        IoEnd = KeQueryPerformanceCounter(NULL);
        IoTicks = IoEnd.QuadPart - IoStart.QuadPart;
        if (IoTicks > MaxIoTicks)
        {
            MaxIoTicks = IoTicks;
            MaxIoOffset = Offset.QuadPart;
        }
        if (IoTicks >= Frequency.QuadPart / 100)
            Over10Ms++;
        if (IoTicks >= Frequency.QuadPart / 10)
            Over100Ms++;
        if (IoTicks >= Frequency.QuadPart)
            Over1S++;
        if (!NT_SUCCESS(Status) ||
            IoStatus.Information != STORAGE_READ_BENCHMARK_BLOCK_SIZE)
        {
            DbgPrint("USBREADBENCH KERNEL FAILED read status=%08lx iosb=%08lx offset=%I64u transferred=%Iu\n",
                     Status,
                     IoStatus.Status,
                     Offset.QuadPart,
                     IoStatus.Information);
            if (NT_SUCCESS(Status))
                Status = STATUS_DEVICE_DATA_ERROR;
            break;
        }
        Done += STORAGE_READ_BENCHMARK_BLOCK_SIZE;
        Ops++;
        if ((Ops % 512) == 0 && Done != STORAGE_READ_BENCHMARK_LENGTH)
        {
            DbgPrint("USBREADBENCH KERNEL PROGRESS bytes=%I64u ops=%lu\n",
                     Done,
                     Ops);
        }
    }
    End = KeQueryPerformanceCounter(NULL);

    if (NT_SUCCESS(Status) && End.QuadPart != Start.QuadPart)
    {
        MiBps100 = ((Done >> 20) * (ULONGLONG)Frequency.QuadPart * 100) /
                    (ULONGLONG)(End.QuadPart - Start.QuadPart);
        DbgPrint("USBREADBENCH KERNEL RESULT bytes=%I64u ops=%lu qpc=%I64u freq=%I64u mibps=%I64u.%02I64u status=passed\n",
                 Done,
                 Ops,
                 End.QuadPart - Start.QuadPart,
                 Frequency.QuadPart,
                 MiBps100 / 100,
                 MiBps100 % 100);
        MaxIoUs = ((ULONGLONG)MaxIoTicks * 1000000) /
                  (ULONGLONG)Frequency.QuadPart;
        DbgPrint("USBREADBENCH KERNEL LATENCY max-us=%I64u offset=%I64u over-10ms=%lu over-100ms=%lu over-1s=%lu\n",
                 MaxIoUs,
                 MaxIoOffset,
                 Over10Ms,
                 Over100Ms,
                 Over1S);
    }

    ExFreePoolWithTag(Buffer, USBREAD_TAG);

Finished:
    if (Handle != NULL)
        ZwClose(Handle);
    InterlockedExchange(&BenchmarkState, NT_SUCCESS(Status) ? 2 : 0);
    ExFreePoolWithTag(Context, USBREAD_TAG);
}

static NTSTATUS NTAPI
UsbReadInterfaceChange(
    _In_ PVOID NotificationStructure,
    _In_opt_ PVOID Context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION Notification = NotificationStructure;
    PUSBREAD_WORK_CONTEXT WorkContext;
    SIZE_T AllocationSize;

    UNREFERENCED_PARAMETER(Context);

    if (!IsEqualGUIDAligned(&Notification->Event, &GUID_DEVICE_INTERFACE_ARRIVAL) ||
        !UsbReadUnicodeContains(Notification->SymbolicLinkName, L"USBSTOR") ||
        InterlockedCompareExchange(&BenchmarkState, 1, 0) != 0)
    {
        return STATUS_SUCCESS;
    }

    AllocationSize = FIELD_OFFSET(USBREAD_WORK_CONTEXT, NameBuffer) +
                     Notification->SymbolicLinkName->Length + sizeof(UNICODE_NULL);
    WorkContext = ExAllocatePoolWithTag(NonPagedPool, AllocationSize, USBREAD_TAG);
    if (WorkContext == NULL)
    {
        InterlockedExchange(&BenchmarkState, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    WorkContext->InterfaceName.Buffer = WorkContext->NameBuffer;
    WorkContext->InterfaceName.Length = Notification->SymbolicLinkName->Length;
    WorkContext->InterfaceName.MaximumLength = Notification->SymbolicLinkName->Length +
                                               sizeof(UNICODE_NULL);
    RtlCopyMemory(WorkContext->NameBuffer,
                  Notification->SymbolicLinkName->Buffer,
                  Notification->SymbolicLinkName->Length);
    WorkContext->NameBuffer[Notification->SymbolicLinkName->Length / sizeof(WCHAR)] = UNICODE_NULL;
    ExInitializeWorkItem(&WorkContext->WorkItem, UsbReadBenchmarkWorker, WorkContext);
    ExQueueWorkItem(&WorkContext->WorkItem, DelayedWorkQueue);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);

    Status = IoRegisterPlugPlayNotification(
                 EventCategoryDeviceInterfaceChange,
                 PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
                 (PVOID)&GUID_DEVINTERFACE_DISK,
                 DriverObject,
                 UsbReadInterfaceChange,
                 NULL,
                 &NotificationEntry);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("USBREADBENCH KERNEL FAILED notification status=%08lx\n", Status);
        return Status;
    }

    DbgPrint("USBREADBENCH KERNEL ARMED block=%lu offset=%I64u bytes=%I64u qd=1\n",
             STORAGE_READ_BENCHMARK_BLOCK_SIZE,
             STORAGE_READ_BENCHMARK_OFFSET,
             STORAGE_READ_BENCHMARK_LENGTH);
    return STATUS_SUCCESS;
}
