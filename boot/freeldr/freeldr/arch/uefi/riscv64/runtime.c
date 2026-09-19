/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 UEFI loader runtime support
 */

#include <uefildr.h>
#include <stdarg.h>

#ifdef KeGetCurrentIrql
#undef KeGetCurrentIrql
#endif

extern volatile BOOLEAN BootServicesExitedFlag;

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return PASSIVE_LEVEL;
}

/* RISC-V has no I/O port space: legacy port probes find no device. */
UCHAR NTAPI READ_PORT_UCHAR(PUCHAR Port) { UNREFERENCED_PARAMETER(Port); return 0xFF; }
USHORT NTAPI READ_PORT_USHORT(PUSHORT Port) { UNREFERENCED_PARAMETER(Port); return 0xFFFF; }
ULONG NTAPI READ_PORT_ULONG(PULONG Port) { UNREFERENCED_PARAMETER(Port); return 0xFFFFFFFF; }
VOID NTAPI WRITE_PORT_UCHAR(PUCHAR Port, UCHAR Value) { UNREFERENCED_PARAMETER(Port); UNREFERENCED_PARAMETER(Value); }
VOID NTAPI WRITE_PORT_USHORT(PUSHORT Port, USHORT Value) { UNREFERENCED_PARAMETER(Port); UNREFERENCED_PARAMETER(Value); }
VOID NTAPI WRITE_PORT_ULONG(PULONG Port, ULONG Value) { UNREFERENCED_PARAMETER(Port); UNREFERENCED_PARAMETER(Value); }

DECLSPEC_NORETURN
VOID
FrLdrBugCheckWithMessage(
    ULONG BugCode,
    PCHAR File,
    ULONG Line,
    PCSTR Format,
    ...)
{
    CHAR Detail[320];
    CHAR Message[512];
    va_list Arguments;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Detail, sizeof(Detail), Format, Arguments);
    va_end(Arguments);

    RtlStringCbPrintfA(Message,
                       sizeof(Message),
                       "FreeLdr bug check %lu at %s:%lu\n%s",
                       BugCode,
                       File ? File : "<unknown>",
                       Line,
                       Detail);

    if (!BootServicesExitedFlag)
        UiMessageBoxCritical(Message);

    __asm__ __volatile__("csrci sstatus, 2" ::: "memory");
    for (;;)
        __asm__ __volatile__("wfi");
}
