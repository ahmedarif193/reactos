//
// DllMainCRTStartup.cpp
//
//      Copyright (c) 2024 ReactOS Team  
//
// DLL entry point CRT startup
//
// SPDX-License-Identifier: MIT
//

#include "commonCRTStartup.hpp"

// Forward declare with weak linkage to provide fallback
extern "C" __attribute__((weak)) BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved);

// Fallback DllMain implementation
BOOL WINAPI DefaultDllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    return TRUE;
}

extern "C" BOOL WINAPI DllMainCRTStartup(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    // Initialize security cookie
    __security_init_cookie();

    // For DLL_PROCESS_ATTACH, initialize the CRT
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        if (!__scrt_initialize())
            return FALSE;
    }

    // Call the DLL's main function, or use fallback if not provided
    BOOL result = DllMain ? DllMain(hinstDLL, fdwReason, lpvReserved) : DefaultDllMain(hinstDLL, fdwReason, lpvReserved);

    // For DLL_PROCESS_DETACH, uninitialize the CRT
    if (fdwReason == DLL_PROCESS_DETACH)
    {
        __vcrt_uninitialize(false);
    }

    return result;
}