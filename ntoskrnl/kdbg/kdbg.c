/*
 * PROJECT:     ReactOS KDBG Kernel Debugger
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel Debugger Initialization
 * COPYRIGHT:   Copyright 2020-2021 Hervé Poussineau <hpoussin@reactos.org>
 *              Copyright 2021 Jérôme Gardou <jerome.gardou@reactos.org>
 *              Copyright 2023 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "kdb.h"

/* GLOBALS *******************************************************************/

static ULONG KdbgNextApiNumber = DbgKdContinueApi;
static CONTEXT KdbgContext;
static EXCEPTION_RECORD64 KdbgExceptionRecord;
static BOOLEAN KdbgFirstChanceException;
static NTSTATUS KdbgContinueStatus = STATUS_SUCCESS;
static USHORT KdbgProcessor;
static PVOID volatile KdbgDeferredSymbolBase;
static BOOLEAN KdbgDeferredSymbolLoad;

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
KdD0Transition(VOID)
#undef KdD0Transition
#define pKdD0Transition KdD0Transition
{
    /* Call KdTerm */
    return pKdD0Transition();
}

NTSTATUS
NTAPI
KdD3Transition(VOID)
#undef KdD3Transition
#define pKdD3Transition KdD3Transition
{
    /* Call KdTerm */
    return pKdD3Transition();
}

NTSTATUS
NTAPI
KdSave(
    _In_ BOOLEAN SleepTransition)
#undef KdSave
#define pKdSave KdSave
{
    /* Call KdTerm */
    return pKdSave(SleepTransition);
}

NTSTATUS
NTAPI
KdRestore(
    _In_ BOOLEAN SleepTransition)
#undef KdRestore
#define pKdRestore KdRestore
{
    /* Call KdTerm */
    return pKdRestore(SleepTransition);
}

BOOLEAN
KdbgTakeDeferredSymbolRequest(
    _Out_ PVOID *Base,
    _Out_ PBOOLEAN Load)
{
    if (!KdbgDeferredSymbolBase)
        return FALSE;

    *Base = KdbgDeferredSymbolBase;
    *Load = KdbgDeferredSymbolLoad;
    KdbgDeferredSymbolBase = NULL;
    return TRUE;
}

VOID
KdbgProcessDeferredSymbolRequest(
    _In_ PVOID Base,
    _In_ BOOLEAN Load)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    if (KdbpSymFindModule(Base, -1, &LdrEntry))
        KdbSymProcessSymbols(LdrEntry, Load);
}

VOID
NTAPI
KdSendPacket(
    _In_ ULONG PacketType,
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData,
    _Inout_ PKD_CONTEXT Context)
#undef KdSendPacket
#define pKdSendPacket KdSendPacket
{
    if (PacketType == PACKET_TYPE_KD_DEBUG_IO)
    {
        /* Call KdTerm */
        pKdSendPacket(PacketType, MessageHeader, MessageData, Context);
        return;
    }

    /* Debugger-only packets */
    if (PacketType == PACKET_TYPE_KD_STATE_CHANGE64)
    {
        PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange = (PDBGKD_ANY_WAIT_STATE_CHANGE)MessageHeader->Buffer;
        if (WaitStateChange->NewState == DbgKdLoadSymbolsStateChange)
        {
            /* Defer symbol processing until KD has restored the caller's IRQL. */
            KdbgDeferredSymbolLoad = !WaitStateChange->u.LoadSymbols.UnloadSymbols;
            KdbgDeferredSymbolBase = (PVOID)(ULONG_PTR)WaitStateChange->u.LoadSymbols.BaseOfDll;
            return;
        }
        else if (WaitStateChange->NewState == DbgKdExceptionStateChange)
        {
            KdbgNextApiNumber = DbgKdGetContextApi;
            KdbgProcessor = WaitStateChange->Processor;
            KdbgExceptionRecord = WaitStateChange->u.Exception.ExceptionRecord;
            KdbgFirstChanceException = WaitStateChange->u.Exception.FirstChance;
            return;
        }
    }
    else if (PacketType == PACKET_TYPE_KD_STATE_MANIPULATE)
    {
        PDBGKD_MANIPULATE_STATE64 ManipulateState = (PDBGKD_MANIPULATE_STATE64)MessageHeader->Buffer;
        if (ManipulateState->ApiNumber == DbgKdGetContextApi)
        {
            KD_CONTINUE_TYPE Result;

            Result = KdbEnterDebuggerException(&KdbgExceptionRecord,
#ifdef _M_ARM64
                                               KiGetContextPreviousMode(&KdbgContext),
#else
                                               KdbgContext.SegCs & 1,
#endif
                                               &KdbgContext,
                                               KdbgFirstChanceException);
#if 0
            /* Manually dump the stack for the user */
            KeRosDumpStackFrames(NULL, 0);
            Result = kdHandleException;
#endif
            if (Result != kdHandleException)
            {
                if (KdbgExceptionRecord.ExceptionCode == STATUS_ASSERTION_FAILURE)
                {
                    /* A handled assertion resumes after its breakpoint. */
                    KeSetContextPc(&KdbgContext,
                                   KeGetContextPc(&KdbgContext) + KD_ASSERT_BREAKPOINT_SIZE);
                }
                KdbgContinueStatus = STATUS_SUCCESS;
            }
            else
                KdbgContinueStatus = STATUS_UNSUCCESSFUL;
            KdbgNextApiNumber = DbgKdSetContextApi;
            return;
        }
        else if (ManipulateState->ApiNumber == DbgKdSetContextApi)
        {
            KdbgNextApiNumber = DbgKdContinueApi;
            return;
        }
    }

    KdbPrintf("%s: PacketType %d is UNIMPLEMENTED\n", __FUNCTION__, PacketType);
    return;
}

KDSTATUS
NTAPI
KdReceivePacket(
    _In_ ULONG PacketType,
    _Out_ PSTRING MessageHeader,
    _Out_ PSTRING MessageData,
    _Out_ PULONG DataLength,
    _Inout_ PKD_CONTEXT Context)
#undef KdReceivePacket
#define pKdReceivePacket KdReceivePacket
{
    if (PacketType == PACKET_TYPE_KD_POLL_BREAKIN)
    {
        /*
         * Poll the selected terminal transport.  KDBG owns the debugger
         * state-machine packets, but break-in is transport input just like
         * prompt input; swallowing it made serial BREAKIN_PACKET_BYTE (and
         * the equivalent KDGDB request) incapable of entering KDBG.
         */
        return pKdReceivePacket(PacketType, MessageHeader, MessageData, DataLength, Context);
    }

    if (PacketType == PACKET_TYPE_KD_DEBUG_IO)
    {
        /* Call KdTerm */
        return pKdReceivePacket(PacketType,
                                MessageHeader,
                                MessageData,
                                DataLength,
                                Context);
    }

    /* Debugger-only packets */
    if (PacketType == PACKET_TYPE_KD_STATE_MANIPULATE)
    {
        PDBGKD_MANIPULATE_STATE64 ManipulateState = (PDBGKD_MANIPULATE_STATE64)MessageHeader->Buffer;
        RtlZeroMemory(MessageHeader->Buffer, MessageHeader->MaximumLength);
        ManipulateState->Processor = KdbgProcessor;
        if (KdbgNextApiNumber == DbgKdGetContextApi)
        {
            ManipulateState->ApiNumber = DbgKdGetContextApi;
            MessageData->Length = 0;
            MessageData->Buffer = (PCHAR)&KdbgContext;
            return KdPacketReceived;
        }
        else if (KdbgNextApiNumber == DbgKdSetContextApi)
        {
            ManipulateState->ApiNumber = DbgKdSetContextApi;
            MessageData->Length = sizeof(KdbgContext);
            MessageData->Buffer = (PCHAR)&KdbgContext;
            return KdPacketReceived;
        }
        else if (KdbgNextApiNumber != DbgKdContinueApi)
        {
            KdbPrintf("%s:%d is UNIMPLEMENTED\n", __FUNCTION__, __LINE__);
        }
        ManipulateState->ApiNumber = DbgKdContinueApi;
        ManipulateState->u.Continue.ContinueStatus = KdbgContinueStatus;

        /* Prepare for next time */
        KdbgNextApiNumber = DbgKdContinueApi;
        KdbgContinueStatus = STATUS_SUCCESS;

        return KdPacketReceived;
    }

    KdbPrintf("%s: PacketType %d is UNIMPLEMENTED\n", __FUNCTION__, PacketType);
    return KdPacketTimedOut;
}

/* EOF */
