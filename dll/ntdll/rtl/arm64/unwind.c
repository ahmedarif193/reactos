/*
 * Minimal ARM64 dynamic function table stubs for user-mode ntdll
 */

#include <windef.h>
#include <winnt.h>

BOOLEAN
NTAPI
RtlAddFunctionTable(PRUNTIME_FUNCTION FunctionTable,
                    DWORD EntryCount,
                    DWORD64 BaseAddress)
{
    UNREFERENCED_PARAMETER(FunctionTable);
    UNREFERENCED_PARAMETER(EntryCount);
    UNREFERENCED_PARAMETER(BaseAddress);
    return FALSE;
}

/* Unwind helpers are provided by shared librtl on arm64. */
