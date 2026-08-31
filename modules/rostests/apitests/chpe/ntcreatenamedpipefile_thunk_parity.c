/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64-on-ARM64 NtCreateNamedPipeFile export-thunk parity test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winternl.h>
#include <stdarg.h>
#include <stdio.h>

#define STATUS_SUCCESS ((NTSTATUS)0x00000000)
#define STATUS_INSTANCE_NOT_AVAILABLE ((NTSTATUS)0xC00000ABL)
#define FILE_CREATE 0x00000002
#define FILE_PIPE_FULL_DUPLEX 0x00000002

typedef NTSTATUS (NTAPI *PNT_CREATE_NAMED_PIPE_FILE)(PHANDLE NamedPipeFileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, ULONG WriteModeMessage, ULONG ReadModeMessage, ULONG NonBlocking, ULONG MaxInstances, ULONG InBufferSize, ULONG OutBufferSize, PLARGE_INTEGER DefaultTimeOut);
typedef BOOLEAN (WINAPI *PRTL_IS_EC_CODE)(ULONG_PTR Address);

static int
log_result(PCSTR Format, ...)
{
    CHAR Buffer[512];
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    va_end(Arguments);

    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
    fputs(Buffer, stdout);
    OutputDebugStringA(Buffer);
    return Length;
}

#define printf log_result

int
main(void)
{
    static const WCHAR PipeName[] = L"\\??\\pipe\\chpe_named_pipe_case";
    HMODULE NtDll;
    PNT_CREATE_NAMED_PIPE_FILE NtCreateNamedPipeFileDynamic;
    PRTL_IS_EC_CODE RtlIsEcCode;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Timeout;
    HANDLE Pipe = NULL, SecondPipe = NULL;
    NTSTATUS Status, SecondStatus;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_NTCREATENAMEDPIPEFILE_TEST_BEGIN\n");

    NtDll = GetModuleHandleW(L"ntdll.dll");
    NtCreateNamedPipeFileDynamic = NtDll ? (PNT_CREATE_NAMED_PIPE_FILE)GetProcAddress(NtDll, "NtCreateNamedPipeFile") : NULL;
    RtlIsEcCode = NtDll ? (PRTL_IS_EC_CODE)GetProcAddress(NtDll, "RtlIsEcCode") : NULL;
    if (!NtCreateNamedPipeFileDynamic)
    {
        printf("FAIL GetProcAddress NtCreateNamedPipeFile error=%lu\n", GetLastError());
        return 1;
    }

    Name.Buffer = (PWSTR)PipeName;
    Name.Length = (USHORT)(wcslen(PipeName) * sizeof(WCHAR));
    Name.MaximumLength = Name.Length + sizeof(WCHAR);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Timeout.QuadPart = -100000000;

    Status = NtCreateNamedPipeFileDynamic(&Pipe, GENERIC_READ | GENERIC_WRITE, &Attributes, &IoStatus, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE, FILE_PIPE_FULL_DUPLEX, FALSE, FALSE, FALSE, 1, 256, 256, &Timeout);
    SecondStatus = NtCreateNamedPipeFileDynamic(&SecondPipe, GENERIC_READ | GENERIC_WRITE, &Attributes, &IoStatus, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE, FILE_PIPE_FULL_DUPLEX, FALSE, FALSE, FALSE, 1, 256, 256, &Timeout);

    printf("NTCREATENAMEDPIPEFILE export=1 ec=%d status=0x%08lx second_status=0x%08lx handle=%d second_handle=%d\n", RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)NtCreateNamedPipeFileDynamic) : -1, Status, SecondStatus, Pipe != NULL, SecondPipe != NULL);

    if (SecondPipe) CloseHandle(SecondPipe);
    if (Pipe) CloseHandle(Pipe);

    if (Status != STATUS_SUCCESS || SecondStatus != STATUS_INSTANCE_NOT_AVAILABLE || !Pipe || SecondPipe)
    {
        printf("CHPE_NTCREATENAMEDPIPEFILE_TEST_FAIL\n");
        return 2;
    }

    printf("CHPE_NTCREATENAMEDPIPEFILE_TEST_PASS\n");
    return 0;
}
