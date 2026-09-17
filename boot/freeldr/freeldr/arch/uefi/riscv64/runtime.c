/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
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
