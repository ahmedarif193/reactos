//
// commonCRTStartup.c
//
//      Copyright (c) 2024 Timo Kreuzer
//
// Implementation of common executable startup code.
//
// SPDX-License-Identifier: MIT
//

#include <corecrt_startup.h>
#include <internal_shared.h>
#include <excpt.h>
#include <stdlib.h>
#include <intrin.h>
#include <pseh/pseh2.h>
#include <windows.h>

// Defined in winnt.h
#define FAST_FAIL_FATAL_APP_EXIT 7

extern "C" int __cdecl main(int, char**, char**);
extern "C" int __cdecl wmain(int, wchar_t**, wchar_t**);
extern "C" int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int);
extern "C" int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int);

// Support for TCHAR main functions (used by some GUI applications)
extern "C" int __cdecl _tmain(int, TCHAR**, TCHAR**);

template<typename Tmain>
static int call_main();

template<>
int call_main<decltype(main)>()
{
    _configure_narrow_argv(_crt_argv_unexpanded_arguments);

    return main(*__p___argc(), *__p___argv(), _get_initial_narrow_environment());
}

template<>
int call_main<decltype(wmain)>()
{
    _configure_wide_argv(_crt_argv_unexpanded_arguments);

    return wmain(*__p___argc(), *__p___wargv(), _get_initial_wide_environment());
}

template<>
int call_main<decltype(WinMain)>()
{
    _configure_narrow_argv(_crt_argv_unexpanded_arguments);

    LPSTR cmdline = GetCommandLineA();
    
    // Skip program name to get command line arguments
    if (cmdline && *cmdline) {
        if (*cmdline == '"') {
            cmdline++;
            while (*cmdline && *cmdline != '"') cmdline++;
            if (*cmdline == '"') cmdline++;
        } else {
            while (*cmdline && *cmdline != ' ') cmdline++;
        }
        while (*cmdline == ' ') cmdline++;
    }

    return WinMain(GetModuleHandleA(NULL), NULL, const_cast<LPSTR>(cmdline ? cmdline : ""), SW_SHOWDEFAULT);
}

template<>
int call_main<decltype(wWinMain)>()
{
    _configure_wide_argv(_crt_argv_unexpanded_arguments);

    LPWSTR cmdline = GetCommandLineW();
    
    // Skip program name to get command line arguments
    if (cmdline && *cmdline) {
        if (*cmdline == L'"') {
            cmdline++;
            while (*cmdline && *cmdline != L'"') cmdline++;
            if (*cmdline == L'"') cmdline++;
        } else {
            while (*cmdline && *cmdline != L' ') cmdline++;
        }
        while (*cmdline == L' ') cmdline++;
    }

    return wWinMain(GetModuleHandleW(NULL), NULL, const_cast<LPWSTR>(cmdline ? cmdline : L""), SW_SHOWDEFAULT);
}


static bool __scrt_initialize()
{
    __isa_available_init();

    if (!__vcrt_initialize())
    {
        return false;
    }

    if (!__acrt_initialize())
    {
        __vcrt_uninitialize(false);
        return false;
    }

    if (_initterm_e(__xi_a, __xi_z) != 0)
    {
        return false;
    }

    _initterm(__xc_a, __xc_z);

    return true;
}

template<typename Tmain>
static __declspec(noinline) int __cdecl __commonCRTStartup()
{
    int exitCode;

    if (!__scrt_initialize())
    {
        __fastfail(FAST_FAIL_FATAL_APP_EXIT);
    }

    __try
    {
        exitCode = call_main<Tmain>();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        exitCode = GetExceptionCode();
    }
    __endtry

    exit(exitCode);
}
