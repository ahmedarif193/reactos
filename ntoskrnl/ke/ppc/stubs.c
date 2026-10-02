/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC unavailable services
 */

#include <ntoskrnl.h>

/* Retain the first unavailable operation for an external debugger, including
 * failures before PCR/console initialization and recursive bugcheck failure. */
const CHAR * volatile KiPpcUnimplementedRoutine;

DECLSPEC_NORETURN
VOID
NTAPI
KiPpcUnimplemented(_In_ const CHAR *Routine)
{
    _disable();
    if (KiPpcUnimplementedRoutine == NULL)
    {
        KiPpcUnimplementedRoutine = Routine;
        if (KiPpcConsoleReady())
        {
            static const CHAR Message[] = "\r\n*** PPC: unimplemented ";

            KiPpcConsoleWrite(Message, sizeof(Message) - 1);
            KiPpcConsoleWrite(Routine, strlen(Routine));
            KiPpcConsoleWrite("\r\n", 2);
        }
        if (KeNumberProcessors && KeGetPcr() && (KeGetCurrentPrcb()->Number < (ULONG)(UCHAR)KeNumberProcessors) && (KeGetCurrentPrcb() == KiProcessorBlock[KeGetCurrentPrcb()->Number]))
            KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_NOT_IMPLEMENTED, (ULONG_PTR)Routine, 0, 0);
    }

    for (;;)
        YieldProcessor();
}

/* Failure-returning services do not inspect input or modify output storage. */
NTSTATUS NTAPI NtVdmControl(ULONG ControlCode, PVOID ControlData) { return STATUS_NOT_SUPPORTED; }

NTSTATUS
NTAPI
NtSetLdtEntries(
    _In_ ULONG Selector1,
    _In_ LDT_ENTRY LdtEntry1,
    _In_ ULONG Selector2,
    _In_ LDT_ENTRY LdtEntry2)
{
    UNREFERENCED_PARAMETER(Selector1);
    UNREFERENCED_PARAMETER(LdtEntry1);
    UNREFERENCED_PARAMETER(Selector2);
    UNREFERENCED_PARAMETER(LdtEntry2);
    return STATUS_NOT_IMPLEMENTED;
}

/* 32-bit NT exports the SList primitives under their Exp names; the RTL
 * implements them (sdk/lib/rtl/slist.c) under their Rtl names. */
PSLIST_ENTRY NTAPI RtlInterlockedPushEntrySList(_Inout_ PSLIST_HEADER SListHead, _Inout_ __drv_aliasesMem PSLIST_ENTRY SListEntry);
PSLIST_ENTRY NTAPI RtlInterlockedPopEntrySList(_Inout_ PSLIST_HEADER SListHead);
PSLIST_ENTRY NTAPI RtlInterlockedFlushSList(_Inout_ PSLIST_HEADER SListHead);

PSLIST_ENTRY
FASTCALL
ExpInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY SListEntry)
{
    return RtlInterlockedPushEntrySList(SListHead, SListEntry);
}

PSLIST_ENTRY
FASTCALL
ExpInterlockedPopEntrySList(
    _Inout_ PSLIST_HEADER SListHead)
{
    return RtlInterlockedPopEntrySList(SListHead);
}

PSLIST_ENTRY
FASTCALL
ExpInterlockedFlushSList(
    _Inout_ PSLIST_HEADER SListHead)
{
    return RtlInterlockedFlushSList(SListHead);
}
