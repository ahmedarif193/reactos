/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable AMD64-on-ARM64 loader and call-target parity probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

typedef BOOLEAN (WINAPI *PRTL_IS_EC_CODE)(ULONG_PTR Address);
typedef BOOL (WINAPI *PIS_WOW64_PROCESS2)(HANDLE Process, USHORT *ProcessMachine, USHORT *NativeMachine);
typedef DWORD (WINAPI *PGET_TICK_COUNT)(VOID);

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

static PVOID
find_raw_export(HMODULE Module, PCSTR Name)
{
    PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)Module;
    PIMAGE_NT_HEADERS64 NtHeader;
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PDWORD Names, Functions;
    PWORD Ordinals;
    DWORD ExportRva, ExportSize, Index, FunctionRva;

    if (!Module || DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;

    NtHeader = (PIMAGE_NT_HEADERS64)((PBYTE)Module + DosHeader->e_lfanew);
    if (NtHeader->Signature != IMAGE_NT_SIGNATURE ||
        NtHeader->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        return NULL;
    }

    ExportRva = NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    ExportSize = NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
    if (!ExportRva || ExportRva >= NtHeader->OptionalHeader.SizeOfImage ||
        ExportSize > NtHeader->OptionalHeader.SizeOfImage - ExportRva)
    {
        return NULL;
    }

    ExportDirectory = (PIMAGE_EXPORT_DIRECTORY)((PBYTE)Module + ExportRva);
    Names = (PDWORD)((PBYTE)Module + ExportDirectory->AddressOfNames);
    Ordinals = (PWORD)((PBYTE)Module + ExportDirectory->AddressOfNameOrdinals);
    Functions = (PDWORD)((PBYTE)Module + ExportDirectory->AddressOfFunctions);

    for (Index = 0; Index < ExportDirectory->NumberOfNames; ++Index)
    {
        if (!lstrcmpA((PCSTR)Module + Names[Index], Name))
        {
            if (Ordinals[Index] >= ExportDirectory->NumberOfFunctions)
                return NULL;

            FunctionRva = Functions[Ordinals[Index]];
            if (!FunctionRva || FunctionRva >= NtHeader->OptionalHeader.SizeOfImage)
                return NULL;

            /* This probe deliberately selects a non-forwarded export. */
            if (FunctionRva >= ExportRva && FunctionRva < ExportRva + ExportSize)
                return NULL;

            return (PBYTE)Module + FunctionRva;
        }
    }

    return NULL;
}

int
main(void)
{
    HMODULE Kernel32, NtDll;
    PIS_WOW64_PROCESS2 IsWow64Process2;
    PRTL_IS_EC_CODE RtlIsEcCode;
    PGET_TICK_COUNT GetTickCountDynamic, GetTickCountRaw;
    MEMORY_BASIC_INFORMATION MemoryInfo;
    USHORT ProcessMachine = 0, NativeMachine = 0;
    DWORD DirectTick, DynamicTick, RawTick;
    SIZE_T Result;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_LOADER_PARITY_BEGIN\n");

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    NtDll = GetModuleHandleW(L"ntdll.dll");
    if (!Kernel32 || !NtDll)
    {
        printf("FAIL modules kernel32=%p ntdll=%p error=%lu\n", Kernel32, NtDll, GetLastError());
        return 1;
    }

    IsWow64Process2 = (PIS_WOW64_PROCESS2)GetProcAddress(Kernel32, "IsWow64Process2");
    if (IsWow64Process2 && IsWow64Process2(GetCurrentProcess(), &ProcessMachine, &NativeMachine))
    {
        printf("MACHINES process=0x%04x native=0x%04x\n", ProcessMachine, NativeMachine);
    }
    else
    {
        printf("MACHINES unavailable error=%lu\n", GetLastError());
    }

    GetTickCountDynamic = (PGET_TICK_COUNT)GetProcAddress(Kernel32, "GetTickCount");
    GetTickCountRaw = (PGET_TICK_COUNT)find_raw_export(Kernel32, "GetTickCount");
    printf("TARGET kernel32=%p raw=%p raw_rva=0x%llx gpa=%p gpa_rva=0x%llx same=%u\n",
           Kernel32,
           GetTickCountRaw,
           (unsigned long long)((PBYTE)GetTickCountRaw - (PBYTE)Kernel32),
           GetTickCountDynamic,
           (unsigned long long)((PBYTE)GetTickCountDynamic - (PBYTE)Kernel32),
           GetTickCountRaw == GetTickCountDynamic);

    if (!GetTickCountDynamic || !GetTickCountRaw)
    {
        printf("FAIL unresolved GetTickCount error=%lu\n", GetLastError());
        return 2;
    }

    RtlIsEcCode = (PRTL_IS_EC_CODE)GetProcAddress(NtDll, "RtlIsEcCode");
    if (RtlIsEcCode)
    {
        printf("EC raw=%u gpa=%u direct=%u\n",
               RtlIsEcCode((ULONG_PTR)GetTickCountRaw),
               RtlIsEcCode((ULONG_PTR)GetTickCountDynamic),
               RtlIsEcCode((ULONG_PTR)GetTickCount));
    }
    else
    {
        printf("EC unavailable error=%lu\n", GetLastError());
    }

    Result = VirtualQuery(GetTickCountDynamic, &MemoryInfo, sizeof(MemoryInfo));
    printf("VQUERY result=%llu allocation=%p protect=0x%lx type=0x%lx\n",
           (unsigned long long)Result,
           Result ? MemoryInfo.AllocationBase : NULL,
           Result ? MemoryInfo.Protect : 0,
           Result ? MemoryInfo.Type : 0);

    DirectTick = GetTickCount();
    DynamicTick = GetTickCountDynamic();
    printf("CALL direct=%lu dynamic=%lu delta=%lu\n", DirectTick, DynamicTick, DynamicTick - DirectTick);

    RawTick = GetTickCountRaw();
    printf("CALL raw=%lu delta=%lu\n", RawTick, RawTick - DynamicTick);
    printf("CHPE_LOADER_PARITY_PASS\n");
    return 0;
}
