/*
 * PROJECT:         ReactOS api tests
 * LICENSE:         LGPLv2.1+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Misc heap behavior tests (zeroing, usage accounting, extend)
 * PROGRAMMERS:     Codex (ChatGPT)
 */

#include "precomp.h"

/* Missing prototypes */
extern BOOLEAN NTAPI RtlZeroHeap(PVOID HeapHandle, ULONG Flags);
extern HANDLE NTAPI RtlDestroyHeap(HANDLE HeapHandle);
typedef NTSTATUS (NTAPI *PFN_RtlUsageHeap)(HANDLE HeapHandle, ULONG Flags, PRTL_HEAP_USAGE Usage);
typedef ULONG (NTAPI *PFN_RtlExtendHeap)(HANDLE Heap, ULONG Flags, PVOID P, SIZE_T Size);

static PFN_RtlUsageHeap pRtlUsageHeap;
static PFN_RtlExtendHeap pRtlExtendHeap;

static
BOOL
LoadHeapExports(void)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");

    if (!Ntdll)
        return FALSE;

    pRtlUsageHeap = (PFN_RtlUsageHeap)GetProcAddress(Ntdll, "RtlUsageHeap");
    pRtlExtendHeap = (PFN_RtlExtendHeap)GetProcAddress(Ntdll, "RtlExtendHeap");

    return TRUE;
}

static
BOOLEAN
CheckBytes(
    _In_reads_bytes_(Size) PUCHAR Buffer,
    _In_ SIZE_T Size,
    _In_ UCHAR Value)
{
    SIZE_T i;

    for (i = 0; i < Size; i++)
    {
        if (Buffer[i] != Value)
        {
            trace("Offset %Iu expected %02x, found %02x\n", i, Value, Buffer[i]);
            return FALSE;
        }
    }

    return TRUE;
}

static
void
TestRtlZeroHeapFreeOnly(void)
{
    PVOID Heap;
    PUCHAR Buf1, Buf2, Buf3;
    SIZE_T Size = 0x40;
    NTSTATUS Status;
    BOOLEAN Ok;
    HANDLE Destroyed;

    if (!pRtlUsageHeap || !pRtlExtendHeap)
    {
        win_skip("Heap exports missing\n");
        return;
    }

    Heap = RtlCreateHeap(0, NULL, 0, 0, NULL, NULL);
    ok(Heap != NULL, "RtlCreateHeap failed\n");
    if (!Heap)
        return;

    Buf1 = RtlAllocateHeap(Heap, 0, Size);
    Buf2 = RtlAllocateHeap(Heap, 0, Size);
    ok(Buf1 && Buf2, "Allocations failed: %p %p\n", Buf1, Buf2);
    if (!Buf1 || !Buf2)
        goto cleanup;

    RtlFillMemory(Buf1, Size, 0xAA);
    RtlFillMemory(Buf2, Size, 0xBB);

    Ok = RtlFreeHeap(Heap, 0, Buf1);
    ok(Ok == TRUE, "RtlFreeHeap(Buf1) returned %u\n", Ok);

    Ok = RtlZeroHeap(Heap, 0);
    ok(Ok == TRUE, "RtlZeroHeap returned %u\n", Ok);

    ok(CheckBytes(Buf2, Size, 0xBB), "Busy block was modified by RtlZeroHeap\n");

    Buf3 = RtlAllocateHeap(Heap, 0, Size);
    ok(Buf3 != NULL, "Re-allocation failed\n");
    if (Buf3)
        ok(CheckBytes(Buf3, Size, 0x00), "Freed block payload not zeroed\n");

    if (Buf3)
        RtlFreeHeap(Heap, 0, Buf3);
    RtlFreeHeap(Heap, 0, Buf2);

cleanup:
    Destroyed = RtlDestroyHeap(Heap);
    ok(Destroyed == NULL, "RtlDestroyHeap returned %p\n", Destroyed);
}

static
void
TestRtlUsageHeapVirtualAccounting(void)
{
    PVOID Heap;
    RTL_HEAP_USAGE UsageBefore, UsageAfter;
    NTSTATUS Status;
    SIZE_T BigSize = 0x100000; /* large enough to hit virtual path */
    PVOID Buf;
    SIZE_T DeltaRes, DeltaCom, DeltaAlloc;
    BOOLEAN Ok;
    HANDLE Destroyed;

    if (!pRtlUsageHeap || !pRtlExtendHeap)
    {
        win_skip("Heap exports missing\n");
        return;
    }

    Heap = RtlCreateHeap(0, NULL, 0, 0, NULL, NULL);
    ok(Heap != NULL, "RtlCreateHeap failed\n");
    if (!Heap)
        return;

    RtlZeroMemory(&UsageBefore, sizeof(UsageBefore));
    UsageBefore.Length = sizeof(UsageBefore);
    Status = pRtlUsageHeap(Heap, 0, &UsageBefore);
    ok_hex(Status, STATUS_SUCCESS);

    Buf = RtlAllocateHeap(Heap, 0, BigSize);
    ok(Buf != NULL, "Large allocation failed\n");
    if (!Buf)
    {
        RtlDestroyHeap(Heap);
        return;
    }

    RtlZeroMemory(&UsageAfter, sizeof(UsageAfter));
    UsageAfter.Length = sizeof(UsageAfter);
    Status = pRtlUsageHeap(Heap, 0, &UsageAfter);
    ok_hex(Status, STATUS_SUCCESS);

    DeltaRes = UsageAfter.BytesReserved - UsageBefore.BytesReserved;
    DeltaCom = UsageAfter.BytesCommitted - UsageBefore.BytesCommitted;
    DeltaAlloc = UsageAfter.BytesAllocated - UsageBefore.BytesAllocated;

    ok(DeltaRes >= BigSize, "Reserved delta too small: %Iu\n", DeltaRes);
    ok(DeltaCom >= BigSize, "Committed delta too small: %Iu\n", DeltaCom);
    ok(DeltaAlloc >= BigSize, "Allocated delta too small: %Iu\n", DeltaAlloc);

    Ok = RtlFreeHeap(Heap, 0, Buf);
    ok(Ok == TRUE, "RtlFreeHeap returned %u\n", Ok);

    Destroyed = RtlDestroyHeap(Heap);
    ok(Destroyed == NULL, "RtlDestroyHeap returned %p\n", Destroyed);
}

static
void
TestRtlExtendHeapReturnValue(void)
{
    PVOID Heap;
    PVOID Buf;
    ULONG Ret;
    SIZE_T InitialSize = 0x100;
    SIZE_T GrowSize = 0x200;
    HANDLE Destroyed;
    SIZE_T Reported;

    if (!pRtlUsageHeap || !pRtlExtendHeap)
    {
        win_skip("Heap exports missing\n");
        return;
    }

    Heap = RtlCreateHeap(0, NULL, 0, 0, NULL, NULL);
    ok(Heap != NULL, "RtlCreateHeap failed\n");
    if (!Heap)
        return;

    Buf = RtlAllocateHeap(Heap, 0, InitialSize);
    ok(Buf != NULL, "Allocation failed\n");
    if (!Buf)
    {
        RtlDestroyHeap(Heap);
        return;
    }

    Ret = pRtlExtendHeap(Heap, 0, Buf, GrowSize);
    ok(Ret == GrowSize, "RtlExtendHeap returned %lu, expected %Iu\n", Ret, GrowSize);

    Reported = RtlSizeHeap(Heap, 0, Buf);
    ok(Reported == GrowSize, "RtlSizeHeap returned %Iu, expected %Iu\n", Reported, GrowSize);

    RtlFreeHeap(Heap, 0, Buf);
    Destroyed = RtlDestroyHeap(Heap);
    ok(Destroyed == NULL, "RtlDestroyHeap returned %p\n", Destroyed);
}

START_TEST(RtlHeapMisc)
{
    LoadHeapExports();
    TestRtlZeroHeapFreeOnly();
    TestRtlUsageHeapVirtualAccounting();
    TestRtlExtendHeapReturnValue();
}
