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

/* GLOBALS *******************************************************************/

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

#ifdef _M_AMD64
    DPRINT1("HAL: [AMD64] HalInitSystem starting - BootPhase=%lu\n", BootPhase);
#else
    DPRINT1("HAL: [i386] HalInitSystem starting - BootPhase=%lu\n", BootPhase);
#endif

    /* Check the boot phase */
    if (BootPhase == 0)
    {
        /* Save bus type */
        HalpBusType = LoaderBlock->u.I386.MachineType & 0xFF;
        
#ifdef _M_AMD64
        DPRINT1("HAL: [AMD64] Bus type = 0x%x\n", HalpBusType);
#else
        DPRINT1("HAL: [i386] Bus type = 0x%x\n", HalpBusType);
#endif

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
#ifdef _M_AMD64
        DPRINT1("HAL: [AMD64] Initializing ACPI Phase 0...\n");
#else
        DPRINT1("HAL: [i386] Initializing ACPI Phase 0...\n");
#endif
        Status = HalpSetupAcpiPhase0(LoaderBlock);
        if (!NT_SUCCESS(Status))
        {
#ifdef _M_AMD64
            DPRINT1("HAL: [AMD64] ACPI Phase 0 FAILED! Status=0x%lx\n", Status);
#else
            DPRINT1("HAL: [i386] ACPI Phase 0 FAILED! Status=0x%lx\n", Status);
#endif
            KeBugCheckEx(ACPI_BIOS_ERROR, Status, 0, 0, 0);
        }
#ifdef _M_AMD64
        DPRINT1("HAL: [AMD64] ACPI Phase 0 initialized successfully\n");
#else
        DPRINT1("HAL: [i386] ACPI Phase 0 initialized successfully\n");
#endif

        /* Initialize the PICs */
#ifdef _M_AMD64
        DPRINT1("HAL: [AMD64] Initializing PICs...\n");
#else
        DPRINT1("HAL: [i386] Initializing PICs...\n");
#endif
        HalpInitializePICs(TRUE);

        /* Initialize CMOS lock */
        KeInitializeSpinLock(&HalpSystemHardwareLock);

        /* Initialize CMOS */
        HalpInitializeCmos();

        /* Fill out the dispatch tables */
        HalQuerySystemInformation = HaliQuerySystemInformation;
        HalSetSystemInformation = HaliSetSystemInformation;
        HalInitPnpDriver = HaliInitPnpDriver;
        HalGetDmaAdapter = HalpGetDmaAdapter;

        HalGetInterruptTranslator = NULL;  // FIXME: TODO
        HalResetDisplay = HalpBiosDisplayReset;
        HalHaltSystem = HaliHaltSystem;

        /* Setup I/O space */
        HalpDefaultIoSpace.Next = HalpAddressUsageList;
        HalpAddressUsageList = &HalpDefaultIoSpace;

        /* Setup busy waiting */
        HalpCalibrateStallExecution();

        /* Initialize the clock */
        HalpInitializeClock();

        /*
         * We could be rebooting with a pending profile interrupt,
         * so clear it here before interrupts are enabled
         */
        HalStopProfileInterrupt(ProfileTime);

        /* Do some HAL-specific initialization */
        HalpInitPhase0(LoaderBlock);

        /* Initialize Phase 0 of the x86 emulator */
        HalInitializeBios(0, LoaderBlock);
    }
    else if (BootPhase == 1)
    {
        /* Initialize bus handlers */
        HalpInitBusHandlers();

        /* Do some HAL-specific initialization */
        HalpInitPhase1();

        /* Initialize Phase 1 of the x86 emulator */
        HalInitializeBios(1, LoaderBlock);
    }

    /* All done, return */
    return TRUE;
}
