/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Physical-memory rotation services used by display drivers
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef struct _TEST_ROTATE_COPY_CONTEXT
{
    ULONG Calls;
    SIZE_T NumberOfBytes;
} TEST_ROTATE_COPY_CONTEXT, *PTEST_ROTATE_COPY_CONTEXT;

PPHYSICAL_MEMORY_RANGE
NTAPI
MmGetPhysicalMemoryRangesEx(
    _In_opt_ PVOID PartitionObject);

static
VOID
TestPhysicalMemoryRanges(VOID)
{
    PPHYSICAL_MEMORY_RANGE SystemRanges;

    SystemRanges = MmGetPhysicalMemoryRangesEx(NULL);
    trace("physical ranges: system %p\n", SystemRanges);
    ok(SystemRanges != NULL, "system physical ranges were NULL\n");
    if (SystemRanges != NULL)
        ExFreePool(SystemRanges);
}

static
VOID
TestRotateValidation(VOID)
{
    SIZE_T NumberOfBytes;
    NTSTATUS Status;

    NumberOfBytes = PAGE_SIZE;
    Status = MmRotatePhysicalView((PVOID)(ULONG_PTR)(PAGE_SIZE + 1), &NumberOfBytes, NULL, MmToFrameBuffer, NULL, NULL);
    trace("MmRotatePhysicalView(misaligned VA) returned 0x%08lx, bytes %Iu\n", Status, NumberOfBytes);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_1);
    ok_eq_size(NumberOfBytes, 0);
    NumberOfBytes = PAGE_SIZE + 1;
    Status = MmRotatePhysicalView((PVOID)(ULONG_PTR)PAGE_SIZE, &NumberOfBytes, NULL, MmToFrameBuffer, NULL, NULL);
    trace("MmRotatePhysicalView(misaligned size) returned 0x%08lx, bytes %Iu\n", Status, NumberOfBytes);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_2);
    ok_eq_size(NumberOfBytes, 0);
    NumberOfBytes = PAGE_SIZE;
    Status = MmRotatePhysicalView((PVOID)(ULONG_PTR)PAGE_SIZE, &NumberOfBytes, NULL, MmMaximumRotateDirection, NULL, NULL);
    trace("MmRotatePhysicalView(invalid direction) returned 0x%08lx, bytes %Iu\n", Status, NumberOfBytes);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_3);
    ok_eq_size(NumberOfBytes, 0);
    NumberOfBytes = PAGE_SIZE;
    Status = MmRotatePhysicalView((PVOID)(ULONG_PTR)PAGE_SIZE,
                                  &NumberOfBytes,
                                  NULL,
                                  (MM_ROTATE_DIRECTION)-1,
                                  NULL,
                                  NULL);
    trace("MmRotatePhysicalView(negative direction) returned 0x%08lx, bytes %Iu\n",
          Status,
          NumberOfBytes);
    ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
    ok_eq_size(NumberOfBytes, 0);
}

static
VOID
TestRotateLifecycle(
    _In_ ULONG AllocationType,
    _In_ BOOLEAN ReleaseMapped)
{
    PVOID BaseAddress = NULL;
    PVOID FrameBuffer = NULL;
    PMDL FrameBufferMdl = NULL;
    PVOID FreeBase;
    SIZE_T NumberOfBytes;
    SIZE_T RegionSize = PAGE_SIZE;
    MEMORY_BASIC_INFORMATION MemoryInfo;
    BOOLEAN Mapped = FALSE;
    NTSTATUS Status;

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &BaseAddress, 0, &RegionSize, AllocationType | MEM_RESERVE | MEM_ROTATE, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    FrameBuffer = ExAllocatePoolZero(NonPagedPool, PAGE_SIZE, 'bFmK');
    ok(FrameBuffer != NULL, "could not allocate the frame-buffer page\n");
    if (FrameBuffer == NULL)
        goto Cleanup;
    FrameBufferMdl = IoAllocateMdl(FrameBuffer, PAGE_SIZE, FALSE, FALSE, NULL);
    ok(FrameBufferMdl != NULL, "could not allocate the frame-buffer MDL\n");
    if (FrameBufferMdl == NULL)
        goto Cleanup;
    MmBuildMdlForNonPagedPool(FrameBufferMdl);
    *(PULONG)FrameBuffer = 0x12345678;

    NumberOfBytes = PAGE_SIZE;
    Status = MmRotatePhysicalView(BaseAddress, &NumberOfBytes, FrameBufferMdl, MmToFrameBufferNoCopy, NULL, NULL);
    Mapped = NT_SUCCESS(Status);
    if (!(AllocationType & MEM_COMMIT))
    {
        ok_eq_hex(Status, STATUS_INVALID_PAGE_PROTECTION);
        ok_eq_size(NumberOfBytes, 0);
        goto Cleanup;
    }
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_size(NumberOfBytes, PAGE_SIZE);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    ok_eq_hex(*(volatile ULONG *)BaseAddress, 0x12345678);
    *(volatile ULONG *)BaseAddress = 0x87654321;
    ok_eq_hex(*(PULONG)FrameBuffer, 0x87654321);

    if (ReleaseMapped)
    {
        FreeBase = BaseAddress;
        RegionSize = 0;
        Status = ZwFreeVirtualMemory(NtCurrentProcess(), &FreeBase, &RegionSize, MEM_RELEASE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_eq_pointer(FreeBase, BaseAddress);
            ok_eq_size(RegionSize, PAGE_SIZE);
            BaseAddress = NULL;
            Mapped = FALSE;
            Status = ZwQueryVirtualMemory(NtCurrentProcess(), FreeBase, MemoryBasicInformation, &MemoryInfo, sizeof(MemoryInfo), NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ok_eq_hex(MemoryInfo.State, MEM_FREE);
                ok_eq_pointer(MemoryInfo.AllocationBase, NULL);
            }
            ok_eq_hex(*(PULONG)FrameBuffer, 0x87654321);
        }
    }
    else
    {
        NumberOfBytes = PAGE_SIZE;
        Status = MmRotatePhysicalView(BaseAddress, &NumberOfBytes, NULL, MmToRegularMemoryNoCopy, NULL, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_size(NumberOfBytes, PAGE_SIZE);
        if (NT_SUCCESS(Status))
        {
            Mapped = FALSE;
            ok_eq_hex(*(volatile ULONG *)BaseAddress, 0);
            *(volatile ULONG *)BaseAddress = 0xABCDEF01;
            ok_eq_hex(*(PULONG)FrameBuffer, 0x87654321);
        }
    }

Cleanup:
    if (BaseAddress != NULL)
    {
        RegionSize = 0;
        Status = ZwFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &RegionSize, MEM_RELEASE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status) && Mapped)
        {
            /* Older implementations require restoring regular memory first. */
            NumberOfBytes = PAGE_SIZE;
            Status = MmRotatePhysicalView(BaseAddress, &NumberOfBytes, NULL, MmToRegularMemoryNoCopy, NULL, NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                RegionSize = 0;
                Status = ZwFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &RegionSize, MEM_RELEASE);
                ok_eq_hex(Status, STATUS_SUCCESS);
            }
        }
    }
    if (FrameBufferMdl != NULL)
        IoFreeMdl(FrameBufferMdl);
    if (FrameBuffer != NULL)
        ExFreePoolWithTag(FrameBuffer, 'bFmK');
}

static
NTSTATUS
NTAPI
TestRotateCopyCallback(
    _In_ PMDL DestinationMdl,
    _In_ PMDL SourceMdl,
    _In_ PVOID Context)
{
    PTEST_ROTATE_COPY_CONTEXT CopyContext = Context;
    PVOID Destination;
    PVOID Source;
    PHYSICAL_ADDRESS DestinationAddress;
    PHYSICAL_ADDRESS SourceAddress;

    /* The rotate callback may receive a user alias that is being replaced.
     * Map the locked physical pages without touching that alias. This fixture
     * uses one aligned, cached RAM page for each side of the copy. */
    if (CopyContext->NumberOfBytes != PAGE_SIZE || MmGetMdlByteOffset(DestinationMdl) || MmGetMdlByteOffset(SourceMdl) || MmGetMdlByteCount(DestinationMdl) != PAGE_SIZE || MmGetMdlByteCount(SourceMdl) != PAGE_SIZE)
        return STATUS_INVALID_BUFFER_SIZE;
    DestinationAddress.QuadPart = (ULONGLONG)MmGetMdlPfnArray(DestinationMdl)[0] << PAGE_SHIFT;
    SourceAddress.QuadPart = (ULONGLONG)MmGetMdlPfnArray(SourceMdl)[0] << PAGE_SHIFT;
    Destination = MmMapIoSpace(DestinationAddress, PAGE_SIZE, MmCached);
    if (Destination == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Source = MmMapIoSpace(SourceAddress, PAGE_SIZE, MmCached);
    if (Source == NULL)
    {
        MmUnmapIoSpace(Destination, PAGE_SIZE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(Destination, Source, PAGE_SIZE);
    MmUnmapIoSpace(Source, PAGE_SIZE);
    MmUnmapIoSpace(Destination, PAGE_SIZE);
    CopyContext->Calls++;
    return STATUS_SUCCESS;
}

static
VOID
TestRotateCopyLifecycle(VOID)
{
    PVOID BaseAddress;
    PVOID FrameBuffer = NULL;
    PMDL FrameBufferMdl = NULL;
    TEST_ROTATE_COPY_CONTEXT CopyContext;
    SIZE_T NumberOfBytes;
    SIZE_T RegionSize;
    NTSTATUS Status;

    BaseAddress = NULL;
    RegionSize = PAGE_SIZE;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &BaseAddress, 0, &RegionSize, MEM_COMMIT | MEM_RESERVE | MEM_ROTATE, PAGE_READWRITE);
    trace("committed MEM_ROTATE allocation returned 0x%08lx, base %p, size %Iu\n", Status, BaseAddress, RegionSize);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    FrameBuffer = ExAllocatePoolZero(NonPagedPool, PAGE_SIZE, 'cFmK');
    ok(FrameBuffer != NULL, "could not allocate the copy test frame-buffer page\n");
    if (FrameBuffer == NULL)
        goto Cleanup;
    FrameBufferMdl = IoAllocateMdl(FrameBuffer, PAGE_SIZE, FALSE, FALSE, NULL);
    ok(FrameBufferMdl != NULL, "could not allocate the copy test frame-buffer MDL\n");
    if (FrameBufferMdl == NULL)
        goto Cleanup;
    MmBuildMdlForNonPagedPool(FrameBufferMdl);

    *(PULONG)BaseAddress = 0x13572468;
    CopyContext.Calls = 0;
    CopyContext.NumberOfBytes = PAGE_SIZE;
    NumberOfBytes = PAGE_SIZE;
    Status = MmRotatePhysicalView(BaseAddress, &NumberOfBytes, FrameBufferMdl, MmToFrameBuffer, TestRotateCopyCallback, &CopyContext);
    trace("MmToFrameBuffer returned 0x%08lx, bytes %Iu, callbacks %lu\n", Status, NumberOfBytes, CopyContext.Calls);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_size(NumberOfBytes, PAGE_SIZE);
    ok_eq_ulong(CopyContext.Calls, 1);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(*(PULONG)FrameBuffer, 0x13572468);
        *(PULONG)FrameBuffer = 0x24681357;
        NumberOfBytes = PAGE_SIZE;
        Status = MmRotatePhysicalView(BaseAddress, &NumberOfBytes, NULL, MmToRegularMemory, TestRotateCopyCallback, &CopyContext);
        trace("MmToRegularMemory returned 0x%08lx, bytes %Iu, callbacks %lu\n", Status, NumberOfBytes, CopyContext.Calls);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_size(NumberOfBytes, PAGE_SIZE);
        ok_eq_ulong(CopyContext.Calls, 2);
        if (NT_SUCCESS(Status))
            ok_eq_hex(*(PULONG)BaseAddress, 0x24681357);
    }

Cleanup:
    RegionSize = 0;
    Status = ZwFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &RegionSize, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (FrameBufferMdl != NULL)
        IoFreeMdl(FrameBufferMdl);
    if (FrameBuffer != NULL)
        ExFreePoolWithTag(FrameBuffer, 'cFmK');
}

START_TEST(MmWddmRanges)
{
    TestPhysicalMemoryRanges();
}

START_TEST(MmWddmRotate)
{
    TestRotateValidation();
}

START_TEST(MmWddmRotateValid)
{
    TestRotateLifecycle(0, FALSE);
    TestRotateLifecycle(MEM_COMMIT, FALSE);
    TestRotateLifecycle(MEM_COMMIT, TRUE);
    TestRotateCopyLifecycle();
}
