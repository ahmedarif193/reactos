/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS NT Library
 * FILE:            dll/ntdll/dispatch/dispatch.c
 * PURPOSE:         User-Mode NT Dispatchers
 * PROGRAMERS:      Alex Ionescu (alex@relsoft.net)
 *                  David Welch <welch@cwcom.net>
 */

/* INCLUDES *****************************************************************/

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

typedef NTSTATUS (NTAPI *USER_CALL)(PVOID Argument, ULONG ArgumentLength);
typedef VOID (NTAPI *WOW64_PREPARE_FOR_EXCEPTION)(PEXCEPTION_RECORD, PCONTEXT);

#if defined(_WIN64) && !defined(_M_AMD64)
PVOID LdrpWow64PrepareForException = NULL;
#endif

/* FUNCTIONS ****************************************************************/

/*
 * @implemented
 */
VOID
NTAPI
#if defined(_M_ARM64) || defined(_M_RISCV64)
KiUserExceptionDispatcherWorker(PEXCEPTION_RECORD ExceptionRecord,
                                PCONTEXT Context)
#else
KiUserExceptionDispatcher(PEXCEPTION_RECORD ExceptionRecord,
                          PCONTEXT Context)
#endif
{
    EXCEPTION_RECORD NestedExceptionRecord;
    NTSTATUS Status;

    /* Dispatch the exception and check the result */
#if defined(_WIN64)
    if (LdrpWow64PrepareForException != NULL)
    {
        ((WOW64_PREPARE_FOR_EXCEPTION)LdrpWow64PrepareForException)(ExceptionRecord, Context);
    }
#endif

#if defined(_M_ARM64)
    if (ChpeIsChpeProcess() && ChpeDispatchException(ExceptionRecord, Context))
    {
        Status = NtContinue(Context, FALSE);
    }
    else
#endif
    if (RtlDispatchException(ExceptionRecord, Context))
    {
        /* Continue executing */
        Status = NtContinue(Context, FALSE);
    }
    else
    {
        /* Raise an exception */
        Status = NtRaiseException(ExceptionRecord, Context, FALSE);
    }

    /* Setup the Exception record */
    NestedExceptionRecord.ExceptionCode = Status;
    NestedExceptionRecord.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    NestedExceptionRecord.ExceptionRecord = ExceptionRecord;
    NestedExceptionRecord.NumberParameters = Status;

    /* Raise the exception */
    RtlRaiseException(&NestedExceptionRecord);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KiRaiseUserExceptionDispatcher(VOID)
{
    EXCEPTION_RECORD ExceptionRecord;
    NTSTATUS ExceptionCode = ((PTEB)NtCurrentTeb())->ExceptionCode;

    /* Setup the exception record */
    ExceptionRecord.ExceptionCode = ExceptionCode;
    ExceptionRecord.ExceptionFlags = 0;
    ExceptionRecord.ExceptionRecord = NULL;
    ExceptionRecord.NumberParameters = 0;

    /* Raise the exception */
    RtlRaiseException(&ExceptionRecord);
    return ExceptionCode;
}

/*
 * @implemented
 */
VOID
NTAPI
KiUserCallbackDispatcher(ULONG Index,
                         PVOID Argument,
                         ULONG ArgumentLength)
{
    /* Return with the result of the callback function */
    USER_CALL *KernelCallbackTable = NtCurrentPeb()->KernelCallbackTable;
    ZwCallbackReturn(NULL,
                     0,
                     KernelCallbackTable[Index](Argument, ArgumentLength));
}
