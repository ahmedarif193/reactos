/*
 * PROJECT:     ReactOS Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Unavailable RISC-V native KD transport
 */

#define NOEXTAPI
#include <ntifs.h>
#include <arc/arc.h>
#include <windbgkd.h>
#include <kddll.h>

DECLSPEC_NORETURN VOID NTAPI KiRiscvUnimplemented(const CHAR *Routine);

NTSTATUS NTAPI KdDebuggerInitialize0(PLOADER_PARAMETER_BLOCK LoaderBlock) { return STATUS_NOT_IMPLEMENTED; }
NTSTATUS NTAPI KdDebuggerInitialize1(PLOADER_PARAMETER_BLOCK LoaderBlock) { return STATUS_NOT_IMPLEMENTED; }
NTSTATUS NTAPI KdSave(BOOLEAN SleepTransition) { return STATUS_DEBUGGER_INACTIVE; }
NTSTATUS NTAPI KdRestore(BOOLEAN SleepTransition) { return STATUS_DEBUGGER_INACTIVE; }
NTSTATUS NTAPI KdD0Transition(VOID) { return STATUS_DEBUGGER_INACTIVE; }
NTSTATUS NTAPI KdD3Transition(VOID) { return STATUS_DEBUGGER_INACTIVE; }

KDSTATUS
NTAPI
KdReceivePacket(ULONG PacketType, PSTRING MessageHeader, PSTRING MessageData, PULONG DataLength, PKD_CONTEXT Context)
{
    /* No packet received, no output buffers or protocol state modified. */
    return KdPacketTimedOut;
}

VOID
NTAPI
KdSendPacket(ULONG PacketType, PSTRING MessageHeader, PSTRING MessageData, PKD_CONTEXT Context)
{
    /* Native KD is disabled. An unexpected send must not pretend to have
     * transmitted a packet or update the retry/sequence state. */
    KiRiscvUnimplemented("KdSendPacket");
}
