/*
 * PROJECT:     ReactOS ARM64 HAL
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     IRQL Management for ARM64
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <hal.h>

/* Undefine the macros that conflict with our function definitions */
#undef KeGetCurrentIrql
#undef KeRaiseIrqlToDpcLevel
#undef KeRaiseIrqlToSynchLevel
#undef KeLowerIrql

/* GLOBALS *******************************************************************/

KIRQL HalpCurrentIrql = PASSIVE_LEVEL;

/* FUNCTIONS *****************************************************************/

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return HalpCurrentIrql;
}

KIRQL
FASTCALL
KfRaiseIrql(IN KIRQL NewIrql)
{
    KIRQL OldIrql;
    
    /* Save old IRQL */
    OldIrql = HalpCurrentIrql;
    
    /* Validate IRQL */
    if (NewIrql < OldIrql)
    {
        /* Can't lower IRQL with RaiseIrql */
        KeBugCheck(IRQL_NOT_GREATER_OR_EQUAL);
    }
    
    /* Set new IRQL */
    HalpCurrentIrql = NewIrql;
    
    /* TODO: Update interrupt controller priority mask */
    
    return OldIrql;
}

VOID
FASTCALL
KfLowerIrql(IN KIRQL OldIrql)
{
    /* Validate IRQL */
    if (OldIrql > HalpCurrentIrql)
    {
        /* Can't raise IRQL with LowerIrql */
        KeBugCheck(IRQL_NOT_LESS_OR_EQUAL);
    }
    
    /* Set new IRQL */
    HalpCurrentIrql = OldIrql;
    
    /* TODO: Update interrupt controller priority mask */
}

KIRQL
NTAPI
KeRaiseIrqlToDpcLevel(VOID)
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}

KIRQL
NTAPI
KeRaiseIrqlToSynchLevel(VOID)
{
    return KfRaiseIrql(SYNCH_LEVEL);
}

VOID
NTAPI
KeRaiseIrql(IN KIRQL NewIrql,
            OUT PKIRQL OldIrql)
{
    *OldIrql = KfRaiseIrql(NewIrql);
}

VOID
NTAPI
KeLowerIrql(IN KIRQL NewIrql)
{
    KfLowerIrql(NewIrql);
}

/* EOF */