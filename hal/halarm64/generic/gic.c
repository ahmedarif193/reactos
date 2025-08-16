/*
 * PROJECT:     ReactOS ARM64 HAL
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     GIC (Generic Interrupt Controller) Support for ARM64
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <hal.h>

/* Undefine the HAL macros that conflict with our function definitions */
#undef HalEnableSystemInterrupt
#undef HalDisableSystemInterrupt
#undef HalBeginSystemInterrupt
#undef HalEndSystemInterrupt
#undef KfRaiseIrql
#undef KfLowerIrql
#undef KeGetCurrentIrql

/* Forward declarations for HAL-internal functions */
KIRQL FASTCALL KfRaiseIrql(IN KIRQL NewIrql);
VOID FASTCALL KfLowerIrql(IN KIRQL OldIrql);
KIRQL NTAPI KeGetCurrentIrql(VOID);

/* GLOBALS *******************************************************************/

PGIC_DISTRIBUTOR HalpGicDistributor = NULL;
PGIC_CPU_INTERFACE HalpGicCpuInterface = NULL;

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
HalpInitializeGIC(VOID)
{
    ULONG i;
    
    /* Map GIC memory regions */
    HalpGicDistributor = (PGIC_DISTRIBUTOR)MmMapIoSpace(
        (PHYSICAL_ADDRESS){.QuadPart = HALP_GIC_DISTRIBUTOR_BASE},
        sizeof(GIC_DISTRIBUTOR),
        MmNonCached);
        
    HalpGicCpuInterface = (PGIC_CPU_INTERFACE)MmMapIoSpace(
        (PHYSICAL_ADDRESS){.QuadPart = HALP_GIC_CPU_INTERFACE_BASE},
        sizeof(GIC_CPU_INTERFACE),
        MmNonCached);
    
    /* Disable distributor */
    HalpGicDistributor->Control = 0;
    
    /* Configure all interrupts as non-secure group 1 */
    for (i = 0; i < 32; i++)
    {
        HalpGicDistributor->ClearEnable[i] = 0xFFFFFFFF;
        HalpGicDistributor->ClearPending[i] = 0xFFFFFFFF;
    }
    
    /* Set all interrupt priorities to default */
    for (i = 0; i < 255; i++)
    {
        HalpGicDistributor->Priority[i] = 0xA0;
    }
    
    /* Enable distributor */
    HalpGicDistributor->Control = 1;
    
    /* Configure CPU interface */
    HalpGicCpuInterface->PriorityMask = 0xFF;
    HalpGicCpuInterface->BinaryPoint = 0;
    
    /* Enable CPU interface */
    HalpGicCpuInterface->Control = 1;
}

BOOLEAN
NTAPI
HalEnableSystemInterrupt(
    IN ULONG Vector,
    IN KIRQL Irql,
    IN KINTERRUPT_MODE InterruptMode)
{
    ULONG IntId = Vector;
    ULONG RegIndex = IntId / 32;
    ULONG BitMask = 1 << (IntId % 32);
    
    /* Set interrupt priority based on IRQL */
    HalpGicDistributor->Priority[IntId] = (15 - Irql) << 4;
    
    /* Configure interrupt as edge or level triggered */
    if (InterruptMode == Latched)
    {
        /* Edge triggered */
        HalpGicDistributor->Config[IntId / 16] |= (2 << ((IntId % 16) * 2));
    }
    else
    {
        /* Level triggered */
        HalpGicDistributor->Config[IntId / 16] &= ~(2 << ((IntId % 16) * 2));
    }
    
    /* Enable the interrupt */
    HalpGicDistributor->SetEnable[RegIndex] = BitMask;
    
    return TRUE;
}

VOID
NTAPI
HalDisableSystemInterrupt(
    IN ULONG Vector,
    IN KIRQL Irql)
{
    ULONG IntId = Vector;
    ULONG RegIndex = IntId / 32;
    ULONG BitMask = 1 << (IntId % 32);
    
    /* Disable the interrupt */
    HalpGicDistributor->ClearEnable[RegIndex] = BitMask;
}

BOOLEAN
NTAPI
HalBeginSystemInterrupt(
    IN KIRQL Irql,
    IN ULONG Vector,
    OUT PKIRQL OldIrql)
{
    ULONG IntId;
    
    /* Read interrupt acknowledge register */
    IntId = HalpGicCpuInterface->IntAck;
    
    /* Check if this is a valid interrupt */
    if ((IntId & 0x3FF) == 0x3FF)
    {
        /* Spurious interrupt */
        return FALSE;
    }
    
    /* Save old IRQL and raise to new IRQL */
    *OldIrql = KeGetCurrentIrql();
    KfRaiseIrql(Irql);
    
    return TRUE;
}

VOID
NTAPI
HalEndSystemInterrupt(
    IN KIRQL OldIrql,
    IN PKTRAP_FRAME TrapFrame)
{
    /* Write End of Interrupt register */
    /* TODO: Get vector from trap frame or current context */
    /* For now, using a placeholder */
    ULONG Vector = 0; /* TODO: Retrieve from context */
    HalpGicCpuInterface->EndOfInt = Vector;
    
    /* Lower IRQL */
    KfLowerIrql(OldIrql);
}

/* EOF */