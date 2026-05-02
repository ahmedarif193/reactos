/*
 * PROJECT:     ReactOS System Libraries
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     ARM64 fiber stubs
 */

#include <k32.h>

VOID
NTAPI
BaseFiberStartup(VOID)
{
    RtlRaiseStatus(STATUS_NOT_IMPLEMENTED);
}

VOID
WINAPI
SwitchToFiber(_In_ LPVOID Fiber)
{
    UNREFERENCED_PARAMETER(Fiber);
    RtlRaiseStatus(STATUS_NOT_IMPLEMENTED);
}
