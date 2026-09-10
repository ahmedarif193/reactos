/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite MmMapLockedPagesSpecifyCache test user-mode part
 * PROGRAMMER:      Pierre Schweitzer <pierre@reactos.org>
 */

#include <kmt_test.h>
#include <ndk/exfuncs.h>

#include "MmMapLockedPagesSpecifyCache.h"

#define ALIGN_DOWN_BY(size, align) \
        ((ULONG_PTR)(size) & ~((ULONG_PTR)(align) - 1))

#define SET_BUFFER_LENGTH(Var, Length)         \
{                                              \
    C_ASSERT(((Length) % sizeof(ULONG)) == 0); \
    Var = (Length);                            \
}

#define FILL_QUERY_BUFFER(QueryBuffer, BufferLength, UseCache) \
{                                                              \
    QueryBuffer.Length = BufferLength;                         \
    QueryBuffer.Buffer = NULL;                                 \
    QueryBuffer.Cached = UseCache;                             \
    QueryBuffer.Status = STATUS_SUCCESS;                       \
    QueryBuffer.MappingFlags = 0;                              \
}

#define FILL_READ_BUFFER(QueryBuffer, ReadBuffer)               \
{                                                               \
    PULONG Buffer;                                              \
    ReadBuffer.Buffer = QueryBuffer.Buffer;                     \
    if (!skip(QueryBuffer.Buffer != NULL, "Buffer is NULL\n"))  \
    {                                                           \
        ReadBuffer.Pattern = WRITE_PATTERN;                     \
        ReadBuffer.Length = QueryBuffer.Length;                 \
        Buffer = QueryBuffer.Buffer;                            \
        for (i = 0; i < ReadBuffer.Length / sizeof(ULONG); ++i) \
        {                                                       \
            Buffer[i] = ReadBuffer.Pattern;                     \
        }                                                       \
    }                                                           \
}

static VOID
TestQueryMapping(PVOID Address, SIZE_T Length, ULONG Protection)
{
    MEMORY_BASIC_INFORMATION Information;
    SIZE_T ReturnedLength = 0;
    NTSTATUS Status;

    Status = NtQueryVirtualMemory(NtCurrentProcess(), Address,
                                   MemoryBasicInformation, &Information,
                                   sizeof(Information), &ReturnedLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_eq_size(ReturnedLength, sizeof(Information));
    ok_eq_pointer(Information.BaseAddress, (PVOID)PAGE_ROUND_DOWN(Address));
    ok_eq_pointer(Information.AllocationBase, (PVOID)PAGE_ROUND_DOWN(Address));
    ok_eq_size(Information.RegionSize,
               PAGE_ROUND_UP(Length + ((ULONG_PTR)Address & (PAGE_SIZE - 1))));
    ok_eq_hex(Information.State, MEM_COMMIT);
    ok_eq_hex(Information.Protect & 0xff, Protection);
    ok_eq_hex(Information.AllocationProtect & 0xff, Protection);
    trace("MDL query: size=%Iu state=%lx protect=%lx allocation=%lx type=%lx\n",
          Information.RegionSize, Information.State, Information.Protect,
          Information.AllocationProtect, Information.Type);
}

#define CHECK_ALLOC(MappedBuffer, BufferLength)                 \
{                                                               \
    NTSTATUS Status;                                            \
    PVOID BaseAddress;                                          \
    SIZE_T Size;                                                \
    BaseAddress = MappedBuffer;                                 \
    Size = BufferLength;                                        \
    Status = NtAllocateVirtualMemory(NtCurrentProcess(),        \
                                     &BaseAddress,              \
                                     0,                         \
                                     &Size,                     \
                                     MEM_RESERVE,               \
                                     PAGE_READWRITE);           \
    ok_eq_hex(Status, STATUS_CONFLICTING_ADDRESSES);            \
    BaseAddress = MappedBuffer;                                 \
    Size = 0;                                                   \
    Status = NtFreeVirtualMemory(NtCurrentProcess(),            \
                                 &BaseAddress,                  \
                                 &Size,                         \
                                 MEM_DECOMMIT);                 \
    ok_eq_hex(Status, STATUS_UNABLE_TO_DELETE_SECTION);         \
    BaseAddress = MappedBuffer;                                 \
    Size = 0;                                                   \
    Status = NtFreeVirtualMemory(NtCurrentProcess(),            \
                                 &BaseAddress,                  \
                                 &Size,                         \
                                 MEM_RELEASE);                  \
    ok_eq_hex(Status, STATUS_UNABLE_TO_DELETE_SECTION);         \
    Status = NtUnmapViewOfSection(NtCurrentProcess(),           \
                                  MappedBuffer);                \
    ok_eq_hex(Status, STATUS_NOT_MAPPED_VIEW);                  \
}

static VOID
TestProcessExitMappings(VOID)
{
    static const ULONG Sizes[] = {PAGE_SIZE, 1024 * 1024, 4 * 1024 * 1024};
    WCHAR Image[MAX_PATH];
    ULONG Index;

    if (!GetModuleFileNameW(NULL, Image, ARRAYSIZE(Image)))
    {
        ok(0, "Could not obtain the child executable path: %lu\n", GetLastError());
        return;
    }
    for (Index = 0; Index < ARRAYSIZE(Sizes); ++Index)
    {
        STARTUPINFOW Startup = {sizeof(Startup)};
        PROCESS_INFORMATION Child;
        EXIT_BUFFER Request = {0};
        DWORD Length = sizeof(Request);
        DWORD Error;
        DWORD Wait;
        BOOL Created;
        ULONG Value = 0;
        SIZE_T Read;

        /* The child never executes user code or a driver cleanup callback. */
        Created = CreateProcessW(Image, NULL, NULL, NULL, FALSE,
                                 CREATE_SUSPENDED, NULL, NULL, &Startup, &Child);
        ok(Created, "Could not create the suspended child: %lu\n", GetLastError());
        if (!Created)
            return;
        Request.Process = Child.hProcess;
        Request.Length = Sizes[Index];
        Request.Pattern = WRITE_PATTERN;
        Error = KmtSendBufferToDriver(IOCTL_MAP_EXIT_BUFFER, &Request,
                                     sizeof(Request), &Length);
        ok_eq_ulong(Error, ERROR_SUCCESS);
        if (Error == ERROR_SUCCESS)
        {
            ok(Request.Address != NULL, "No mapping returned for the child\n");
            ok(ReadProcessMemory(Child.hProcess, Request.Address, &Value,
                                 sizeof(Value), &Read), "Cannot read child mapping: %lu\n", GetLastError());
            ok_eq_ulong(Value, WRITE_PATTERN);
            Value = 0;
            ok(ReadProcessMemory(Child.hProcess,
                                 (PUCHAR)Request.Address + Request.Length - sizeof(Value),
                                 &Value, sizeof(Value), &Read),
               "Cannot read the end of the child mapping: %lu\n", GetLastError());
            ok_eq_ulong(Value, WRITE_PATTERN);
        }
        ok(TerminateProcess(Child.hProcess, 0), "Cannot terminate child: %lu\n", GetLastError());
        Wait = WaitForSingleObject(Child.hProcess, 10000);
        ok_eq_ulong(Wait, WAIT_OBJECT_0);
        if (Wait == WAIT_OBJECT_0 && Error == ERROR_SUCCESS)
        {
            Length = sizeof(Request);
            Error = KmtSendBufferToDriver(IOCTL_CHECK_EXIT_BUFFER, &Request,
                                         sizeof(Request), &Length);
            ok_eq_ulong(Error, ERROR_SUCCESS);
        }
        CloseHandle(Child.hThread);
        CloseHandle(Child.hProcess);
        if (Wait != WAIT_OBJECT_0 || Error != ERROR_SUCCESS)
            return;
    }
}

START_TEST(MmMapLockedPagesSpecifyCache)
{
    QUERY_BUFFER QueryBuffer;
    READ_BUFFER ReadBuffer;
    DWORD Length;
    USHORT i;
    USHORT BufferLength;
    SYSTEM_BASIC_INFORMATION BasicInfo;
    NTSTATUS Status;
    ULONG_PTR HighestAddress;
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"MmMapLockedPagesSpecifyCache", FALSE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
        return;

    // Less than a page
    SET_BUFFER_LENGTH(BufferLength, 2048);
    Length = sizeof(QUERY_BUFFER);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, FALSE);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer != NULL, "Buffer is NULL\n");
    TestQueryMapping(QueryBuffer.Buffer, BufferLength, PAGE_READWRITE);
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);

    Length = 0;
    FILL_READ_BUFFER(QueryBuffer, ReadBuffer);
    ok(KmtSendBufferToDriver(IOCTL_READ_BUFFER, &ReadBuffer, sizeof(READ_BUFFER), &Length) == ERROR_SUCCESS, "\n");

    Length = sizeof(QUERY_BUFFER);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, TRUE);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer != NULL, "Buffer is NULL\n");
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);

    Length = 0;
    FILL_READ_BUFFER(QueryBuffer, ReadBuffer);
    ok(KmtSendBufferToDriver(IOCTL_READ_BUFFER, &ReadBuffer, sizeof(READ_BUFFER), &Length) == ERROR_SUCCESS, "\n");

    // read-only, non-executable user mapping
    Length = sizeof(QUERY_BUFFER);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, TRUE);
    QueryBuffer.MappingFlags = TEST_MDL_MAPPING_NO_WRITE | TEST_MDL_MAPPING_NO_EXECUTE;
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer != NULL, "Buffer is NULL\n");
    TestQueryMapping(QueryBuffer.Buffer, BufferLength, PAGE_READONLY);
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);
    KmtStartSeh()
    *(volatile ULONG *)QueryBuffer.Buffer;
    KmtEndSeh(STATUS_SUCCESS);
    KmtStartSeh()
    *(volatile ULONG *)QueryBuffer.Buffer = WRITE_PATTERN;
    KmtEndSeh(STATUS_ACCESS_VIOLATION);
    Length = 0;
    ok(KmtSendBufferToDriver(IOCTL_CLEAN, NULL, 0, &Length) == ERROR_SUCCESS, "\n");

    // 1 page
    SET_BUFFER_LENGTH(BufferLength, 4096);
    Length = sizeof(QUERY_BUFFER);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, FALSE);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer != NULL, "Buffer is NULL\n");
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);

    Length = 0;
    FILL_READ_BUFFER(QueryBuffer, ReadBuffer);
    ok(KmtSendBufferToDriver(IOCTL_READ_BUFFER, &ReadBuffer, sizeof(READ_BUFFER), &Length) == ERROR_SUCCESS, "\n");

    Length = sizeof(QUERY_BUFFER);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, TRUE);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer != NULL, "Buffer is NULL\n");
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);

    Length = 0;
    FILL_READ_BUFFER(QueryBuffer, ReadBuffer);
    ok(KmtSendBufferToDriver(IOCTL_READ_BUFFER, &ReadBuffer, sizeof(READ_BUFFER), &Length) == ERROR_SUCCESS, "\n");

    // more than 1 page
    SET_BUFFER_LENGTH(BufferLength, 4096 + 2048);
    Length = sizeof(QUERY_BUFFER);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, FALSE);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer != NULL, "Buffer is NULL\n");
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);

    Length = 0;
    FILL_READ_BUFFER(QueryBuffer, ReadBuffer);
    ok(KmtSendBufferToDriver(IOCTL_READ_BUFFER, &ReadBuffer, sizeof(READ_BUFFER), &Length) == ERROR_SUCCESS, "\n");

    Length = sizeof(QUERY_BUFFER);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, TRUE);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer != NULL, "Buffer is NULL\n");
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);

    Length = 0;
    FILL_READ_BUFFER(QueryBuffer, ReadBuffer);
    ok(KmtSendBufferToDriver(IOCTL_READ_BUFFER, &ReadBuffer, sizeof(READ_BUFFER), &Length) == ERROR_SUCCESS, "\n");

    // 2 pages
    SET_BUFFER_LENGTH(BufferLength, 2 * 4096);
    Length = sizeof(QUERY_BUFFER);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, FALSE);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer != NULL, "Buffer is NULL\n");
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);

    Length = 0;
    FILL_READ_BUFFER(QueryBuffer, ReadBuffer);
    ok(KmtSendBufferToDriver(IOCTL_READ_BUFFER, &ReadBuffer, sizeof(READ_BUFFER), &Length) == ERROR_SUCCESS, "\n");

    Length = sizeof(QUERY_BUFFER);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, TRUE);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer != NULL, "Buffer is NULL\n");
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);

    Length = 0;
    FILL_READ_BUFFER(QueryBuffer, ReadBuffer);
    ok(KmtSendBufferToDriver(IOCTL_READ_BUFFER, &ReadBuffer, sizeof(READ_BUFFER), &Length) == ERROR_SUCCESS, "\n");

    // more than 2 pages
    SET_BUFFER_LENGTH(BufferLength, 2 * 4096 + 2048);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, FALSE);
    Length = sizeof(QUERY_BUFFER);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer != NULL, "Buffer is NULL\n");
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);

    Length = 0;
    FILL_READ_BUFFER(QueryBuffer, ReadBuffer);
    ok(KmtSendBufferToDriver(IOCTL_READ_BUFFER, &ReadBuffer, sizeof(READ_BUFFER), &Length) == ERROR_SUCCESS, "\n");

    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, TRUE);
    Length = sizeof(QUERY_BUFFER);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer != NULL, "Buffer is NULL\n");
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);

    Length = 0;
    FILL_READ_BUFFER(QueryBuffer, ReadBuffer);
    ok(KmtSendBufferToDriver(IOCTL_READ_BUFFER, &ReadBuffer, sizeof(READ_BUFFER), &Length) == ERROR_SUCCESS, "\n");

    // ask for a specific address (we know that ReadBuffer.Buffer is free)
    SET_BUFFER_LENGTH(BufferLength, 4096);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, FALSE);
    QueryBuffer.Buffer = ReadBuffer.Buffer;
    Length = sizeof(QUERY_BUFFER);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer == ReadBuffer.Buffer, "Buffer is NULL\n");
    CHECK_ALLOC(QueryBuffer.Buffer, BufferLength);

    Length = 0;
    FILL_READ_BUFFER(QueryBuffer, ReadBuffer);
    ok(KmtSendBufferToDriver(IOCTL_READ_BUFFER, &ReadBuffer, sizeof(READ_BUFFER), &Length) == ERROR_SUCCESS, "\n");

    // ask for an unaligned address
    SET_BUFFER_LENGTH(BufferLength, 4096);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, FALSE);
    QueryBuffer.Buffer = (PVOID)((ULONG_PTR)ReadBuffer.Buffer + 2048);
    QueryBuffer.Status = STATUS_INVALID_ADDRESS;
    Length = sizeof(QUERY_BUFFER);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer == NULL, "Buffer is %p\n", QueryBuffer.Buffer);

    Length = 0;
    ok(KmtSendBufferToDriver(IOCTL_CLEAN, NULL, 0, &Length) == ERROR_SUCCESS, "\n");

    // get system info for MmHighestUserAddress
    Status = NtQuerySystemInformation(SystemBasicInformation,
                                      &BasicInfo,
                                      sizeof(BasicInfo),
                                      NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    trace("MaximumUserModeAddress: %lx\n", BasicInfo.MaximumUserModeAddress);
    HighestAddress = ALIGN_DOWN_BY(BasicInfo.MaximumUserModeAddress, PAGE_SIZE);

    // near MmHighestUserAddress
    SET_BUFFER_LENGTH(BufferLength, 4096);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, FALSE);
    QueryBuffer.Buffer = (PVOID)(HighestAddress - 15 * PAGE_SIZE); // 7ffe0000
    QueryBuffer.Status = STATUS_INVALID_ADDRESS;
    trace("QueryBuffer.Buffer %p\n", QueryBuffer.Buffer);
    Length = sizeof(QUERY_BUFFER);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Buffer == NULL, "Buffer is %p\n", QueryBuffer.Buffer);

    Length = 0;
    ok(KmtSendBufferToDriver(IOCTL_CLEAN, NULL, 0, &Length) == ERROR_SUCCESS, "\n");

    // far enough away from MmHighestUserAddress
    SET_BUFFER_LENGTH(BufferLength, 4096);
    FILL_QUERY_BUFFER(QueryBuffer, BufferLength, FALSE);
    QueryBuffer.Buffer = (PVOID)(HighestAddress - 16 * PAGE_SIZE); // 7ffdf000
    QueryBuffer.Status = -1;
    trace("QueryBuffer.Buffer %p\n", QueryBuffer.Buffer);
    Length = sizeof(QUERY_BUFFER);
    ok(KmtSendBufferToDriver(IOCTL_QUERY_BUFFER, &QueryBuffer, sizeof(QUERY_BUFFER), &Length) == ERROR_SUCCESS, "\n");
    ok_eq_int(QueryBuffer.Length, BufferLength);
    ok(QueryBuffer.Status == STATUS_SUCCESS ||
       QueryBuffer.Status == STATUS_CONFLICTING_ADDRESSES, "Status = %lx\n", QueryBuffer.Status);

    Length = 0;
    ok(KmtSendBufferToDriver(IOCTL_CLEAN, NULL, 0, &Length) == ERROR_SUCCESS, "\n");

    TestProcessExitMappings();

    KmtCloseDriver();
    KmtUnloadDriver();
}
