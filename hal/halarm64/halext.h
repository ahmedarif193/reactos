/*
 * PROJECT:     ReactOS HAL — ARM64 platform-extension contract
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Registration interfaces between the generic ARM64 HAL and
 *              statically linked platform extensions (SoC support code).
 *
 *              Modeled on the NT HAL-extension idea: the generic HAL never
 *              references a platform by name.  During early init it runs the
 *              platform probe list (see halext.c — the only file that knows
 *              which platforms exist); a matching platform registers the
 *              services it provides through the interfaces below:
 *
 *                - HAL_ARM64_INTERRUPT_CONTROLLER: replaces the GIC on SoCs
 *                  without one (function-table contract mirroring the NT
 *                  INTERRUPT_FUNCTION_TABLE shape).
 *
 *                - HAL_ARM64_PCI_CONFIG_BACKEND: PCI configuration-space
 *                  accessor for SoCs whose root complexes are not ECAM/MCFG
 *                  reachable.
 *
 *                - HAL_ARM64_PLATFORM_DEVICE: fixed SoC devices missing from
 *                  the firmware ACPI namespace, reported by the HAL bus
 *                  driver as child PDOs from a resource descriptor.
 *
 *              Adding a platform (RPi4, QCOM, ...) means: one new platform
 *              file implementing a probe that registers its services, plus
 *              one line in the platform table in halext.c.
 *
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/* ------------------------------------------------------------------ */
/*  Generic MMIO accessor (defined in halarm64.c): identity-mapping   */
/*  aware during Phase 0, physical-alias/kernel-VA aware afterwards.  */
/* ------------------------------------------------------------------ */
extern volatile ULONG *HalpMmio(ULONG_PTR Base, ULONG Offset);

/* ------------------------------------------------------------------ */
/*  Interrupt controller contract                                     */
/*                                                                    */
/*  The backend must honor GIC INTID/priority semantics: INTIDs are   */
/*  the kernel-visible vectors (SGIs 0-15, timer PPIs 26-30, device   */
/*  sources 32+), priorities are GIC-style bytes (lower = more        */
/*  urgent), and SetPriorityMask() implements the software PMR:       */
/*  AcknowledgeInterrupt() may only deliver a source whose priority   */
/*  is numerically below the current mask, and must quiesce (defer)   */
/*  blocked level-triggered sources until the mask rises above them.  */
/* ------------------------------------------------------------------ */
#define HAL_ARM64_SGI_IPI    0
#define HAL_ARM64_SGI_APC    1
#define HAL_ARM64_SGI_DPC    2
#define HAL_ARM64_SGI_FREEZE 3

#define HAL_ARM64_SGI_IPI_PRIORITY    0x10
#define HAL_ARM64_SGI_APC_PRIORITY    0xE0
#define HAL_ARM64_SGI_DPC_PRIORITY    0xD0
#define HAL_ARM64_SGI_FREEZE_PRIORITY 0x00

typedef struct _HAL_ARM64_INTERRUPT_CONTROLLER
{
    PCSTR Name;
    ULONG (*AcknowledgeInterrupt)(VOID);
    VOID (*EndInterrupt)(_In_ ULONG IntId);
    VOID (*EnableInterrupt)(_In_ ULONG IntId);
    VOID (*DisableInterrupt)(_In_ ULONG IntId);
    VOID (*SetInterruptPriority)(_In_ ULONG IntId, _In_ ULONG Priority);
    VOID (*SetTriggerMode)(_In_ ULONG IntId, _In_ BOOLEAN EdgeTriggered);
    VOID (*SetPriorityMask)(_In_ KIRQL Irql);
    ULONG (*GetPriorityMask)(VOID);
    VOID (*SendSgi)(_In_ KAFFINITY TargetSet, _In_ ULONG SgiId);
    VOID (*InitializeProcessor)(_In_ ULONG ProcessorNumber);
    BOOLEAN (*Phase0Initialize)(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);
    BOOLEAN (*Phase1MapRuntimeMmio)(VOID);
} HAL_ARM64_INTERRUPT_CONTROLLER, *PHAL_ARM64_INTERRUPT_CONTROLLER;

/* NULL when a GIC drives interrupts. */
extern const HAL_ARM64_INTERRUPT_CONTROLLER *HalpArm64InterruptController;

/* ------------------------------------------------------------------ */
/*  PCI configuration-space backend contract                          */
/* ------------------------------------------------------------------ */
typedef struct _HAL_ARM64_PCI_CONFIG_BACKEND
{
    PCSTR Name;

    /*
     * Returns TRUE when the access was handled (absent devices read as
     * all-ones), FALSE when the backend cannot serve the request and the
     * generic ECAM/_CBA paths should run instead.
     */
    BOOLEAN (*AccessConfigSpace)(
        _In_ BOOLEAN Write,
        _In_ USHORT Segment,
        _In_ ULONG Bus,
        _In_ PCI_SLOT_NUMBER Slot,
        _Inout_updates_bytes_(Length) PVOID Buffer,
        _In_ ULONG Offset,
        _In_ ULONG Length);
} HAL_ARM64_PCI_CONFIG_BACKEND, *PHAL_ARM64_PCI_CONFIG_BACKEND;

/* NULL when config space is reached through ECAM/MCFG or _CBA. */
extern const HAL_ARM64_PCI_CONFIG_BACKEND *HalpArm64PciConfigBackend;

/* ------------------------------------------------------------------ */
/*  Platform device contract (fixed SoC devices absent from ACPI)     */
/* ------------------------------------------------------------------ */
#define HAL_ARM64_PLATFORM_DEVICE_MAX_MEMORY    4
#define HAL_ARM64_MAX_PLATFORM_DEVICES          8

typedef struct _HAL_ARM64_PLATFORM_DEVICE
{
    PCSTR Name;                 /* Diagnostics only                       */
    PCWSTR DeviceId;            /* PnP device/hardware ID                 */
    PCWSTR CompatibleId;        /* Optional second hardware ID, or NULL   */

    ULONG MemoryCount;
    struct
    {
        ULONGLONG Base;
        ULONG Length;
    } Memory[HAL_ARM64_PLATFORM_DEVICE_MAX_MEMORY];

    ULONG Gsi;                  /* 0 = no interrupt resource              */
    BOOLEAN EdgeTriggered;      /* FALSE = level-sensitive                */
} HAL_ARM64_PLATFORM_DEVICE, *PHAL_ARM64_PLATFORM_DEVICE;

/* ------------------------------------------------------------------ */
/*  Registration (called from platform probes)                        */
/* ------------------------------------------------------------------ */

NTSTATUS
HalpArm64RegisterInterruptController(
    _In_ const HAL_ARM64_INTERRUPT_CONTROLLER *Controller);

NTSTATUS
HalpArm64RegisterPciConfigBackend(
    _In_ const HAL_ARM64_PCI_CONFIG_BACKEND *Backend);

NTSTATUS
HalpArm64RegisterPlatformDevice(
    _In_ const HAL_ARM64_PLATFORM_DEVICE *Device);

/* ------------------------------------------------------------------ */
/*  Generic HAL consumption                                           */
/* ------------------------------------------------------------------ */

/*
 * Run the platform probe table.  Idempotent; called from the GIC
 * interface selection (HalInitializeProcessor on the BSP, before
 * KeInitInterrupts) and again from HalInitSystem Phase 0 as a safety
 * net.  Once a platform matches, further calls return immediately.
 */
VOID
HalpArm64ProbePlatforms(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
HalpArm64Phase1PlatformInit(VOID);

ULONG
HalpArm64GetPlatformDeviceCount(VOID);

const HAL_ARM64_PLATFORM_DEVICE *
HalpArm64GetPlatformDevice(
    _In_ ULONG Index);
