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
    Status = MmRotatePhysicalView((PVOID)(ULONG_PTR)PAGE_SIZE, &NumberOfBytes, NULL, (MM_ROTATE_DIRECTION)-1, NULL, NULL);
    trace("MmRotatePhysicalView(negative direction) returned 0x%08lx, bytes %Iu\n", Status, NumberOfBytes);
    ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
    ok_eq_size(NumberOfBytes, 0);
}

static
VOID
TestRotateLifecycle(VOID)
{
    PVOID BaseAddress;
    PVOID FrameBuffer;
    PVOID FreeBase;
    PMDL FrameBufferMdl;
    SIZE_T FreeSize;
    SIZE_T NumberOfBytes;
    SIZE_T RegionSize;
    BOOLEAN IsReactOS;
    NTSTATUS Status;

    BaseAddress = NULL;
    RegionSize = PAGE_SIZE;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &BaseAddress, 0, &RegionSize, MEM_RESERVE | MEM_ROTATE, PAGE_READWRITE);
    trace("MEM_ROTATE reserve returned 0x%08lx, base %p, size %Iu\n", Status, BaseAddress, RegionSize);
    if (skip(NT_SUCCESS(Status), "MEM_ROTATE reservations are unavailable\n"))
        return;

    IsReactOS = *(volatile ULONG *)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) == 0x8eac705;
    if (IsReactOS)
    {
        NumberOfBytes = PAGE_SIZE;
        Status = MmRotatePhysicalView(BaseAddress, &NumberOfBytes, NULL, MmToRegularMemoryNoCopy, NULL, NULL);
        trace("MmToRegularMemoryNoCopy on an unrotated VAD returned 0x%08lx, bytes %Iu\n", Status, NumberOfBytes);
        ok_eq_hex(Status, STATUS_NOT_MAPPED_VIEW);
        ok_eq_size(NumberOfBytes, 0);
    }

    FrameBuffer = ExAllocatePoolZero(NonPagedPool, PAGE_SIZE, 'bFmK');
    if (skip(FrameBuffer != NULL, "could not allocate the test frame-buffer page\n"))
        goto CleanupVad;

    FrameBufferMdl = IoAllocateMdl(FrameBuffer, PAGE_SIZE, FALSE, FALSE, NULL);
    if (skip(FrameBufferMdl != NULL, "could not allocate the test frame-buffer MDL\n"))
        goto CleanupFrameBuffer;

    MmBuildMdlForNonPagedPool(FrameBufferMdl);
    *(PULONG)FrameBuffer = 0x12345678;
    NumberOfBytes = PAGE_SIZE;
    Status = MmRotatePhysicalView(BaseAddress, &NumberOfBytes, FrameBufferMdl, MmToFrameBufferNoCopy, NULL, NULL);
    trace("MmToFrameBufferNoCopy returned 0x%08lx, bytes %Iu\n", Status, NumberOfBytes);
    if (!IsReactOS && (Status == STATUS_INVALID_PAGE_PROTECTION))
    {
        skip(FALSE, "native requires a display aperture; the RAM-backed test MDL was rejected\n");
        goto CleanupMdl;
    }

    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_size(NumberOfBytes, PAGE_SIZE);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(*(volatile ULONG *)BaseAddress, 0x12345678);
        *(volatile ULONG *)BaseAddress = 0x87654321;
        ok_eq_hex(*(PULONG)FrameBuffer, 0x87654321);

        FreeBase = BaseAddress;
        FreeSize = 0;
        Status = ZwFreeVirtualMemory(NtCurrentProcess(), &FreeBase, &FreeSize, MEM_RELEASE);
        trace("free of mapped rotate VAD returned 0x%08lx\n", Status);
        ok_eq_hex(Status, STATUS_UNABLE_TO_DELETE_SECTION);

        NumberOfBytes = PAGE_SIZE;
        Status = MmRotatePhysicalView(BaseAddress, &NumberOfBytes, NULL, MmToRegularMemoryNoCopy, NULL, NULL);
        trace("MmToRegularMemoryNoCopy returned 0x%08lx, bytes %Iu\n", Status, NumberOfBytes);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_size(NumberOfBytes, PAGE_SIZE);
        if (NT_SUCCESS(Status))
        {
            ok_eq_hex(*(volatile ULONG *)BaseAddress, 0);
            *(volatile ULONG *)BaseAddress = 0xABCDEF01;
            ok_eq_hex(*(PULONG)FrameBuffer, 0x87654321);
        }
    }

CleanupMdl:
    IoFreeMdl(FrameBufferMdl);
CleanupFrameBuffer:
    ExFreePoolWithTag(FrameBuffer, 'bFmK');
CleanupVad:
    RegionSize = 0;
    Status = ZwFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &RegionSize, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
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

    Destination = MmGetSystemAddressForMdlSafe(DestinationMdl, NormalPagePriority);
    Source = MmGetSystemAddressForMdlSafe(SourceMdl, NormalPagePriority);
    if ((Destination == NULL) || (Source == NULL))
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Destination, Source, CopyContext->NumberOfBytes);
    CopyContext->Calls++;
    return STATUS_SUCCESS;
}

static
VOID
TestRotateCopyLifecycle(VOID)
{
    PVOID BaseAddress;
    PVOID FrameBuffer;
    PMDL FrameBufferMdl;
    TEST_ROTATE_COPY_CONTEXT CopyContext;
    SIZE_T NumberOfBytes;
    SIZE_T RegionSize;
    BOOLEAN IsReactOS;
    NTSTATUS Status;

    BaseAddress = NULL;
    RegionSize = PAGE_SIZE;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &BaseAddress, 0, &RegionSize, MEM_COMMIT | MEM_RESERVE | MEM_ROTATE, PAGE_READWRITE);
    trace("committed MEM_ROTATE allocation returned 0x%08lx, base %p, size %Iu\n", Status, BaseAddress, RegionSize);
    if (skip(NT_SUCCESS(Status), "committed MEM_ROTATE allocations are unavailable\n"))
        return;

    FrameBuffer = ExAllocatePoolZero(NonPagedPool, PAGE_SIZE, 'cFmK');
    if (skip(FrameBuffer != NULL, "could not allocate the copy test frame-buffer page\n"))
        goto CleanupVad;

    FrameBufferMdl = IoAllocateMdl(FrameBuffer, PAGE_SIZE, FALSE, FALSE, NULL);
    if (skip(FrameBufferMdl != NULL, "could not allocate the copy test frame-buffer MDL\n"))
        goto CleanupFrameBuffer;

    MmBuildMdlForNonPagedPool(FrameBufferMdl);
    *(PULONG)BaseAddress = 0x13572468;
    CopyContext.Calls = 0;
    CopyContext.NumberOfBytes = PAGE_SIZE;
    NumberOfBytes = PAGE_SIZE;
    Status = MmRotatePhysicalView(BaseAddress, &NumberOfBytes, FrameBufferMdl, MmToFrameBuffer, TestRotateCopyCallback, &CopyContext);
    trace("MmToFrameBuffer returned 0x%08lx, bytes %Iu, callbacks %lu\n", Status, NumberOfBytes, CopyContext.Calls);
    IsReactOS = *(volatile ULONG *)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)) == 0x8eac705;
    if (!IsReactOS && (Status == STATUS_INVALID_PAGE_PROTECTION))
    {
        skip(FALSE, "native requires a display aperture; the RAM-backed copy test MDL was rejected\n");
        goto CleanupMdl;
    }

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

CleanupMdl:
    IoFreeMdl(FrameBufferMdl);
CleanupFrameBuffer:
    ExFreePoolWithTag(FrameBuffer, 'cFmK');
CleanupVad:
    RegionSize = 0;
    Status = ZwFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &RegionSize, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
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
    TestRotateLifecycle();
    TestRotateCopyLifecycle();
}
