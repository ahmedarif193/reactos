/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/generic/halinit.c
 * PURPOSE:         HAL Entrypoint and Initialization
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

#if defined(_MSC_VER)
/*
 * Provide a no-op fallback for builds that do not link in the ACPI phase-1
 * helpers. The linker will redirect HalpAcpiPhase1Init to this implementation
 * if no strong symbol is available.
 */
VOID
HalpAcpiPhase1InitFallback(VOID)
{
    /* Nothing to do when ACPI support is absent. */
}

#pragma comment(linker, "/alternatename:HalpAcpiPhase1Init=HalpAcpiPhase1InitFallback")
#else
VOID HalpAcpiPhase1Init(VOID) __attribute__((weak));
#endif

/* GLOBALS *******************************************************************/

/*
 * On UEFI systems, provide a safe no-op HalResetDisplay implementation
 * so callers can safely fall back to their own initialization paths.
 */
static BOOLEAN NTAPI HalpNoopResetDisplay(VOID)
{
    return FALSE;
}

//#ifdef CONFIG_SMP // FIXME: Reenable conditional once HAL is consistently compiled for SMP mode
BOOLEAN HalpOnlyBootProcessor;
//#endif
BOOLEAN HalpPciLockSettings;

/* PRIVATE FUNCTIONS *********************************************************/

static
CODE_SEG("INIT")
VOID
HalpGetParameters(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /* Make sure we have a loader block and command line */
    if (LoaderBlock && LoaderBlock->LoadOptions)
    {
        /* Read the command line */
        PCSTR CommandLine = LoaderBlock->LoadOptions;

//#ifdef CONFIG_SMP // FIXME: Reenable conditional once HAL is consistently compiled for SMP mode
        /* Check whether we should only start one CPU */
        if (strstr(CommandLine, "ONECPU"))
            HalpOnlyBootProcessor = TRUE;
//#endif

        /* Check if PCI is locked */
        if (strstr(CommandLine, "PCILOCK"))
            HalpPciLockSettings = TRUE;

        /* Check for initial breakpoint */
        if (strstr(CommandLine, "BREAK"))
            DbgBreakPoint();
    }
}

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
HalInitializeProcessor(
    IN ULONG ProcessorNumber,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    DPRINT1("HAL: HalInitializeProcessor CPU %lu begin\n", ProcessorNumber);
    /* Hal specific initialization for this cpu */
    HalpInitProcessor(ProcessorNumber, LoaderBlock);

    /* Set default stall count */
    KeGetPcr()->StallScaleFactor = INITIAL_STALL_COUNT;

    /* Update the interrupt affinity and processor mask */
    InterlockedBitTestAndSetAffinity(&HalpActiveProcessors, ProcessorNumber);
    InterlockedBitTestAndSetAffinity(&HalpDefaultInterruptAffinity, ProcessorNumber);

    if (ProcessorNumber == 0)
    {
        /* Register routines for KDCOM */
        HalpRegisterKdSupportFunctions();
    }

    DPRINT1("HAL: HalInitializeProcessor CPU %lu done\n", ProcessorNumber);
}

/*
 * @implemented
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
HalInitSystem(
    _In_ ULONG BootPhase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    NTSTATUS Status;

    DPRINT1("HAL: HalInitSystem Phase=%d, IRQL=%d\n", BootPhase, KeGetCurrentIrql());

    /* Check the boot phase */
    if (BootPhase == 0)
    {
        /* Save bus type */
        HalpBusType = LoaderBlock->u.I386.MachineType & 0xFF;

        /* Get command-line parameters */
        HalpGetParameters(LoaderBlock);

        /* Check for PRCB version mismatch */
        if (Prcb->MajorVersion != PRCB_MAJOR_VERSION)
        {
            /* No match, bugcheck */
            KeBugCheckEx(MISMATCHED_HAL, 1, Prcb->MajorVersion, PRCB_MAJOR_VERSION, 0);
        }

        /* Checked/free HAL requires checked/free kernel */
        if (Prcb->BuildType != HalpBuildType)
        {
            /* No match, bugcheck */
            KeBugCheckEx(MISMATCHED_HAL, 2, Prcb->BuildType, HalpBuildType, 0);
        }

        /* Initialize ACPI */
        Status = HalpSetupAcpiPhase0(LoaderBlock);
        DPRINT1("HAL: After HalpSetupAcpiPhase0 -> %lx\n", Status);
        if (!NT_SUCCESS(Status))
        {
            KeBugCheckEx(ACPI_BIOS_ERROR, Status, 0, 0, 0);
        }

        /* Initialize the PICs */
        HalpInitializePICs(TRUE);
        DPRINT1("HAL: After HalpInitializePICs\n");

        /* Initialize CMOS lock */
        KeInitializeSpinLock(&HalpSystemHardwareLock);

        /* Initialize CMOS */
        HalpInitializeCmos();
        DPRINT1("HAL: After HalpInitializeCmos\n");

        /* Fill out the dispatch tables */
        HalQuerySystemInformation = HaliQuerySystemInformation;
        HalSetSystemInformation = HaliSetSystemInformation;
        HalInitPnpDriver = HaliInitPnpDriver;
        HalGetDmaAdapter = HalpGetDmaAdapter;

        HalGetInterruptTranslator = NULL;  // FIXME: TODO
        /* Provide a safe default for HalResetDisplay on UEFI systems */
        if (LoaderBlock->FirmwareInformation.FirmwareTypeEfi == 0)
        {
            HalResetDisplay = HalpBiosDisplayReset;
        }
        else
        {
            /*
             * Bootvid may call HalResetDisplay() unconditionally via the HAL
             * private dispatch. Never leave this pointer NULL: provide a
             * no-op stub that returns FALSE so Bootvid can take its fallback
             * path without crashing.
             */
            HalResetDisplay = HalpNoopResetDisplay;
        }
        HalHaltSystem = HaliHaltSystem;
        DPRINT1("HAL: After filling dispatch table\n");

        /* Setup I/O space */
        HalpDefaultIoSpace.Next = HalpAddressUsageList;
        HalpAddressUsageList = &HalpDefaultIoSpace;
        DPRINT1("HAL: After setting up IO space\n");

        /* Setup busy waiting */
        HalpCalibrateStallExecution();
        DPRINT1("HAL: After HalpCalibrateStallExecution\n");

        /* Initialize the clock */
        HalpInitializeClock();
        DPRINT1("HAL: After HalpInitializeClock\n");

        /*
         * We could be rebooting with a pending profile interrupt,
         * so clear it here before interrupts are enabled
         */
        HalStopProfileInterrupt(ProfileTime);
        DPRINT1("HAL: After HalStopProfileInterrupt\n");

        /* Do some HAL-specific initialization */
        HalpInitPhase0(LoaderBlock);
        DPRINT1("HAL: After HalpInitPhase0\n");

        /* Initialize Phase 0 of the x86 emulator only for BIOS systems */
        if (LoaderBlock->FirmwareInformation.FirmwareTypeEfi == 0)
        {
            HalInitializeBios(0, LoaderBlock);
        }
        
    }
    else if (BootPhase == 1)
    {
        /* Initialize bus handlers */
        HalpInitBusHandlers();

        /* Do some HAL-specific initialization */
        HalpInitPhase1();

        /* ACPI late init: allocate ECAM gating map and process registry parameters */
#if defined(_MSC_VER)
        HalpAcpiPhase1Init();
#else
        if (HalpAcpiPhase1Init)
            HalpAcpiPhase1Init();
#endif

        /* Initialize Phase 1 of the x86 emulator only for BIOS systems */
        if (LoaderBlock->FirmwareInformation.FirmwareTypeEfi == 0)
        {
            HalInitializeBios(1, LoaderBlock);
        }
    }

    /* All done, return */
    return TRUE;
}
