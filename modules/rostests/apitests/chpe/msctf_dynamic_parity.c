/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable AMD64-on-ARM64 dynamic MSCTF routing parity probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

typedef BOOLEAN (WINAPI *PRTL_IS_EC_CODE)(ULONG_PTR Address);
typedef BOOL (WINAPI *PTF_DLL_DETACH_IN_OTHER)(VOID);
typedef VOID (WINAPI *PTF_INVALID_ASSEMBLY_LIST_CACHE_IF_EXIST)(VOID);

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
    PRTL_IS_EC_CODE RtlIsEcCode;
    PTF_DLL_DETACH_IN_OTHER TfDllDetachInOther;
    PTF_INVALID_ASSEMBLY_LIST_CACHE_IF_EXIST TfInvalidAssemblyListCacheIfExist;
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS64 NtHeader;
    CHAR ModulePath[MAX_PATH];
    HMODULE Module, NtDll;
    ULONG Iteration;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_MSCTF_DYNAMIC_BEGIN\n");

    NtDll = GetModuleHandleW(L"ntdll.dll");
    RtlIsEcCode = NtDll ? (PRTL_IS_EC_CODE)GetProcAddress(NtDll, "RtlIsEcCode") : NULL;

    for (Iteration = 0; Iteration < 8; ++Iteration)
    {
        Module = LoadLibraryW(L"msctf.dll");
        if (!Module)
        {
            printf("CHPE_MSCTF_DYNAMIC_FAIL load iteration=%lu error=%lu\n", Iteration, GetLastError());
            return 1;
        }

        DosHeader = (PIMAGE_DOS_HEADER)Module;
        NtHeader = DosHeader->e_magic == IMAGE_DOS_SIGNATURE ? (PIMAGE_NT_HEADERS64)((PBYTE)Module + DosHeader->e_lfanew) : NULL;
        TfDllDetachInOther = (PTF_DLL_DETACH_IN_OTHER)GetProcAddress(Module, "TF_DllDetachInOther");
        TfInvalidAssemblyListCacheIfExist = (PTF_INVALID_ASSEMBLY_LIST_CACHE_IF_EXIST)GetProcAddress(Module, "TF_InvalidAssemblyListCacheIfExist");
        if (!TfInvalidAssemblyListCacheIfExist)
        {
            printf("CHPE_MSCTF_DYNAMIC_FAIL export iteration=%lu module=%p error=%lu\n", Iteration, Module, GetLastError());
            FreeLibrary(Module);
            return 2;
        }

        if (Iteration == 0)
        {
            ModulePath[0] = ANSI_NULL;
            GetModuleFileNameA(Module, ModulePath, sizeof(ModulePath));
            printf("MODULE msctf=%p machine=0x%04x invalid=%p invalid_ec=%u detach=%p detach_ec=%u path=%s\n", Module, NtHeader && NtHeader->Signature == IMAGE_NT_SIGNATURE ? NtHeader->FileHeader.Machine : 0, TfInvalidAssemblyListCacheIfExist, RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)TfInvalidAssemblyListCacheIfExist) : 0, TfDllDetachInOther, TfDllDetachInOther && RtlIsEcCode ? RtlIsEcCode((ULONG_PTR)TfDllDetachInOther) : 0, ModulePath);
        }

        TfInvalidAssemblyListCacheIfExist();
        if (TfDllDetachInOther && !TfDllDetachInOther())
        {
            printf("CHPE_MSCTF_DYNAMIC_FAIL call iteration=%lu\n", Iteration);
            FreeLibrary(Module);
            return 3;
        }

        if (!FreeLibrary(Module))
        {
            printf("CHPE_MSCTF_DYNAMIC_FAIL unload iteration=%lu error=%lu\n", Iteration, GetLastError());
            return 4;
        }
    }

    printf("CHPE_MSCTF_DYNAMIC_PASS iterations=%lu\n", Iteration);
    return 0;
}
