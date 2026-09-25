/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Stack guarantees must enlarge and preserve the stack guard.
 */

#include <windows.h>
#include <wine/test.h>

#define PAGE_BYTES 0x1000
#define GUARANTEE_BYTES 0x20000

static DWORD overflow_thread;
static LONG overflow_count;

static LONG CALLBACK overflow_handler(EXCEPTION_POINTERS *exception)
{
    if (GetCurrentThreadId() == overflow_thread &&
        exception->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW &&
        InterlockedIncrement(&overflow_count) == 1)
        return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

static BOOL check_guard(ULONG size)
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    MEMORY_BASIC_INFORMATION info;
    SIZE_T ret;

    ret = VirtualQuery((BYTE *)tib->StackLimit - size - PAGE_BYTES, &info, sizeof(info));
    ok(ret == sizeof(info), "VirtualQuery returned %Iu, error %lu\n", ret, GetLastError());
    if (ret != sizeof(info)) return FALSE;
    ok(info.State == MEM_COMMIT, "Guard state %#lx\n", info.State);
    ok(info.Protect == (PAGE_READWRITE | PAGE_GUARD), "Guard protection %#lx\n", info.Protect);
    ok(info.RegionSize == size + PAGE_BYTES, "Guard size %#Ix, expected %#lx\n",
       info.RegionSize, size + PAGE_BYTES);
    ok((BYTE *)info.BaseAddress + info.RegionSize == tib->StackLimit,
       "Guard ends at %p, stack limit %p\n", (BYTE *)info.BaseAddress + info.RegionSize, tib->StackLimit);
    return info.State == MEM_COMMIT && info.Protect == (PAGE_READWRITE | PAGE_GUARD) &&
           info.RegionSize == size + PAGE_BYTES;
}

static DWORD WINAPI test_stack(void *arg)
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    MEMORY_BASIC_INFORMATION info;
    BYTE *allocation, *previous_limit;
    volatile BYTE *target = NULL;
    ULONG size, old_size, minimum = sizeof(void *) / 4 * PAGE_BYTES;
    SIZE_T reserve;
    unsigned int i;
    BOOL ret, safe;
    PVOID handler;

    /* Warm up the test reporting code before inspecting the current limit. */
    trace("Testing stack guarantee and guard growth\n");
    if (!VirtualQuery(tib->StackLimit, &info, sizeof(info)))
    {
        ok(0, "Initial VirtualQuery failed %lu\n", GetLastError());
        return 1;
    }
    allocation = info.AllocationBase;
    reserve = (BYTE *)tib->StackBase - allocation;

    size = 0;
    ret = SetThreadStackGuarantee(&size);
    ok(ret, "Initial query failed %lu\n", GetLastError());
    ok(size == 0, "Initial guarantee %#lx\n", size);
    size = 1;
    ret = SetThreadStackGuarantee(&size);
    ok(ret, "Small guarantee failed %lu\n", GetLastError());
    ok(size == 0, "Previous guarantee %#lx\n", size);
    check_guard(minimum);

    size = GUARANTEE_BYTES;
    ret = SetThreadStackGuarantee(&size);
    ok(ret, "Large guarantee failed %lu\n", GetLastError());
    ok(size == minimum, "Previous guarantee %#lx\n", size);
    safe = check_guard(GUARANTEE_BYTES);

    /* A write 64 KiB below StackLimit must be inside the enlarged guard.
     * Do not perform it on an implementation whose guard check failed. */
    if (safe)
    {
        previous_limit = tib->StackLimit;
        target = previous_limit - 0x10000;
        *target = 0x5a;
        ok(*target == 0x5a, "Stack write was lost\n");
        ok((BYTE *)tib->StackLimit <= (BYTE *)target, "Stack limit did not grow: %p\n", tib->StackLimit);
        check_guard(GUARANTEE_BYTES);
    }

    size = GUARANTEE_BYTES + 1;
    ret = SetThreadStackGuarantee(&size);
    ok(ret, "Unaligned guarantee failed %lu\n", GetLastError());
    ok(size == GUARANTEE_BYTES, "Previous guarantee %#lx\n", size);
    old_size = GUARANTEE_BYTES + PAGE_BYTES;
    safe = check_guard(old_size);
    size = 1;
    ret = SetThreadStackGuarantee(&size);
    ok(ret && size == old_size, "Smaller request returned %d, size %#lx\n", ret, size);
    size = 0;
    ret = SetThreadStackGuarantee(&size);
    ok(ret && size == old_size, "Query returned %d, size %#lx\n", ret, size);

    size = ~0u;
    SetLastError(0xdeadbeef);
    ret = SetThreadStackGuarantee(&size);
    ok(!ret, "Overflowing request succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER || GetLastError() == ERROR_INVALID_ADDRESS,
       "Overflowing request error %lu\n", GetLastError());
    ok(size == old_size, "Failed request returned size %#lx\n", size);
    size = reserve;
    ret = SetThreadStackGuarantee(&size);
    ok(!ret, "Request for entire reservation succeeded\n");
    ok(size == old_size, "Failed request returned size %#lx\n", size);
    size = 0;
    ret = SetThreadStackGuarantee(&size);
    ok(ret && size == old_size, "Failed request changed guarantee to %#lx\n", size);

    if (!safe) return 0;

    /* Keep RSP high while touching guard pages. The exception handler has its
     * normal stack and can retry the write after the overflow reserve opens. */
    handler = AddVectoredExceptionHandler(1, overflow_handler);
    ok(handler != NULL, "AddVectoredExceptionHandler failed\n");
    if (!handler) return 0;
    overflow_thread = GetCurrentThreadId();
    for (i = 0; i < reserve / PAGE_BYTES && !overflow_count; ++i)
    {
        previous_limit = tib->StackLimit;
        target = previous_limit - PAGE_BYTES;
        *target = 0x6b;
        if (!overflow_count && !check_guard(old_size)) break;
    }
    ok(overflow_count == 1, "Overflow count %ld\n", overflow_count);
    ok(tib->StackLimit == allocation + PAGE_BYTES, "Overflow stack limit %p, allocation %p\n",
       tib->StackLimit, allocation);
    VirtualQuery(allocation + PAGE_BYTES, &info, sizeof(info));
    ok(info.State == MEM_COMMIT && info.Protect == PAGE_READWRITE,
       "Overflow reserve state/protection %#lx/%#lx\n", info.State, info.Protect);
    ok(info.RegionSize >= old_size, "Overflow reserve only %#Ix bytes\n", info.RegionSize);
    VirtualQuery(allocation, &info, sizeof(info));
    ok(info.State == MEM_RESERVE || info.Protect == PAGE_NOACCESS,
       "Bottom page state/protection %#lx/%#lx\n", info.State, info.Protect);
    RemoveVectoredExceptionHandler(handler);
    overflow_thread = 0;
    return 0;
}

START_TEST(SetThreadStackGuarantee)
{
    HANDLE thread;
    DWORD result, code;

    thread = CreateThread(NULL, 0x100000, test_stack, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    ok(thread != NULL, "CreateThread failed %lu\n", GetLastError());
    if (!thread) return;
    result = WaitForSingleObject(thread, 30000);
    ok(result == WAIT_OBJECT_0, "Thread wait returned %#lx\n", result);
    if (result == WAIT_OBJECT_0)
    {
        GetExitCodeThread(thread, &code);
        ok(code == 0, "Thread exited %#lx\n", code);
    }
    CloseHandle(thread);
}
