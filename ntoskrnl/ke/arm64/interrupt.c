/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Interrupt Handling
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* Interrupt table for ARM64 - supports up to 1020 interrupts (GICv3) */
PVOID KiInterruptTable[1020];

/* GIC (Generic Interrupt Controller) addresses */
PVOID KiGicDistributorBase = NULL;
PVOID KiGicRedistributorBase = NULL;
PVOID KiGicCpuInterfaceBase = NULL;

/* FUNCTIONS *****************************************************************/

VOID
FASTCALL
KfLowerIrql(IN KIRQL NewIrql)
{
    PKPCR Pcr;
    KIRQL OldIrql;
    
    /* Get PCR from TPIDR_EL1 */
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r" (Pcr));
    OldIrql = Pcr->CurrentIrql;
    
    /* Validate IRQL lowering */
    ASSERT(NewIrql <= OldIrql);
    
    /* Set new IRQL */
    Pcr->CurrentIrql = NewIrql;
    
    /* Enable interrupts if going below DISPATCH_LEVEL */
    if (NewIrql < DISPATCH_LEVEL && OldIrql >= DISPATCH_LEVEL)
    {
        _enable();
    }
}

KIRQL
FASTCALL
KfRaiseIrql(IN KIRQL NewIrql)
{
    PKPCR Pcr;
    KIRQL OldIrql;
    
    /* Get PCR from TPIDR_EL1 */
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r" (Pcr));
    OldIrql = Pcr->CurrentIrql;
    
    /* Validate IRQL raising */
    ASSERT(NewIrql >= OldIrql);
    
    /* Disable interrupts if going to DISPATCH_LEVEL or higher */
    if (NewIrql >= DISPATCH_LEVEL && OldIrql < DISPATCH_LEVEL)
    {
        _disable();
    }
    
    /* Set new IRQL */
    Pcr->CurrentIrql = NewIrql;
    
    return OldIrql;
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

BOOLEAN
NTAPI
KeDisableInterrupts(VOID)
{
    ULONG Daif;
    BOOLEAN WasEnabled;
    
    /* Get current interrupt state */
    __asm__ __volatile__("mrs %0, daif" : "=r" (Daif));
    WasEnabled = !(Daif & 0x80);  /* Check I bit (IRQ) */
    
    /* Disable interrupts */
    _disable();
    
    return WasEnabled;
}

VOID
NTAPI
KeRestoreInterrupts(IN BOOLEAN Enable)
{
    if (Enable)
    {
        _enable();
    }
    else
    {
        _disable();
    }
}

VOID
NTAPI
KiInitializeInterrupts(VOID)
{
    ULONG i;
    
    /* Clear interrupt table */
    for (i = 0; i < 1020; i++)
    {
        KiInterruptTable[i] = NULL;
    }
    
    /* Initialize GIC */
    KiInitializeGIC();
    
    /* Enable interrupts at CPU level */
    _enable();
    
    DPRINT1("ARM64 Interrupts initialized\n");
}

VOID
NTAPI
KiInitializeGIC(VOID)
{
    /* GIC initialization for ARM64 */
    /* This would normally get the GIC addresses from ACPI MADT table */
    
    /* For now, use default addresses for QEMU virt machine */
    KiGicDistributorBase = (PVOID)0x08000000;
    KiGicCpuInterfaceBase = (PVOID)0x08010000;
    
    /* Enable GIC distributor */
    if (KiGicDistributorBase)
    {
        /* GICD_CTLR - Enable Group 1 interrupts */
        *(volatile ULONG*)KiGicDistributorBase = 0x1;
    }
    
    /* Enable GIC CPU interface */
    if (KiGicCpuInterfaceBase)
    {
        /* GICC_CTLR - Enable Group 1 interrupts */
        *(volatile ULONG*)KiGicCpuInterfaceBase = 0x1;
        
        /* GICC_PMR - Set priority mask to allow all priorities */
        *(volatile ULONG*)((ULONG_PTR)KiGicCpuInterfaceBase + 0x4) = 0xFF;
    }
    
    DPRINT1("GIC initialized\n");
}

BOOLEAN
NTAPI
KeConnectInterrupt(IN PKINTERRUPT Interrupt)
{
    ULONG Vector;
    KIRQL OldIrql;
    
    /* Get the vector */
    Vector = Interrupt->Vector;
    
    /* Validate vector */
    if (Vector >= 1020)
    {
        DPRINT1("Invalid interrupt vector: %d\n", Vector);
        return FALSE;
    }
    
    /* Raise IRQL */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    
    /* Check if vector is already in use */
    if (KiInterruptTable[Vector] != NULL)
    {
        /* Vector already connected */
        KeLowerIrql(OldIrql);
        return FALSE;
    }
    
    /* Connect the interrupt */
    KiInterruptTable[Vector] = Interrupt;
    
    /* Enable the interrupt in GIC */
    KiEnableInterrupt(Vector);
    
    /* Lower IRQL */
    KeLowerIrql(OldIrql);
    
    return TRUE;
}

BOOLEAN
NTAPI
KeDisconnectInterrupt(IN PKINTERRUPT Interrupt)
{
    ULONG Vector;
    KIRQL OldIrql;
    
    /* Get the vector */
    Vector = Interrupt->Vector;
    
    /* Validate vector */
    if (Vector >= 1020)
    {
        return FALSE;
    }
    
    /* Raise IRQL */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    
    /* Disable the interrupt in GIC */
    KiDisableInterrupt(Vector);
    
    /* Disconnect the interrupt */
    KiInterruptTable[Vector] = NULL;
    
    /* Lower IRQL */
    KeLowerIrql(OldIrql);
    
    return TRUE;
}

VOID
NTAPI
KiEnableInterrupt(IN ULONG Vector)
{
    ULONG Register, Bit;
    volatile ULONG *EnableReg;
    
    if (!KiGicDistributorBase) return;
    
    /* Calculate register and bit for this interrupt */
    Register = Vector / 32;
    Bit = Vector % 32;
    
    /* GICD_ISENABLER - Interrupt Set-Enable Registers */
    EnableReg = (volatile ULONG*)((ULONG_PTR)KiGicDistributorBase + 0x100 + (Register * 4));
    *EnableReg = (1 << Bit);
}

VOID
NTAPI
KiDisableInterrupt(IN ULONG Vector)
{
    ULONG Register, Bit;
    volatile ULONG *DisableReg;
    
    if (!KiGicDistributorBase) return;
    
    /* Calculate register and bit for this interrupt */
    Register = Vector / 32;
    Bit = Vector % 32;
    
    /* GICD_ICENABLER - Interrupt Clear-Enable Registers */
    DisableReg = (volatile ULONG*)((ULONG_PTR)KiGicDistributorBase + 0x180 + (Register * 4));
    *DisableReg = (1 << Bit);
}

VOID
NTAPI
KiHandleDeviceInterrupt(IN ULONG IntId,
                       IN PKTRAP_FRAME TrapFrame)
{
    PKINTERRUPT Interrupt;
    BOOLEAN Handled = FALSE;
    
    /* Validate interrupt ID */
    if (IntId >= 1020)
    {
        DPRINT1("Invalid interrupt ID: %d\n", IntId);
        return;
    }
    
    /* Get the interrupt object */
    Interrupt = KiInterruptTable[IntId];
    
    /* Check if we have a handler */
    if (Interrupt && Interrupt->ServiceRoutine)
    {
        /* Call the ISR */
        Handled = Interrupt->ServiceRoutine(Interrupt, 
                                          Interrupt->ServiceContext);
    }
    
    if (!Handled)
    {
        /* Spurious or unhandled interrupt */
        DPRINT1("Unhandled interrupt: %d\n", IntId);
    }
}

/* HalRequestSoftwareInterrupt is implemented in HAL */
#if 0
VOID
FASTCALL
HalRequestSoftwareInterrupt(IN KIRQL Irql)
{
    /* Generate a software interrupt for the specified IRQL */
    switch (Irql)
    {
        case APC_LEVEL:
            /* Generate SGI 0 */
            KiGenerateSGI(0);
            break;
            
        case DISPATCH_LEVEL:
            /* Generate SGI 1 */
            KiGenerateSGI(1);
            break;
            
        default:
            DPRINT1("Invalid software interrupt IRQL: %d\n", Irql);
            break;
    }
}
#endif

VOID
NTAPI
KiGenerateSGI(IN ULONG SgiId)
{
    /* Generate Software Generated Interrupt */
    /* For GICv3, use ICC_SGI1R_EL1 system register */
    ULONG64 SgiValue;
    
    /* Target all CPUs except self */
    SgiValue = (1ULL << 40) |  /* IRM - Interrupt Routing Mode (all except self) */
               (SgiId & 0xF);   /* INTID */
    
    /* Write to ICC_SGI1R_EL1 */
    __asm__ __volatile__("msr s3_0_c12_c11_5, %0" : : "r" (SgiValue));
}

/* Forward declarations */
VOID NTAPI KiSetInterruptPriorityMask(IN KIRQL Irql);
VOID NTAPI KiCheckForSoftwareInterrupt(IN KIRQL Irql);

VOID
NTAPI
KiSetInterruptPriorityMask(IN KIRQL Irql)
{
    ULONG Priority;
    
    /* Map IRQL to GIC priority */
    /* Higher IRQL = Lower GIC priority value (higher priority) */
    switch (Irql)
    {
        case PASSIVE_LEVEL:
            Priority = 0xFF;  /* Lowest priority - all interrupts enabled */
            break;
        case APC_LEVEL:
            Priority = 0xC0;
            break;
        case DISPATCH_LEVEL:
            Priority = 0x80;
            break;
        case HIGH_LEVEL:
            Priority = 0x00;  /* Highest priority - all interrupts disabled */
            break;
        default:
            Priority = 0xFF - (Irql * 8);
            break;
    }
    
    /* Set priority mask in GIC */
    if (KiGicCpuInterfaceBase)
    {
        /* GICC_PMR - Priority Mask Register */
        *(volatile ULONG*)((ULONG_PTR)KiGicCpuInterfaceBase + 0x4) = Priority;
    }
}

VOID
NTAPI
KiCheckForSoftwareInterrupt(IN KIRQL Irql)
{
    /* Check for pending software interrupts */
    /* TODO: Implement software interrupt checking */
}