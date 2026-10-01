/*
 * PROJECT:     LiberNT PowerPC HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     PReP platform definitions
 */

#pragma once

#include <ndk/ketypes.h>

/* Device interrupt priority is fixed; synchronization is UP/MP-dependent.
 * This matches the kernel-private KI_PPC_EXTERNAL_IRQL. */
#define PPC_HAL_EXTERNAL_IRQL      (IPI_LEVEL - 2)
#define PPC_HAL_ISA_IRQ_COUNT      16
#define PPC_HAL_SPURIOUS           0xFFFFFFFFUL

#define PPC_HAL_MAXIMUM_INCREMENT  100000UL
#define PPC_HAL_MINIMUM_INCREMENT  10000UL

/* MSR[EE] masking; the kernel's helpers are private to it. */
FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    ULONG Msr;

    __asm__ __volatile__("mfmsr %0" : "=r"(Msr) :: "memory");
    __asm__ __volatile__("mtmsr %0\n\tisync" :: "r"(Msr & ~0x8000UL) : "memory");
    return (Msr & 0x8000UL) != 0;
}

FORCEINLINE
VOID
KeRestoreInterrupts(_In_ BOOLEAN WereEnabled)
{
    if (WereEnabled)
        _enable();
}

extern ULONG_PTR HalpPpcIoBase;
extern ULONG HalpPpcDmaOffset;
extern ULONG HalpPpcPciMemoryBase;
extern ULONG HalpPpcTimebaseFrequency;

VOID HalpPpcInitializePic(VOID);
VOID HalpPpcStartClock(VOID);
BOOLEAN NTAPI HalpPpcGetPciBusRange(_Out_ PULONG FirstBus, _Out_ PULONG LastBus);
BOOLEAN HalpPpcGetPciResource(_Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource);
BOOLEAN HalpPpcPciDmaCoherent(VOID);
NTSTATUS NTAPI HaliInitPnpDriver(VOID);
ULONGLONG HalpPpcReadTimebase(VOID);
ULONG HalpPpcCalibrateTimebase(VOID);

/* Kernel imports (ntoskrnl.exe). */
VOID NTAPI KeSetDmaIoCoherency(_In_ ULONG Coherency);
VOID FASTCALL KeUpdateSystemTime(_In_ PKTRAP_FRAME TrapFrame, _In_ ULONG Increment, _In_ KIRQL Irql);
VOID NTAPI KeSetTimeIncrement(ULONG MaximumIncrement, ULONG MinimumIncrement);

/* Exports consumed by the kernel's interrupt model. */
VOID NTAPI HalpPpcClockInterrupt(_In_ PKTRAP_FRAME TrapFrame);
VOID NTAPI HalpPpcParkClock(VOID);
ULONG NTAPI HalpPpcClaimInterrupt(VOID);
VOID NTAPI HalpPpcCompleteInterrupt(_In_ ULONG Vector);
VOID NTAPI HalpPpcDeferExternal(VOID);
VOID NTAPI HalpPpcRestoreExternal(VOID);
