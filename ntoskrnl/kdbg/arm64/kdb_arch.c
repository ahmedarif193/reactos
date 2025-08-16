/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64-specific KDB Functions
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

ULONG
NTAPI
KdbpGetInstLength(IN ULONG_PTR Address)
{
    /* ARM64 instructions are fixed at 4 bytes */
    UNREFERENCED_PARAMETER(Address);
    return 4;
}

BOOLEAN
NTAPI
KdbpDisassemble(IN ULONG_PTR Address,
                IN ULONG InstructionCount)
{
    ULONG i;
    PULONG Instruction;
    
    /* Simple ARM64 disassembly stub */
    for (i = 0; i < InstructionCount; i++)
    {
        Instruction = (PULONG)(Address + (i * 4));
        DbgPrint("%p: %08lx    ; ARM64 instruction\n", 
                 (PVOID)(Address + (i * 4)), *Instruction);
    }
    
    return TRUE;
}

VOID
NTAPI
KdbpStackSwitchAndCall(IN PVOID NewStack,
                       IN PVOID Function,
                       IN PVOID Argument)
{
    /* ARM64 stack switch stub */
    DPRINT1("ARM64: KdbpStackSwitchAndCall not yet implemented\n");
    
    /* For now, just call the function directly */
    ((VOID (*)(PVOID))Function)(Argument);
}

/* MmGetPageProtect is implemented in mm/arm64/page.c */

KIRQL
NTAPI
KeAcquireSpinLockRaiseToDpc(IN PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;
    
    /* Raise IRQL to DISPATCH_LEVEL */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    
    /* Acquire the spinlock */
    KeAcquireSpinLockAtDpcLevel(SpinLock);
    
    return OldIrql;
}

VOID
NTAPI
KeReleaseSpinLock(IN PKSPIN_LOCK SpinLock,
                  IN KIRQL OldIrql)
{
    /* Release the spinlock */
    KeReleaseSpinLockFromDpcLevel(SpinLock);
    
    /* Lower IRQL back */
    KeLowerIrql(OldIrql);
}

/* EOF */