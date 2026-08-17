/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable AMD64-on-ARM64 virtual-memory write protection probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((LONG)(Status)) >= 0)
#endif

typedef LONG NTSTATUS;
typedef INT (__cdecl *PCODE_FUNCTION)(VOID);

#define STATUS_PARTIAL_COPY ((NTSTATUS)0x8000000dL)

__declspec(dllimport) NTSTATUS NTAPI NtWriteVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, SIZE_T NumberOfBytesToWrite, PSIZE_T NumberOfBytesWritten);
__declspec(dllimport) NTSTATUS NTAPI NtFlushInstructionCache(HANDLE ProcessHandle, PVOID BaseAddress, SIZE_T NumberOfBytesToFlush);

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

static VOID
make_return_code(BYTE Code[6], LONG Value)
{
    Code[0] = 0xb8;
    CopyMemory(&Code[1], &Value, sizeof(Value));
    Code[5] = 0xc3;
}

static INT
check_query(PCSTR Stage, PVOID Address, DWORD ExpectedProtect, DWORD ExpectedAllocationProtect)
{
    MEMORY_BASIC_INFORMATION Information;
    SIZE_T Result;
    INT Failed;

    ZeroMemory(&Information, sizeof(Information));
    Result = VirtualQuery(Address, &Information, sizeof(Information));
    Failed = Result != sizeof(Information) || Information.Protect != ExpectedProtect || Information.AllocationProtect != ExpectedAllocationProtect || Information.State != MEM_COMMIT || Information.Type != MEM_PRIVATE;
    printf("QUERY stage=%s result=%Iu protect=0x%08lx alloc=0x%08lx state=0x%08lx type=0x%08lx\n", Stage, Result, Information.Protect, Information.AllocationProtect, Information.State, Information.Type);
    return Failed;
}

static INT
test_tracked_rwx(VOID)
{
    PCODE_FUNCTION Function;
    PVOID Page;
    BYTE Code[6];
    SIZE_T Written;
    NTSTATUS FirstStatus;
    NTSTATUS SecondStatus;
    INT First;
    INT Second = -1;
    INT QueryResult = 0;

    Page = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!Page)
        return 1;

    Function = (PCODE_FUNCTION)Page;
    QueryResult |= check_query("rwx_alloc", Page, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_READWRITE);
    make_return_code(Code, 11);
    Written = 0;
    FirstStatus = NtWriteVirtualMemory(GetCurrentProcess(), Page, Code, sizeof(Code), &Written);
    NtFlushInstructionCache(GetCurrentProcess(), Page, sizeof(Code));
    First = NT_SUCCESS(FirstStatus) && Written == sizeof(Code) ? Function() : -1;
    QueryResult |= check_query("rwx_after_first_exec", Page, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_READWRITE);

    make_return_code(Code, 22);
    Written = 0;
    SecondStatus = NtWriteVirtualMemory(GetCurrentProcess(), Page, Code, sizeof(Code), &Written);
    QueryResult |= check_query("rwx_after_second_write", Page, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_READWRITE);
    if (NT_SUCCESS(SecondStatus) && Written == sizeof(Code))
    {
        NtFlushInstructionCache(GetCurrentProcess(), Page, sizeof(Code));
        Second = Function();
    }
    QueryResult |= check_query("rwx_after_second_exec", Page, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_READWRITE);
    printf("TRACKED_RWX first_status=0x%08lx second_status=0x%08lx written=%Iu first=%d second=%d query=%d result=%d\n", FirstStatus, SecondStatus, Written, First, Second, QueryResult, NT_SUCCESS(FirstStatus) && NT_SUCCESS(SecondStatus) && Written == sizeof(Code) && First == 11 && Second == 22 && !QueryResult ? 0 : 1);
    VirtualFree(Page, 0, MEM_RELEASE);
    return NT_SUCCESS(FirstStatus) && NT_SUCCESS(SecondStatus) && Written == sizeof(Code) && First == 11 && Second == 22 && !QueryResult ? 0 : 1;
}

static INT
test_explicit_rx(VOID)
{
    PCODE_FUNCTION Function;
    PVOID Page;
    BYTE Code[6];
    SIZE_T Written;
    NTSTATUS Status;
    DWORD OldProtect;
    INT Before;
    INT After = -1;
    INT QueryResult = 0;

    Page = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!Page)
        return 1;

    Function = (PCODE_FUNCTION)Page;
    make_return_code(Code, 33);
    CopyMemory(Page, Code, sizeof(Code));
    if (!VirtualProtect(Page, 0x1000, PAGE_EXECUTE_READ, &OldProtect))
    {
        VirtualFree(Page, 0, MEM_RELEASE);
        return 2;
    }
    NtFlushInstructionCache(GetCurrentProcess(), Page, sizeof(Code));
    Before = Function();
    QueryResult |= check_query("rx_before_write", Page, PAGE_EXECUTE_READ, PAGE_READWRITE);

    make_return_code(Code, 44);
    Written = 0;
    Status = NtWriteVirtualMemory(GetCurrentProcess(), Page, Code, sizeof(Code), &Written);
    QueryResult |= check_query("rx_after_write", Page, PAGE_EXECUTE_READ, PAGE_READWRITE);
    After = Function();
    printf("EXPLICIT_RX status=0x%08lx written=%Iu before=%d after=%d query=%d result=%d\n", Status, Written, Before, After, QueryResult, Status == STATUS_PARTIAL_COPY && Written == 0 && Before == 33 && After == 33 && !QueryResult ? 0 : 1);
    VirtualFree(Page, 0, MEM_RELEASE);
    return Status == STATUS_PARTIAL_COPY && Written == 0 && Before == 33 && After == 33 && !QueryResult ? 0 : 3;
}

int
main(VOID)
{
    INT RwxResult;
    INT RxResult;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_WRITEVM_PROTECTION_BEGIN\n");
    RwxResult = test_tracked_rwx();
    RxResult = test_explicit_rx();
    printf("CHPE_WRITEVM_PROTECTION_END rwx=%d rx=%d\n", RwxResult, RxResult);
    return RwxResult || RxResult;
}
