/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Minimal AArch64 disassembler stubs for KDBG
 * LICENSE:         GPL-2.0-or-later
 */

#include <ntoskrnl.h>
#include "kdbg/kdb.h"

#define NDEBUG
#include <debug.h>

/*
 * AArch64 instructions are fixed 32-bit wide. For now we provide a
 * minimal printer that shows the raw encoding. This keeps the 'u'
 * disassemble command functional during bring-up.
 */

LONG
KdbpGetInstLength(IN ULONG_PTR Address)
{
    UNREFERENCED_PARAMETER(Address);
    return 4; /* 4 bytes per instruction on AArch64 */
}

LONG
KdbpDisassemble(IN ULONG_PTR Address, IN ULONG IntelSyntax)
{
    ULONG Inst;
    NTSTATUS Status;
    UNREFERENCED_PARAMETER(IntelSyntax);

    Status = KdbpSafeReadMemory(&Inst, (PVOID)Address, sizeof(Inst));
    if (!NT_SUCCESS(Status))
    {
        KdbpPrint("<unreadable>\n");
        return -1;
    }

    /* Print as: 32-bit hex word */
    KdbpPrint("%p:  %08x", (PVOID)Address, Inst);
    return 4;
}
