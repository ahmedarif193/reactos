/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPLv2.1+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Test driver for CcCopyRead function
 * PROGRAMMER:      Pierre Schweitzer <pierre@reactos.org>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define IOCTL_RUN_TEST 1

typedef struct _TEST_FCB
{
    FSRTL_ADVANCED_FCB_HEADER Header;
    SECTION_OBJECT_POINTERS SectionObjectPointers;
    FAST_MUTEX HeaderMutex;
    BOOLEAN BigFile;
} TEST_FCB, *PTEST_FCB;

static PFILE_OBJECT TestFileObject;
static PDEVICE_OBJECT TestDeviceObject;
static KMT_IRP_HANDLER TestIrpHandler;
static KMT_MESSAGE_HANDLER TestMessageHandler;
static FAST_IO_DISPATCH TestFastIoDispatch;

BOOLEAN ReadCalledNonCached;
LARGE_INTEGER ReadOffset;
ULONG ReadLength;

static
BOOLEAN
NTAPI
FastIoRead(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _In_ ULONG LockKey,
    _Out_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    IoStatus->Status = STATUS_NOT_SUPPORTED;
    return FALSE;
}

NTSTATUS
TestEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PCUNICODE_STRING RegistryPath,
    _Out_ PCWSTR *DeviceName,
    _Inout_ INT *Flags)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(RegistryPath);

    *DeviceName = L"CcCopyRead";
    *Flags = TESTENTRY_NO_EXCLUSIVE_DEVICE |
             TESTENTRY_BUFFERED_IO_DEVICE |
             TESTENTRY_NO_READONLY_DEVICE;

    KmtInitializeCcPagingReadIrql();

    KmtRegisterIrpHandler(IRP_MJ_CLEANUP, NULL, TestIrpHandler);
    KmtRegisterIrpHandler(IRP_MJ_CLOSE, NULL, TestIrpHandler);
    KmtRegisterIrpHandler(IRP_MJ_CREATE, NULL, TestIrpHandler);
    KmtRegisterIrpHandler(IRP_MJ_READ, NULL, TestIrpHandler);
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);

    TestFastIoDispatch.SizeOfFastIoDispatch = sizeof(TestFastIoDispatch);
    TestFastIoDispatch.FastIoRead = FastIoRead;
    DriverObject->FastIoDispatch = &TestFastIoDispatch;


    return Status;
}

VOID
TestUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();
}

BOOLEAN
NTAPI
AcquireForLazyWrite(
    _In_ PVOID Context,
    _In_ BOOLEAN Wait)
{
    return TRUE;
}

VOID
NTAPI
ReleaseFromLazyWrite(
    _In_ PVOID Context)
{
    return;
}

BOOLEAN
NTAPI
AcquireForReadAhead(
    _In_ PVOID Context,
    _In_ BOOLEAN Wait)
{
    return TRUE;
}

VOID
NTAPI
ReleaseFromReadAhead(
    _In_ PVOID Context)
{
    return;
}

static CACHE_MANAGER_CALLBACKS Callbacks = {
    AcquireForLazyWrite,
    ReleaseFromLazyWrite,
    AcquireForReadAhead,
    ReleaseFromReadAhead,
};

static
PVOID
MapAndLockUserBuffer(
    _In_ _Out_ PIRP Irp,
    _In_ ULONG BufferLength)
{
    PMDL Mdl;

    if (Irp->MdlAddress == NULL)
    {
        Mdl = IoAllocateMdl(Irp->UserBuffer, BufferLength, FALSE, FALSE, Irp);
        if (Mdl == NULL)
        {
            return NULL;
        }

        _SEH2_TRY
        {
            MmProbeAndLockPages(Mdl, Irp->RequestorMode, IoWriteAccess);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            IoFreeMdl(Mdl);
            Irp->MdlAddress = NULL;
            _SEH2_YIELD(return NULL);
        }
        _SEH2_END;
    }

    return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
}

static
void
reset_read(void)
{
    ReadCalledNonCached = FALSE;
    ReadOffset.QuadPart = MAXLONGLONG;
    ReadLength = MAXULONG;
}

#define ok_read_called(_Offset, _Length) do {                                               \
    LONGLONG ExpectedOffset = (LONGLONG)(_Offset);                                          \
    ok(ReadCalledNonCached, "CcCopyRead should have triggerred a non-cached read.\n");      \
    ok(ReadOffset.QuadPart == ExpectedOffset ||                                             \
       ReadOffset.QuadPart == ExpectedOffset + PAGE_SIZE,                                   \
       "ReadOffset.QuadPart = %I64d, expected %I64d or %I64d\n",                            \
       ReadOffset.QuadPart, ExpectedOffset, ExpectedOffset + PAGE_SIZE);                    \
    ok_eq_ulong(ReadLength, (ULONG)(_Length));                                              \
}while(0)

#define ok_read_not_called() ok(!ReadCalledNonCached, "CcCopyRead shouldn't have triggered a read.\n")

static
VOID
Test_CcCopyRead(PFILE_OBJECT FileObject)
{

    BOOLEAN Ret;
    LARGE_INTEGER Offset;
    CHAR Buffer[10];
    IO_STATUS_BLOCK IoStatus;
    BOOLEAN IsNt5 = KmtIsNt5I386();

    memset(Buffer, 0xAC, 10);

    /* Test bogus file object & file offset */
    Ret = 'x';
    KmtStartSeh()
        Ret = CcCopyRead(FileObject, NULL, 0, FALSE, NULL, &IoStatus);
    KmtEndSeh(STATUS_ACCESS_VIOLATION);
    ok_eq_char(Ret, 'x');

    Ret = 'x';
    Offset.QuadPart = 0;
    KmtStartSeh()
        Ret = CcCopyRead(NULL, &Offset, 10, FALSE, Buffer, &IoStatus);
    KmtEndSeh(STATUS_ACCESS_VIOLATION);
    ok_eq_char(Ret, 'x');

    /* What happens on invalid buffer */
    Ret = 'x';
    Offset.QuadPart = 0;
    memset(&IoStatus, 0xAB, sizeof(IoStatus));
    reset_read();
    KmtStartSeh()
        Ret = CcCopyRead(FileObject, &Offset, 0, TRUE, NULL, &IoStatus);
    KmtEndSeh(IsNt5 ? STATUS_SUCCESS : STATUS_INVALID_USER_BUFFER);
    if (IsNt5)
    {
        ok_bool_true(Ret, "CcCopyRead(0, NULL) should succeed\n");
    }
    else
    {
        ok_eq_char(Ret, 'x');
    }
    ok_read_not_called();
    if (IsNt5)
    {
        ok_eq_hex(IoStatus.Status, STATUS_SUCCESS);
        ok_eq_ulongptr(IoStatus.Information, 0);
    }
    else
    {
        ok_eq_hex(IoStatus.Status, 0xABABABAB);
        ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)0xABABABABABABABAB);
    }

    skip(FALSE, "CcCopyRead with a NULL nonzero output buffer hangs Windows Vista+\n");

    /* Make the first page resident. */
    Ret = 'x';
    Offset.QuadPart = 0;
    memset(&IoStatus, 0xAB, sizeof(IoStatus));
    reset_read();
    KmtStartSeh()
        Ret = CcCopyRead(FileObject, &Offset, 10, TRUE, Buffer, &IoStatus);
    KmtEndSeh(STATUS_SUCCESS);
    ok_bool_true(Ret, "CcCopyRead should succeed\n");
    ok_read_called(0, PAGE_SIZE);
    ok_eq_hex(IoStatus.Status, STATUS_SUCCESS);
    ok_eq_ulongptr(IoStatus.Information, 10);

    /* So this one succeeds, as the page is now resident */
    Ret = 'x';
    Offset.QuadPart = 0;
    memset(&IoStatus, 0xAB, sizeof(IoStatus));
    reset_read();
    KmtStartSeh()
        Ret = CcCopyRead(FileObject, &Offset, 10, FALSE, Buffer, &IoStatus);
    KmtEndSeh(STATUS_SUCCESS);
    ok_bool_true(Ret, "CcCopyRead should succeed\n");
    /* But there was no read triggered, as the page is already resident. */
    ok_read_not_called();
    ok_eq_hex(IoStatus.Status, STATUS_SUCCESS);
    ok_eq_ulongptr(IoStatus.Information, 10);

    /* But this one doesn't */
    Ret = 'x';
    Offset.QuadPart = PAGE_SIZE * 128;
    memset(&IoStatus, 0xAB, sizeof(IoStatus));
    reset_read();
    KmtStartSeh()
        Ret = CcCopyRead(FileObject, &Offset, 10, FALSE, Buffer, &IoStatus);
    KmtEndSeh(STATUS_SUCCESS);
    ok_bool_false(Ret, "CcCopyRead should fail\n");
    ok_read_not_called();
    ok_eq_hex(IoStatus.Status, 0xABABABAB);
    ok_eq_ulongptr(IoStatus.Information, (ULONG_PTR)0xABABABABABABABAB);

    /* Of course, waiting for it succeeds and triggers the read */
    Ret = 'x';
    Offset.QuadPart = PAGE_SIZE * 129;
    memset(&IoStatus, 0xAB, sizeof(IoStatus));
    reset_read();
    KmtStartSeh()
        Ret = CcCopyRead(FileObject, &Offset, 10, TRUE, Buffer, &IoStatus);
    KmtEndSeh(STATUS_SUCCESS);
    ok_bool_true(Ret, "CcCopyRead should succeed\n");
    ok_read_called(PAGE_SIZE * 129, PAGE_SIZE);
    ok_eq_hex(IoStatus.Status, STATUS_SUCCESS);
    ok_eq_ulongptr(IoStatus.Information, 10);

    /* Try the same without a status block */
    Ret = 'x';
    Offset.QuadPart = PAGE_SIZE * 129;
    KmtStartSeh()
        Ret = CcCopyRead(FileObject, &Offset, 10, TRUE, Buffer, NULL);
    KmtEndSeh(STATUS_ACCESS_VIOLATION);
    ok_eq_char(Ret, 'x');
}

static
VOID
RunCcCopyReadBehaviorTest(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PTEST_FCB Fcb;

    ok_eq_pointer(TestFileObject, NULL);
    ok_eq_pointer(TestDeviceObject, NULL);

    TestDeviceObject = DeviceObject;
    TestFileObject = IoCreateStreamFileObject(NULL, DeviceObject);
    ok(TestFileObject != NULL, "Failed to allocate FO\n");
    if (TestFileObject == NULL)
    {
        TestDeviceObject = NULL;
        return;
    }

    Fcb = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Fcb), 'FwrI');
    ok(Fcb != NULL, "ExAllocatePoolWithTag failed\n");
    if (Fcb != NULL)
    {
        RtlZeroMemory(Fcb, sizeof(*Fcb));
        ExInitializeFastMutex(&Fcb->HeaderMutex);
        FsRtlSetupAdvancedHeader(&Fcb->Header, &Fcb->HeaderMutex);
        Fcb->Header.AllocationSize.QuadPart = 1000000;
        Fcb->Header.FileSize.QuadPart = 1000000;
        Fcb->Header.ValidDataLength.QuadPart = 1000000;
        Fcb->Header.IsFastIoPossible = FastIoIsNotPossible;

        TestFileObject->FsContext = Fcb;
        TestFileObject->SectionObjectPointer = &Fcb->SectionObjectPointers;

        CcInitializeCacheMap(TestFileObject,
                             (PCC_FILE_SIZES)&Fcb->Header.AllocationSize,
                             FALSE,
                             &Callbacks,
                             NULL);

        Test_CcCopyRead(TestFileObject);

        if (CcIsFileCached(TestFileObject))
        {
            KmtCcUninitializeCacheMap(TestFileObject, NULL);
            CcPurgeCacheSection(&Fcb->SectionObjectPointers, NULL, 0, FALSE);
        }

        ExFreePoolWithTag(Fcb, 'FwrI');
        TestFileObject->FsContext = NULL;
        TestFileObject->SectionObjectPointer = NULL;
    }

    ObDereferenceObject(TestFileObject);
    TestFileObject = NULL;
    TestDeviceObject = NULL;
}

static
NTSTATUS
TestMessageHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_opt_ PVOID Buffer,
    _In_ SIZE_T InLength,
    _Inout_ PSIZE_T OutLength)
{
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    FsRtlEnterFileSystem();

    switch (ControlCode)
    {
        case IOCTL_RUN_TEST:
            RunCcCopyReadBehaviorTest(DeviceObject);
            break;

        default:
            Status = STATUS_NOT_IMPLEMENTED;
            break;
    }

    FsRtlExitFileSystem();

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
    PTEST_FCB Fcb;

    PAGED_CODE();

    DPRINT("IRP %x/%x\n", IoStack->MajorFunction, IoStack->MinorFunction);
    ASSERT(IoStack->MajorFunction == IRP_MJ_CLEANUP ||
           IoStack->MajorFunction == IRP_MJ_CLOSE ||
           IoStack->MajorFunction == IRP_MJ_CREATE ||
           IoStack->MajorFunction == IRP_MJ_READ);

    Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;

    if (IoStack->MajorFunction == IRP_MJ_CREATE)
    {
        ok_irql(PASSIVE_LEVEL);

        if (IoStack->FileObject->FileName.Length < 2 * sizeof(WCHAR))
            goto CreateSuccess;

        TestDeviceObject = DeviceObject;
        TestFileObject = IoStack->FileObject;

        Fcb = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Fcb), 'FwrI');
        RtlZeroMemory(Fcb, sizeof(*Fcb));
        ExInitializeFastMutex(&Fcb->HeaderMutex);
        FsRtlSetupAdvancedHeader(&Fcb->Header, &Fcb->HeaderMutex);
        Fcb->BigFile = FALSE;
        if (IoStack->FileObject->FileName.Length >= 2 * sizeof(WCHAR) &&
            IoStack->FileObject->FileName.Buffer[1] == 'B')
        {
            Fcb->Header.AllocationSize.QuadPart = 1000000;
            Fcb->Header.FileSize.QuadPart = 1000000;
            Fcb->Header.ValidDataLength.QuadPart = 1000000;
        }
        else if (IoStack->FileObject->FileName.Length >= 2 * sizeof(WCHAR) &&
                 IoStack->FileObject->FileName.Buffer[1] == 'S')
        {
            Fcb->Header.AllocationSize.QuadPart = 1004;
            Fcb->Header.FileSize.QuadPart = 1004;
            Fcb->Header.ValidDataLength.QuadPart = 1004;
        }
        else if (IoStack->FileObject->FileName.Length >= 2 * sizeof(WCHAR) &&
                 IoStack->FileObject->FileName.Buffer[1] == 'R')
        {
            Fcb->Header.AllocationSize.QuadPart = 62;
            Fcb->Header.FileSize.QuadPart = 62;
            Fcb->Header.ValidDataLength.QuadPart = 62;
        }
        else if (IoStack->FileObject->FileName.Length >= 2 * sizeof(WCHAR) &&
                 IoStack->FileObject->FileName.Buffer[1] == 'F')
        {
            Fcb->Header.AllocationSize.QuadPart = 4294967296;
            Fcb->Header.FileSize.QuadPart = 4294967296;
            Fcb->Header.ValidDataLength.QuadPart = 4294967296;
            Fcb->BigFile = TRUE;
        }
        else
        {
            Fcb->Header.AllocationSize.QuadPart = 512;
            Fcb->Header.FileSize.QuadPart = 512;
            Fcb->Header.ValidDataLength.QuadPart = 512;
        }
        Fcb->Header.IsFastIoPossible = FastIoIsNotPossible;
        IoStack->FileObject->FsContext = Fcb;
        IoStack->FileObject->SectionObjectPointer = &Fcb->SectionObjectPointers;

        CcInitializeCacheMap(IoStack->FileObject,
                             (PCC_FILE_SIZES)&Fcb->Header.AllocationSize,
                             FALSE, &Callbacks, NULL);

CreateSuccess:
        Irp->IoStatus.Information = FILE_OPENED;
        Status = STATUS_SUCCESS;
    }
    else if (IoStack->MajorFunction == IRP_MJ_READ)
    {
        BOOLEAN Ret;
        ULONG Length;
        PVOID Buffer;
        LARGE_INTEGER Offset;

        Offset = IoStack->Parameters.Read.ByteOffset;
        Length = IoStack->Parameters.Read.Length;
        Fcb = IoStack->FileObject->FsContext;

        ok_eq_pointer(DeviceObject, TestDeviceObject);
        ok_eq_pointer(IoStack->FileObject, TestFileObject);

        if (!FlagOn(Irp->Flags, IRP_NOCACHE))
        {
            ok_irql(PASSIVE_LEVEL);

            if (Offset.QuadPart >= Fcb->Header.FileSize.QuadPart)
            {
                Status = Irp->IoStatus.Status = STATUS_END_OF_FILE;
            }
            else
            {
                if (Offset.QuadPart + Length > Fcb->Header.FileSize.QuadPart)
                {
                    Length = (ULONG)(Fcb->Header.FileSize.QuadPart - Offset.QuadPart);
                }

                /* We don't want to test alignement for big files (not the purpose of the test) */
                if (!Fcb->BigFile)
                {
                    ok(Offset.QuadPart % PAGE_SIZE != 0, "Offset is aligned: %I64i\n", Offset.QuadPart);
                    ok(Length % PAGE_SIZE != 0, "Length is aligned: %I64i\n", Length);
                }

                Buffer = Irp->AssociatedIrp.SystemBuffer;
                ok(Buffer != NULL, "Null pointer!\n");

                _SEH2_TRY
                {
                    Ret = CcCopyRead(IoStack->FileObject, &Offset, Length, TRUE, Buffer,
                                     &Irp->IoStatus);
                    ok_bool_true(Ret, "CcCopyRead");
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Irp->IoStatus.Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;

                Status = Irp->IoStatus.Status;

                if (NT_SUCCESS(Status))
                {
                    if (Offset.QuadPart <= 1000LL && Offset.QuadPart + Length > 1000LL)
                    {
                        ok_eq_hex(*(PUSHORT)((ULONG_PTR)Buffer + (ULONG_PTR)(1000LL - Offset.QuadPart)), 0xFFFF);
                    }
                    else
                    {
                        ok_eq_hex(*(PUSHORT)Buffer, 0xBABA);
                    }
                }
            }
        }
        else
        {
            PMDL Mdl;

            ReadCalledNonCached = TRUE;
            ReadOffset = Offset;
            ReadLength = Length;

            ok_irql(KmtCcPagingReadIrql());
            ok((Offset.QuadPart % PAGE_SIZE == 0 || Offset.QuadPart == 0), "Offset is not aligned: %I64i\n", Offset.QuadPart);
            ok(Length % PAGE_SIZE == 0, "Length is not aligned: %I64i\n", Length);

            ok(Irp->AssociatedIrp.SystemBuffer == NULL, "A SystemBuffer was allocated!\n");
            Buffer = MapAndLockUserBuffer(Irp, Length);
            ok(Buffer != NULL, "Null pointer!\n");
            RtlFillMemory(Buffer, Length, 0xBA);

            Status = STATUS_SUCCESS;
            if (Offset.QuadPart <= 1000LL && Offset.QuadPart + Length > 1000LL)
            {
                *(PUSHORT)((ULONG_PTR)Buffer + (ULONG_PTR)(1000LL - Offset.QuadPart)) = 0xFFFF;
            }

            Mdl = Irp->MdlAddress;
            ok(Mdl != NULL, "Null pointer for MDL!\n");
            ok((Mdl->MdlFlags & MDL_PAGES_LOCKED) != 0, "MDL not locked\n");
            ok((Mdl->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL) == 0, "MDL from non paged\n");
            ok((Mdl->MdlFlags & MDL_IO_PAGE_READ) != 0, "Non paging IO\n");
            ok((Irp->Flags & IRP_PAGING_IO) != 0, "Non paging IO\n");
        }

        if (NT_SUCCESS(Status))
        {
            Irp->IoStatus.Information = Length;
            IoStack->FileObject->CurrentByteOffset.QuadPart = Offset.QuadPart + Length;
        }
    }
    else if (IoStack->MajorFunction == IRP_MJ_CLEANUP)
    {
        ok_irql(PASSIVE_LEVEL);
        Fcb = IoStack->FileObject->FsContext;
        if (Fcb)
        {
            KmtCcUninitializeCacheMap(IoStack->FileObject, NULL);
            CcPurgeCacheSection(&Fcb->SectionObjectPointers, NULL, 0, FALSE);
        }
        Status = STATUS_SUCCESS;
    }
    else if (IoStack->MajorFunction == IRP_MJ_CLOSE)
    {
        ok_irql(PASSIVE_LEVEL);
        Fcb = IoStack->FileObject->FsContext;
        if (Fcb)
        {
            ExFreePoolWithTag(Fcb, 'FwrI');
            IoStack->FileObject->FsContext = NULL;
            IoStack->FileObject->SectionObjectPointer = NULL;
            if (TestFileObject == IoStack->FileObject)
            {
                TestFileObject = NULL;
                TestDeviceObject = NULL;
            }
        }
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
