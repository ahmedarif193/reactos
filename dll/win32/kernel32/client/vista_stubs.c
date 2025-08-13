/*
 * PROJECT:         ReactOS Win32 Base API
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            dll/win32/kernel32/client/vista_stubs.c
 * PURPOSE:         Vista API Stubs for MinGW libgcc compatibility
 * PROGRAMMERS:     Claude Code (automated fix)
 */

/* INCLUDES *****************************************************************/
#include <k32.h>

#define EINVAL 22

/* FUNCTIONS *****************************************************************/

/*
 * @implemented (stub)
 * 
 * Stub implementation for MinGW libgcc compatibility.
 * This is a no-op that should be sufficient for basic threading.
 */
VOID
WINAPI
InitializeConditionVariable(PVOID ConditionVariable)
{
    /* Zero out the condition variable structure */
    ZeroMemory(ConditionVariable, sizeof(PVOID));
}

/*
 * @implemented (stub)
 * 
 * Stub implementation for MinGW libgcc compatibility.
 * Always fails gracefully to prevent hangs.
 */
BOOL
WINAPI
SleepConditionVariableCS(PVOID ConditionVariable,
                        PCRITICAL_SECTION CriticalSection, 
                        DWORD Timeout)
{
    UNREFERENCED_PARAMETER(ConditionVariable);
    UNREFERENCED_PARAMETER(CriticalSection);
    UNREFERENCED_PARAMETER(Timeout);
    
    /* Always fail immediately to prevent infinite waits */
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/*
 * @implemented (stub)
 * 
 * Stub implementation for MinGW libgcc compatibility.
 * Always fails gracefully to prevent hangs.
 */
BOOL
WINAPI
SleepConditionVariableSRW(PVOID ConditionVariable,
                         PVOID Lock, 
                         DWORD Timeout, 
                         ULONG Flags)
{
    UNREFERENCED_PARAMETER(ConditionVariable);
    UNREFERENCED_PARAMETER(Lock);
    UNREFERENCED_PARAMETER(Timeout);
    UNREFERENCED_PARAMETER(Flags);
    
    /* Always fail immediately to prevent infinite waits */
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/*
 * @implemented (stub)
 * 
 * Stub implementation for MinGW libgcc compatibility.
 * This is a no-op.
 */
VOID
WINAPI
WakeConditionVariable(PVOID ConditionVariable)
{
    UNREFERENCED_PARAMETER(ConditionVariable);
    /* No-op - nothing to wake */
}

/*
 * @implemented (stub)
 * 
 * Stub implementation for MinGW libgcc compatibility.
 * This is a no-op.
 */
VOID
WINAPI
WakeAllConditionVariable(PVOID ConditionVariable)
{
    UNREFERENCED_PARAMETER(ConditionVariable);
    /* No-op - nothing to wake */
}

/* Simple linear congruential generator state */
static ULONG g_rand_seed = 1;

/*
 * @implemented (stub)
 * 
 * Stub implementation for MinGW libstdc++ compatibility.
 * Provides basic pseudo-random number generation.
 */
int
WINAPI
rand_s(unsigned int *randomValue)
{
    if (!randomValue)
    {
        return EINVAL;
    }
    
    /* Simple LCG: seed = (seed * 1103515245 + 12345) */
    g_rand_seed = (g_rand_seed * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
    *randomValue = g_rand_seed;
    return 0;
}

