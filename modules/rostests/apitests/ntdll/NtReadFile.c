/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         LGPLv2.1+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Test for NtReadFile
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include "precomp.h"

static
BOOL
Is64BitSystem(VOID)
{
#ifdef _WIN64
    return TRUE;
#else
    NTSTATUS Status;
    ULONG_PTR IsWow64;

    Status = NtQueryInformationProcess(NtCurrentProcess(),
                                       ProcessWow64Information,
                                       &IsWow64,
                                       sizeof(IsWow64),
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        return IsWow64 != 0;
    }

    return FALSE;
#endif
}

#ifdef _WIN64
#define IsWow64() FALSE
#else
#define IsWow64() Is64BitSystem()
#endif

static
ULONG
SizeOfMdl(VOID)
{
    return Is64BitSystem() ? 48 : 28;
}

START_TEST(NtReadFileAsync)
{
    static const CHAR Data[] = "0123456789abcdef";
    NTSTATUS (NTAPI *CancelIoFileEx)(HANDLE, PIO_STATUS_BLOCK, PIO_STATUS_BLOCK);
    WCHAR PipeName[80];
    CHAR Buffer[sizeof(Data) - 1];
    struct
    {
        IO_STATUS_BLOCK IoStatus;
        ULONG Guard[2];
    } Result;
    IO_STATUS_BLOCK CancelStatus;
    HANDLE Server, Client = INVALID_HANDLE_VALUE, Event = NULL;
    NTSTATUS Status, CancelResult;
    DWORD Bytes = 0, Wait;
    BOOL Success;

    _snwprintf(PipeName, _countof(PipeName),
               L"\\\\.\\pipe\\apitest-iosb-%lu", GetCurrentProcessId());
    Server = CreateNamedPipeW(PipeName,
                              PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                              1, 4096, 4096, 1000, NULL);
    ok(Server != INVALID_HANDLE_VALUE, "CreateNamedPipe failed: %lu\n", GetLastError());
    if (Server == INVALID_HANDLE_VALUE) return;

    Client = CreateFileW(PipeName, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok(Client != INVALID_HANDLE_VALUE, "CreateFile failed: %lu\n", GetLastError());
    if (Client == INVALID_HANDLE_VALUE) goto Cleanup;

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Event != NULL, "CreateEvent failed: %lu\n", GetLastError());
    if (!Event) goto Cleanup;

    /* The write occurs after NtReadFile returns, so the thunk's temporary
     * native IOSB cannot serve as the destination of a pending WoW64 read. */
    memset(&Result, 0x55, sizeof(Result));
    memset(Buffer, 0xcc, sizeof(Buffer));
    Status = NtReadFile(Server, Event, NULL, NULL, &Result.IoStatus,
                        Buffer, sizeof(Buffer), NULL, NULL);
    ok_hex(Status, STATUS_PENDING);
    ok_hex(Result.IoStatus.Status, 0x55555555);
    ok_eq_ulongptr(Result.IoStatus.Information, ~(ULONG_PTR)0 / 3);

    Success = WriteFile(Client, Data, sizeof(Buffer), &Bytes, NULL);
    ok(Success, "WriteFile failed: %lu\n", GetLastError());
    ok_eq_ulong(Bytes, sizeof(Buffer));
    Wait = WaitForSingleObject(Event, 5000);
    ok_eq_ulong(Wait, WAIT_OBJECT_0);
    if (Wait != WAIT_OBJECT_0) goto Cleanup;
    ok_hex(Result.IoStatus.Status, STATUS_SUCCESS);
    ok_eq_ulongptr(Result.IoStatus.Information, sizeof(Buffer));
    ok(!memcmp(Buffer, Data, sizeof(Buffer)), "Pending read returned wrong data\n");
    ok_hex(Result.Guard[0], 0x55555555);
    ok_hex(Result.Guard[1], 0x55555555);

    /* An asynchronous handle uses the same IOSB layout when data is already
     * queued and the read completes before returning from the system call. */
    Success = WriteFile(Client, Data, sizeof(Buffer), &Bytes, NULL);
    ok(Success, "WriteFile failed: %lu\n", GetLastError());
    ResetEvent(Event);
    memset(&Result, 0x55, sizeof(Result));
    memset(Buffer, 0xcc, sizeof(Buffer));
    Status = NtReadFile(Server, Event, NULL, NULL, &Result.IoStatus,
                        Buffer, sizeof(Buffer), NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    Wait = WaitForSingleObject(Event, 5000);
    ok_eq_ulong(Wait, WAIT_OBJECT_0);
    if (Wait != WAIT_OBJECT_0) goto Cleanup;
    ok_hex(Result.IoStatus.Status, STATUS_SUCCESS);
    ok_eq_ulongptr(Result.IoStatus.Information, sizeof(Buffer));
    ok(!memcmp(Buffer, Data, sizeof(Buffer)), "Immediate read returned wrong data\n");
    ok_hex(Result.Guard[0], 0x55555555);
    ok_hex(Result.Guard[1], 0x55555555);

    CancelIoFileEx = (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCancelIoFileEx");
    if (!CancelIoFileEx)
    {
        skip("NtCancelIoFileEx is unavailable\n");
        goto Cleanup;
    }

    /* Cancellation must find the request by the original caller's IOSB. */
    ResetEvent(Event);
    memset(&Result, 0x55, sizeof(Result));
    Status = NtReadFile(Server, Event, NULL, NULL, &Result.IoStatus,
                        Buffer, sizeof(Buffer), NULL, NULL);
    ok_hex(Status, STATUS_PENDING);
    CancelResult = CancelIoFileEx(Server, &Result.IoStatus, &CancelStatus);
    ok_hex(CancelResult, STATUS_SUCCESS);
    if (!NT_SUCCESS(CancelResult))
        WriteFile(Client, Data, sizeof(Buffer), &Bytes, NULL);
    Wait = WaitForSingleObject(Event, 5000);
    ok_eq_ulong(Wait, WAIT_OBJECT_0);
    if (Wait != WAIT_OBJECT_0) goto Cleanup;
    ok_hex(Result.IoStatus.Status, STATUS_CANCELLED);
    ok_eq_ulongptr(Result.IoStatus.Information, 0);
    ok_hex(Result.Guard[0], 0x55555555);
    ok_hex(Result.Guard[1], 0x55555555);

Cleanup:
    CloseHandle(Server);
    if (Client != INVALID_HANDLE_VALUE) CloseHandle(Client);
    if (Event) CloseHandle(Event);
}

START_TEST(NtReadFile)
{
    NTSTATUS Status;
    HANDLE FileHandle;
    UNICODE_STRING FileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\ntdll-apitest-NtReadFile-test.bin");
    PVOID Buffer;
    SIZE_T BufferSize;
    LARGE_INTEGER ByteOffset;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    FILE_DISPOSITION_INFORMATION DispositionInfo;
    ULONG TooLargeDataSize = (MAXUSHORT + 1 - SizeOfMdl()) / sizeof(ULONG_PTR) * PAGE_SIZE; // 0x3FF9000 on x86
    ULONG LargeMdlMaxDataSize = TooLargeDataSize - PAGE_SIZE;

    trace("System is %d bits, Size of MDL: %lu\n", Is64BitSystem() ? 64 : 32, SizeOfMdl());
    trace("Max MDL data size: 0x%lx bytes\n", LargeMdlMaxDataSize);

    ByteOffset.QuadPart = 0;

    Buffer = NULL;
    BufferSize = TooLargeDataSize;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(),
                                     &Buffer,
                                     0,
                                     &BufferSize,
                                     MEM_RESERVE | MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to allocate memory, status %lx\n", Status);
        return;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &FileName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtCreateFile(&FileHandle,
                          FILE_READ_DATA | FILE_WRITE_DATA | DELETE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatus,
                          NULL,
                          0,
                          0,
                          FILE_SUPERSEDE,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
                                                    FILE_NO_INTERMEDIATE_BUFFERING,
                          NULL,
                          0);
    ok_hex(Status, STATUS_SUCCESS);

    ByteOffset.QuadPart = 0x10000;
    Status = NtWriteFile(FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatus,
                         Buffer,
                         BufferSize - 0x10000,
                         &ByteOffset,
                         NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ByteOffset.QuadPart = 0;

    /* non-cached, max size -- succeeds */
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatus,
                        Buffer,
                        LargeMdlMaxDataSize - PAGE_SIZE,
                        &ByteOffset,
                        NULL);
    ok_hex(Status, STATUS_SUCCESS);

    /* non-cached, max size -- succeeds */
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatus,
                        Buffer,
                        LargeMdlMaxDataSize,
                        &ByteOffset,
                        NULL);
    ok_hex(Status, STATUS_SUCCESS);

    /* non-cached, too large -- fails to allocate MDL
     * Note: this returns STATUS_SUCCESS on Vista+ -- higher MDL size limit */
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatus,
                        Buffer,
                        LargeMdlMaxDataSize + PAGE_SIZE,
                        &ByteOffset,
                        NULL);
    if (GetNTVersion() >= _WIN32_WINNT_VISTA)
        ok_hex(Status, STATUS_SUCCESS);
    else
        ok_hex(Status, STATUS_INSUFFICIENT_RESOURCES);

    /* Invalid buffer address */
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatus,
                        LongToPtr(-1),
                        PAGE_SIZE,
                        &ByteOffset,
                        NULL);
    ok_hex(Status, STATUS_ACCESS_VIOLATION);

    /* Buffer probing fails */
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatus,
                        Buffer,
                        2 * LargeMdlMaxDataSize,
                        &ByteOffset,
                        NULL);
    ok_hex(Status, STATUS_ACCESS_VIOLATION); // Different to NtWriteFile

    /* non-cached, unaligned -- fails with invalid parameter */
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatus,
                        Buffer,
                        LargeMdlMaxDataSize + 1,
                        &ByteOffset,
                        NULL);
    ok_hex(Status, STATUS_INVALID_PARAMETER); // Different to NtWriteFile

    DispositionInfo.DeleteFile = TRUE;
    Status = NtSetInformationFile(FileHandle,
                                  &IoStatus,
                                  &DispositionInfo,
                                  sizeof(DispositionInfo),
                                  FileDispositionInformation);
    ok_hex(Status, STATUS_SUCCESS);
    Status = NtClose(FileHandle);
    ok_hex(Status, STATUS_SUCCESS);

    Status = NtCreateFile(&FileHandle,
                          FILE_READ_DATA | FILE_WRITE_DATA | DELETE | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatus,
                          NULL,
                          0,
                          0,
                          FILE_SUPERSEDE,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                          NULL,
                          0);
    ok_hex(Status, STATUS_SUCCESS);

    ByteOffset.QuadPart = 0x10000;
    Status = NtWriteFile(FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatus,
                         Buffer,
                         BufferSize - 0x10000,
                         &ByteOffset,
                         NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ByteOffset.QuadPart = 0;

    /* cached: succeeds with arbitrary length */
    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatus,
                        Buffer,
                        LargeMdlMaxDataSize,
                        &ByteOffset,
                        NULL);
    ok_hex(Status, STATUS_SUCCESS);

    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatus,
                        Buffer,
                        LargeMdlMaxDataSize + 1,
                        &ByteOffset,
                        NULL);
    ok_hex(Status, STATUS_SUCCESS);

    Status = NtReadFile(FileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatus,
                        Buffer,
                        TooLargeDataSize,
                        &ByteOffset,
                        NULL);
    ok_hex(Status, STATUS_SUCCESS);

    DispositionInfo.DeleteFile = TRUE;
    Status = NtSetInformationFile(FileHandle,
                                  &IoStatus,
                                  &DispositionInfo,
                                  sizeof(DispositionInfo),
                                  FileDispositionInformation);
    ok_hex(Status, STATUS_SUCCESS);
    Status = NtClose(FileHandle);
    ok_hex(Status, STATUS_SUCCESS);

    Status = NtFreeVirtualMemory(NtCurrentProcess(),
                                 &Buffer,
                                 &BufferSize,
                                 MEM_RELEASE);
    ok_hex(Status, STATUS_SUCCESS);
}
