/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-Mode Test Suite for exports a Windows 8+ display
 *              miniport binds against
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

/*
 * A vendor WDDM miniport built against the Windows 8 or later WDK imports
 * these by name, and an unresolved import stops the driver from loading at
 * all, so each one is exercised rather than merely linked.
 */

static
VOID
TestExInitializePushLock(VOID)
{
    ULONG_PTR PushLock;

    RtlFillMemory(&PushLock, sizeof(PushLock), 0xAA);
    ExInitializePushLock(&PushLock);
    ok_eq_ulongptr(PushLock, (ULONG_PTR)0);
}

static
VOID
TestIoUninitializeWorkItem(VOID)
{
    PDEVICE_OBJECT DeviceObject = NULL;
    PIO_WORKITEM WorkItem;
    NTSTATUS Status;
    ULONG Size;

    Size = IoSizeofWorkItem();
    ok(Size >= sizeof(PVOID), "IoSizeofWorkItem returned %lu\n", Size);

    Status = IoCreateDevice(KmtDriverObject,
                            0,
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status) && DeviceObject != NULL, "No device object\n"))
        return;

    WorkItem = ExAllocatePoolWithTag(NonPagedPool, Size, 'tIWK');
    if (!skip(WorkItem != NULL, "Failed to allocate a work item\n"))
    {
        IoInitializeWorkItem(DeviceObject, WorkItem);
        /* Uninitializing an item that was never queued must not touch the
         * device object reference count, and must leave the item inert. */
        IoUninitializeWorkItem(WorkItem);
        ExFreePoolWithTag(WorkItem, 'tIWK');
    }

    IoDeleteDevice(DeviceObject);
}

static
VOID
TestMmAllocateMdlForIoSpace(VOID)
{
    MM_PHYSICAL_ADDRESS_LIST List[2];
    PMDL Mdl;
    NTSTATUS Status;

    /* A range that is not page aligned is rejected */
    List[0].PhysicalAddress.QuadPart = 0xF0000000 + 1;
    List[0].NumberOfBytes = PAGE_SIZE;
    Mdl = NULL;
    Status = MmAllocateMdlForIoSpace(List, 1, &Mdl);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_1);
    ok_eq_pointer(Mdl, NULL);

    /* A size that is not a whole number of pages is rejected */
    List[0].PhysicalAddress.QuadPart = 0xF0000000;
    List[0].NumberOfBytes = PAGE_SIZE - 1;
    Mdl = NULL;
    Status = MmAllocateMdlForIoSpace(List, 1, &Mdl);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_1);
    ok_eq_pointer(Mdl, NULL);

    /* A zero-length list is rejected */
    Mdl = NULL;
    Status = MmAllocateMdlForIoSpace(List, 0, &Mdl);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_2);

    /* RAM is not I/O space, so page zero must be refused */
    List[0].PhysicalAddress.QuadPart = 0;
    List[0].NumberOfBytes = PAGE_SIZE;
    Mdl = NULL;
    Status = MmAllocateMdlForIoSpace(List, 1, &Mdl);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_1);
    ok_eq_pointer(Mdl, NULL);

    /*
     * Two discontiguous device ranges high above installed memory.  The MDL
     * has to describe both, in order, without being mapped anywhere.
     */
    List[0].PhysicalAddress.QuadPart = 0xFED00000;
    List[0].NumberOfBytes = 2 * PAGE_SIZE;
    List[1].PhysicalAddress.QuadPart = 0xFEE00000;
    List[1].NumberOfBytes = PAGE_SIZE;
    Mdl = NULL;
    Status = MmAllocateMdlForIoSpace(List, 2, &Mdl);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!skip(NT_SUCCESS(Status) && Mdl != NULL, "No MDL returned\n"))
    {
        PPFN_NUMBER Pages = MmGetMdlPfnArray(Mdl);

        ok_eq_ulong(MmGetMdlByteCount(Mdl), 3 * PAGE_SIZE);
        ok_eq_ulong(MmGetMdlByteOffset(Mdl), 0UL);
        ok_eq_pointer(MmGetMdlVirtualAddress(Mdl), NULL);
        ok((Mdl->MdlFlags & MDL_IO_SPACE) != 0, "MDL_IO_SPACE is not set\n");
        ok((Mdl->MdlFlags & MDL_PAGES_LOCKED) != 0, "MDL_PAGES_LOCKED is not set\n");
        ok_eq_ulongptr((ULONG_PTR)Pages[0], (ULONG_PTR)(0xFED00000 >> PAGE_SHIFT));
        ok_eq_ulongptr((ULONG_PTR)Pages[1], (ULONG_PTR)((0xFED00000 >> PAGE_SHIFT) + 1));
        ok_eq_ulongptr((ULONG_PTR)Pages[2], (ULONG_PTR)(0xFEE00000 >> PAGE_SHIFT));
        IoFreeMdl(Mdl);
    }
}

static
VOID
TestObDereferenceObjectDeferDelete(VOID)
{
    HANDLE Handle = NULL;
    NTSTATUS Status;
    PVOID Object = NULL;

    Status = ObOpenObjectByPointer(PsGetCurrentProcess(),
                                   OBJ_KERNEL_HANDLE,
                                   NULL,
                                   PROCESS_ALL_ACCESS,
                                   *PsProcessType,
                                   KernelMode,
                                   &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Handle,
                                           0,
                                           *PsProcessType,
                                           KernelMode,
                                           &Object,
                                           NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            /* The deferring form drops the same reference the ordinary one
             * does; the object stays alive because the handle still holds it. */
            ObDereferenceObjectDeferDelete(Object);
        }
        ObCloseHandle(Handle, KernelMode);
    }

}

static
VOID
TestStringHelpers(VOID)
{
    CHAR Buffer[] = "amdkmdag sys;driver";
    CHAR *Context = NULL;
    CHAR *Token;
    WCHAR Wide[] = L"1280 720";
    CHAR NarrowInput[] = "1920 display";
    CHAR NarrowWord[16];
    WCHAR WideInput[] = L"1080 monitor";
    WCHAR WideWord[16];
    int Value;
    int Width = 0, Height = 0, Converted;

    Token = strtok_s(Buffer, " ;", &Context);
    ok(Token != NULL && strcmp(Token, "amdkmdag") == 0,
       "First token is '%s'\n", Token ? Token : "(null)");
    Token = strtok_s(NULL, " ;", &Context);
    ok(Token != NULL && strcmp(Token, "sys") == 0,
       "Second token is '%s'\n", Token ? Token : "(null)");
    Token = strtok_s(NULL, " ;", &Context);
    ok(Token != NULL && strcmp(Token, "driver") == 0,
       "Third token is '%s'\n", Token ? Token : "(null)");
    Token = strtok_s(NULL, " ;", &Context);
    ok_eq_pointer(Token, NULL);

    Converted = swscanf_s(Wide, L"%d %d", &Width, &Height);
    ok_eq_int(Converted, 2);
    ok_eq_int(Width, 1280);
    ok_eq_int(Height, 720);

    Value = 0;
    Converted = sscanf_s(NarrowInput,
                         "%d %15s",
                         &Value,
                         NarrowWord,
                         (unsigned)RTL_NUMBER_OF(NarrowWord));
    ok_eq_int(Converted, 2);
    ok_eq_int(Value, 1920);
    ok(strcmp(NarrowWord, "display") == 0,
       "Narrow word is '%s'\n",
       NarrowWord);

    Value = 0;
    Converted = swscanf_s(WideInput,
                          L"%d %15s",
                          &Value,
                          WideWord,
                          (unsigned)RTL_NUMBER_OF(WideWord));
    ok_eq_int(Converted, 2);
    ok_eq_int(Value, 1080);
    ok(wcscmp(WideWord, L"monitor") == 0,
       "Wide word is '%S'\n",
       WideWord);

    ok_eq_wchar(RtlDowncaseUnicodeChar(L'A'), L'a');
    ok_eq_wchar(RtlDowncaseUnicodeChar(L'z'), L'z');
}

START_TEST(KeWddmExports)
{
    TestExInitializePushLock();
    TestIoUninitializeWorkItem();
    TestMmAllocateMdlForIoSpace();
    TestObDereferenceObjectDeferDelete();
    TestStringHelpers();
}
