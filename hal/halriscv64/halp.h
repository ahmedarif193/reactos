/*
 * PROJECT:     ReactOS RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Private bootstrap platform contracts
 */

#pragma once

#include <ndk/ketypes.h>

#define RISCV_SBI_EXTENSION_BASE       0x10UL
#define RISCV_SBI_EXTENSION_TIME       0x54494D45UL
#define RISCV_SBI_EXTENSION_HSM        0x48534DUL
#define RISCV_SBI_EXTENSION_IPI        0x735049UL
#define RISCV_SBI_EXTENSION_RFENCE     0x52464E43UL
#define RISCV_SBI_BASE_GET_VERSION     0UL
#define RISCV_SBI_BASE_GET_IMPL_ID     1UL
#define RISCV_SBI_BASE_PROBE_EXTENSION 3UL
#define RISCV_SBI_TIME_SET_TIMER       0UL
#define RISCV_SBI_EXTENSION_SRST       0x53525354UL
#define RISCV_SBI_SRST_SYSTEM_RESET    0UL
#define RISCV_SBI_SRST_TYPE_SHUTDOWN   0UL
#define RISCV_SBI_SRST_TYPE_COLD_REBOOT 1UL
#define RISCV_SBI_SRST_REASON_NONE     0UL

#define RISCV_HAL_MAXIMUM_INCREMENT    100000UL
#define RISCV_HAL_MINIMUM_INCREMENT    10000UL

/* Kernel-private bridge values (ntoskrnl internal/riscv64/ke.h): the native
 * image path imports functions only, so the HAL keeps its own copies. */
#define RISCV_HAL_SIE_STIE             (1UL << 5)   /* RISCV_SIE_STIE */
#define RISCV_HAL_SIE_SSIE             (1UL << 1)
#define RISCV_HAL_SIE_SEIE             (1UL << 9)   /* RISCV_SIE_SEIE */
#define RISCV_HAL_EXTERNAL_IRQL        12
#define RISCV_HAL_FEATURE_SSTC         0x00000100   /* KI_RISCV_FEATURE_SSTC */

typedef struct _RISCV_SBI_RETURN
{
    LONG_PTR Error;
    ULONG_PTR Value;
} RISCV_SBI_RETURN;

extern volatile ULONG HalpRiscvInitializationPhase;
extern volatile ULONG HalpRiscvInitializationFailure;
extern ULONG HalpRiscvSbiVersion;
extern ULONG_PTR HalpRiscvSbiImplementationId;
extern ULONG64 HalpRiscvTimebaseFrequency;
extern ULONG64 HalpRiscvBootCounter;
extern ULONG HalpRiscvCurrentTimeIncrement;
extern ULONG_PTR HalpRiscvHartIds[MAXIMUM_PROCESSORS];
extern ULONG HalpRiscvHartCount;
extern ULONG HalpRiscvStartedProcessors;
RISCV_SBI_RETURN HalpRiscvSbiCall(ULONG_PTR Extension, ULONG_PTR Function,
                                ULONG_PTR Arg0, ULONG_PTR Arg1, ULONG_PTR Arg2,
                                ULONG_PTR Arg3);
BOOLEAN HalpRiscvDiscoverHarts(struct _LOADER_PARAMETER_BLOCK *LoaderBlock);
BOOLEAN NTAPI HalpRiscvQueryProcessorHartId(ULONG Number, PULONG_PTR HartId);
BOOLEAN HalpRiscvSbiExtensionAvailable(ULONG_PTR Extension);

/* Kernel imports (ntoskrnl.exe, listed in CMakeLists.txt IMPORTS). */
VOID NTAPI KiRiscvSetInterruptEnabled(_In_ ULONG_PTR Mask, _In_ BOOLEAN Enable);
ULONG NTAPI KiRiscvQueryFeatureFlags(VOID);
VOID NTAPI KeSetDmaIoCoherency(_In_ ULONG Coherency);
VOID FASTCALL KeUpdateSystemTime(_In_ PKTRAP_FRAME TrapFrame, _In_ ULONG Increment, _In_ KIRQL Irql);

VOID HalpRiscvStartClock(VOID);
VOID NTAPI HalpRiscvClockInterrupt(_In_ PKTRAP_FRAME TrapFrame);
BOOLEAN HalpRiscvInitializeSbi(VOID);
RISCV_SBI_RETURN HalpRiscvSetTimer(_In_ ULONG64 Deadline);
BOOLEAN HalpRiscvSystemReset(_In_ ULONG_PTR ResetType);
BOOLEAN HalpRiscvInitializePci(_In_reads_bytes_(DeviceTreeSize) const VOID *DeviceTree, _In_ SIZE_T DeviceTreeSize);
BOOLEAN HalpRiscvMapPciConfig(VOID);
BOOLEAN HalpRiscvInitializePlic(_In_reads_bytes_(DeviceTreeSize) const VOID *DeviceTree, _In_ SIZE_T DeviceTreeSize, _In_ ULONG64 BootHartId);
BOOLEAN HalpRiscvMapPlic(VOID);
BOOLEAN HalpRiscvPlicHasSource(_In_ ULONG Phandle, _In_ ULONG Source);
BOOLEAN HalpRiscvPlicValidSource(_In_ ULONG Source);
ULONG NTAPI HalpRiscvClaimPlicInterrupt(VOID);
VOID NTAPI HalpRiscvCompletePlicInterrupt(_In_ ULONG Source);
BOOLEAN NTAPI HalpRiscvPlicIsDeviceMemory(_In_ PHYSICAL_ADDRESS Address, _In_ SIZE_T Length);
BOOLEAN HalpRiscvGetPciResource(_Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource);
BOOLEAN NTAPI HalpRiscvGetPciBusRange(_Out_ PULONG FirstBus, _Out_ PULONG LastBus);
BOOLEAN HalpRiscvPciDmaCoherent(VOID);
NTSTATUS NTAPI HaliInitPnpDriver(VOID);
BOOLEAN HalpRiscvInitializeRtc(const VOID *DeviceTree, SIZE_T DeviceTreeSize);
BOOLEAN HalpRiscvMapRtc(VOID);
BOOLEAN HalpRiscvRtcIsDeviceMemory(PHYSICAL_ADDRESS Address, SIZE_T Length);
BOOLEAN NTAPI HalpRiscvIsDeviceMemory(_In_ PHYSICAL_ADDRESS Address, _In_ SIZE_T Length);
ULONG64 HalpRiscvReadTime(VOID);
BOOLEAN HalpRiscvReadTimebaseFrequency(
    _In_reads_bytes_(DeviceTreeSize) const VOID *DeviceTree,
    _In_ SIZE_T DeviceTreeSize,
    _Out_ PULONG64 Frequency);
