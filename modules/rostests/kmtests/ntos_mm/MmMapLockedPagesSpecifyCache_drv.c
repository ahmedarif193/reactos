/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Test driver for MmMapLockedPagesSpecifyCache function
 * PROGRAMMER:      Pierre Schweitzer <pierre@reactos.org>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#include "MmMapLockedPagesSpecifyCache.h"

C_ASSERT(MdlMappingNoWrite == TEST_MDL_MAPPING_NO_WRITE);
C_ASSERT(MdlMappingNoExecute == TEST_MDL_MAPPING_NO_EXECUTE);

static KMT_IRP_HANDLER TestIrpHandler;
static KMT_MESSAGE_HANDLER TestMessageHandler;

static PVOID CurrentBuffer;
static PMDL CurrentMdl;
static PVOID CurrentUser;
static SIZE_T NonCachedLength;
static PMDL ExitMdl;
static PVOID ExitKernelAddress;
static PEPROCESS ExitProcess;

static VOID
TestFreeExitBuffer(VOID)
{
    if (ExitKernelAddress != NULL)
    {
        MmUnmapLockedPages(ExitKernelAddress, ExitMdl);
        ExitKernelAddress = NULL;
    }
    if (ExitMdl != NULL)
    {
        MmFreePagesFromMdl(ExitMdl);
        ExFreePool(ExitMdl);
        ExitMdl = NULL;
    }
    if (ExitProcess != NULL)
    {
        ObDereferenceObject(ExitProcess);
        ExitProcess = NULL;
    }
}

static NTSTATUS
TestMapExitBuffer(PEXIT_BUFFER Request)
{
    PHYSICAL_ADDRESS Low = {{0}};
    PHYSICAL_ADDRESS High;
    PHYSICAL_ADDRESS Skip = {{0}};
    KAPC_STATE ApcState;
    NTSTATUS Status;
    ULONG Index;

    if (ExitMdl != NULL || ExitProcess != NULL || Request->Length == 0 ||
        Request->Length > 16 * 1024 * 1024 || (Request->Length % PAGE_SIZE) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Request->Address = NULL;
    Status = ObReferenceObjectByHandle(Request->Process, PROCESS_VM_OPERATION,
                                      *PsProcessType, UserMode,
                                      (PVOID *)&ExitProcess, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    High.QuadPart = MAXLONGLONG;
    ExitMdl = MmAllocatePagesForMdl(Low, High, Skip, Request->Length);
    if (ExitMdl == NULL || MmGetMdlByteCount(ExitMdl) != Request->Length)
    {
        TestFreeExitBuffer();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ExitKernelAddress = MmMapLockedPagesSpecifyCache(ExitMdl, KernelMode,
        MmCached, NULL, FALSE, NormalPagePriority);
    if (ExitKernelAddress == NULL)
    {
        TestFreeExitBuffer();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (Index = 0; Index < Request->Length / sizeof(ULONG); ++Index)
        ((PULONG)ExitKernelAddress)[Index] = Request->Pattern;

    KeStackAttachProcess((PRKPROCESS)ExitProcess, &ApcState);
    _SEH2_TRY
    {
        Request->Address = MmMapLockedPagesSpecifyCache(ExitMdl, UserMode,
            MmCached, NULL, FALSE, NormalPagePriority);
        Status = Request->Address != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    KeUnstackDetachProcess(&ApcState);
    if (!NT_SUCCESS(Status))
        TestFreeExitBuffer();
    return Status;
}

static NTSTATUS
TestCheckExitBuffer(PEXIT_BUFFER Request)
{
    LARGE_INTEGER Timeout = {{0}};
    NTSTATUS Status;
    ULONG Index;
    ULONG Mismatches = 0;

    if (ExitMdl == NULL || ExitProcess == NULL || ExitKernelAddress == NULL)
        return STATUS_INVALID_PARAMETER;
    /* A signaled process has finished tearing down its address space. The
     * MDL and backing remain held by this driver throughout that teardown. */
    Status = KeWaitForSingleObject(ExitProcess, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS)
        return STATUS_DEVICE_BUSY;
    for (Index = 0; Index < MmGetMdlByteCount(ExitMdl) / sizeof(ULONG); ++Index)
        Mismatches += ((PULONG)ExitKernelAddress)[Index] != Request->Pattern;
    ok_eq_ulong(Mismatches, 0);
    trace("MDL process exit: retained %lu bytes, %lu mismatches\n",
          MmGetMdlByteCount(ExitMdl), Mismatches);
    TestFreeExitBuffer();
    return STATUS_SUCCESS;
}

NTSTATUS
TestEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PCUNICODE_STRING RegistryPath,
    OUT PCWSTR *DeviceName,
    IN OUT INT *Flags)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(Flags);

    *DeviceName = L"MmMapLockedPagesSpecifyCache";

    KmtRegisterIrpHandler(IRP_MJ_CLEANUP, NULL, TestIrpHandler);
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);

    return Status;
}

VOID
TestUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();
}

VOID
TestCleanEverything(VOID)
{
    NTSTATUS SehStatus;

    if (CurrentMdl == NULL)
    {
        return;
    }

    if (CurrentUser != NULL)
    {
        SehStatus = STATUS_SUCCESS;
        _SEH2_TRY
        {
            MmUnmapLockedPages(CurrentUser, CurrentMdl);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            SehStatus = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        ok_eq_hex(SehStatus, STATUS_SUCCESS);
        CurrentUser = NULL;
    }

    SehStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        MmUnlockPages(CurrentMdl);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        SehStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    ok_eq_hex(SehStatus, STATUS_SUCCESS);
    IoFreeMdl(CurrentMdl);
    if (NonCachedLength)
    {
        MmFreeNonCachedMemory(CurrentBuffer, NonCachedLength);
    }
    else
    {
        ExFreePoolWithTag(CurrentBuffer, 'MLPC');
    }
    CurrentMdl = NULL;
}

static
NTSTATUS
TestMessageHandler(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG ControlCode,
    IN PVOID Buffer OPTIONAL,
    IN SIZE_T InLength,
    IN OUT PSIZE_T OutLength)
{
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS SehStatus;

    switch (ControlCode)
    {
        case IOCTL_QUERY_BUFFER:
        {
            ok(Buffer != NULL, "Buffer is NULL\n");
            ok_eq_size(InLength, sizeof(QUERY_BUFFER));
            ok_eq_size(*OutLength, sizeof(QUERY_BUFFER));
            ok_eq_pointer(CurrentMdl, NULL);

            TestCleanEverything();

            ok(ExGetPreviousMode() == UserMode, "Not coming from umode!\n");
            if (!skip(Buffer && InLength >= sizeof(QUERY_BUFFER) && *OutLength >= sizeof(QUERY_BUFFER), "Cannot read/write from/to buffer!\n"))
            {
                PQUERY_BUFFER QueryBuffer;
                USHORT Length;
                MEMORY_CACHING_TYPE CacheType;

                QueryBuffer = Buffer;
                CacheType = (QueryBuffer->Cached ? MmCached : MmNonCached);
                Length = QueryBuffer->Length;
                CurrentUser = NULL;
                ok(Length > 0, "Null size!\n");

                if (!skip(Length > 0, "Null size!\n"))
                {
                    if (QueryBuffer->Cached)
                    {
                        CurrentBuffer = ExAllocatePoolWithTag(NonPagedPool, Length, 'MLPC');
                        ok(CurrentBuffer != NULL, "ExAllocatePool failed!\n");
                        NonCachedLength = 0;
                    }
                    else
                    {
                        CurrentBuffer = MmAllocateNonCachedMemory(Length);
                        ok(CurrentBuffer != NULL, "MmAllocateNonCachedMemory failed!\n");
                        if (CurrentBuffer)
                        {
                            RtlZeroMemory(CurrentBuffer, Length);
                            NonCachedLength = Length;
                        }
                    }
                    if (!skip(CurrentBuffer != NULL, "ExAllocatePool failed!\n"))
                    {
                        CurrentMdl = IoAllocateMdl(CurrentBuffer, Length, FALSE, FALSE, NULL);
                        ok(CurrentMdl != NULL, "IoAllocateMdl failed!\n");
                        if (!skip(CurrentMdl != NULL, "IoAllocateMdl failed!\n"))
                        {
                            KIRQL Irql;

                            SehStatus = STATUS_SUCCESS;
                            _SEH2_TRY
                            {
                                MmProbeAndLockPages(CurrentMdl, KernelMode, IoWriteAccess);
                            }
                            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                            {
                                SehStatus = _SEH2_GetExceptionCode();
                            }
                            _SEH2_END;
                            ok_eq_hex(SehStatus, STATUS_SUCCESS);

                            Irql = KeGetCurrentIrql();
                            ok(Irql <= APC_LEVEL, "IRQL > APC_LEVEL: %d\n", Irql);

                            SehStatus = STATUS_SUCCESS;
                            _SEH2_TRY
                            {
                                CurrentUser = MmMapLockedPagesSpecifyCache(CurrentMdl, UserMode, CacheType, QueryBuffer->Buffer, FALSE, NormalPagePriority | QueryBuffer->MappingFlags);
                            }
                            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                            {
                                SehStatus = _SEH2_GetExceptionCode();
                            }
                            _SEH2_END;
                            if (QueryBuffer->Status != -1)
                            {
                                ok_eq_hex(SehStatus, QueryBuffer->Status);
                                if (NT_SUCCESS(QueryBuffer->Status))
                                {
                                    ok(CurrentUser != NULL, "MmMapLockedPagesSpecifyCache failed!\n");
                                }
                                else
                                {
                                    ok(CurrentUser == NULL, "MmMapLockedPagesSpecifyCache succeeded!\n");
                                }
                            }
                            QueryBuffer->Status = SehStatus;
                        }
                        else
                        {
                            ExFreePoolWithTag(CurrentBuffer, 'MLPC');
                        }
                    }
                }

                QueryBuffer->Buffer = CurrentUser;
                *OutLength = sizeof(QUERY_BUFFER);
            }

            break;
        }
        case IOCTL_READ_BUFFER:
        {
            ok(Buffer != NULL, "Buffer is NULL\n");
            ok_eq_size(InLength, sizeof(READ_BUFFER));
            ok_eq_size(*OutLength, 0);
            ok(CurrentMdl != NULL, "MDL is not in use!\n");

            if (!skip(Buffer && InLength >= sizeof(READ_BUFFER), "Cannot read from buffer!\n"))
            {
                PREAD_BUFFER ReadBuffer;

                ReadBuffer = Buffer;
                if (!skip(ReadBuffer && ReadBuffer->Buffer == CurrentUser, "Cannot find matching MDL\n"))
                {
                    if (ReadBuffer->Buffer != NULL)
                    {
                        USHORT i;
                        PULONG KBuffer = MmGetSystemAddressForMdlSafe(CurrentMdl, NormalPagePriority);
                        ok(KBuffer != NULL, "Failed to get kmode ptr\n");
                        ok(ReadBuffer->Length % sizeof(ULONG) == 0, "Invalid size: %d\n", ReadBuffer->Length);

                        if (!skip(Buffer != NULL, "Failed to get kmode ptr\n"))
                        {
                            for (i = 0; i < ReadBuffer->Length / sizeof(ULONG); ++i)
                            {
                                ok_eq_ulong(KBuffer[i], ReadBuffer->Pattern);
                            }
                        }
                    }
                }

                TestCleanEverything();
            }

            break;
        }
        case IOCTL_CLEAN:
        {
            TestCleanEverything();
            break;
        }
        case IOCTL_MAP_EXIT_BUFFER:
        case IOCTL_CHECK_EXIT_BUFFER:
        {
            if (Buffer == NULL || InLength != sizeof(EXIT_BUFFER) ||
                *OutLength < sizeof(EXIT_BUFFER))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Status = ControlCode == IOCTL_MAP_EXIT_BUFFER ?
                TestMapExitBuffer(Buffer) : TestCheckExitBuffer(Buffer);
            *OutLength = sizeof(EXIT_BUFFER);
            break;
        }
        default:
            ok(0, "Got an unknown message! DeviceObject=%p, ControlCode=%lu, Buffer=%p, In=%lu, Out=%lu bytes\n",
                    DeviceObject, ControlCode, Buffer, InLength, *OutLength);
            break;
    }

    return Status;
}

static
NTSTATUS
TestIrpHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IoStack)
{
    NTSTATUS Status;

    PAGED_CODE();

    DPRINT("IRP %x/%x\n", IoStack->MajorFunction, IoStack->MinorFunction);
    ASSERT(IoStack->MajorFunction == IRP_MJ_CLEANUP);

    Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;

    if (IoStack->MajorFunction == IRP_MJ_CLEANUP)
    {
        TestCleanEverything();
        Status = STATUS_SUCCESS;
    }

    if (Status == STATUS_PENDING)
    {
        IoMarkIrpPending(Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        Status = STATUS_PENDING;
    }
    else
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return Status;
}
