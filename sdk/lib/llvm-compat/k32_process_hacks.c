/*
 * llvm-mingw's libunwind can reference K32 process APIs even when ReactOS is
 * built with a pre-Win7 export set. Provide the single wrapper it needs from
 * the compatibility library instead of widening kernel32's versioned exports.
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <psapi.h>

BOOL
WINAPI
K32EnumProcessModules(
    HANDLE Process,
    HMODULE *Module,
    DWORD Count,
    LPDWORD Needed)
{
    return EnumProcessModules(Process, Module, Count, Needed);
}
