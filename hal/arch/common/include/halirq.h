/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/arch/common/include/halirq.h
 * PURPOSE:         IRQ/Vector translation stubs for non-x86 architectures
 *
 * On ARM64, interrupt translation is handled differently than on x86.
 * The GIC (Generic Interrupt Controller) uses INTIDs directly as vectors,
 * so most of these functions simply return 0 to indicate that
 * HalpGetRootInterruptVector() should be used for full translation.
 */

#pragma once

/*
 * HalpIrqToVector - Convert an IRQ (GSI) to a system vector
 *
 * On ARM64 with GIC, there is no simple IRQ-to-vector mapping table.
 * Always return 0 to force the caller to use HalpGetRootInterruptVector()
 * which performs the full GSI-to-INTID translation and IRQL assignment.
 */
static inline UCHAR
FASTCALL
HalpIrqToVector(UCHAR Irq)
{
    UNREFERENCED_PARAMETER(Irq);
    return 0;
}

/*
 * HalpVectorToIrql - Convert a system vector to an IRQL
 *
 * On ARM64, IRQL is computed based on the interrupt priority in the GIC.
 * This stub returns a safe default. The actual IRQL is determined during
 * interrupt allocation in HalpGetRootInterruptVector().
 */
static inline KIRQL
FASTCALL
HalpVectorToIrql(UCHAR Vector)
{
    UNREFERENCED_PARAMETER(Vector);
    return PASSIVE_LEVEL;
}

/*
 * HalpVectorToIrq - Convert a system vector to an IRQ (GSI)
 *
 * On ARM64 with GIC, the INTID can serve as both vector and GSI.
 * This stub returns the vector unchanged.
 */
static inline UCHAR
FASTCALL
HalpVectorToIrq(UCHAR Vector)
{
    return Vector;
}

#define VECTOR2IRQ(vector)      HalpVectorToIrq(vector)
#define VECTOR2IRQL(vector)     HalpVectorToIrql(vector)
#define IRQ2VECTOR(irq)         HalpIrqToVector(irq)
