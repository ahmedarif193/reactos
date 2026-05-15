/*
 * PROJECT:         ReactOS NT Library
 * FILE:            dll/ntdll/rtl/exit.c
 * PURPOSE:         RtlExitUserProcess implementation
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

/*
 * @implemented
 *
 * Performs orderly process shutdown: detaches all loaded DLLs via
 * LdrShutdownProcess, then terminates the process via NtTerminateProcess.
 */
VOID
NTAPI
RtlExitUserProcess(NTSTATUS Status)
{
    /* Call the Loader to send DLL_PROCESS_DETACH notifications */
    LdrShutdownProcess();

    /* Terminate the process. NtTerminateProcess does not return. */
    NtTerminateProcess(NtCurrentProcess(), Status);

    /* Not reached */
    for (;;)
        ;
}
