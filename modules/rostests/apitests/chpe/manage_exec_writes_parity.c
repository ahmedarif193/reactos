/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64-on-ARM64 executable-memory write management parity probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define PROCESS_MANAGE_WRITES_TO_EXECUTABLE_MEMORY 83
#define THREAD_MANAGE_WRITES_TO_EXECUTABLE_MEMORY 48
#define VM_PAGE_DIRTY_STATE_INFORMATION 3
#ifndef STATUS_IN_PAGE_ERROR
#define STATUS_IN_PAGE_ERROR ((DWORD)0xc0000006UL)
#endif
#ifndef STATUS_EXECUTABLE_MEMORY_WRITE
#define STATUS_EXECUTABLE_MEMORY_WRITE ((LONG)0xc0000723L)
#endif

typedef LONG NTSTATUS;

typedef struct _MANAGE_WRITES_TO_EXECUTABLE_MEMORY
{
    ULONG Version : 8;
    ULONG ProcessEnableWriteExceptions : 1;
    ULONG ThreadAllowWrites : 1;
    ULONG Spare : 22;
    PVOID KernelWriteToExecutableSignal;
} MANAGE_WRITES_TO_EXECUTABLE_MEMORY, *PMANAGE_WRITES_TO_EXECUTABLE_MEMORY;

typedef struct _MEMORY_RANGE_ENTRY_NATIVE
{
    PVOID VirtualAddress;
    SIZE_T NumberOfBytes;
} MEMORY_RANGE_ENTRY_NATIVE, *PMEMORY_RANGE_ENTRY_NATIVE;

typedef NTSTATUS (NTAPI *PNT_SET_INFORMATION_PROCESS)(HANDLE ProcessHandle, ULONG ProcessInformationClass, PVOID ProcessInformation, ULONG ProcessInformationLength);
typedef NTSTATUS (NTAPI *PNT_SET_INFORMATION_THREAD)(HANDLE ThreadHandle, ULONG ThreadInformationClass, PVOID ThreadInformation, ULONG ThreadInformationLength);
typedef NTSTATUS (NTAPI *PNT_SET_INFORMATION_VIRTUAL_MEMORY)(HANDLE ProcessHandle, ULONG InformationClass, ULONG_PTR NumberOfEntries, PMEMORY_RANGE_ENTRY_NATIVE VirtualAddresses, PVOID Information, ULONG InformationLength);
typedef NTSTATUS (NTAPI *PNT_WRITE_VIRTUAL_MEMORY)(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, SIZE_T NumberOfBytesToWrite, PSIZE_T NumberOfBytesWritten);

static PNT_SET_INFORMATION_THREAD NtSetInformationThreadPtr;
static volatile BYTE *ExpectedWriteAddress;
static volatile LONG HandlerCount;
static volatile LONG HandlerError;

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

static LONG CALLBACK
write_exception_handler(PEXCEPTION_POINTERS ExceptionPointers)
{
    MANAGE_WRITES_TO_EXECUTABLE_MEMORY Information;
    PEXCEPTION_RECORD ExceptionRecord;
    volatile BYTE *FaultAddress;
    NTSTATUS Status;
    BYTE Value;

    ExceptionRecord = ExceptionPointers->ExceptionRecord;
    if (ExceptionRecord->ExceptionCode != (DWORD)STATUS_IN_PAGE_ERROR || ExceptionRecord->NumberParameters != 3 || ExceptionRecord->ExceptionInformation[0] != 1 || ExceptionRecord->ExceptionInformation[2] != (ULONG_PTR)STATUS_EXECUTABLE_MEMORY_WRITE)
        return EXCEPTION_CONTINUE_SEARCH;

    FaultAddress = (volatile BYTE *)ExceptionRecord->ExceptionInformation[1];
    if (FaultAddress != ExpectedWriteAddress)
    {
        HandlerError = 1;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    InterlockedIncrement(&HandlerCount);
    ZeroMemory(&Information, sizeof(Information));
    Information.Version = 2;
    Information.ThreadAllowWrites = 1;
    Status = NtSetInformationThreadPtr(GetCurrentThread(), THREAD_MANAGE_WRITES_TO_EXECUTABLE_MEMORY, &Information, sizeof(Information));
    if (Status)
    {
        HandlerError = 2;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    Value = *FaultAddress;
    *FaultAddress = Value;
    Information.ThreadAllowWrites = 0;
    Status = NtSetInformationThreadPtr(GetCurrentThread(), THREAD_MANAGE_WRITES_TO_EXECUTABLE_MEMORY, &Information, sizeof(Information));
    if (Status)
    {
        HandlerError = 3;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    return EXCEPTION_CONTINUE_EXECUTION;
}

static NTSTATUS
clear_dirty_state(PNT_SET_INFORMATION_VIRTUAL_MEMORY NtSetInformationVirtualMemoryPtr, PVOID Address)
{
    MEMORY_RANGE_ENTRY_NATIVE Range;
    ULONG Flag = 0;

    Range.VirtualAddress = Address;
    Range.NumberOfBytes = 1;
    return NtSetInformationVirtualMemoryPtr(GetCurrentProcess(), VM_PAGE_DIRTY_STATE_INFORMATION, 1, &Range, &Flag, sizeof(Flag));
}

static INT
check_query(PVOID Address)
{
    MEMORY_BASIC_INFORMATION Information;
    SIZE_T Result;

    ZeroMemory(&Information, sizeof(Information));
    Result = VirtualQuery(Address, &Information, sizeof(Information));
    printf("QUERY result=%Iu protect=0x%08lx alloc=0x%08lx state=0x%08lx type=0x%08lx\n", Result, Information.Protect, Information.AllocationProtect, Information.State, Information.Type);
    return Result != sizeof(Information) || Information.Protect != PAGE_EXECUTE_READWRITE || Information.AllocationProtect != PAGE_EXECUTE_READWRITE || Information.State != MEM_COMMIT || Information.Type != MEM_PRIVATE;
}

int
main(VOID)
{
    MANAGE_WRITES_TO_EXECUTABLE_MEMORY Information;
    PNT_SET_INFORMATION_PROCESS NtSetInformationProcessPtr;
    PNT_SET_INFORMATION_VIRTUAL_MEMORY NtSetInformationVirtualMemoryPtr;
    PNT_WRITE_VIRTUAL_MEMORY NtWriteVirtualMemoryPtr;
    PVOID Handler;
    HMODULE Ntdll;
    PVOID Page;
    BYTE Value;
    SIZE_T Written;
    NTSTATUS EnableStatus;
    NTSTATUS ClearDirectStatus;
    NTSTATUS ClearNtWriteStatus;
    NTSTATUS WriteStatus;
    NTSTATUS DisableStatus;
    LONG DirectHandlers;
    LONG NtWriteHandlers;
    INT Failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_MANAGE_EXEC_WRITES_BEGIN\n");
    Ntdll = GetModuleHandleW(L"ntdll.dll");
    NtSetInformationProcessPtr = (PNT_SET_INFORMATION_PROCESS)(ULONG_PTR)GetProcAddress(Ntdll, "NtSetInformationProcess");
    NtSetInformationThreadPtr = (PNT_SET_INFORMATION_THREAD)(ULONG_PTR)GetProcAddress(Ntdll, "NtSetInformationThread");
    NtSetInformationVirtualMemoryPtr = (PNT_SET_INFORMATION_VIRTUAL_MEMORY)(ULONG_PTR)GetProcAddress(Ntdll, "NtSetInformationVirtualMemory");
    NtWriteVirtualMemoryPtr = (PNT_WRITE_VIRTUAL_MEMORY)(ULONG_PTR)GetProcAddress(Ntdll, "NtWriteVirtualMemory");
    if (!NtSetInformationProcessPtr || !NtSetInformationThreadPtr || !NtSetInformationVirtualMemoryPtr || !NtWriteVirtualMemoryPtr)
    {
        printf("RESOLVE result=1\n");
        return 1;
    }

    Page = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!Page)
        return 2;
    Handler = AddVectoredExceptionHandler(TRUE, write_exception_handler);
    if (!Handler)
        return 3;

    ZeroMemory(&Information, sizeof(Information));
    Information.Version = 2;
    Information.ProcessEnableWriteExceptions = 1;
    EnableStatus = NtSetInformationProcessPtr(GetCurrentProcess(), PROCESS_MANAGE_WRITES_TO_EXECUTABLE_MEMORY, &Information, sizeof(Information));
    Failed |= EnableStatus != 0;
    Failed |= check_query(Page);

    ExpectedWriteAddress = (volatile BYTE *)Page + 17;
    ClearDirectStatus = clear_dirty_state(NtSetInformationVirtualMemoryPtr, Page);
    *ExpectedWriteAddress = 0x5a;
    DirectHandlers = HandlerCount;
    Failed |= ClearDirectStatus != 0 || DirectHandlers != 1 || HandlerError != 0 || *ExpectedWriteAddress != 0x5a;

    ClearNtWriteStatus = clear_dirty_state(NtSetInformationVirtualMemoryPtr, Page);
    Value = 0xa5;
    Written = 0;
    WriteStatus = NtWriteVirtualMemoryPtr(GetCurrentProcess(), (PVOID)ExpectedWriteAddress, &Value, sizeof(Value), &Written);
    NtWriteHandlers = HandlerCount - DirectHandlers;
    Failed |= ClearNtWriteStatus != 0 || WriteStatus != 0 || Written != sizeof(Value) || HandlerError != 0 || *ExpectedWriteAddress != Value;
    Failed |= check_query(Page);

    Information.ProcessEnableWriteExceptions = 0;
    DisableStatus = NtSetInformationProcessPtr(GetCurrentProcess(), PROCESS_MANAGE_WRITES_TO_EXECUTABLE_MEMORY, &Information, sizeof(Information));
    Failed |= DisableStatus != 0;
    printf("MANAGE enable=0x%08lx clear_direct=0x%08lx direct_handlers=%ld clear_ntwrite=0x%08lx ntwrite=0x%08lx written=%Iu ntwrite_handlers=%ld disable=0x%08lx handler_error=%ld value=0x%02x result=%d\n", EnableStatus, ClearDirectStatus, DirectHandlers, ClearNtWriteStatus, WriteStatus, Written, NtWriteHandlers, DisableStatus, HandlerError, *ExpectedWriteAddress, Failed);

    RemoveVectoredExceptionHandler(Handler);
    VirtualFree(Page, 0, MEM_RELEASE);
    printf("CHPE_MANAGE_EXEC_WRITES_%s\n", Failed ? "FAIL" : "PASS");
    return Failed;
}
