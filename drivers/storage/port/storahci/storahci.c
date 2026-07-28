/*
 * PROJECT:        ReactOS Kernel
 * LICENSE:        GNU GPLv2 only as published by the Free Software Foundation
 * PURPOSE:        To Implement AHCI Miniport driver targeting storport NT 5.2
 * PROGRAMMERS:    Aman Priyadarshi (aman.eureka@gmail.com)
 */

#include "storahci.h"

/**
 * @name AhciPortInitialize
 * @implemented
 *
 * Initialize port by setting up PxCLB & PxFB Registers
 *
 * @param PortExtension
 *
 * @return
 * Return true if intialization was successful
 */
BOOLEAN NTAPI AhciPortInitialize(__in PVOID DeviceExtension)
{
    PAHCI_PORT_EXTENSION PortExtension;
    AHCI_PORT_CMD cmd;
    PAHCI_MEMORY_REGISTERS abar;
    ULONG mappedLength, portNumber, ticks;
    PAHCI_ADAPTER_EXTENSION adapterExtension;
    STOR_PHYSICAL_ADDRESS commandListPhysical, receivedFISPhysical;

    AhciDebugPrint("AhciPortInitialize()\n");

    PortExtension = (PAHCI_PORT_EXTENSION)DeviceExtension;
    adapterExtension = PortExtension->AdapterExtension;
    abar = adapterExtension->ABAR_Address;
    portNumber = PortExtension->PortNumber;

    NT_ASSERT(abar != NULL);
    NT_ASSERT(portNumber < adapterExtension->PortCount);

    PortExtension->Port = &abar->PortList[portNumber];

    commandListPhysical = StorPortGetPhysicalAddress(adapterExtension,
                                                     NULL,
                                                     PortExtension->CommandList,
                                                     &mappedLength);

    if ((mappedLength == 0) || ((commandListPhysical.LowPart % 1024) != 0))
    {
        AhciDebugPrint("\tcommandListPhysical mappedLength:%d\n", mappedLength);
        return FALSE;
    }

    receivedFISPhysical = StorPortGetPhysicalAddress(adapterExtension,
                                                     NULL,
                                                     PortExtension->ReceivedFIS,
                                                     &mappedLength);

    if ((mappedLength == 0) || ((receivedFISPhysical.LowPart % 256) != 0))
    {
        AhciDebugPrint("\treceivedFISPhysical mappedLength:%d\n", mappedLength);
        return FALSE;
    }

    // Ensure that the controller is not in the running state by reading and examining each
    // implemented port’s PxCMD register. If PxCMD.ST, PxCMD.CR, PxCMD.FRE and
    // PxCMD.FR are all cleared, the port is in an idle state. Otherwise, the port is not idle and
    // should be placed in the idle state prior to manipulating HBA and port specific registers.
    // System software places a port into the idle state by clearing PxCMD.ST and waiting for
    // PxCMD.CR to return ‘0’ when read. Software should wait at least 500 milliseconds for
    // this to occur. If PxCMD.FRE is set to ‘1’, software should clear it to ‘0’ and wait at least
    // 500 milliseconds for PxCMD.FR to return ‘0’ when read. If PxCMD.CR or PxCMD.FR do
    // not clear to ‘0’ correctly, then software may attempt a port reset or a full HBA reset to recove

    /* Place the port in the idle state before programming PxCLB/PxFB.
     * Clear PxCMD.ST and wait for PxCMD.CR to clear, then clear PxCMD.FRE and
     * wait for PxCMD.FR to clear; the spec allows 500 ms for each.
     *
     * NOTE: the previous code cleared ST/FRE in its local copy but never wrote
     * the value back, then waited for CR/FR to clear on their own. On real
     * hardware the firmware leaves ports running, so the port never went idle
     * and initialization always failed here. */
    cmd.Status = StorPortReadRegisterUlong(adapterExtension, &PortExtension->Port->CMD);
    if ((cmd.ST != 0) || (cmd.CR != 0) || (cmd.FRE != 0) || (cmd.FR != 0))
    {
        AhciDebugPrint("\tPort %u not idle (CMD=%08x), stopping it\n", portNumber, cmd.Status);

        if (cmd.ST != 0)
        {
            cmd.ST = 0;
            StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->CMD, cmd.Status);
        }

        for (ticks = 0; ticks < 500; ticks++)
        {
            cmd.Status = StorPortReadRegisterUlong(adapterExtension, &PortExtension->Port->CMD);
            if (cmd.CR == 0) break;
            StorPortStallExecution(1000);
        }

        if (cmd.CR != 0)
        {
            AhciDebugPrint("\tPort %u: PxCMD.CR stuck set (CMD=%08x)\n", portNumber, cmd.Status);
            return FALSE;
        }

        if (cmd.FRE != 0)
        {
            cmd.FRE = 0;
            StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->CMD, cmd.Status);
        }

        for (ticks = 0; ticks < 500; ticks++)
        {
            cmd.Status = StorPortReadRegisterUlong(adapterExtension, &PortExtension->Port->CMD);
            if (cmd.FR == 0) break;
            StorPortStallExecution(1000);
        }

        if (cmd.FR != 0)
        {
            AhciDebugPrint("\tPort %u: PxCMD.FR stuck set (CMD=%08x)\n", portNumber, cmd.Status);
            return FALSE;
        }

        AhciDebugPrint("\tPort %u is now idle\n", portNumber);
    }

    // 10.1.2 For each implemented port, system software shall allocate memory for and program:
    // ? PxCLB and PxCLBU (if CAP.S64A is set to ‘1’)
    // ? PxFB and PxFBU (if CAP.S64A is set to ‘1’)
    // Note: Assuming 32bit support only
    StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->CLB, commandListPhysical.LowPart);
    if (IsAdapterCAPS64(adapterExtension->CAP))
    {
        StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->CLBU, commandListPhysical.HighPart);
    }

    StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->FB, receivedFISPhysical.LowPart);
    if (IsAdapterCAPS64(adapterExtension->CAP))
    {
        StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->FBU, receivedFISPhysical.HighPart);
    }

    PortExtension->IdentifyDeviceDataPhysicalAddress = StorPortGetPhysicalAddress(adapterExtension,
                                                                                  NULL,
                                                                                  PortExtension->IdentifyDeviceData,
                                                                                  &mappedLength);

    PortExtension->NcqErrorLogPhysicalAddress = StorPortGetPhysicalAddress(adapterExtension,
                                                                           NULL,
                                                                           PortExtension->NcqErrorLog,
                                                                           &mappedLength);
    if (mappedLength < sizeof(*PortExtension->NcqErrorLog))
    {
        AhciDebugPrint("\tNCQ error log mappedLength:%u\n", mappedLength);
        return FALSE;
    }

    PortExtension->NcqErrorCommandTablePhysicalAddress = StorPortGetPhysicalAddress(adapterExtension,
                                                                                    NULL,
                                                                                    PortExtension->NcqErrorCommandTable,
                                                                                    &mappedLength);
    if ((mappedLength < sizeof(*PortExtension->NcqErrorCommandTable)) ||
        ((PortExtension->NcqErrorCommandTablePhysicalAddress.LowPart % 128) != 0))
    {
        AhciDebugPrint("\tNCQ error command table mappedLength:%u\n", mappedLength);
        return FALSE;
    }

    // set device power state flag to D0
    PortExtension->DevicePowerState = StorPowerDeviceD0;

    // clear pending interrupts
    StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->SERR, (ULONG)~0);
    StorPortWriteRegisterUlong(adapterExtension, &PortExtension->Port->IS, (ULONG)~0);
    StorPortWriteRegisterUlong(adapterExtension, adapterExtension->IS, (1 << PortExtension->PortNumber));

    return TRUE;
}// -- AhciPortInitialize();

/**
 * @name AhciAllocateResourceForAdapter
 * @implemented
 *
 * Allocate memory from poll for required pointers
 *
 * @param AdapterExtension
 * @param ConfigInfo
 *
 * @return
 * return TRUE if allocation was successful
 */
BOOLEAN AhciAllocateResourceForAdapter(__in PAHCI_ADAPTER_EXTENSION AdapterExtension, __in PPORT_CONFIGURATION_INFORMATION ConfigInfo)
{
    PCHAR nonCachedExtension;
    ULONG index, NCS, AlignedNCS;
    ULONG commandListSize, receivedFisOffset, identifyDataOffset;
    ULONG ncqErrorLogOffset, ncqErrorCommandTableOffset;
    ULONG portCount, portImplemented, nonCachedExtensionSize;
    PAHCI_PORT_EXTENSION PortExtension;

    AhciDebugPrint("AhciAllocateResourceForAdapter()\n");

    NCS = AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP);
    AlignedNCS = ROUND_UP(NCS, 8);

    // get port count -- Number of set bits in `AdapterExtension->PortImplemented`
    portCount = 0;
    portImplemented = AdapterExtension->PortImplemented;

    NT_ASSERT(portImplemented != 0);
    for (index = MAXIMUM_AHCI_PORT_COUNT - 1; index > 0; index--)
        if ((portImplemented & (1 << index)) != 0)
            break;

    portCount = index + 1;
    AhciDebugPrint("\tPort Count: %d\n", portCount);

    AdapterExtension->PortCount = portCount;
    commandListSize = sizeof(AHCI_COMMAND_HEADER) * AlignedNCS;
    receivedFisOffset = commandListSize;
    identifyDataOffset = receivedFisOffset + sizeof(AHCI_RECEIVED_FIS);
    ncqErrorLogOffset = identifyDataOffset + sizeof(IDENTIFY_DEVICE_DATA);
    ncqErrorCommandTableOffset = ROUND_UP(ncqErrorLogOffset + sizeof(GP_LOG_NCQ_COMMAND_ERROR), 128);
    nonCachedExtensionSize = ncqErrorCommandTableOffset + sizeof(AHCI_COMMAND_TABLE);

    /*
     * Each port block starts on a 1 KB boundary for PxCLB. The received-FIS
     * area remains 256-byte aligned, and the internal recovery table is
     * explicitly rounded to AHCI's 128-byte command-table alignment.
     */
    nonCachedExtensionSize = ROUND_UP(nonCachedExtensionSize, 1024);

    AdapterExtension->NonCachedExtension = StorPortGetUncachedExtension(AdapterExtension,
                                                                        ConfigInfo,
                                                                        nonCachedExtensionSize * portCount);

    if (AdapterExtension->NonCachedExtension == NULL)
    {
        AhciDebugPrint("\tadapterExtension->NonCachedExtension == NULL\n");
        return FALSE;
    }

    nonCachedExtension = AdapterExtension->NonCachedExtension;
    AhciZeroMemory(nonCachedExtension, nonCachedExtensionSize * portCount);

    for (index = 0; index < portCount; index++)
    {
        PortExtension = &AdapterExtension->PortExtension[index];

        PortExtension->DeviceParams.IsActive = FALSE;
        if ((AdapterExtension->PortImplemented & (1 << index)) != 0)
        {
            PortExtension->PortNumber = index;
            PortExtension->DeviceParams.IsActive = TRUE;
            PortExtension->AdapterExtension = AdapterExtension;
            PortExtension->CommandList = (PAHCI_COMMAND_HEADER)nonCachedExtension;
            PortExtension->ReceivedFIS = (PAHCI_RECEIVED_FIS)(nonCachedExtension + receivedFisOffset);
            PortExtension->IdentifyDeviceData = (PIDENTIFY_DEVICE_DATA)(nonCachedExtension + identifyDataOffset);
            PortExtension->NcqErrorLog = (PGP_LOG_NCQ_COMMAND_ERROR)(nonCachedExtension + ncqErrorLogOffset);
            PortExtension->NcqErrorCommandTable = (PAHCI_COMMAND_TABLE)(nonCachedExtension + ncqErrorCommandTableOffset);
            /* Until IDENTIFY confirms NCQ support, serialize commands. */
            PortExtension->MaxPortQueueDepth = 1;
            nonCachedExtension += nonCachedExtensionSize;
        }
    }

    return TRUE;
}// -- AhciAllocateResourceForAdapter();

/**
 * @name AhciStartPort
 * @implemented
 *
 * Try to start the port device
 *
 * @param AdapterExtension
 * @param PortExtension
 *
 */
BOOLEAN AhciStartPort(__in PAHCI_PORT_EXTENSION PortExtension)
{
    ULONG index;
    AHCI_PORT_CMD cmd;
    AHCI_TASK_FILE_DATA tfd;
    AHCI_INTERRUPT_ENABLE ie;
    AHCI_SERIAL_ATA_STATUS ssts;
    AHCI_SERIAL_ATA_CONTROL sctl;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AhciDebugPrint("AhciStartPort()\n");

    AdapterExtension = PortExtension->AdapterExtension;
    cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);

    if ((cmd.FR == 1) && (cmd.CR == 1) && (cmd.FRE == 1) && (cmd.ST == 1))
    {
        // Already Running
        return TRUE;
    }

    /* Spin the device up. On controllers that support staggered spin-up
     * (CAP.SSS) PxCMD.SUD is the trigger; elsewhere it is read-only and the
     * write is harmless. Also assert POD for cold-presence-capable ports. */
    cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
    cmd.SUD = 1;
    if (cmd.CPD) cmd.POD = 1;
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);

    /*
     * DET==0 is the normal state of an empty implemented port and DET==4 is
     * an offline PHY. Neither state indicates a device waiting for COMRESET.
     * A real presence transition will raise PCS/PRCS and can be enumerated by
     * the hot-plug path when that support is added.
     */
    ssts.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SSTS);
    if ((ssts.DET == 0) || (ssts.DET == 4))
    {
        return FALSE;
    }

    /* Give the PHY a moment to establish communication before deciding a
     * COMRESET is required; firmware has usually already done this. */
    for (index = 0; index < 100; index++)
    {
        ssts.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SSTS);
        if (ssts.DET == 0x3) break;
        if ((ssts.DET == 0) || (ssts.DET == 4)) return FALSE;
        StorPortStallExecution(10000);
    }

    if (ssts.DET != 0x3)
    {
        if (ssts.DET != 0x1)
        {
            return FALSE;
        }

        AhciDebugPrint("\tPort %u: DET=%x after spin-up, issuing COMRESET\n", PortExtension->PortNumber, ssts.DET);

        // section 10.4.2
        // Software causes a port reset (COMRESET) by writing 1h to the PxSCTL.DET field to invoke a
        // COMRESET on the interface and start a re-establishment of Phy layer communications. Software shall
        // wait at least 1 millisecond before clearing PxSCTL.DET to 0h; this ensures that at least one COMRESET
        // signal is sent over the interface. After clearing PxSCTL.DET to 0h, software should wait for
        // communication to be re-established as indicated by PxSSTS.DET being set to 3h. Then software should
        // write all 1s to the PxSERR register to clear any bits that were set as part of the port reset.

        sctl.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL);
        sctl.DET = 1;
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL, sctl.Status);

        /* At least 1 ms of COMRESET; be generous. */
        StorPortStallExecution(10000);

        sctl.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL);
        sctl.DET = 0;
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL, sctl.Status);

        /* Wait for the link to come up (DET==3), up to ~1 second. A device that
         * is merely present but not communicating reports DET==1, so waiting on
         * "DET != 0" (as this code used to) returns before the link is usable. */
        for (index = 0; index < 100; index++)
        {
            StorPortStallExecution(10000);
            ssts.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SSTS);
            if (ssts.DET == 0x3) break;
        }
    }

    /* Clear any error latched by the reset before going further. */
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SERR, (ULONG)~0);

    ssts.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SSTS);
    switch (ssts.DET)
    {
        case 0x3:
            {

                /* Enable FIS receive, then poll PxCMD.FR with a fresh read.
                 * The old loop tested the value it had just written, so FR was
                 * always stale and the check was meaningless. */
                cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
                cmd.FRE = 1;
                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);

                for (index = 0; index < 500; index++)
                {
                    cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
                    if (cmd.FR == 1) break;
                    StorPortStallExecution(1000);
                }

                if (cmd.FR != 1)
                {
                    // failed to start FIS DMA engine
                    // it can crash the driver later
                    // so better to turn this port off
                    AhciDebugPrint("\tPort %u: FIS receive engine did not start (CMD=%08x)\n", PortExtension->PortNumber, cmd.Status);
                    return FALSE;
                }

                /* Wait for the device to finish its reset/spin-up sequence.
                 * PxCMD.ST must not be set while the task file reports BSY or
                 * DRQ, and a spinning disk can hold BSY for several seconds
                 * after COMRESET. Previously this condition was only logged and
                 * the port was started anyway. */
                for (index = 0; index < 3000; index++)
                {
                    tfd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->TFD);
                    if ((tfd.STS.BSY == 0) && (tfd.STS.DRQ == 0)) break;
                    StorPortStallExecution(10000);
                }

                if ((tfd.STS.BSY) || (tfd.STS.DRQ))
                {
                    AhciDebugPrint("\tPort %u: device still busy after 30s (TFD=%08x)\n", PortExtension->PortNumber, tfd.Status);
                    return FALSE;
                }

                AhciDebugPrint("\tPort %u: link up, device ready (SIG=%08x)\n", PortExtension->PortNumber, StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SIG));

                // clear pending interrupts
                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SERR, (ULONG)~0);
                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IS, (ULONG)~0);
                StorPortWriteRegisterUlong(AdapterExtension, AdapterExtension->IS, (1 << PortExtension->PortNumber));

                // set IE
                ie.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->IE);
                /* Device to Host Register FIS Interrupt Enable */
                ie.DHRE = 1;
                /* PIO Setup FIS Interrupt Enable */
                ie.PSE = 1;
                /* DMA Setup FIS Interrupt Enable  */
                ie.DSE = 1;
                /* Set Device Bits FIS Interrupt Enable */
                ie.SDBE = 1;
                /* Unknown FIS Interrupt Enable */
                ie.UFE = 0;
                /* Descriptor Processed Interrupt Enable */
                ie.DPE = 0;
                /* Port Change Interrupt Enable */
                ie.PCE = 1;
                /* Device Mechanical Presence Enable */
                ie.DMPE = 0;
                /* PhyRdy Change Interrupt Enable */
                ie.PRCE = 1;
                /* Incorrect Port Multiplier Enable */
                ie.IPME = 0;
                /* Overflow Enable */
                ie.OFE = 1;
                /* Interface Non-fatal Error Enable */
                ie.INFE = 1;
                /* Interface Fatal Error Enable */
                ie.IFE = 1;
                /* Host Bus Data Error Enable */
                ie.HBDE = 1;
                /* Host Bus Fatal Error Enable */
                ie.HBFE = 1;
                /* Task File Error Enable */
                ie.TFEE = 1;

                cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
                /* Cold Presence Detect Enable */
                if (cmd.CPD) // does it support CPD?
                {
                    // disable it for now
                    ie.CPDE = 0;
                }

                // should I replace this to single line?
                // by directly setting ie.Status?

                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IE, ie.Status);

                cmd.ST = 1;
                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);
                cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);

                if (cmd.ST != 1)
                {
                    AhciDebugPrint("\tFailed to start Port\n");
                    return FALSE;
                }

                return TRUE;
            }
        default:
            // unhandled case
            AhciDebugPrint("\tDET == %x Unsupported\n", ssts.DET);
            return FALSE;
    }
}// -- AhciStartPort();

/**
 * @name AhciCommandCompletionDpcRoutine
 * @implemented
 *
 * Handles Completed Commands
 *
 * @param Dpc
 * @param AdapterExtension
 * @param SystemArgument1
 * @param SystemArgument2
 */
VOID AhciCommandCompletionDpcRoutine(__in PSTOR_DPC Dpc, __in PVOID HwDeviceExtension, __in PVOID SystemArgument1, __in PVOID SystemArgument2)
{
    BOOLEAN CommandSucceeded;
    PSCSI_REQUEST_BLOCK Srb;
    PAHCI_SRB_EXTENSION SrbExtension;
    STOR_LOCK_HANDLE lockhandle = {0};
    PAHCI_COMPLETION_ROUTINE CompletionRoutine;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    PAHCI_PORT_EXTENSION PortExtension;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument2);

    DPRINT("AhciCommandCompletionDpcRoutine()\n");

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)HwDeviceExtension;
    PortExtension = (PAHCI_PORT_EXTENSION)SystemArgument1;

    /*
     * One interrupt can retire several NCQ tags. KeInsertQueueDpc coalesces
     * repeated requests for the same DPC object, so one invocation must drain
     * the whole completion queue rather than complete only its first SRB.
     */
    for (;;)
    {
        StorPortAcquireSpinLock(AdapterExtension, InterruptLock, NULL, &lockhandle);
        Srb = RemoveQueue(&PortExtension->CompletionQueue);
        StorPortReleaseSpinLock(AdapterExtension, &lockhandle);

        if (Srb == NULL)
        {
            break;
        }

        CommandSucceeded = (Srb->SrbStatus == SRB_STATUS_PENDING);
        if (CommandSucceeded)
        {
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
        }

        SrbExtension = GetSrbExtension(Srb);
        NT_ASSERT(SrbExtension != NULL);

        CompletionRoutine = SrbExtension->CompletionRoutine;

        // now it's completion routine responsibility to set SrbStatus
        if (CommandSucceeded && (CompletionRoutine != NULL))
        {
            CompletionRoutine(PortExtension, Srb);
        }

        StorPortNotification(RequestComplete, AdapterExtension, Srb);
    }

    return;
}// -- AhciCommandCompletionDpcRoutine();

/**
 * @name AhciHwPassiveInitialize
 * @implemented
 *
 * initializes the HBA and finds all devices that are of interest to the miniport driver. (at PASSIVE LEVEL)
 *
 * @param adapterExtension
 *
 * @return
 * return TRUE if intialization was successful
 */
BOOLEAN AhciHwPassiveInitialize(__in PVOID DeviceExtension)
{
    ULONG index;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    PAHCI_PORT_EXTENSION PortExtension;

    AhciDebugPrint("AhciHwPassiveInitialize()\n");

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;

    for (index = 0; index < AdapterExtension->PortCount; index++)
    {
        if ((AdapterExtension->PortImplemented & (0x1 << index)) != 0)
        {
            PortExtension = &AdapterExtension->PortExtension[index];
            StorPortInitializeDpc(AdapterExtension, &PortExtension->CommandCompletion, AhciCommandCompletionDpcRoutine);
            StorPortInitializeDpc(AdapterExtension, &PortExtension->ErrorRecovery, AhciErrorRecoveryDpcRoutine);
            PortExtension->DeviceParams.IsActive = AhciStartPort(PortExtension);
        }
    }

    return TRUE;
}// -- AhciHwPassiveInitialize();

/**
 * @name AhciHwInitialize
 * @implemented
 *
 * initializes the HBA and finds all devices that are of interest to the miniport driver.
 *
 * @param adapterExtension
 *
 * @return
 * return TRUE if intialization was successful
 */
BOOLEAN NTAPI AhciHwInitialize(__in PVOID DeviceExtension)
{
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AhciDebugPrint("AhciHwInitialize()\n");

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;
    AdapterExtension->StateFlags.MessagePerPort = FALSE;

    StorPortEnablePassiveInitialization(AdapterExtension, AhciHwPassiveInitialize);

    return TRUE;
}// -- AhciHwInitialize();

/**
 * @name AhciCompleteIssuedSrb
 * @implemented
 *
 * Complete issued Srbs
 *
 * @param PortExtension
 *
 */
VOID AhciCompleteIssuedSrb(__in PAHCI_PORT_EXTENSION PortExtension, __in ULONG CommandsToComplete, __in BOOLEAN Failed)
{
    ULONG NCS, i;
    BOOLEAN CompletionQueued;
    PSCSI_REQUEST_BLOCK Srb;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    DPRINT("AhciCompleteIssuedSrb()\n");

    NT_ASSERT(CommandsToComplete != 0);

    DPRINT("\tCompleted Commands: %x Failed: %u\n", CommandsToComplete, Failed);

    AdapterExtension = PortExtension->AdapterExtension;
    NCS = AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP);
    CompletionQueued = FALSE;

    for (i = 0; i < NCS; i++)
    {
        if (((1UL << i) & CommandsToComplete) != 0)
        {
            Srb = PortExtension->Slot[i];

            if (Srb == NULL)
            {
                continue;
            }

            /* Release the slot before completing, otherwise the stale pointer
             * is handed out again the next time this slot is reused. */
            PortExtension->Slot[i] = NULL;

            SrbExtension = GetSrbExtension(Srb);
            NT_ASSERT(SrbExtension != NULL);

            if (Failed)
            {
                if (SrbExtension->AutosenseActive)
                {
                    /* The original command already failed and its follow-up
                     * REQUEST SENSE failed as well. */
                    SrbExtension->AutosenseActive = FALSE;
                    Srb->SrbStatus = SRB_STATUS_REQUEST_SENSE_FAILED;
                    Srb->ScsiStatus = SCSISTAT_GOOD;
                }
                else
                {
                    /* The HBA stopped on an error and discarded the command
                     * list, so nothing in flight actually ran. */
                    Srb->SrbStatus = SRB_STATUS_ERROR;
                    Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
                }
            }

            /*
             * Complete from the miniport DPC, not directly at the device's
             * interrupt IRQL. This also batches simultaneous NCQ completions.
             */
            if (AddQueue(&PortExtension->CompletionQueue, Srb))
            {
                CompletionQueued = TRUE;
            }
            else
            {
                AhciDebugPrint("\tCompletion queue full on port %u\n", PortExtension->PortNumber);
                Srb->SrbStatus = SRB_STATUS_BUSY;
                StorPortNotification(RequestComplete, AdapterExtension, Srb);
            }
        }
    }

    if (CompletionQueued)
    {
        StorPortIssueDpc(AdapterExtension, &PortExtension->CommandCompletion, PortExtension, NULL);
    }

    return;
}// -- AhciCompleteIssuedSrb();

#define AHCI_RECOVERY_POLL_INTERVAL_US       10000
#define AHCI_RECOVERY_ENGINE_TIMEOUT_TICKS   50
#define AHCI_RECOVERY_LOG_TIMEOUT_TICKS      300
#define AHCI_RECOVERY_LINK_TIMEOUT_TICKS     100
#define AHCI_RECOVERY_READY_TIMEOUT_TICKS    3000

static
VOID
AhciResetNcqRecoveryContext(
    __in PAHCI_PORT_EXTENSION PortExtension)
{
    PortExtension->RecoveryNcqActiveSlots = 0;
    PortExtension->RecoveryInternalSlot = 0;
    PortExtension->RecoveryIsNcqError = FALSE;
    PortExtension->RecoveryNcqLogError = FALSE;
}

static
VOID
AhciClearIssuedCommands(
    __in PAHCI_PORT_EXTENSION PortExtension)
{
    ULONG outstanding;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AdapterExtension = PortExtension->AdapterExtension;
    outstanding = PortExtension->CommandIssuedSlots | PortExtension->QueueSlots;

    if (outstanding != 0)
    {
        AhciCompleteIssuedSrb(PortExtension, outstanding, TRUE);
    }

    PortExtension->CommandIssuedSlots = 0;
    PortExtension->QueueSlots = 0;
    PortExtension->NcqIssuedSlots = 0;
    PortExtension->NcqQueueSlots = 0;
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SACT, 0);
    AhciResetNcqRecoveryContext(PortExtension);
}

static
VOID
AhciFailQueuedRequests(
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in UCHAR SrbStatus)
{
    BOOLEAN CompletionQueued;
    PSCSI_REQUEST_BLOCK Srb;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AdapterExtension = PortExtension->AdapterExtension;
    CompletionQueued = FALSE;

    while ((Srb = RemoveQueue(&PortExtension->SrbQueue)) != NULL)
    {
        Srb->SrbStatus = SrbStatus;
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;

        if (AddQueue(&PortExtension->CompletionQueue, Srb))
        {
            CompletionQueued = TRUE;
        }
        else
        {
            StorPortNotification(RequestComplete, AdapterExtension, Srb);
        }
    }

    if (CompletionQueued)
    {
        StorPortIssueDpc(AdapterExtension, &PortExtension->CommandCompletion, PortExtension, NULL);
    }
}

static
VOID
AhciSetFixedSenseData(
    __in PSCSI_REQUEST_BLOCK Srb,
    __in UCHAR SenseKey,
    __in UCHAR AdditionalSenseCode,
    __in UCHAR AdditionalSenseCodeQualifier,
    __in ULONGLONG Lba)
{
    ULONG CopyLength;
    SENSE_DATA SenseData;

    Srb->SrbStatus = SRB_STATUS_ERROR;
    Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;

    if ((Srb->SenseInfoBuffer == NULL) || (Srb->SenseInfoBufferLength == 0))
    {
        return;
    }

    AhciZeroMemory((PCHAR)&SenseData, sizeof(SenseData));
    SenseData.Valid = 1;
    SenseData.ErrorCode = AHCI_SENSE_ERRORCODE_FIXED_CURRENT;
    SenseData.SenseKey = SenseKey;
    SenseData.Information[0] = (UCHAR)(Lba >> 24);
    SenseData.Information[1] = (UCHAR)(Lba >> 16);
    SenseData.Information[2] = (UCHAR)(Lba >> 8);
    SenseData.Information[3] = (UCHAR)Lba;
    SenseData.AdditionalSenseLength =
        sizeof(SenseData) - RTL_SIZEOF_THROUGH_FIELD(SENSE_DATA, AdditionalSenseLength);
    SenseData.AdditionalSenseCode = AdditionalSenseCode;
    SenseData.AdditionalSenseCodeQualifier = AdditionalSenseCodeQualifier;

    CopyLength = Srb->SenseInfoBufferLength;
    if (CopyLength > sizeof(SenseData))
    {
        CopyLength = sizeof(SenseData);
    }

    StorPortCopyMemory(Srb->SenseInfoBuffer, &SenseData, CopyLength);
    Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
}

static
VOID
AhciSetNcqErrorSense(
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR SenseKey, Asc, Ascq;
    ULONGLONG Lba;
    PGP_LOG_NCQ_COMMAND_ERROR LogPage;
    PAHCI_SRB_EXTENSION SrbExtension;

    LogPage = PortExtension->NcqErrorLog;
    SrbExtension = GetSrbExtension(Srb);
    NT_ASSERT(SrbExtension != NULL);

    Lba = ((ULONGLONG)LogPage->LBA7_0) |
          ((ULONGLONG)LogPage->LBA15_8 << 8) |
          ((ULONGLONG)LogPage->LBA23_16 << 16) |
          ((ULONGLONG)LogPage->LBA31_24 << 24) |
          ((ULONGLONG)LogPage->LBA39_32 << 32) |
          ((ULONGLONG)LogPage->LBA47_40 << 40);

    if ((LogPage->Status & IDE_STATUS_DEVICE_FAULT) &&
        (LogPage->SenseKey != 0) &&
        AtaDevHasNcqAutosense(PortExtension->IdentifyDeviceData))
    {
        PUCHAR LogBytes = (PUCHAR)LogPage;

        SenseKey = LogPage->SenseKey;
        Asc = LogPage->ASC;
        Ascq = LogPage->ASCQ;
        Lba = ((ULONGLONG)LogBytes[17]) |
              ((ULONGLONG)LogBytes[18] << 8) |
              ((ULONGLONG)LogBytes[19] << 16) |
              ((ULONGLONG)LogBytes[20] << 24) |
              ((ULONGLONG)LogBytes[21] << 32) |
              ((ULONGLONG)LogBytes[22] << 40);
    }
    else if (LogPage->Status & IDE_STATUS_DEVICE_FAULT)
    {
        SenseKey = SCSI_SENSE_HARDWARE_ERROR;
        Asc = AHCI_ADSENSE_INTERNAL_TARGET_FAILURE;
        Ascq = 0;
    }
    else if (LogPage->Error & IDE_ERROR_DATA_ERROR)
    {
        if (SrbExtension->Flags & ATA_FLAGS_DATA_OUT)
        {
            SenseKey = SCSI_SENSE_DATA_PROTECT;
            Asc = AHCI_ADSENSE_WRITE_PROTECT;
            Ascq = 0;
        }
        else
        {
            SenseKey = SCSI_SENSE_MEDIUM_ERROR;
            Asc = AHCI_ADSENSE_UNRECOVERED_ERROR;
            Ascq = 0;
        }
    }
    else if (LogPage->Error & IDE_ERROR_ID_NOT_FOUND)
    {
        SenseKey = SCSI_SENSE_ILLEGAL_REQUEST;
        Asc = AHCI_ADSENSE_ILLEGAL_BLOCK;
        Ascq = 0;
    }
    else if (LogPage->Error & IDE_ERROR_CRC_ERROR)
    {
        SenseKey = SCSI_SENSE_HARDWARE_ERROR;
        Asc = AHCI_ADSENSE_LUN_COMMUNICATION;
        Ascq = AHCI_SENSEQ_COMM_CRC_ERROR;
    }
    else
    {
        SenseKey = SCSI_SENSE_ABORTED_COMMAND;
        Asc = AHCI_ADSENSE_NO_SENSE;
        Ascq = 0;
    }

    AhciSetFixedSenseData(Srb, SenseKey, Asc, Ascq, Lba);
}

static
BOOLEAN
AhciIssueNcqErrorLog(
    __in PAHCI_PORT_EXTENSION PortExtension)
{
    ULONG NCS, SlotIndex;
    AHCI_PORT_CMD Cmd;
    PAHCI_PRDT Prdt;
    PAHCI_COMMAND_HEADER CommandHeader;
    PAHCI_COMMAND_TABLE CommandTable;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AdapterExtension = PortExtension->AdapterExtension;
    CommandTable = PortExtension->NcqErrorCommandTable;
    if ((CommandTable == NULL) || (PortExtension->NcqErrorLog == NULL))
    {
        return FALSE;
    }

    NCS = AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP);
    if (NCS == 0)
    {
        return FALSE;
    }

    /*
     * Clearing PxCMD.ST reset every PxCI/PxSACT bit, so slot zero is free
     * even when it still carries the software pointer for an aborted SRB.
     * Recovery owns the command header until the internal command finishes.
     */
    SlotIndex = 0;
    CommandHeader = &PortExtension->CommandList[SlotIndex];

    AhciZeroMemory((PCHAR)PortExtension->NcqErrorLog, sizeof(*PortExtension->NcqErrorLog));
    AhciZeroMemory((PCHAR)CommandTable, sizeof(*CommandTable));
    AhciZeroMemory((PCHAR)CommandHeader, sizeof(*CommandHeader));

    CommandTable->CFIS[AHCI_ATA_CFIS_FisType] = FIS_TYPE_REG_H2D;
    CommandTable->CFIS[AHCI_ATA_CFIS_PMPort_C] = (1 << 7);
    CommandTable->CFIS[AHCI_ATA_CFIS_CommandReg] = IDE_COMMAND_READ_LOG_EXT;
    CommandTable->CFIS[AHCI_ATA_CFIS_LBA0] = IDE_GP_LOG_NCQ_COMMAND_ERROR_ADDRESS;
    CommandTable->CFIS[AHCI_ATA_CFIS_SectorCountLow] = 1;

    Prdt = &CommandTable->PRDT[0];
    Prdt->DBA = PortExtension->NcqErrorLogPhysicalAddress.LowPart;
    if (IsAdapterCAPS64(AdapterExtension->CAP))
    {
        Prdt->DBAU = PortExtension->NcqErrorLogPhysicalAddress.HighPart;
    }
    Prdt->DBC = IDE_GP_LOG_SECTOR_SIZE - 1;

    CommandHeader->DI.CFL = 5;
    CommandHeader->DI.PRDTL = 1;
    CommandHeader->CTBA = PortExtension->NcqErrorCommandTablePhysicalAddress.LowPart;
    if (IsAdapterCAPS64(AdapterExtension->CAP))
    {
        CommandHeader->CTBA_U = PortExtension->NcqErrorCommandTablePhysicalAddress.HighPart;
    }

    Cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
    Cmd.ST = 1;
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, Cmd.Status);
    Cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
    if (Cmd.ST == 0)
    {
        return FALSE;
    }

    PortExtension->RecoveryInternalSlot = SlotIndex;
    PortExtension->RecoveryNcqLogError = FALSE;
    PortExtension->RecoveryTicks = 0;
    PortExtension->RecoveryState = AhciRecoveryWaitNcqLog;

    KeMemoryBarrier();
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CI, 1UL << SlotIndex);
    return TRUE;
}

static
BOOLEAN
AhciCompleteNcqErrorRecovery(
    __in PAHCI_PORT_EXTENSION PortExtension)
{
    UCHAR Checksum;
    ULONG i, NCS, FailedSlot, FailedMask;
    ULONG ActiveSlots, IssuedSlots, QueuedSlots, RetrySlots, SuccessSlots;
    PSCSI_REQUEST_BLOCK Srb;
    PGP_LOG_NCQ_COMMAND_ERROR LogPage;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AdapterExtension = PortExtension->AdapterExtension;
    LogPage = PortExtension->NcqErrorLog;
    IssuedSlots = PortExtension->NcqIssuedSlots;
    QueuedSlots = PortExtension->QueueSlots;
    NCS = AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP);

    /*
     * Page 10h identifies one failed command from an NCQ batch. Do not
     * interpret it if a non-NCQ command was mixed into either software mask;
     * the conservative reset path can safely complete that inconsistent state.
     */
    if ((PortExtension->CommandIssuedSlots != IssuedSlots) ||
        (PortExtension->NcqQueueSlots != QueuedSlots))
    {
        return FALSE;
    }

    Checksum = 0;
    for (i = 0; i < IDE_GP_LOG_SECTOR_SIZE; i++)
    {
        Checksum = (UCHAR)(Checksum + ((PUCHAR)LogPage)[i]);
    }

    FailedSlot = LogPage->NcqTag;
    if ((Checksum != 0) ||
        LogPage->NonQueuedCmd ||
        (FailedSlot >= NCS) ||
        ((IssuedSlots & (1UL << FailedSlot)) == 0) ||
        (PortExtension->Slot[FailedSlot] == NULL))
    {
        return FALSE;
    }

    FailedMask = 1UL << FailedSlot;
    ActiveSlots = PortExtension->RecoveryNcqActiveSlots & IssuedSlots;
    RetrySlots = (ActiveSlots & ~FailedMask) | QueuedSlots;
    SuccessSlots = IssuedSlots & ~(ActiveSlots | FailedMask);

    for (i = 0; i < NCS; i++)
    {
        ULONG SlotMask = 1UL << i;

        Srb = PortExtension->Slot[i];
        if (Srb == NULL)
        {
            continue;
        }

        if (SlotMask & FailedMask)
        {
            AhciSetNcqErrorSense(PortExtension, Srb);
        }
        else if (SlotMask & RetrySlots)
        {
            Srb->SrbStatus = SRB_STATUS_BUSY;
            Srb->ScsiStatus = SCSISTAT_BUSY;
        }
        else if (SlotMask & SuccessSlots)
        {
            Srb->SrbStatus = SRB_STATUS_PENDING;
            Srb->ScsiStatus = SCSISTAT_GOOD;
        }
    }

    if (PortExtension->ErrorLogCount < 4)
    {
        AhciDebugPrint("\tPort %u NCQ error: tag=%u status=%02x error=%02x retry=%08x\n",
                       PortExtension->PortNumber,
                       FailedSlot,
                       LogPage->Status,
                       LogPage->Error,
                       RetrySlots);
    }
    else if (PortExtension->ErrorLogCount == 4)
    {
        AhciDebugPrint("\tPort %u: suppressing repeated NCQ error diagnostics\n",
                       PortExtension->PortNumber);
    }
    PortExtension->ErrorLogCount++;

    PortExtension->CommandIssuedSlots = 0;
    PortExtension->QueueSlots = 0;
    PortExtension->NcqIssuedSlots = 0;
    PortExtension->NcqQueueSlots = 0;
    PortExtension->RecoveryState = AhciRecoveryIdle;
    PortExtension->RecoveryTicks = 0;
    PortExtension->RecoveryIsCommandError = FALSE;
    AhciResetNcqRecoveryContext(PortExtension);

    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SACT, 0);
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SERR, (ULONG)~0);
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IS, (ULONG)~0);

    AhciCompleteIssuedSrb(PortExtension, IssuedSlots | QueuedSlots, FALSE);
    AhciSchedulePort(PortExtension);
    AhciActivatePort(PortExtension);
    return TRUE;
}

static
VOID
AhciFallbackNcqErrorRecovery(
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in PCSTR Reason)
{
    AHCI_PORT_CMD Cmd;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AdapterExtension = PortExtension->AdapterExtension;

    if (PortExtension->ErrorLogCount < 4)
    {
        AhciDebugPrint("\tPort %u NCQ log recovery failed: %s\n",
                       PortExtension->PortNumber,
                       Reason);
    }
    else if (PortExtension->ErrorLogCount == 4)
    {
        AhciDebugPrint("\tPort %u: suppressing repeated NCQ recovery diagnostics\n",
                       PortExtension->PortNumber);
    }
    PortExtension->ErrorLogCount++;

    Cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
    Cmd.ST = 0;
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, Cmd.Status);

    PortExtension->RecoveryTicks = 0;
    PortExtension->RecoveryIsCommandError = FALSE;
    PortExtension->RecoveryIsNcqError = FALSE;
    PortExtension->RecoveryNcqLogError = FALSE;
    PortExtension->RecoveryState = AhciRecoveryWaitCommandEngine;
}

static
VOID
AhciRecoveryFailed(
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in PCSTR Reason)
{
    AhciDebugPrint("\tPort %u recovery failed: %s\n", PortExtension->PortNumber, Reason);

    AhciClearIssuedCommands(PortExtension);
    AhciFailQueuedRequests(PortExtension, SRB_STATUS_NO_DEVICE);

    PortExtension->DeviceParams.IsActive = FALSE;
    PortExtension->RecoveryState = AhciRecoveryIdle;
    PortExtension->RecoveryTicks = 0;
    PortExtension->RecoveryIsCommandError = FALSE;
}

static
VOID
AhciRequestSenseCompletion(
    __in PVOID _PortExtension,
    __in PVOID _Srb)
{
    PSCSI_REQUEST_BLOCK Srb;
    PAHCI_SRB_EXTENSION SrbExtension;

    UNREFERENCED_PARAMETER(_PortExtension);

    Srb = (PSCSI_REQUEST_BLOCK)_Srb;
    SrbExtension = GetSrbExtension(Srb);
    NT_ASSERT(SrbExtension != NULL);
    NT_ASSERT(SrbExtension->AutosenseActive);

    SrbExtension->AutosenseActive = FALSE;
    Srb->SrbStatus = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
    Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
}

static
BOOLEAN
AhciPrepareRequestSense(
    __in PAHCI_PORT_EXTENSION PortExtension,
    __in PSCSI_REQUEST_BLOCK Srb)
{
    ULONG Length, MappedLength, Remaining;
    PUCHAR Buffer;
    STOR_PHYSICAL_ADDRESS PhysicalAddress;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    if ((Srb->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE) ||
        (Srb->SenseInfoBuffer == NULL) ||
        (Srb->SenseInfoBufferLength == 0))
    {
        return FALSE;
    }

    SrbExtension = GetSrbExtension(Srb);
    NT_ASSERT(SrbExtension != NULL);
    if (SrbExtension->AutosenseActive)
    {
        return FALSE;
    }

    AdapterExtension = PortExtension->AdapterExtension;
    Buffer = (PUCHAR)Srb->SenseInfoBuffer;
    Remaining = Srb->SenseInfoBufferLength;

    AhciZeroMemory((PCHAR)Buffer, Remaining);
    SrbExtension->Sgl.NumberOfElements = 0;
    SrbExtension->Sgl.Reserved = 0;

    while (Remaining != 0)
    {
        if (SrbExtension->Sgl.NumberOfElements == MAXIMUM_AHCI_PRDT_ENTRIES)
        {
            return FALSE;
        }

        PhysicalAddress = StorPortGetPhysicalAddress(AdapterExtension,
                                                     Srb,
                                                     Buffer,
                                                     &MappedLength);
        if (MappedLength == 0)
        {
            return FALSE;
        }

        Length = (MappedLength < Remaining) ? MappedLength : Remaining;
        SrbExtension->Sgl.List[SrbExtension->Sgl.NumberOfElements].PhysicalAddress = PhysicalAddress;
        SrbExtension->Sgl.List[SrbExtension->Sgl.NumberOfElements].Length = Length;
        SrbExtension->Sgl.NumberOfElements++;

        Buffer += Length;
        Remaining -= Length;
    }

    SrbExtension->AtaFunction = ATA_FUNCTION_ATAPI_COMMAND;
    SrbExtension->Flags = ATA_FLAGS_DATA_IN;
    SrbExtension->CommandReg = IDE_COMMAND_ATAPI_PACKET;
    SrbExtension->FeaturesLow = 0;
    SrbExtension->FeaturesHigh = 0;
    SrbExtension->LBA0 = 0;
    SrbExtension->LBA1 = Srb->SenseInfoBufferLength;
    SrbExtension->LBA2 = 0;
    SrbExtension->LBA3 = 0;
    SrbExtension->LBA4 = 0;
    SrbExtension->LBA5 = 0;
    SrbExtension->Device = 0;
    SrbExtension->SectorCountLow = 0;
    SrbExtension->SectorCountHigh = 0;
    SrbExtension->pSgl = &SrbExtension->Sgl;
    SrbExtension->CompletionRoutine = AhciRequestSenseCompletion;
    SrbExtension->AutosenseActive = TRUE;

    return TRUE;
}

static
BOOLEAN
AhciBeginRequestSense(
    __in PAHCI_PORT_EXTENSION PortExtension)
{
    ULONG SlotIndex, Outstanding, NCS;
    PSCSI_REQUEST_BLOCK Srb;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AdapterExtension = PortExtension->AdapterExtension;
    Outstanding = PortExtension->CommandIssuedSlots;
    if ((Outstanding == 0) || ((Outstanding & (Outstanding - 1)) != 0))
    {
        return FALSE;
    }

    NCS = AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP);
    for (SlotIndex = 0; SlotIndex < NCS; SlotIndex++)
    {
        if (Outstanding & (1UL << SlotIndex))
        {
            break;
        }
    }

    if (SlotIndex == NCS)
    {
        return FALSE;
    }

    Srb = PortExtension->Slot[SlotIndex];
    if (Srb == NULL)
    {
        return FALSE;
    }

    SrbExtension = GetSrbExtension(Srb);
    NT_ASSERT(SrbExtension != NULL);
    if (SrbExtension->AutosenseActive ||
        (PortExtension->DeviceParams.DeviceType != AHCI_DEVICE_TYPE_ATAPI) ||
        !AhciPrepareRequestSense(PortExtension, Srb))
    {
        return FALSE;
    }

    PortExtension->Slot[SlotIndex] = NULL;
    PortExtension->CommandIssuedSlots &= ~(1UL << SlotIndex);
    PortExtension->NcqIssuedSlots &= ~(1UL << SlotIndex);
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SACT, 0);

    AhciProcessSrb(PortExtension, Srb, SlotIndex);
    return TRUE;
}

static
BOOLEAN
AhciPortRecoveryStep(
    __in PAHCI_PORT_EXTENSION PortExtension)
{
    BOOLEAN HadNcq;
    AHCI_PORT_CMD cmd;
    AHCI_TASK_FILE_DATA tfd;
    AHCI_SERIAL_ATA_STATUS ssts;
    AHCI_SERIAL_ATA_CONTROL sctl;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    AdapterExtension = PortExtension->AdapterExtension;

    switch ((AHCI_PORT_RECOVERY_STATE)PortExtension->RecoveryState)
    {
        case AhciRecoveryWaitCommandEngine:
            cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
            if (cmd.CR != 0)
            {
                if (++PortExtension->RecoveryTicks < AHCI_RECOVERY_ENGINE_TIMEOUT_TICKS)
                {
                    return TRUE;
                }

                AhciRecoveryFailed(PortExtension, "command engine did not stop");
                return FALSE;
            }

            HadNcq = (PortExtension->NcqIssuedSlots != 0);

            StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SERR, (ULONG)~0);
            StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IS, (ULONG)~0);

            tfd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->TFD);
            if (PortExtension->RecoveryIsNcqError &&
                HadNcq &&
                !tfd.STS.BSY &&
                !tfd.STS.DRQ)
            {
                if (AhciIssueNcqErrorLog(PortExtension))
                {
                    return TRUE;
                }

                AhciFallbackNcqErrorRecovery(PortExtension,
                                             "could not issue READ LOG EXT");
                return TRUE;
            }

            if (PortExtension->RecoveryIsCommandError &&
                !HadNcq &&
                !tfd.STS.BSY &&
                !tfd.STS.DRQ &&
                AhciBeginRequestSense(PortExtension))
            {
                cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
                cmd.ST = 1;
                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);

                PortExtension->RecoveryState = AhciRecoveryIdle;
                PortExtension->RecoveryTicks = 0;
                PortExtension->RecoveryIsCommandError = FALSE;
                AhciResetNcqRecoveryContext(PortExtension);
                AhciActivatePort(PortExtension);
                return FALSE;
            }

            AhciClearIssuedCommands(PortExtension);
            PortExtension->RecoveryIsCommandError = FALSE;

            if (HadNcq || tfd.STS.BSY || tfd.STS.DRQ)
            {
                sctl.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL);
                sctl.DET = 1;
                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL, sctl.Status);

                PortExtension->RecoveryState = AhciRecoveryComresetAsserted;
                PortExtension->RecoveryTicks = 0;
                return TRUE;
            }

            cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
            cmd.ST = 1;
            StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);

            PortExtension->RecoveryState = AhciRecoveryIdle;
            PortExtension->RecoveryTicks = 0;
            PortExtension->RecoveryIsCommandError = FALSE;
            AhciSchedulePort(PortExtension);
            AhciActivatePort(PortExtension);
            return FALSE;

        case AhciRecoveryWaitNcqLog:
        {
            ULONG Ci, PxIs, SlotMask;

            PxIs = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->IS);
            if (PxIs != 0)
            {
                AHCI_INTERRUPT_STATUS InterruptStatus;

                InterruptStatus.Status = PxIs;
                if (InterruptStatus.HBFS ||
                    InterruptStatus.HBDS ||
                    InterruptStatus.IFS ||
                    InterruptStatus.TFES)
                {
                    PortExtension->RecoveryNcqLogError = TRUE;
                }

                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IS, PxIs);
                StorPortWriteRegisterUlong(AdapterExtension,
                                           AdapterExtension->IS,
                                           (1 << PortExtension->PortNumber));
            }

            SlotMask = 1UL << PortExtension->RecoveryInternalSlot;
            Ci = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CI);
            tfd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->TFD);

            if (PortExtension->RecoveryNcqLogError)
            {
                AhciFallbackNcqErrorRecovery(PortExtension,
                                             "READ LOG EXT command failed");
                return TRUE;
            }

            if (Ci & SlotMask)
            {
                if (++PortExtension->RecoveryTicks < AHCI_RECOVERY_LOG_TIMEOUT_TICKS)
                {
                    return TRUE;
                }

                AhciFallbackNcqErrorRecovery(PortExtension,
                                             "READ LOG EXT command timed out");
                return TRUE;
            }

            if (tfd.STS.ERR)
            {
                AhciFallbackNcqErrorRecovery(PortExtension,
                                             "READ LOG EXT command failed");
                return TRUE;
            }

            KeMemoryBarrier();
            if (PortExtension->CommandList[PortExtension->RecoveryInternalSlot].PRDBC <
                IDE_GP_LOG_SECTOR_SIZE)
            {
                AhciFallbackNcqErrorRecovery(PortExtension,
                                             "READ LOG EXT returned a short page");
                return TRUE;
            }

            if (!AhciCompleteNcqErrorRecovery(PortExtension))
            {
                AhciFallbackNcqErrorRecovery(PortExtension,
                                             "invalid READ LOG EXT page 0x10");
                return TRUE;
            }

            return FALSE;
        }

        case AhciRecoveryComresetAsserted:
            /* The timer interval is longer than AHCI's one millisecond minimum. */
            sctl.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL);
            sctl.DET = 0;
            StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SCTL, sctl.Status);

            PortExtension->RecoveryState = AhciRecoveryWaitLink;
            PortExtension->RecoveryTicks = 0;
            return TRUE;

        case AhciRecoveryWaitLink:
            ssts.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SSTS);
            if (ssts.DET != 0x3)
            {
                if (++PortExtension->RecoveryTicks < AHCI_RECOVERY_LINK_TIMEOUT_TICKS)
                {
                    return TRUE;
                }

                AhciRecoveryFailed(PortExtension, "link did not return after COMRESET");
                return FALSE;
            }

            StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SERR, (ULONG)~0);
            PortExtension->RecoveryState = AhciRecoveryWaitReady;
            PortExtension->RecoveryTicks = 0;
            return TRUE;

        case AhciRecoveryWaitReady:
            tfd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->TFD);
            if (tfd.STS.BSY || tfd.STS.DRQ)
            {
                if (++PortExtension->RecoveryTicks < AHCI_RECOVERY_READY_TIMEOUT_TICKS)
                {
                    return TRUE;
                }

                AhciRecoveryFailed(PortExtension, "device stayed busy after COMRESET");
                return FALSE;
            }

            cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
            if (cmd.FRE == 0)
            {
                cmd.FRE = 1;
                StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);
            }

            PortExtension->RecoveryState = AhciRecoveryWaitFisEngine;
            PortExtension->RecoveryTicks = 0;
            return TRUE;

        case AhciRecoveryWaitFisEngine:
            cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
            if (cmd.FR == 0)
            {
                if (++PortExtension->RecoveryTicks < AHCI_RECOVERY_ENGINE_TIMEOUT_TICKS)
                {
                    return TRUE;
                }

                AhciRecoveryFailed(PortExtension, "FIS receive engine did not start");
                return FALSE;
            }

            cmd.ST = 1;
            StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);

            PortExtension->RecoveryState = AhciRecoveryIdle;
            PortExtension->RecoveryTicks = 0;
            PortExtension->RecoveryIsCommandError = FALSE;
            AhciResetNcqRecoveryContext(PortExtension);
            AhciSchedulePort(PortExtension);
            AhciActivatePort(PortExtension);
            return FALSE;

        case AhciRecoveryIdle:
        default:
            return FALSE;
    }
}

VOID NTAPI AhciRecoveryTimer(__in PVOID DeviceExtension)
{
    ULONG index;
    BOOLEAN TimerRequired;
    STOR_LOCK_HANDLE lockhandle = {0};
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    PAHCI_PORT_EXTENSION PortExtension;

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;
    TimerRequired = FALSE;

    StorPortAcquireSpinLock(AdapterExtension, InterruptLock, NULL, &lockhandle);

    for (index = 0; index < AdapterExtension->PortCount; index++)
    {
        PortExtension = &AdapterExtension->PortExtension[index];
        if (PortExtension->RecoveryState != AhciRecoveryIdle)
        {
            if (AhciPortRecoveryStep(PortExtension))
            {
                TimerRequired = TRUE;
            }
        }
    }

    StorPortReleaseSpinLock(AdapterExtension, &lockhandle);

    if (TimerRequired)
    {
        StorPortNotification(RequestTimerCall,
                             AdapterExtension,
                             AhciRecoveryTimer,
                             AHCI_RECOVERY_POLL_INTERVAL_US);
    }
}

VOID
AhciErrorRecoveryDpcRoutine(
    __in PSTOR_DPC Dpc,
    __in PVOID HwDeviceExtension,
    __in PVOID SystemArgument1,
    __in PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    AhciRecoveryTimer(HwDeviceExtension);
}

/**
 * Start AHCI 6.2.2 recovery without polling at interrupt IRQL. Windows
 * storahci likewise leaves its interrupt path after starting a port recovery
 * state machine; the timer advances one bounded register step at a time.
 */
VOID AhciPortErrorRecovery(__in PAHCI_PORT_EXTENSION PortExtension)
{
    AHCI_PORT_CMD cmd;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    if (PortExtension->RecoveryState != AhciRecoveryIdle)
    {
        return;
    }

    AdapterExtension = PortExtension->AdapterExtension;

    cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);
    cmd.ST = 0;
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CMD, cmd.Status);

    PortExtension->RecoveryTicks = 0;
    PortExtension->RecoveryState = AhciRecoveryWaitCommandEngine;

    StorPortIssueDpc(AdapterExtension, &PortExtension->ErrorRecovery, PortExtension, NULL);
}

/**
 * @name AhciInterruptHandler
 * @not_implemented
 *
 * Interrupt Handler for PortExtension
 *
 * @param PortExtension
 *
 */
VOID AhciInterruptHandler(__in PAHCI_PORT_EXTENSION PortExtension)
{
    BOOLEAN CommandError, NcqCommandError;
    ULONG ci, sact;
    ULONG completed, ncqCompleted, nonNcqCompleted, nonNcqIssued;
    AHCI_INTERRUPT_STATUS PxIS;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    DPRINT("AhciInterruptHandler() port %u\n", PortExtension->PortNumber);

    AdapterExtension = PortExtension->AdapterExtension;
    NT_ASSERT(IsPortValid(AdapterExtension, PortExtension->PortNumber));

    // 5.5.3
    // 1. Software determines the cause of the interrupt by reading the PxIS register.
    //    It is possible for multiple bits to be set
    // 2. Software clears appropriate bits in the PxIS register corresponding to the cause of the interrupt.
    // 3. Software clears the interrupt bit in IS.IPS corresponding to the port.
    // 4. If executing non-queued commands, software reads the PxCI register, and compares the current value to
    //    the list of commands previously issued by software that are still outstanding.
    //    If executing native queued commands, software reads the PxSACT register and compares the current
    //    value to the list of commands previously issued by software.
    //    Software completes with success any outstanding command whose corresponding bit has been cleared in
    //    the respective register. PxCI and PxSACT are volatile registers; software should only use their values
    //    to determine commands that have completed, not to determine which commands have previously been issued.
    // 5. If there were errors, noted in the PxIS register, software performs error recovery actions (see section 6.2.2).
    PxIS.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->IS);

    if (PortExtension->RecoveryState != AhciRecoveryIdle)
    {
        if (PortExtension->RecoveryState == AhciRecoveryWaitNcqLog &&
            (PxIS.HBFS || PxIS.HBDS || PxIS.IFS || PxIS.TFES))
        {
            PortExtension->RecoveryNcqLogError = TRUE;
        }

        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IS, PxIS.Status);
        StorPortWriteRegisterUlong(AdapterExtension, AdapterExtension->IS, (1 << PortExtension->PortNumber));
        return;
    }

    // 6.2.2
    // Fatal Error
    // signified by the setting of PxIS.HBFS, PxIS.HBDS, PxIS.IFS, or PxIS.TFES
    if (PxIS.HBFS || PxIS.HBDS || PxIS.IFS || PxIS.TFES)
    {
        // In this state, the HBA shall not issue any new commands nor acknowledge DMA Setup FISes to process
        // any native command queuing commands. To recover, the port must be restarted
        // To detect an error that requires software recovery actions to be performed,
        // software should check whether any of the following status bits are set on an interrupt:
        // PxIS.HBFS, PxIS.HBDS, PxIS.IFS, and PxIS.TFES.  If any of these bits are set,
        // software should perform the appropriate error recovery actions based on whether
        // non-queued commands were being issued or native command queuing commands were being issued.

        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IS, PxIS.Status);
        StorPortWriteRegisterUlong(AdapterExtension, AdapterExtension->IS, (1 << PortExtension->PortNumber));

        CommandError = PxIS.TFES &&
                       !PxIS.HBFS &&
                       !PxIS.HBDS &&
                       !PxIS.IFS &&
                       !PxIS.INFS &&
                       !PxIS.OFS &&
                       !PxIS.IPMS &&
                       !PxIS.PRCS &&
                       !PxIS.PCS &&
                       !PxIS.UFS;
        NcqCommandError = CommandError &&
                          (PortExtension->NcqIssuedSlots != 0);

        PortExtension->RecoveryIsCommandError = CommandError;
        PortExtension->RecoveryIsNcqError = NcqCommandError;
        PortExtension->RecoveryNcqLogError = FALSE;
        PortExtension->RecoveryNcqActiveSlots = 0;
        if (NcqCommandError)
        {
            sact = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SACT);
            PortExtension->RecoveryNcqActiveSlots =
                sact & PortExtension->NcqIssuedSlots;
        }

        if (!CommandError && (PortExtension->ErrorLogCount < 4))
        {
            AhciDebugPrint("\tFatal transport error on port %u: IS=%08x TFD=%08x SERR=%08x\n", PortExtension->PortNumber, PxIS.Status, StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->TFD), StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SERR));
        }
        else if (!CommandError && (PortExtension->ErrorLogCount == 4))
        {
            AhciDebugPrint("\tPort %u: suppressing repeated transport-error diagnostics\n", PortExtension->PortNumber);
        }
        if (!CommandError)
        {
            PortExtension->ErrorLogCount++;
        }

        /* Acknowledge the port interrupt before recovering, then fail the
         * outstanding commands and restart the engine. This used to be a bare
         * debug print: the port stayed stopped and every queued SRB was left
         * pending forever. */
        AhciPortErrorRecovery(PortExtension);
        return;
    }

    /*
     * Every PxIS cause is write-one-to-clear. Acknowledging only the command
     * completion subset leaves PCS/PRCS or a non-fatal status asserted and
     * turns one event into a shared-interrupt storm.
     */
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->IS, PxIS.Status);
    StorPortWriteRegisterUlong(AdapterExtension, AdapterExtension->IS, (1 << PortExtension->PortNumber));

    ci = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CI);
    sact = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SACT);

    /*
     * AHCI defines a different authoritative completion register for each
     * command class: PxSACT for native queued commands and PxCI for ordinary
     * commands. ORing the two registers keeps a completed NCQ slot alive when
     * its PxCI bit lags behind PxSACT.
     */
    ncqCompleted = PortExtension->NcqIssuedSlots & ~sact;
    nonNcqIssued = PortExtension->CommandIssuedSlots & ~PortExtension->NcqIssuedSlots;
    nonNcqCompleted = nonNcqIssued & ~ci;
    completed = ncqCompleted | nonNcqCompleted;

    if (completed != 0)
    {
        PortExtension->CommandIssuedSlots &= ~completed;
        PortExtension->NcqIssuedSlots &= ~completed;
        AhciCompleteIssuedSrb(PortExtension, completed, FALSE);
    }

    /* Refill freed tags while still serialized with HwStartIo. */
    AhciSchedulePort(PortExtension);
    AhciActivatePort(PortExtension);

    return;
}// -- AhciInterruptHandler();

/**
 * @name AhciHwInterrupt
 * @implemented
 *
 * The Storport driver calls the HwStorInterrupt routine after the HBA generates an interrupt request.
 *
 * @param AdapterExtension
 *
 * @return
 * return TRUE Indicates that an interrupt was pending on adapter.
 * return FALSE Indicates the interrupt was not ours.
 */
BOOLEAN NTAPI AhciHwInterrupt(__in PVOID DeviceExtension)
{
    BOOLEAN InterruptHandled;
    AHCI_INTERRUPT_STATUS PxIS;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    ULONG portPending, nextPort, i, portCount;

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;

    if (AdapterExtension->StateFlags.Removed)
    {
        return FALSE;
    }

    portPending = StorPortReadRegisterUlong(AdapterExtension, AdapterExtension->IS);

    // we process interrupt for implemented ports only
    portCount = AdapterExtension->PortCount;
    portPending = portPending & AdapterExtension->PortImplemented;

    if (portPending == 0)
    {
        return FALSE;
    }

    InterruptHandled = FALSE;

    for (i = 1; i <= portCount; i++)
    {
        nextPort = (AdapterExtension->LastInterruptPort + i) % portCount;
        if ((portPending & (0x1 << nextPort)) == 0)
            continue;

        NT_ASSERT(IsPortValid(AdapterExtension, nextPort));

        AdapterExtension->LastInterruptPort = nextPort;
        InterruptHandled = TRUE;

        if (AdapterExtension->PortExtension[nextPort].DeviceParams.IsActive == FALSE)
        {
            /*
             * Empty or failed ports can still report a presence-change
             * interrupt. Drain both levels even though there is no active
             * device, otherwise the line remains asserted forever.
             */
            PxIS.Status = StorPortReadRegisterUlong(AdapterExtension,
                                                    &AdapterExtension->PortExtension[nextPort].Port->IS);
            StorPortWriteRegisterUlong(AdapterExtension,
                                       &AdapterExtension->PortExtension[nextPort].Port->IS,
                                       PxIS.Status);
            StorPortWriteRegisterUlong(AdapterExtension,
                                       AdapterExtension->IS,
                                       (1 << nextPort));
            portPending &= ~(1 << nextPort);
            continue;
        }

        AhciInterruptHandler(&AdapterExtension->PortExtension[nextPort]);

        portPending &= ~(1 << nextPort);
    }

    return InterruptHandled;
}// -- AhciHwInterrupt();

/**
 * @name AhciHwStartIo
 * @not_implemented
 *
 * The Storport driver calls the HwStorStartIo routine one time for each incoming I/O request.
 *
 * @param adapterExtension
 * @param Srb
 *
 * @return
 * return TRUE if the request was accepted
 * return FALSE if the request must be submitted later
 */
BOOLEAN NTAPI AhciHwStartIo(__in PVOID DeviceExtension, __in PSCSI_REQUEST_BLOCK Srb)
{
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    DPRINT("AhciHwStartIo()\n");

    AdapterExtension = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;

    if (!IsPortValid(AdapterExtension, Srb->PathId))
    {
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        StorPortNotification(RequestComplete, AdapterExtension, Srb);
        return TRUE;
    }

    switch(Srb->Function)
    {
        case SRB_FUNCTION_PNP:
            {
                // https://learn.microsoft.com/en-us/previous-versions/windows/drivers/storage/handling-srb-function-pnp
                // If the function member of an SRB is set to SRB_FUNCTION_PNP,
                // the SRB is a structure of type SCSI_PNP_REQUEST_BLOCK.

                PSCSI_PNP_REQUEST_BLOCK pnpRequest;
                pnpRequest = (PSCSI_PNP_REQUEST_BLOCK)Srb;
                if ((pnpRequest->SrbPnPFlags & SRB_PNP_FLAGS_ADAPTER_REQUEST) != 0)
                {
                    switch(pnpRequest->PnPAction)
                    {
                        case StorRemoveDevice:
                        case StorSurpriseRemoval:
                            {
                                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                                AdapterExtension->StateFlags.Removed = 1;
                                AhciDebugPrint("\tAdapter removed\n");
                            }
                            break;
                        case StorStopDevice:
                            {
                                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                                AhciDebugPrint("\tRequested to Stop the adapter\n");
                            }
                            break;
                        default:
                            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                            break;
                    }
                }
                else
                {
                    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                }
            }
            break;
        case SRB_FUNCTION_EXECUTE_SCSI:
            {
                // https://learn.microsoft.com/en-us/previous-versions/windows/drivers/storage/handling-srb-function-execute-scsi
                // On receipt of an SRB_FUNCTION_EXECUTE_SCSI request, a miniport driver's HwScsiStartIo
                // routine does the following:
                //
                // - Gets and/or sets up whatever context the miniport driver maintains in its device,
                //   logical unit, and/or SRB extensions
                //   For example, a miniport driver might set up a logical unit extension with pointers
                //   to the SRB itself and the SRB DataBuffer pointer, the SRB DataTransferLength value,
                //   and a driver-defined value (or CDB SCSIOP_XXX value) indicating the operation to be
                //   carried out on the HBA.
                //
                // - Calls an internal routine to program the HBA, as partially directed by the SrbFlags,
                //   for the requested operation
                //   For a device I/O operation, such an internal routine generally selects the target device
                //   and sends the CDB over the bus to the target logical unit.
                PCDB cdb = (PCDB)&Srb->Cdb;
                if (Srb->CdbLength == 0)
                {
                    DPRINT("AhciHwStartIo: zero-length CDB\n");
                    Srb->SrbStatus = SRB_STATUS_BAD_FUNCTION;
                    break;
                }

                NT_ASSERT(cdb != NULL);

                switch(cdb->CDB10.OperationCode)
                {
                    case SCSIOP_INQUIRY:
                        Srb->SrbStatus = DeviceInquiryRequest(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_REPORT_LUNS:
                        Srb->SrbStatus = DeviceReportLuns(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_READ_CAPACITY:
                    case SCSIOP_READ_CAPACITY16:
                        Srb->SrbStatus = DeviceRequestCapacity(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_TEST_UNIT_READY:
                    case SCSIOP_START_STOP_UNIT:
                    case SCSIOP_MEDIUM_REMOVAL:
                    case SCSIOP_VERIFY:
                    case SCSIOP_VERIFY16:
                        Srb->SrbStatus = DeviceRequestComplete(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_SYNCHRONIZE_CACHE:
                    case SCSIOP_SYNCHRONIZE_CACHE16:
                        Srb->SrbStatus = DeviceRequestFlush(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_MODE_SENSE:
                        Srb->SrbStatus = DeviceRequestSense(AdapterExtension, Srb, cdb);
                        break;
                    case SCSIOP_READ:
                    case SCSIOP_WRITE:
                    case SCSIOP_READ16:
                    case SCSIOP_WRITE16:
                        Srb->SrbStatus = DeviceRequestReadWrite(AdapterExtension, Srb, cdb);
                        break;
                    default:
                        /*
                         * ATAPI transports SCSI packet commands directly.
                         * Windows storahci routes unhandled packet CDBs through
                         * AtapiCommonRequest as well; rejecting commands such
                         * as GET CONFIGURATION, GET EVENT STATUS and READ TOC
                         * here prevents the CD class stack from probing media.
                         */
                        if (AdapterExtension->PortExtension[Srb->PathId].DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
                        {
                            Srb->SrbStatus = AhciATAPICommand(AdapterExtension, Srb, cdb);
                        }
                        else
                        {
                            DPRINT("AhciHwStartIo: unsupported SCSI operation 0x%02x\n", cdb->CDB10.OperationCode);
                            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                        }
                        break;
                }
            }
            break;
        case SRB_FUNCTION_SHUTDOWN:
        case SRB_FUNCTION_FLUSH:
            /*
             * Class drivers issue these SRBs with no SCSI CDB. For ATA disks,
             * turn them into a real FLUSH CACHE command and let the normal
             * scheduler serialize it behind any outstanding NCQ commands.
             * ATAPI devices have no equivalent ATA cache command here.
             */
            if (AdapterExtension->PortExtension[Srb->PathId].DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
            {
                Srb->ScsiStatus = SCSISTAT_GOOD;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
            }
            else
            {
                Srb->SrbStatus = DeviceRequestFlush(AdapterExtension, Srb, (PCDB)&Srb->Cdb);
            }
            break;
        default:
            DPRINT("AhciHwStartIo: unsupported SRB function 0x%02x\n", Srb->Function);
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }

    if (Srb->SrbStatus != SRB_STATUS_PENDING)
    {
        StorPortNotification(RequestComplete, AdapterExtension, Srb);
    }
    else
    {
        AhciProcessIO(AdapterExtension, Srb->PathId, Srb);
    }
    return TRUE;
}// -- AhciHwStartIo();

/**
 * @name AhciHwResetBus
 * @not_implemented
 *
 * The HwStorResetBus routine is called by the port driver to clear error conditions.
 *
 * @param adapterExtension
 * @param PathId
 *
 * @return
 * return TRUE if bus was successfully reset
 */
BOOLEAN NTAPI AhciHwResetBus(__in PVOID AdapterExtension, __in ULONG PathId)
{
    ULONG outstanding;
    BOOLEAN CompletionQueued;
    PSCSI_REQUEST_BLOCK Srb;
    STOR_LOCK_HANDLE lockhandle = {0};
    PAHCI_PORT_EXTENSION PortExtension;
    PAHCI_ADAPTER_EXTENSION adapterExtension;

    AhciDebugPrint("AhciHwResetBus()\n");

    adapterExtension = (PAHCI_ADAPTER_EXTENSION)AdapterExtension;
    CompletionQueued = FALSE;

    if (PathId >= adapterExtension->PortCount)
    {
        return FALSE;
    }

    if ((adapterExtension->PortImplemented & (1 << PathId)) == 0)
    {
        return FALSE;
    }

    PortExtension = &adapterExtension->PortExtension[PathId];

    /* Fail everything that is in flight or queued, under the interrupt lock so
     * the ISR cannot complete the same SRBs concurrently. */
    StorPortAcquireSpinLock(adapterExtension, InterruptLock, NULL, &lockhandle);

    outstanding = PortExtension->CommandIssuedSlots | PortExtension->QueueSlots;
    if (outstanding != 0)
    {
        AhciCompleteIssuedSrb(PortExtension, outstanding, TRUE);
    }

    PortExtension->CommandIssuedSlots = 0;
    PortExtension->QueueSlots = 0;
    PortExtension->NcqIssuedSlots = 0;
    PortExtension->NcqQueueSlots = 0;
    PortExtension->RecoveryState = AhciRecoveryIdle;
    PortExtension->RecoveryTicks = 0;
    PortExtension->RecoveryIsCommandError = FALSE;
    AhciResetNcqRecoveryContext(PortExtension);

    /* A bus reset also completes requests that had not obtained a slot yet. */
    while ((Srb = RemoveQueue(&PortExtension->SrbQueue)) != NULL)
    {
        Srb->SrbStatus = SRB_STATUS_BUS_RESET;
        if (AddQueue(&PortExtension->CompletionQueue, Srb))
        {
            CompletionQueued = TRUE;
        }
        else
        {
            StorPortNotification(RequestComplete, adapterExtension, Srb);
        }
    }

    StorPortReleaseSpinLock(adapterExtension, &lockhandle);

    if (CompletionQueued)
    {
        StorPortIssueDpc(adapterExtension, &PortExtension->CommandCompletion, PortExtension, NULL);
    }

    /* Now bring the link and the command engine back up. This runs at
     * DISPATCH_LEVEL or below and may stall, so it is done outside the lock.
     * The whole routine used to be a TODO that returned FALSE, so a wedged
     * port could never be recovered. */
    PortExtension->DeviceParams.IsActive = AhciStartPort(PortExtension);

    AhciDebugPrint("\tPort %u reset, active=%u\n", PathId, PortExtension->DeviceParams.IsActive);

    return PortExtension->DeviceParams.IsActive;
}// -- AhciHwResetBus();

/**
 * @name AhciHwFindAdapter
 * @implemented
 *
 * The HwStorFindAdapter routine uses the supplied configuration to determine whether a specific
 * HBA is supported and, if it is, to return configuration information about that adapter.
 *
 *  10.1 Platform Communication
 *  http://www.intel.in/content/dam/www/public/us/en/documents/technical-specifications/serial-ata-ahci-spec-rev1_2.pdf

 * @param DeviceExtension
 * @param HwContext
 * @param BusInformation
 * @param ArgumentString
 * @param ConfigInfo
 * @param Reserved3
 *
 * @return
 *      SP_RETURN_FOUND
 *          Indicates that a supported HBA was found and that the HBA-relevant configuration information was successfully determined and set in the PORT_CONFIGURATION_INFORMATION structure.
 *
 *      SP_RETURN_ERROR
 *          Indicates that an HBA was found but there was an error obtaining the configuration information. If possible, such an error should be logged with StorPortLogError.
 *
 *      SP_RETURN_BAD_CONFIG
 *          Indicates that the supplied configuration information was invalid for the adapter.
 *
 *      SP_RETURN_NOT_FOUND
 *          Indicates that no supported HBA was found for the supplied configuration information.
 *
 * @remarks Called by Storport.
 */
ULONG NTAPI AhciHwFindAdapter(__in PVOID DeviceExtension, __in PVOID HwContext, __in PVOID BusInformation, __in PCHAR ArgumentString, __inout PPORT_CONFIGURATION_INFORMATION ConfigInfo, __in PBOOLEAN Reserved3)
{
    AHCI_GHC ghc;
    ULONG index, pci_cfg_len;
    PACCESS_RANGE accessRange;
    UCHAR pci_cfg_buf[sizeof(PCI_COMMON_CONFIG)];

    PAHCI_MEMORY_REGISTERS abar;
    PPCI_COMMON_CONFIG pciConfigData;
    PAHCI_ADAPTER_EXTENSION adapterExtension;

    AhciDebugPrint("AhciHwFindAdapter()\n");

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);
    UNREFERENCED_PARAMETER(Reserved3);

    adapterExtension = DeviceExtension;
    adapterExtension->SlotNumber = ConfigInfo->SlotNumber;
    adapterExtension->SystemIoBusNumber = ConfigInfo->SystemIoBusNumber;

    // get PCI configuration header
    pci_cfg_len = StorPortGetBusData(
                        adapterExtension,
                        PCIConfiguration,
                        adapterExtension->SystemIoBusNumber,
                        adapterExtension->SlotNumber,
                        pci_cfg_buf,
                        sizeof(PCI_COMMON_CONFIG));

    if (pci_cfg_len != sizeof(PCI_COMMON_CONFIG))
    {
        AhciDebugPrint("\tpci_cfg_len != %d :: %d", sizeof(PCI_COMMON_CONFIG), pci_cfg_len);
        return SP_RETURN_ERROR;//Not a valid device at the given bus number
    }

    pciConfigData = (PPCI_COMMON_CONFIG)pci_cfg_buf;
    adapterExtension->VendorID = pciConfigData->VendorID;
    adapterExtension->DeviceID = pciConfigData->DeviceID;
    adapterExtension->RevisionID = pciConfigData->RevisionID;
    // The last PCI base address register (BAR[5], header offset 0x24) points to the AHCI base memory, it’s called ABAR (AHCI Base Memory Register).
    adapterExtension->AhciBaseAddress = pciConfigData->u.type0.BaseAddresses[5] & (0xFFFFFFF0);

    AhciDebugPrint("\tVendorID: %04x  DeviceID: %04x  RevisionID: %02x\n",
                   adapterExtension->VendorID,
                   adapterExtension->DeviceID,
                   adapterExtension->RevisionID);

    // 2.1.11
    abar = NULL;
    if (ConfigInfo->NumberOfAccessRanges > 0)
    {
        accessRange = *(ConfigInfo->AccessRanges);
        for (index = 0; index < ConfigInfo->NumberOfAccessRanges; index++)
        {
            if (accessRange[index].RangeStart.QuadPart == adapterExtension->AhciBaseAddress)
            {
                abar = StorPortGetDeviceBase(adapterExtension,
                                             ConfigInfo->AdapterInterfaceType,
                                             ConfigInfo->SystemIoBusNumber,
                                             accessRange[index].RangeStart,
                                             accessRange[index].RangeLength,
                                             !accessRange[index].RangeInMemory);
                break;
            }
        }
    }

    if (abar == NULL)
    {
        AhciDebugPrint("\tabar == NULL\n");
        return SP_RETURN_ERROR; // corrupted information supplied
    }

    adapterExtension->ABAR_Address = abar;
    adapterExtension->CAP = StorPortReadRegisterUlong(adapterExtension, &abar->CAP);
    adapterExtension->CAP2 = StorPortReadRegisterUlong(adapterExtension, &abar->CAP2);
    adapterExtension->Version = StorPortReadRegisterUlong(adapterExtension, &abar->VS);
    adapterExtension->LastInterruptPort = (ULONG)-1;

    // 10.1.2
    // 1. Indicate that system software is AHCI aware by setting GHC.AE to ‘1’.
    // 3.1.2 -- AE bit is read-write only if CAP.SAM is '0'
    ghc.Status = StorPortReadRegisterUlong(adapterExtension, &abar->GHC);
    // AE := Highest Significant bit of GHC
    if (ghc.AE != 0)// Hmm, controller was already in power state
    {
        // reset controller to have it in known state
        AhciDebugPrint("\tAE Already set, Reset()\n");
        if (!AhciAdapterReset(adapterExtension))
        {
            AhciDebugPrint("\tReset Failed!\n");
            return SP_RETURN_ERROR;// reset failed
        }
    }

    ghc.Status = 0;
    ghc.AE = 1;// only AE=1
    // tell the controller that we know about AHCI
    StorPortWriteRegisterUlong(adapterExtension, &abar->GHC, ghc.Status);

    adapterExtension->IS = &abar->IS;
    adapterExtension->PortImplemented = StorPortReadRegisterUlong(adapterExtension, &abar->PI);

    if (adapterExtension->PortImplemented == 0)
    {
        AhciDebugPrint("\tadapterExtension->PortImplemented == 0\n");
        return SP_RETURN_ERROR;
    }

    ConfigInfo->Master = TRUE;
    ConfigInfo->AlignmentMask = 0x3;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->DmaWidth = Width32Bits;
    ConfigInfo->WmiDataProvider = FALSE;
    ConfigInfo->Dma32BitAddresses = TRUE;

    if (IsAdapterCAPS64(adapterExtension->CAP))
    {
        ConfigInfo->Dma64BitAddresses = TRUE;
    }

    ConfigInfo->MaximumNumberOfTargets = 1;
    ConfigInfo->ResetTargetSupported = TRUE;
    /* Keep this tied to the PRDT size so storport can never hand us a scatter
     * gather list longer than the command table can describe. */
    ConfigInfo->NumberOfPhysicalBreaks = MAXIMUM_AHCI_PRDT_ENTRIES - 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;
    ConfigInfo->NumberOfBuses = MAXIMUM_AHCI_PORT_COUNT;
    ConfigInfo->MaximumTransferLength = MAXIMUM_TRANSFER_LENGTH;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;

    // Turn IE -- Interrupt Enabled
    ghc.Status = StorPortReadRegisterUlong(adapterExtension, &abar->GHC);
    ghc.IE = 1;
    StorPortWriteRegisterUlong(adapterExtension, &abar->GHC, ghc.Status);

    // allocate necessary resource for each port
    if (!AhciAllocateResourceForAdapter(adapterExtension, ConfigInfo))
    {
        NT_ASSERT(FALSE);
        return SP_RETURN_ERROR;
    }

    for (index = 0; index < adapterExtension->PortCount; index++)
    {
        if ((adapterExtension->PortImplemented & (0x1 << index)) != 0)
            AhciPortInitialize(&adapterExtension->PortExtension[index]);
    }

    return SP_RETURN_FOUND;
}// -- AhciHwFindAdapter();

/**
 * @name DriverEntry
 * @implemented
 *
 * Initial Entrypoint for storahci miniport driver
 *
 * @param DriverObject
 * @param RegistryPath
 *
 * @return
 * NT_STATUS in case of driver loaded successfully.
 */
ULONG NTAPI DriverEntry(__in PVOID DriverObject, __in PVOID RegistryPath)
{
    ULONG status;
    // initialize the hardware data structure
    HW_INITIALIZATION_DATA hwInitializationData = {0};

    // set size of hardware initialization structure
    hwInitializationData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);

    // identity required miniport entry point routines
    hwInitializationData.HwStartIo = AhciHwStartIo;
    hwInitializationData.HwResetBus = AhciHwResetBus;
    hwInitializationData.HwInterrupt = AhciHwInterrupt;
    hwInitializationData.HwInitialize = AhciHwInitialize;
    hwInitializationData.HwFindAdapter = AhciHwFindAdapter;

    // adapter specific information
    hwInitializationData.TaggedQueuing = TRUE;
    hwInitializationData.AutoRequestSense = TRUE;
    hwInitializationData.MultipleRequestPerLu = TRUE;
    hwInitializationData.NeedPhysicalAddresses = TRUE;

    hwInitializationData.NumberOfAccessRanges = 6;
    hwInitializationData.AdapterInterfaceType = PCIBus;
    hwInitializationData.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;

    // set required extension sizes
    hwInitializationData.SrbExtensionSize = sizeof(AHCI_SRB_EXTENSION);
    hwInitializationData.DeviceExtensionSize = sizeof(AHCI_ADAPTER_EXTENSION);

    // register our hw init data
    status = StorPortInitialize(DriverObject,
                                RegistryPath,
                                &hwInitializationData,
                                NULL);

    NT_ASSERT(status == STATUS_SUCCESS);
    return status;
}// -- DriverEntry();

/**
 * @name AhciATA_CFIS
 * @implemented
 *
 * create ATA CFIS from Srb
 *
 * @param PortExtension
 * @param Srb
 *
 * @return
 * Number of CFIS fields used in DWORD
 */
ULONG AhciATA_CFIS(__in PAHCI_PORT_EXTENSION PortExtension, __in PAHCI_SRB_EXTENSION SrbExtension)
{
    PAHCI_COMMAND_TABLE cmdTable;

    UNREFERENCED_PARAMETER(PortExtension);

    DPRINT("AhciATA_CFIS()\n");

    cmdTable = (PAHCI_COMMAND_TABLE)SrbExtension;

    AhciZeroMemory((PCHAR)cmdTable->CFIS, sizeof(cmdTable->CFIS));

    cmdTable->CFIS[AHCI_ATA_CFIS_FisType] = FIS_TYPE_REG_H2D;       // FIS Type
    cmdTable->CFIS[AHCI_ATA_CFIS_PMPort_C] = (1 << 7);              // PM Port & C
    cmdTable->CFIS[AHCI_ATA_CFIS_CommandReg] = SrbExtension->CommandReg;

    cmdTable->CFIS[AHCI_ATA_CFIS_FeaturesLow] = SrbExtension->FeaturesLow;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA0] = SrbExtension->LBA0;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA1] = SrbExtension->LBA1;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA2] = SrbExtension->LBA2;
    cmdTable->CFIS[AHCI_ATA_CFIS_Device] = SrbExtension->Device;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA3] = SrbExtension->LBA3;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA4] = SrbExtension->LBA4;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA5] = SrbExtension->LBA5;
    cmdTable->CFIS[AHCI_ATA_CFIS_FeaturesHigh] = SrbExtension->FeaturesHigh;
    cmdTable->CFIS[AHCI_ATA_CFIS_SectorCountLow] = SrbExtension->SectorCountLow;
    cmdTable->CFIS[AHCI_ATA_CFIS_SectorCountHigh] = SrbExtension->SectorCountHigh;

    return 5;
}// -- AhciATA_CFIS();

/**
 * @name AhciATAPI_CFIS
 * @not_implemented
 *
 * create ATAPI CFIS from Srb
 *
 * @param PortExtension
 * @param Srb
 *
 * @return
 * Number of CFIS fields used in DWORD
 */
ULONG AhciATAPI_CFIS(__in PAHCI_PORT_EXTENSION PortExtension, __in PAHCI_SRB_EXTENSION SrbExtension)
{
    PAHCI_COMMAND_TABLE cmdTable;
    UNREFERENCED_PARAMETER(PortExtension);

    DPRINT("AhciATAPI_CFIS()\n");

    cmdTable = (PAHCI_COMMAND_TABLE)SrbExtension;

    NT_ASSERT(SrbExtension->CommandReg == IDE_COMMAND_ATAPI_PACKET);

    AhciZeroMemory((PCHAR)cmdTable->CFIS, sizeof(cmdTable->CFIS));

    cmdTable->CFIS[AHCI_ATA_CFIS_FisType] = FIS_TYPE_REG_H2D;       // FIS Type
    cmdTable->CFIS[AHCI_ATA_CFIS_PMPort_C] = (1 << 7);              // PM Port & C
    cmdTable->CFIS[AHCI_ATA_CFIS_CommandReg] = SrbExtension->CommandReg;

    cmdTable->CFIS[AHCI_ATA_CFIS_FeaturesLow] = SrbExtension->FeaturesLow;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA0] = SrbExtension->LBA0;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA1] = SrbExtension->LBA1;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA2] = SrbExtension->LBA2;
    cmdTable->CFIS[AHCI_ATA_CFIS_Device] = SrbExtension->Device;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA3] = SrbExtension->LBA3;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA4] = SrbExtension->LBA4;
    cmdTable->CFIS[AHCI_ATA_CFIS_LBA5] = SrbExtension->LBA5;
    cmdTable->CFIS[AHCI_ATA_CFIS_FeaturesHigh] = SrbExtension->FeaturesHigh;
    cmdTable->CFIS[AHCI_ATA_CFIS_SectorCountLow] = SrbExtension->SectorCountLow;
    cmdTable->CFIS[AHCI_ATA_CFIS_SectorCountHigh] = SrbExtension->SectorCountHigh;

    return 5;
}// -- AhciATAPI_CFIS();

/**
 * @name AhciBuild_PRDT
 * @implemented
 *
 * Build PRDT for data transfer
 *
 * @param PortExtension
 * @param Srb
 *
 * @return
 * Return number of entries in PRDT.
 */
ULONG AhciBuild_PRDT(__in PAHCI_PORT_EXTENSION PortExtension, __in PAHCI_SRB_EXTENSION SrbExtension)
{
    ULONG index;
    PAHCI_COMMAND_TABLE cmdTable;
    PLOCAL_SCATTER_GATHER_LIST sgl;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    DPRINT("AhciBuild_PRDT()\n");

    sgl = SrbExtension->pSgl;
    cmdTable = (PAHCI_COMMAND_TABLE)SrbExtension;
    AdapterExtension = PortExtension->AdapterExtension;

    NT_ASSERT(sgl != NULL);

    /* Storport is told NumberOfPhysicalBreaks, so it should never hand us more
     * elements than the table holds -- but clamp rather than scribble past the
     * end of PRDT[] if it ever does. */
    if (sgl->NumberOfElements > MAXIMUM_AHCI_PRDT_ENTRIES)
    {
        AhciDebugPrint("\tSGL has %u elements, table holds %u\n", sgl->NumberOfElements, MAXIMUM_AHCI_PRDT_ENTRIES);
        return 0;
    }

    for (index = 0; index < sgl->NumberOfElements; index++)
    {
        NT_ASSERT(sgl->List[index].Length <= MAXIMUM_TRANSFER_LENGTH);

        cmdTable->PRDT[index].DBA = sgl->List[index].PhysicalAddress.LowPart;
        if (IsAdapterCAPS64(AdapterExtension->CAP))
        {
            cmdTable->PRDT[index].DBAU = sgl->List[index].PhysicalAddress.HighPart;
        }

        // Data Byte Count (DBC): A ‘0’ based value that Indicates the length, in bytes, of the data block.
        // A maximum of length of 4MB may exist for any entry. Bit ‘0’ of this field must always be ‘1’ to
        // indicate an even byte count. A value of ‘1’ indicates 2 bytes, ‘3’ indicates 4 bytes, etc.
        cmdTable->PRDT[index].DBC = sgl->List[index].Length - 1;
    }

    return sgl->NumberOfElements;
}// -- AhciBuild_PRDT();

/**
 * @name AhciProcessSrb
 * @implemented
 *
 * Prepare Srb for IO processing
 *
 * @param PortExtension
 * @param Srb
 * @param SlotIndex
 *
 */
VOID AhciProcessSrb(__in PAHCI_PORT_EXTENSION PortExtension, __in PSCSI_REQUEST_BLOCK Srb, __in ULONG SlotIndex)
{
    ULONG prdtlen, sig, length, cfl;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_COMMAND_HEADER CommandHeader;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;
    STOR_PHYSICAL_ADDRESS CommandTablePhysicalAddress;

    DPRINT("AhciProcessSrb()\n");

    NT_ASSERT(Srb->PathId == PortExtension->PortNumber);

    SrbExtension = GetSrbExtension(Srb);
    AdapterExtension = PortExtension->AdapterExtension;

    NT_ASSERT(SrbExtension != NULL);
    NT_ASSERT(SrbExtension->AtaFunction != 0);

    if ((SrbExtension->AtaFunction == ATA_FUNCTION_ATA_IDENTIFY) &&
        (SrbExtension->CommandReg == IDE_COMMAND_NOT_VALID))
    {
        // Here we are safe to check SIG register
        sig = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->SIG);
        if (sig == 0x101)
        {
            AhciDebugPrint("\tATA Device Found!\n");
            SrbExtension->CommandReg = IDE_COMMAND_IDENTIFY;
        }
        else
        {
            AhciDebugPrint("\tATAPI Device Found!\n");
            SrbExtension->CommandReg = IDE_COMMAND_ATAPI_IDENTIFY;
        }
    }

    NT_ASSERT(SlotIndex < AHCI_Global_Port_CAP_NCS(AdapterExtension->CAP));
    SrbExtension->SlotIndex = SlotIndex;

    /*
     * For FPDMA commands Sector Count[7:3] carries the hardware queue tag;
     * the transfer count is in Features[15:0].
     */
    if (SrbExtension->Flags & ATA_FLAGS_NCQ)
    {
        SrbExtension->SectorCountLow = (UCHAR)(SlotIndex << 3);
        SrbExtension->SectorCountHigh = 0;

        if ((SrbExtension->CommandReg == IDE_COMMAND_READ_FPDMA_QUEUED) &&
            !PortExtension->DeviceParams.NcqReadLogged)
        {
            AhciDebugPrint("\tPort %u: first READ FPDMA QUEUED command, slot %u\n",
                           PortExtension->PortNumber,
                           SlotIndex);
            PortExtension->DeviceParams.NcqReadLogged = TRUE;
        }
        else if ((SrbExtension->CommandReg == IDE_COMMAND_WRITE_FPDMA_QUEUED) &&
                 !PortExtension->DeviceParams.NcqWriteLogged)
        {
            AhciDebugPrint("\tPort %u: first WRITE FPDMA QUEUED command, slot %u\n",
                           PortExtension->PortNumber,
                           SlotIndex);
            PortExtension->DeviceParams.NcqWriteLogged = TRUE;
        }
    }

    // program the CFIS in the CommandTable
    CommandHeader = &PortExtension->CommandList[SlotIndex];

    cfl = 0;
    if (IsAtapiCommand(SrbExtension->AtaFunction))
    {
        PAHCI_COMMAND_TABLE atapiTable;
        ULONG cdbLength;

        cfl = AhciATAPI_CFIS(PortExtension, SrbExtension);

        /* Carry the SCSI CDB in the ATAPI command block. Without this the
         * PACKET command was issued with an all-zero CDB and every ATAPI
         * device rejected it. */
        atapiTable = (PAHCI_COMMAND_TABLE)SrbExtension;
        cdbLength = Srb->CdbLength;
        if (cdbLength > sizeof(atapiTable->ACMD)) cdbLength = sizeof(atapiTable->ACMD);

        AhciZeroMemory((PCHAR)atapiTable->ACMD, sizeof(atapiTable->ACMD));
        if (SrbExtension->AutosenseActive)
        {
            atapiTable->ACMD[0] = SCSIOP_REQUEST_SENSE;
            atapiTable->ACMD[4] = Srb->SenseInfoBufferLength;
        }
        else
        {
            StorPortCopyMemory(atapiTable->ACMD, Srb->Cdb, cdbLength);
        }
    }
    else if (IsAtaCommand(SrbExtension->AtaFunction))
    {
        cfl = AhciATA_CFIS(PortExtension, SrbExtension);
    }
    else
    {
        NT_ASSERT(FALSE);
    }

    prdtlen = 0;
    if (IsDataTransferNeeded(SrbExtension))
    {
        prdtlen = AhciBuild_PRDT(PortExtension, SrbExtension);
        NT_ASSERT(prdtlen != -1);
    }

    // Program the command header
    CommandHeader->DI.PRDTL = prdtlen; // number of entries in PRD table
    CommandHeader->DI.CFL = cfl;
    CommandHeader->DI.A = (SrbExtension->AtaFunction & ATA_FUNCTION_ATAPI_COMMAND) ? 1 : 0;
    CommandHeader->DI.W = (SrbExtension->Flags & ATA_FLAGS_DATA_OUT) ? 1 : 0;
    CommandHeader->DI.P = 0;    // ATA Specifications says so
    CommandHeader->DI.PMP = 0;  // Port Multiplier

    // Reset -- Manual Configuation
    CommandHeader->DI.R = 0;
    CommandHeader->DI.B = 0;
    CommandHeader->DI.C = 0;

    CommandHeader->PRDBC = 0;

    CommandHeader->Reserved[0] = 0;
    CommandHeader->Reserved[1] = 0;
    CommandHeader->Reserved[2] = 0;
    CommandHeader->Reserved[3] = 0;

    // set CommandHeader CTBA
    CommandTablePhysicalAddress = StorPortGetPhysicalAddress(AdapterExtension,
                                                             NULL,
                                                             SrbExtension,
                                                             &length);

    NT_ASSERT(length != 0);

    // command table alignment
    NT_ASSERT((CommandTablePhysicalAddress.LowPart % 128) == 0);

    CommandHeader->CTBA = CommandTablePhysicalAddress.LowPart;

    if (IsAdapterCAPS64(AdapterExtension->CAP))
    {
        CommandHeader->CTBA_U = CommandTablePhysicalAddress.HighPart;
    }

    // mark this slot
    PortExtension->Slot[SlotIndex] = Srb;
    PortExtension->QueueSlots |= 1UL << SlotIndex;
    if (SrbExtension->Flags & ATA_FLAGS_NCQ)
    {
        PortExtension->NcqQueueSlots |= 1UL << SlotIndex;
    }
    return;
}// -- AhciProcessSrb();

/**
 * @name AhciActivatePort
 * @implemented
 *
 * Program Port and populate command list
 *
 * @param PortExtension
 *
 */

VOID AhciActivatePort(__in PAHCI_PORT_EXTENSION PortExtension)
{
    AHCI_PORT_CMD cmd;
    ULONG QueueSlots, slotToActivate, ncqSlots;
    PAHCI_ADAPTER_EXTENSION AdapterExtension;

    DPRINT("AhciActivatePort()\n");

    AdapterExtension = PortExtension->AdapterExtension;

    if (PortExtension->RecoveryState != AhciRecoveryIdle)
    {
        return;
    }

    QueueSlots = PortExtension->QueueSlots;

    if (QueueSlots == 0)
    {
        return;
    }

    // section 3.3.14
    // Bits in this field shall only be set to ‘1’ by software when PxCMD.ST is set to ‘1’
    cmd.Status = StorPortReadRegisterUlong(AdapterExtension, &PortExtension->Port->CMD);

    if (cmd.ST == 0) // PxCMD.ST == 0
    {
        return;
    }

    /* Issue every slot that has been prepared. PxCI is write-1-to-set, so a
     * single write starts them all.
     *
     * The old code peeled off the lowest set bit and issued just that one,
     * leaving any other prepared slots queued until another request happened
     * to arrive -- which stalls the queue whenever the class driver submits a
     * batch and then waits for it. */
    slotToActivate = QueueSlots;
    ncqSlots = slotToActivate & PortExtension->NcqQueueSlots;

    PortExtension->QueueSlots &= ~slotToActivate;
    PortExtension->NcqQueueSlots &= ~ncqSlots;
    PortExtension->CommandIssuedSlots |= slotToActivate;
    PortExtension->NcqIssuedSlots |= ncqSlots;

    /*
     * The command headers and tables must be visible before either doorbell.
     * Native queued tags are activated in PxSACT before the matching PxCI bits.
     */
    KeMemoryBarrier();

    if (ncqSlots != 0)
    {
        StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->SACT, ncqSlots);
    }

    // tell the HBA to issue these Command Slots to the given port
    StorPortWriteRegisterUlong(AdapterExtension, &PortExtension->Port->CI, slotToActivate);

    return;
}// -- AhciActivatePort();

/**
 * @name AhciSchedulePort
 *
 * Assign queued SRBs to free hardware slots. The caller holds InterruptLock
 * (or is the interrupt handler itself), so the software slot masks and PxCI /
 * PxSACT observations are serialized.
 */
VOID AhciSchedulePort(__in PAHCI_PORT_EXTENSION PortExtension)
{
    BOOLEAN NcqMode, HeadUsesNcq;
    ULONG assignedSlots, freeSlots, ncqSlots;
    ULONG depth, assignedCount, slotIndex, NCS;
    PAHCI_SRB_EXTENSION SrbExtension;
    PSCSI_REQUEST_BLOCK Srb;

    if (!PortExtension->DeviceParams.IsActive)
    {
        return;
    }

    if (PortExtension->RecoveryState != AhciRecoveryIdle)
    {
        return;
    }

    NCS = AHCI_Global_Port_CAP_NCS(PortExtension->AdapterExtension->CAP);
    assignedSlots = PortExtension->CommandIssuedSlots | PortExtension->QueueSlots;
    ncqSlots = PortExtension->NcqIssuedSlots | PortExtension->NcqQueueSlots;

    /* Queued and non-queued ATA commands are mutually exclusive on a port. */
    if ((ncqSlots != 0) && (ncqSlots != assignedSlots))
    {
        AhciDebugPrint("\tPort %u has mixed NCQ/non-NCQ slots (%08x/%08x)\n", PortExtension->PortNumber, ncqSlots, assignedSlots);
        return;
    }

    NcqMode = (ncqSlots != 0);

    /* An ordinary command owns the port until its PxCI bit clears. */
    if ((assignedSlots != 0) && !NcqMode)
    {
        return;
    }

    Srb = (PSCSI_REQUEST_BLOCK)PeekQueue(&PortExtension->SrbQueue);
    if (Srb == NULL)
    {
        return;
    }

    SrbExtension = GetSrbExtension(Srb);
    NT_ASSERT(SrbExtension != NULL);
    HeadUsesNcq = (SrbExtension->Flags & ATA_FLAGS_NCQ) != 0;

    if (assignedSlots == 0)
    {
        NcqMode = HeadUsesNcq;
    }
    else if (!HeadUsesNcq)
    {
        /* Preserve FIFO order: the exclusive command waits behind active NCQ. */
        return;
    }

    depth = NcqMode ? PortExtension->MaxPortQueueDepth : 1;
    if (depth == 0)
    {
        depth = 1;
    }
    if (depth > NCS)
    {
        depth = NCS;
    }

    assignedCount = 0;
    for (slotIndex = 0; slotIndex < NCS; slotIndex++)
    {
        if (assignedSlots & (1UL << slotIndex))
        {
            assignedCount++;
        }
    }

    freeSlots = (NCS == 32) ? 0xFFFFFFFFUL : ((1UL << NCS) - 1);
    freeSlots &= ~assignedSlots;

    while ((freeSlots != 0) && (assignedCount < depth))
    {
        Srb = (PSCSI_REQUEST_BLOCK)PeekQueue(&PortExtension->SrbQueue);
        if (Srb == NULL)
        {
            break;
        }

        SrbExtension = GetSrbExtension(Srb);
        NT_ASSERT(SrbExtension != NULL);
        HeadUsesNcq = (SrbExtension->Flags & ATA_FLAGS_NCQ) != 0;

        if (HeadUsesNcq != NcqMode)
        {
            break;
        }

        for (slotIndex = 0; slotIndex < NCS; slotIndex++)
        {
            if (freeSlots & (1UL << slotIndex))
            {
                break;
            }
        }

        if (slotIndex == NCS)
        {
            break;
        }

        Srb = (PSCSI_REQUEST_BLOCK)RemoveQueue(&PortExtension->SrbQueue);
        NT_ASSERT(Srb != NULL);
        NT_ASSERT(Srb->PathId == PortExtension->PortNumber);

        AhciProcessSrb(PortExtension, Srb, slotIndex);
        assignedSlots |= 1UL << slotIndex;
        freeSlots &= ~(1UL << slotIndex);
        assignedCount++;

        /* A non-NCQ command is always the only command assigned to the port. */
        if (!NcqMode)
        {
            break;
        }
    }
}// -- AhciSchedulePort();

/**
 * @name AhciProcessIO
 * @implemented
 *
 * Queue an incoming SRB, assign eligible hardware slots and ring the port
 * doorbell.
 */
VOID AhciProcessIO(__in PAHCI_ADAPTER_EXTENSION AdapterExtension, __in UCHAR PathId, __in PSCSI_REQUEST_BLOCK Srb)
{
    BOOLEAN Queued;
    STOR_LOCK_HANDLE lockhandle = {0};
    PAHCI_PORT_EXTENSION PortExtension;

    DPRINT("AhciProcessIO() PathId: %d\n", PathId);

    PortExtension = &AdapterExtension->PortExtension[PathId];

    NT_ASSERT(PathId < AdapterExtension->PortCount);

    StorPortAcquireSpinLock(AdapterExtension, InterruptLock, NULL, &lockhandle);

    if (!PortExtension->DeviceParams.IsActive)
    {
        StorPortReleaseSpinLock(AdapterExtension, &lockhandle);
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        StorPortNotification(RequestComplete, AdapterExtension, Srb);
        return;
    }

    Queued = AddQueue(&PortExtension->SrbQueue, Srb);
    if (!Queued)
    {
        StorPortReleaseSpinLock(AdapterExtension, &lockhandle);
        Srb->SrbStatus = SRB_STATUS_BUSY;
        StorPortNotification(RequestComplete, AdapterExtension, Srb);
        return;
    }

    if (PortExtension->RecoveryState == AhciRecoveryIdle)
    {
        AhciSchedulePort(PortExtension);
        AhciActivatePort(PortExtension);
    }

    StorPortReleaseSpinLock(AdapterExtension, &lockhandle);
}// -- AhciProcessIO();

/**
 * @name AtapiInquiryCompletion
 * @implemented
 *
 * AtapiInquiryCompletion routine should be called after device signals
 * for device inquiry request is completed (through interrupt) -- ATAPI Device only
 *
 * @param PortExtension
 * @param Srb
 *
 */
VOID AtapiInquiryCompletion(__in PVOID _Extension, __in PVOID _Srb)
{
    PAHCI_PORT_EXTENSION PortExtension;
    PSCSI_REQUEST_BLOCK Srb;

    DPRINT("AtapiInquiryCompletion()\n");

    PortExtension = (PAHCI_PORT_EXTENSION)_Extension;
    Srb = (PSCSI_REQUEST_BLOCK)_Srb;

    NT_ASSERT(Srb != NULL);
    NT_ASSERT(PortExtension != NULL);

    PortExtension->DeviceParams.NcqSupported = FALSE;
    PortExtension->DeviceParams.NcqQueueDepth = 1;
    PortExtension->MaxPortQueueDepth = 1;
    return;
}// -- AtapiInquiryCompletion();

/**
 * @name InquiryCompletion
 * @implemented
 *
 * InquiryCompletion routine should be called after device signals
 * for device inquiry request is completed (through interrupt)
 *
 * @param PortExtension
 * @param Srb
 *
 */
VOID InquiryCompletion(__in PVOID _Extension, __in PVOID _Srb)
{
    PAHCI_PORT_EXTENSION PortExtension;
    PSCSI_REQUEST_BLOCK Srb;

//    PCDB cdb;
    ULONG SectorExponent, DeviceQueueDepth, HbaQueueDepth;
    PINQUIRYDATA InquiryData;
    PAHCI_SRB_EXTENSION SrbExtension;
    PIDENTIFY_DEVICE_DATA IdentifyDeviceData;

    DPRINT("InquiryCompletion()\n");

    PortExtension = (PAHCI_PORT_EXTENSION)_Extension;
    Srb = (PSCSI_REQUEST_BLOCK)_Srb;

    NT_ASSERT(Srb != NULL);
    NT_ASSERT(PortExtension != NULL);

//    cdb = (PCDB)&Srb->Cdb;
    InquiryData = Srb->DataBuffer;
    SrbExtension = GetSrbExtension(Srb);
    IdentifyDeviceData = PortExtension->IdentifyDeviceData;

    if (Srb->SrbStatus != SRB_STATUS_SUCCESS)
    {
        if (Srb->SrbStatus == SRB_STATUS_NO_DEVICE)
        {
            PortExtension->DeviceParams.DeviceType = AHCI_DEVICE_TYPE_NODEVICE;
        }
        return;
    }

    NT_ASSERT(InquiryData != NULL);
    NT_ASSERT(Srb->SrbStatus == SRB_STATUS_SUCCESS);

    // Device specific data
    PortExtension->DeviceParams.MaxLba.QuadPart = 0;
    PortExtension->DeviceParams.NcqSupported = FALSE;
    PortExtension->DeviceParams.NcqQueueDepth = 1;
    PortExtension->MaxPortQueueDepth = 1;

    if (SrbExtension->CommandReg == IDE_COMMAND_IDENTIFY)
    {
        PortExtension->DeviceParams.DeviceType = AHCI_DEVICE_TYPE_ATA;
        PortExtension->DeviceParams.RemovableDevice = AtaDevIsRemovable(IdentifyDeviceData) ? 1 : 0;
        PortExtension->DeviceParams.Lba48BitMode = AtaDevHas48BitAddressFeature(IdentifyDeviceData) ? 1 : 0;
        PortExtension->DeviceParams.AccessType = DIRECT_ACCESS_DEVICE;

        /* Total number of addressable sectors (not the last LBA). */
        if (PortExtension->DeviceParams.Lba48BitMode)
        {
            PortExtension->DeviceParams.MaxLba.QuadPart = AtaDevUserAddressableSectors48Bit(IdentifyDeviceData);
        }
        else
        {
            PortExtension->DeviceParams.MaxLba.QuadPart = AtaDevUserAddressableSectors28Bit(IdentifyDeviceData);
        }

        /* Sector geometry. Both of these are completely normal on modern
         * drives and used to hit NT_ASSERT(FALSE):
         *   - 4Kn drives report a logical sector larger than 512 bytes
         *   - 512e drives report several 512-byte logical sectors per physical
         * Derive the real values instead of forcing 512/512. */
        PortExtension->DeviceParams.BytesPerLogicalSector = AtaDevBytesPerLogicalSector(IdentifyDeviceData);
        PortExtension->DeviceParams.BytesPerPhysicalSector = PortExtension->DeviceParams.BytesPerLogicalSector * AtaDevLogicalSectorsPerPhysicalSector(IdentifyDeviceData, &SectorExponent);

        /* Guard against a drive that reports nonsense. */
        if (PortExtension->DeviceParams.BytesPerLogicalSector < DEVICE_ATA_BLOCK_SIZE)
        {
            PortExtension->DeviceParams.BytesPerLogicalSector = DEVICE_ATA_BLOCK_SIZE;
        }
        if (PortExtension->DeviceParams.BytesPerPhysicalSector < PortExtension->DeviceParams.BytesPerLogicalSector)
        {
            PortExtension->DeviceParams.BytesPerPhysicalSector = PortExtension->DeviceParams.BytesPerLogicalSector;
        }

        /* IDENTIFY text fields are big-endian USHORTs and space padded. */
        AhciCopyAtaString(PortExtension->DeviceParams.VendorId, (PUCHAR)IdentifyDeviceData->ModelNumber, sizeof(PortExtension->DeviceParams.VendorId) - 1);
        AhciCopyAtaString(PortExtension->DeviceParams.RevisionID, (PUCHAR)IdentifyDeviceData->FirmwareRevision, sizeof(PortExtension->DeviceParams.RevisionID) - 1);
        AhciCopyAtaString(PortExtension->DeviceParams.SerialNumber, (PUCHAR)IdentifyDeviceData->SerialNumber, sizeof(PortExtension->DeviceParams.SerialNumber) - 1);

        DeviceQueueDepth = AtaDevQueueDepth(IdentifyDeviceData);
        HbaQueueDepth = AHCI_Global_Port_CAP_NCS(PortExtension->AdapterExtension->CAP);

        /*
         * FPDMA uses 48-bit task-file fields and is valid only when both ends
         * advertise NCQ. Limit the device's depth to the number of HBA slots.
         */
        if ((PortExtension->AdapterExtension->CAP & AHCI_Global_HBA_CAP_SNCQ) &&
            PortExtension->DeviceParams.Lba48BitMode &&
            (DeviceQueueDepth > 1))
        {
            if (DeviceQueueDepth > HbaQueueDepth)
            {
                DeviceQueueDepth = HbaQueueDepth;
            }

            if ((DeviceQueueDepth > 1) &&
                StorPortSetDeviceQueueDepth(PortExtension->AdapterExtension,
                                            Srb->PathId,
                                            Srb->TargetId,
                                            Srb->Lun,
                                            DeviceQueueDepth))
            {
                PortExtension->DeviceParams.NcqSupported = TRUE;
                PortExtension->DeviceParams.NcqQueueDepth = DeviceQueueDepth;
                PortExtension->MaxPortQueueDepth = DeviceQueueDepth;
            }
        }

        AhciDebugPrint("\tATA Device: '%s' fw '%s'\n", PortExtension->DeviceParams.VendorId, PortExtension->DeviceParams.RevisionID);
        AhciDebugPrint("\tLBA48=%u Sectors=%I64u LogicalSector=%u PhysicalSector=%u\n", PortExtension->DeviceParams.Lba48BitMode, PortExtension->DeviceParams.MaxLba.QuadPart, PortExtension->DeviceParams.BytesPerLogicalSector, PortExtension->DeviceParams.BytesPerPhysicalSector);
        AhciDebugPrint("\tNCQ=%u QueueDepth=%u (device=%u HBA=%u)\n", PortExtension->DeviceParams.NcqSupported, PortExtension->DeviceParams.NcqQueueDepth, AtaDevQueueDepth(IdentifyDeviceData), HbaQueueDepth);
    }
    else
    {
        AhciDebugPrint("\tATAPI Device\n");
        PortExtension->DeviceParams.DeviceType = AHCI_DEVICE_TYPE_ATAPI;
        PortExtension->DeviceParams.AccessType = READ_ONLY_DIRECT_ACCESS_DEVICE;
    }

    // INQUIRYDATABUFFERSIZE = 36 ; Defined in storport.h
    if (Srb->DataTransferLength < INQUIRYDATABUFFERSIZE)
    {
        AhciDebugPrint("\tDataBufferLength < sizeof(INQUIRYDATA), Could crash the driver.\n");
        NT_ASSERT(FALSE);
    }

    // update data transfer length
    Srb->DataTransferLength = INQUIRYDATABUFFERSIZE;

    // prepare data to send
    InquiryData->Versions = 2;
    InquiryData->Wide32Bit = 1;
    InquiryData->CommandQueue = PortExtension->DeviceParams.NcqSupported;
    InquiryData->ResponseDataFormat = 0x2;
    InquiryData->DeviceTypeModifier = 0;
    InquiryData->DeviceTypeQualifier = DEVICE_CONNECTED;
    InquiryData->AdditionalLength = INQUIRYDATABUFFERSIZE - 5;
    InquiryData->DeviceType = PortExtension->DeviceParams.AccessType;
    InquiryData->RemovableMedia = PortExtension->DeviceParams.RemovableDevice;

    /* SAT-2 5.4.2: for an ATA device the INQUIRY strings are
     *   Vendor   = "ATA     "
     *   Product  = the IDENTIFY model number
     *   Revision = the IDENTIFY firmware revision
     * SCSI text fields are space padded and are not NUL terminated.
     *
     * The old code put the firmware revision in ProductId and the serial
     * number in ProductRevisionLevel, and NUL terminated both fields, so the
     * disk showed up under the wrong name. */
    AhciFillScsiString(InquiryData->VendorId, sizeof(InquiryData->VendorId), (PUCHAR)"ATA");
    AhciFillScsiString(InquiryData->ProductId, sizeof(InquiryData->ProductId), PortExtension->DeviceParams.VendorId);
    AhciFillScsiString(InquiryData->ProductRevisionLevel, sizeof(InquiryData->ProductRevisionLevel), PortExtension->DeviceParams.RevisionID);

    return;
}// -- InquiryCompletion();

 /**
 * @name AhciATAPICommand
 * @implemented
 *
 * Handles ATAPI Requests commands
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for AhciATAPICommand
 */
UCHAR AhciATAPICommand(__in PAHCI_ADAPTER_EXTENSION AdapterExtension, __in PSCSI_REQUEST_BLOCK Srb, __in PCDB Cdb)
{
    ULONG SrbFlags, DataBufferLength;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_PORT_EXTENSION PortExtension;

    DPRINT("AhciATAPICommand()\n");

    SrbFlags = Srb->SrbFlags;
    SrbExtension = GetSrbExtension(Srb);
    DataBufferLength = Srb->DataTransferLength;
    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    NT_ASSERT(PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI);

    NT_ASSERT(SrbExtension != NULL);

    SrbExtension->AtaFunction = ATA_FUNCTION_ATAPI_COMMAND;
    SrbExtension->Flags = 0;

    if (SrbFlags & SRB_FLAGS_DATA_IN)
    {
        SrbExtension->Flags |= ATA_FLAGS_DATA_IN;
    }

    if (SrbFlags & SRB_FLAGS_DATA_OUT)
    {
        SrbExtension->Flags |= ATA_FLAGS_DATA_OUT;
    }

    SrbExtension->FeaturesLow = 0;

    SrbExtension->CompletionRoutine = NULL;

    NT_ASSERT(Cdb != NULL);
    switch(Cdb->CDB10.OperationCode)
    {
        case SCSIOP_INQUIRY:
            SrbExtension->Flags |= ATA_FLAGS_DATA_IN;
            SrbExtension->CompletionRoutine = AtapiInquiryCompletion;
            break;
        case SCSIOP_READ:
            SrbExtension->Flags |= ATA_FLAGS_USE_DMA;
            SrbExtension->FeaturesLow = 0x5;
            break;
        case SCSIOP_WRITE:
            SrbExtension->Flags |= ATA_FLAGS_USE_DMA;
            SrbExtension->FeaturesLow = 0x1;
            break;
    }

    SrbExtension->CommandReg = IDE_COMMAND_ATAPI_PACKET;

    SrbExtension->LBA0 = 0;
    SrbExtension->LBA1 = (UCHAR)(DataBufferLength >> 0);
    SrbExtension->LBA2 = (UCHAR)(DataBufferLength >> 8);
    SrbExtension->Device = 0;
    SrbExtension->LBA3 = 0;
    SrbExtension->LBA4 = 0;
    SrbExtension->LBA5 = 0;
    SrbExtension->FeaturesHigh = 0;
    SrbExtension->SectorCountLow = 0;
    SrbExtension->SectorCountHigh = 0;

    if ((SrbExtension->Flags & ATA_FLAGS_DATA_IN) || (SrbExtension->Flags & ATA_FLAGS_DATA_OUT))
    {
        SrbExtension->pSgl = (PLOCAL_SCATTER_GATHER_LIST)StorPortGetScatterGatherList(AdapterExtension, Srb);
    }

    return SRB_STATUS_PENDING;
}// -- AhciATAPICommand();

/**
 * @name DeviceRequestSense
 * @implemented
 *
 * Handle SCSIOP_MODE_SENSE OperationCode
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceRequestSense
 */
UCHAR DeviceRequestSense(__in PAHCI_ADAPTER_EXTENSION AdapterExtension, __in PSCSI_REQUEST_BLOCK Srb, __in PCDB Cdb)
{
    PMODE_PARAMETER_HEADER ModeHeader;
    PAHCI_PORT_EXTENSION PortExtension;

    DPRINT("DeviceRequestSense()\n");

    NT_ASSERT(IsPortValid(AdapterExtension, Srb->PathId));
    NT_ASSERT(Cdb->CDB10.OperationCode == SCSIOP_MODE_SENSE);

    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    ModeHeader = (PMODE_PARAMETER_HEADER)Srb->DataBuffer;

    NT_ASSERT(ModeHeader != NULL);

    AhciZeroMemory((PCHAR)ModeHeader, Srb->DataTransferLength);

    ModeHeader->ModeDataLength = sizeof(MODE_PARAMETER_HEADER);
    ModeHeader->MediumType = 0;
    ModeHeader->DeviceSpecificParameter = 0;
    ModeHeader->BlockDescriptorLength = 0;

    if (Cdb->MODE_SENSE.PageCode == MODE_SENSE_CURRENT_VALUES)
    {
        ModeHeader->ModeDataLength = sizeof(MODE_PARAMETER_HEADER) + sizeof(MODE_PARAMETER_BLOCK);
        ModeHeader->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);
    }

    return SRB_STATUS_SUCCESS;
}// -- DeviceRequestSense();

/**
 * @name DeviceRequestReadWrite
 * @implemented
 *
 * Handle SCSIOP_READ SCSIOP_WRITE OperationCode
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceRequestReadWrite
 */
UCHAR DeviceRequestReadWrite(__in PAHCI_ADAPTER_EXTENSION AdapterExtension, __in PSCSI_REQUEST_BLOCK Srb, __in PCDB Cdb)
{
    BOOLEAN IsReading, Use48Bit, UseNcq;
    ULONG64 StartOffset;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_PORT_EXTENSION PortExtension;
    ULONG DataTransferLength, BytesPerSector, SectorCount;

    DPRINT("DeviceRequestReadWrite()\n");

    NT_ASSERT(IsPortValid(AdapterExtension, Srb->PathId));

    SrbExtension = GetSrbExtension(Srb);
    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    DataTransferLength = Srb->DataTransferLength;
    BytesPerSector = PortExtension->DeviceParams.BytesPerLogicalSector;

    NT_ASSERT(BytesPerSector > 0);

    //ROUND_UP(DataTransferLength, BytesPerSector);

    SectorCount = DataTransferLength / BytesPerSector;

    Srb->DataTransferLength = SectorCount * BytesPerSector;

    StartOffset = AhciGetLba(Cdb, Srb->CdbLength);
    IsReading = ((Cdb->CDB10.OperationCode == SCSIOP_READ) || (Cdb->CDB10.OperationCode == SCSIOP_READ16));

    if (SectorCount == 0)
    {
        /* Nothing to move -- a zero length transfer is legal in SCSI. */
        Srb->DataTransferLength = 0;
        Srb->ScsiStatus = SCSISTAT_GOOD;
        return SRB_STATUS_SUCCESS;
    }

    /* Refuse anything that runs past the end of the medium instead of letting
     * the drive abort the command. */
    if ((StartOffset + SectorCount) > (ULONG64)PortExtension->DeviceParams.MaxLba.QuadPart)
    {
        AhciDebugPrint("\tLBA %I64u + %u exceeds capacity %I64u\n", StartOffset, SectorCount, PortExtension->DeviceParams.MaxLba.QuadPart);
        return SRB_STATUS_INVALID_REQUEST;
    }

    /* Decide between the 28-bit and 48-bit command set. A 28-bit command can
     * only reach LBA 0x0FFFFFFF and can move at most 256 sectors, so use the
     * EXT form whenever the device supports it or the request needs it.
     *
     * The old code emitted the 28-bit opcode, then unconditionally required
     * 48-bit support and hit NT_ASSERT(FALSE) on any drive without it. */
    Use48Bit = PortExtension->DeviceParams.Lba48BitMode;
    if ((StartOffset + SectorCount) > MAXIMUM_LBA28_ADDRESS)
    {
        Use48Bit = TRUE;
    }

    if (!Use48Bit && (SectorCount > MAXIMUM_LBA28_SECTORS))
    {
        Use48Bit = TRUE;
    }

    if (Use48Bit && !PortExtension->DeviceParams.Lba48BitMode)
    {
        AhciDebugPrint("\tRequest needs 48-bit addressing but device lacks it\n");
        return SRB_STATUS_INVALID_REQUEST;
    }

    if ((SectorCount == 0) || (SectorCount > (Use48Bit ? MAXIMUM_LBA48_SECTORS : MAXIMUM_LBA28_SECTORS)))
    {
        AhciDebugPrint("\tBad sector count %u\n", SectorCount);
        return SRB_STATUS_INVALID_REQUEST;
    }

    SrbExtension->AtaFunction = ATA_FUNCTION_ATA_READ;
    SrbExtension->Flags |= ATA_FLAGS_USE_DMA;
    SrbExtension->CompletionRoutine = NULL;
    SrbExtension->Flags |= IsReading ? ATA_FLAGS_DATA_IN : ATA_FLAGS_DATA_OUT;
    UseNcq = PortExtension->DeviceParams.NcqSupported && Use48Bit;

    SrbExtension->FeaturesLow = 0;
    SrbExtension->FeaturesHigh = 0;
    SrbExtension->LBA0 = (UCHAR)((StartOffset >> 0) & 0xFF);
    SrbExtension->LBA1 = (UCHAR)((StartOffset >> 8) & 0xFF);
    SrbExtension->LBA2 = (UCHAR)((StartOffset >> 16) & 0xFF);

    if (UseNcq)
    {
        SrbExtension->Flags |= ATA_FLAGS_48BIT_COMMAND | ATA_FLAGS_NCQ;
        SrbExtension->CommandReg = IsReading ? IDE_COMMAND_READ_FPDMA_QUEUED : IDE_COMMAND_WRITE_FPDMA_QUEUED;

        /*
         * FPDMA places the transfer count in Features[15:0]. Sector Count is
         * reserved for the tag (filled once AhciProcessSrb assigns a slot).
         */
        SrbExtension->FeaturesLow = (UCHAR)(SectorCount & 0xFF);
        SrbExtension->FeaturesHigh = (UCHAR)((SectorCount >> 8) & 0xFF);
        SrbExtension->Device = IDE_LBA_MODE;
        SrbExtension->LBA3 = (UCHAR)((StartOffset >> 24) & 0xFF);
        SrbExtension->LBA4 = (UCHAR)((StartOffset >> 32) & 0xFF);
        SrbExtension->LBA5 = (UCHAR)((StartOffset >> 40) & 0xFF);
        SrbExtension->SectorCountLow = 0;
        SrbExtension->SectorCountHigh = 0;
    }
    else if (Use48Bit)
    {
        SrbExtension->Flags |= ATA_FLAGS_48BIT_COMMAND;
        SrbExtension->CommandReg = IsReading ? IDE_COMMAND_READ_DMA_EXT : IDE_COMMAND_WRITE_DMA_EXT;

        /* In 48-bit mode the high LBA bytes travel in their own registers and
         * the device register carries no address bits. */
        SrbExtension->Device = (0x40 | IDE_LBA_MODE);
        SrbExtension->LBA3 = (UCHAR)((StartOffset >> 24) & 0xFF);
        SrbExtension->LBA4 = (UCHAR)((StartOffset >> 32) & 0xFF);
        SrbExtension->LBA5 = (UCHAR)((StartOffset >> 40) & 0xFF);

        /* 16-bit count; 0 means 65536 sectors */
        SrbExtension->SectorCountLow = (UCHAR)((SectorCount >> 0) & 0xFF);
        SrbExtension->SectorCountHigh = (UCHAR)((SectorCount >> 8) & 0xFF);
    }
    else
    {
        SrbExtension->CommandReg = IsReading ? IDE_COMMAND_READ_DMA : IDE_COMMAND_WRITE_DMA;

        /* Bits 27:24 of the LBA live in the low nibble of the device register. */
        SrbExtension->Device = (UCHAR)(0xA0 | IDE_LBA_MODE | ((StartOffset >> 24) & 0x0F));
        SrbExtension->LBA3 = 0;
        SrbExtension->LBA4 = 0;
        SrbExtension->LBA5 = 0;

        /* 8-bit count; 0 means 256 sectors */
        SrbExtension->SectorCountLow = (UCHAR)(SectorCount & 0xFF);
        SrbExtension->SectorCountHigh = 0;
    }

    SrbExtension->pSgl = (PLOCAL_SCATTER_GATHER_LIST)StorPortGetScatterGatherList(AdapterExtension, Srb);

    return SRB_STATUS_PENDING;
}// -- DeviceRequestReadWrite();

/**
 * @name DeviceRequestCapacity
 * @implemented
 *
 * Handle SCSIOP_READ_CAPACITY OperationCode
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceRequestCapacity
 */
UCHAR DeviceRequestCapacity(__in PAHCI_ADAPTER_EXTENSION AdapterExtension, __in PSCSI_REQUEST_BLOCK Srb, __in PCDB Cdb)
{
    ULONG LastLba32, BytesPerLogicalSector;
    ULONG64 LastLba;
    PREAD_CAPACITY_DATA ReadCapacity;
    PREAD_CAPACITY_DATA_EX ReadCapacityEx;
    PAHCI_PORT_EXTENSION PortExtension;

    DPRINT("DeviceRequestCapacity()\n");

    NT_ASSERT(IsPortValid(AdapterExtension, Srb->PathId));

    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    if (Srb->DataBuffer == NULL)
    {
        return SRB_STATUS_INVALID_REQUEST;
    }

    if (PortExtension->DeviceParams.MaxLba.QuadPart == 0)
    {
        AhciDebugPrint("\tCapacity unknown -- IDENTIFY has not completed\n");
        return SRB_STATUS_INVALID_REQUEST;
    }

    BytesPerLogicalSector = PortExtension->DeviceParams.BytesPerLogicalSector;

    /* READ CAPACITY reports the address of the LAST logical block, whereas
     * MaxLba holds the number of addressable sectors. */
    LastLba = (ULONG64)PortExtension->DeviceParams.MaxLba.QuadPart - 1;

    if (Cdb->CDB10.OperationCode == SCSIOP_READ_CAPACITY)
    {
        if (Srb->DataTransferLength < sizeof(READ_CAPACITY_DATA))
        {
            return SRB_STATUS_DATA_OVERRUN;
        }

        ReadCapacity = (PREAD_CAPACITY_DATA)Srb->DataBuffer;

        /* SBC: when the last LBA does not fit in 32 bits the field is reported
         * as 0xFFFFFFFF, which tells the initiator to reissue the request as
         * READ CAPACITY (16). The old code asserted here, so every drive of
         * 2 TB or larger tripped an assertion instead of being reported. */
        LastLba32 = (LastLba > MAXULONG) ? MAXULONG : (ULONG)LastLba;

        REVERSE_BYTES(&ReadCapacity->BytesPerBlock, &BytesPerLogicalSector);
        REVERSE_BYTES(&ReadCapacity->LogicalBlockAddress, &LastLba32);

        Srb->DataTransferLength = sizeof(READ_CAPACITY_DATA);
    }
    else
    {
        if (Srb->DataTransferLength < sizeof(READ_CAPACITY_DATA_EX))
        {
            return SRB_STATUS_DATA_OVERRUN;
        }

        ReadCapacityEx = (PREAD_CAPACITY_DATA_EX)Srb->DataBuffer;

        AhciZeroMemory((PCHAR)ReadCapacityEx, sizeof(READ_CAPACITY_DATA_EX));

        REVERSE_BYTES(&ReadCapacityEx->BytesPerBlock, &BytesPerLogicalSector);
        REVERSE_BYTES_QUAD(&ReadCapacityEx->LogicalBlockAddress, &LastLba);

        Srb->DataTransferLength = sizeof(READ_CAPACITY_DATA_EX);
    }

    AhciDebugPrint("\tCapacity: LastLba=%I64u BytesPerBlock=%u\n", LastLba, BytesPerLogicalSector);

    Srb->ScsiStatus = SCSISTAT_GOOD;
    return SRB_STATUS_SUCCESS;
}// -- DeviceRequestCapacity();

/**
 * @name DeviceRequestComplete
 * @implemented
 *
 * Handle UnHandled Requests
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceRequestComplete
 */
UCHAR DeviceRequestComplete(__in PAHCI_ADAPTER_EXTENSION AdapterExtension, __in PSCSI_REQUEST_BLOCK Srb, __in PCDB Cdb)
{
    PAHCI_PORT_EXTENSION PortExtension;

    DPRINT("DeviceRequestComplete()\n");

    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    Srb->ScsiStatus = SCSISTAT_GOOD;

    return SRB_STATUS_SUCCESS;
}// -- DeviceRequestComplete();

/**
 * @name DeviceRequestFlush
 * @implemented
 *
 * Handle SCSIOP_SYNCHRONIZE_CACHE / SCSIOP_SYNCHRONIZE_CACHE16 by pushing the
 * drive's volatile write cache to media with ATA FLUSH CACHE (EXT).
 *
 * Previously SYNCHRONIZE CACHE fell through to the default case and was
 * rejected as an invalid request, so the file system had no way to force
 * cached writes out and data could be lost on power failure.
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceRequestFlush
 */
UCHAR DeviceRequestFlush(__in PAHCI_ADAPTER_EXTENSION AdapterExtension, __in PSCSI_REQUEST_BLOCK Srb, __in PCDB Cdb)
{
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_PORT_EXTENSION PortExtension;

    DPRINT("DeviceRequestFlush()\n");

    NT_ASSERT(IsPortValid(AdapterExtension, Srb->PathId));

    SrbExtension = GetSrbExtension(Srb);
    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    NT_ASSERT(SrbExtension != NULL);

    SrbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;
    SrbExtension->Flags = 0;
    SrbExtension->CompletionRoutine = NULL;
    SrbExtension->CommandReg = PortExtension->DeviceParams.Lba48BitMode ? IDE_COMMAND_FLUSH_CACHE_EXT : IDE_COMMAND_FLUSH_CACHE;

    SrbExtension->FeaturesLow = 0;
    SrbExtension->FeaturesHigh = 0;
    SrbExtension->LBA0 = 0;
    SrbExtension->LBA1 = 0;
    SrbExtension->LBA2 = 0;
    SrbExtension->LBA3 = 0;
    SrbExtension->LBA4 = 0;
    SrbExtension->LBA5 = 0;
    SrbExtension->Device = (0xA0 | IDE_LBA_MODE);
    SrbExtension->SectorCountLow = 0;
    SrbExtension->SectorCountHigh = 0;
    SrbExtension->pSgl = NULL;

    Srb->DataTransferLength = 0;

    return SRB_STATUS_PENDING;
}// -- DeviceRequestFlush();

/**
 * @name DeviceReportLuns
 * @implemented
 *
 * Handle SCSIOP_REPORT_LUNS OperationCode
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceReportLuns
 */
UCHAR DeviceReportLuns(__in PAHCI_ADAPTER_EXTENSION AdapterExtension, __in PSCSI_REQUEST_BLOCK Srb, __in PCDB Cdb)
{
    PLUN_LIST LunList;
    PAHCI_PORT_EXTENSION PortExtension;

    DPRINT("DeviceReportLuns()\n");

    UNREFERENCED_PARAMETER(Cdb);

    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    NT_ASSERT(Srb->DataTransferLength >= sizeof(LUN_LIST));
    NT_ASSERT(Cdb->CDB10.OperationCode == SCSIOP_REPORT_LUNS);

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    LunList = (PLUN_LIST)Srb->DataBuffer;

    NT_ASSERT(LunList != NULL);

    AhciZeroMemory((PCHAR)LunList, sizeof(LUN_LIST));

    LunList->LunListLength[3] = 8;

    Srb->ScsiStatus = SCSISTAT_GOOD;
    Srb->DataTransferLength = sizeof(LUN_LIST);

    return SRB_STATUS_SUCCESS;
}// -- DeviceReportLuns();

/**
 * @name DeviceInquiryRequest
 * @implemented
 *
 * Tells wheather given port is implemented or not
 *
 * @param AdapterExtension
 * @param Srb
 * @param Cdb
 *
 * @return
 * return STOR status for DeviceInquiryRequest
 *
 * @remark
 * http://www.seagate.com/staticfiles/support/disc/manuals/Interface%20manuals/100293068c.pdf
 */
UCHAR DeviceInquiryRequest(__in PAHCI_ADAPTER_EXTENSION AdapterExtension, __in PSCSI_REQUEST_BLOCK Srb, __in PCDB Cdb)
{
    PVOID DataBuffer;
    PAHCI_SRB_EXTENSION SrbExtension;
    PAHCI_PORT_EXTENSION PortExtension;
    PVPD_SUPPORTED_PAGES_PAGE VpdOutputBuffer;
    ULONG DataBufferLength, RequiredDataBufferLength;

    DPRINT("DeviceInquiryRequest()\n");

    NT_ASSERT(Cdb->CDB10.OperationCode == SCSIOP_INQUIRY);
    NT_ASSERT(IsPortValid(AdapterExtension, Srb->PathId));

    SrbExtension = GetSrbExtension(Srb);
    PortExtension = &AdapterExtension->PortExtension[Srb->PathId];

    if (PortExtension->DeviceParams.DeviceType == AHCI_DEVICE_TYPE_ATAPI)
    {
        return AhciATAPICommand(AdapterExtension, Srb, Cdb);
    }

    if (Srb->Lun != 0)
    {
        return SRB_STATUS_SELECTION_TIMEOUT;
    }
    else if (Cdb->CDB6INQUIRY3.EnableVitalProductData == 0)
    {
        // 3.6.1
        // If the EVPD bit is set to zero, the device server shall return the standard INQUIRY data
        AhciDebugPrint("\tEVPD Inquired\n");
        NT_ASSERT(SrbExtension != NULL);

        SrbExtension->AtaFunction = ATA_FUNCTION_ATA_IDENTIFY;
        SrbExtension->Flags |= ATA_FLAGS_DATA_IN;
        SrbExtension->CompletionRoutine = InquiryCompletion;
        SrbExtension->CommandReg = IDE_COMMAND_NOT_VALID;

        // TODO: Should use AhciZeroMemory
        SrbExtension->FeaturesLow = 0;
        SrbExtension->LBA0 = 0;
        SrbExtension->LBA1 = 0;
        SrbExtension->LBA2 = 0;
        SrbExtension->Device = 0xA0;
        SrbExtension->LBA3 = 0;
        SrbExtension->LBA4 = 0;
        SrbExtension->LBA5 = 0;
        SrbExtension->FeaturesHigh = 0;
        SrbExtension->SectorCountLow = 0;
        SrbExtension->SectorCountHigh = 0;

        SrbExtension->Sgl.NumberOfElements = 1;
        SrbExtension->Sgl.List[0].PhysicalAddress.LowPart = PortExtension->IdentifyDeviceDataPhysicalAddress.LowPart;
        SrbExtension->Sgl.List[0].PhysicalAddress.HighPart = PortExtension->IdentifyDeviceDataPhysicalAddress.HighPart;
        SrbExtension->Sgl.List[0].Length = sizeof(IDENTIFY_DEVICE_DATA);

        SrbExtension->pSgl = &SrbExtension->Sgl;
        return SRB_STATUS_PENDING;
    }
    else
    {
        AhciDebugPrint("\tVPD Inquired\n");

        DataBuffer = Srb->DataBuffer;
        DataBufferLength = Srb->DataTransferLength;
        RequiredDataBufferLength = DataBufferLength; // make the compiler happy :p

        if (DataBuffer == NULL)
        {
            return SRB_STATUS_INVALID_REQUEST;
        }

        AhciZeroMemory(DataBuffer, DataBufferLength);

        switch(Cdb->CDB6INQUIRY3.PageCode)
        {
            case VPD_SUPPORTED_PAGES:
                {
                    AhciDebugPrint("\tVPD_SUPPORTED_PAGES\n");
                    RequiredDataBufferLength = sizeof(VPD_SUPPORTED_PAGES_PAGE) + 1;

                    if (DataBufferLength < RequiredDataBufferLength)
                    {
                        AhciDebugPrint("\tDataBufferLength: %d Required: %d\n", DataBufferLength, RequiredDataBufferLength);
                        return SRB_STATUS_INVALID_REQUEST;
                    }

                    VpdOutputBuffer = (PVPD_SUPPORTED_PAGES_PAGE)DataBuffer;

                    VpdOutputBuffer->DeviceType = PortExtension->DeviceParams.AccessType;
                    VpdOutputBuffer->DeviceTypeQualifier = 0;
                    VpdOutputBuffer->PageCode = VPD_SUPPORTED_PAGES;
                    VpdOutputBuffer->PageLength = 1;
                    VpdOutputBuffer->SupportedPageList[0] = VPD_SUPPORTED_PAGES;
                    //VpdOutputBuffer->SupportedPageList[1] = VPD_SERIAL_NUMBER;
                    //VpdOutputBuffer->SupportedPageList[2] = VPD_DEVICE_IDENTIFIERS;

                    NT_ASSERT(VpdOutputBuffer->DeviceType == DIRECT_ACCESS_DEVICE);
                }
                break;
            case VPD_SERIAL_NUMBER:
                {
                    AhciDebugPrint("\tVPD_SERIAL_NUMBER\n");
                }
                break;
            case VPD_DEVICE_IDENTIFIERS:
                {
                    AhciDebugPrint("\tVPD_DEVICE_IDENTIFIERS\n");
                }
                break;
            default:
                AhciDebugPrint("\tPageCode: %x\n", Cdb->CDB6INQUIRY3.PageCode);
                return SRB_STATUS_INVALID_REQUEST;
        }

        Srb->DataTransferLength = RequiredDataBufferLength;
        return SRB_STATUS_SUCCESS;
    }
}// -- DeviceInquiryRequest();

/**
 * @name AhciAdapterReset
 * @implemented
 *
 * 10.4.3 HBA Reset
 * If the HBA becomes unusable for multiple ports, and a software reset or port reset does not correct the
 * problem, software may reset the entire HBA by setting GHC.HR to ‘1’. When software sets the GHC.HR
 * bit to ‘1’, the HBA shall perform an internal reset action. The bit shall be cleared to ‘0’ by the HBA when
 * the reset is complete. A software write of ‘0’ to GHC.HR shall have no effect. To perform the HBA reset,
 * software sets GHC.HR to ‘1’ and may poll until this bit is read to be ‘0’, at which point software knows that
 * the HBA reset has completed.
 * If the HBA has not cleared GHC.HR to ‘0’ within 1 second of software setting GHC.HR to ‘1’, the HBA is in
 * a hung or locked state.
 *
 * @param AdapterExtension
 *
 * @return
 * TRUE in case AHCI Controller RESTARTED successfully. i.e GHC.HR == 0
 */
BOOLEAN AhciAdapterReset(__in PAHCI_ADAPTER_EXTENSION AdapterExtension)
{
    ULONG ticks;
    AHCI_GHC ghc;
    PAHCI_MEMORY_REGISTERS abar = NULL;

    AhciDebugPrint("AhciAdapterReset()\n");

    abar = AdapterExtension->ABAR_Address;
    if (abar == NULL) // basic sanity
    {
        return FALSE;
    }

    // HR -- Very first bit (lowest significant)
    ghc.Status = StorPortReadRegisterUlong(AdapterExtension, &abar->GHC);
    ghc.HR = 1;
    StorPortWriteRegisterUlong(AdapterExtension, &abar->GHC, ghc.Status);

    for (ticks = 0; ticks < 50; ++ticks)
    {
        ghc.Status = StorPortReadRegisterUlong(AdapterExtension, &abar->GHC);
        if (ghc.HR == 0)
        {
            break;
        }
        StorPortStallExecution(20000);
    }

    if (ticks == 50)// 1 second
    {
        AhciDebugPrint("\tDevice Timeout\n");
        return FALSE;
    }

    return TRUE;
}// -- AhciAdapterReset();

/**
 * @name AhciCopyAtaString
 * @implemented
 *
 * Copy a text field out of IDENTIFY DEVICE data, undoing the 16-bit word byte
 * order and trimming the trailing pad spaces.
 *
 * IDENTIFY strings are stored as big-endian USHORTs, so a straight copy yields
 * transposed characters -- "ST3000DM001" reads back as "TS3000DM001". The
 * result is always NUL terminated, so Destination must hold Length + 1 bytes.
 *
 * @param Destination
 * @param Source
 * @param Length -- number of characters to copy (should be even)
 */
VOID AhciCopyAtaString(__out PUCHAR Destination, __in PUCHAR Source, __in ULONG Length)
{
    ULONG i;

    for (i = 0; (i + 1) < Length; i += 2)
    {
        Destination[i] = Source[i + 1];
        Destination[i + 1] = Source[i];
    }

    Destination[Length] = '\0';

    while ((Length > 0) && ((Destination[Length - 1] == ' ') || (Destination[Length - 1] == '\0')))
    {
        Destination[--Length] = '\0';
    }

    return;
}// -- AhciCopyAtaString();

/**
 * @name AhciFillScsiString
 * @implemented
 *
 * Copy a NUL terminated string into a fixed width SCSI text field, padding
 * with spaces. SCSI INQUIRY text fields are space padded and are NOT NUL
 * terminated.
 *
 * @param Destination
 * @param DestinationLength
 * @param Source
 */
VOID AhciFillScsiString(__out PUCHAR Destination, __in ULONG DestinationLength, __in PUCHAR Source)
{
    ULONG i;
    BOOLEAN End = FALSE;

    for (i = 0; i < DestinationLength; i++)
    {
        if (!End && (Source[i] == '\0')) End = TRUE;
        Destination[i] = End ? ' ' : Source[i];
    }

    return;
}// -- AhciFillScsiString();

/**
 * @name AhciZeroMemory
 * @implemented
 *
 * Clear buffer by filling zeros
 *
 * @param Buffer
 * @param BufferSize
 */
FORCEINLINE VOID AhciZeroMemory(__out PCHAR Buffer, __in ULONG BufferSize)
{
    ULONG i;
    for (i = 0; i < BufferSize; i++)
    {
        Buffer[i] = 0;
    }

    return;
}// -- AhciZeroMemory();

/**
 * @name IsPortValid
 * @implemented
 *
 * Tells wheather given port is implemented or not
 *
 * @param AdapterExtension
 * @param PathId
 *
 * @return
 * return TRUE if provided port is valid (implemented) or not
 */
FORCEINLINE BOOLEAN IsPortValid(__in PAHCI_ADAPTER_EXTENSION AdapterExtension, __in ULONG pathId)
{
    NT_ASSERT(pathId < MAXIMUM_AHCI_PORT_COUNT);

    if (pathId >= AdapterExtension->PortCount)
    {
        return FALSE;
    }

    return AdapterExtension->PortExtension[pathId].DeviceParams.IsActive;
}// -- IsPortValid()

/**
 * @name AddQueue
 * @implemented
 *
 * Add Srb to Queue
 *
 * @param Queue
 * @param Srb
 *
 * @return
 * return TRUE if Srb is successfully added to Queue
 *
 */
FORCEINLINE BOOLEAN AddQueue(__inout PAHCI_QUEUE Queue, __in PVOID Srb)
{
    NT_ASSERT(Queue->Head < MAXIMUM_QUEUE_BUFFER_SIZE);
    NT_ASSERT(Queue->Tail < MAXIMUM_QUEUE_BUFFER_SIZE);

    if (Queue->Tail == ((Queue->Head + 1) % MAXIMUM_QUEUE_BUFFER_SIZE))
        return FALSE;

    Queue->Buffer[Queue->Head++] = Srb;
    Queue->Head %= MAXIMUM_QUEUE_BUFFER_SIZE;

    return TRUE;
}// -- AddQueue();

/**
 * @name RemoveQueue
 * @implemented
 *
 * Remove and return Srb from Queue
 *
 * @param Queue
 *
 * @return
 * return Srb
 *
 */
FORCEINLINE PVOID RemoveQueue(__inout PAHCI_QUEUE Queue)
{
    PVOID Srb;

    NT_ASSERT(Queue->Head < MAXIMUM_QUEUE_BUFFER_SIZE);
    NT_ASSERT(Queue->Tail < MAXIMUM_QUEUE_BUFFER_SIZE);

    if (Queue->Head == Queue->Tail)
        return NULL;

    Srb = Queue->Buffer[Queue->Tail++];
    Queue->Tail %= MAXIMUM_QUEUE_BUFFER_SIZE;

    return Srb;
}// -- RemoveQueue();

/**
 * Return the next queued SRB without consuming it.
 */
FORCEINLINE PVOID PeekQueue(__in PAHCI_QUEUE Queue)
{
    NT_ASSERT(Queue->Head < MAXIMUM_QUEUE_BUFFER_SIZE);
    NT_ASSERT(Queue->Tail < MAXIMUM_QUEUE_BUFFER_SIZE);

    if (Queue->Head == Queue->Tail)
    {
        return NULL;
    }

    return Queue->Buffer[Queue->Tail];
}// -- PeekQueue();

/**
 * @name GetSrbExtension
 * @implemented
 *
 * GetSrbExtension from Srb make sure It is properly aligned
 *
 * @param Srb
 *
 * @return
 * return SrbExtension
 *
 */
FORCEINLINE PAHCI_SRB_EXTENSION GetSrbExtension(__in PSCSI_REQUEST_BLOCK Srb)
{
    ULONG Offset;
    ULONG_PTR SrbExtension;

    SrbExtension = (ULONG_PTR)Srb->SrbExtension;
    Offset = SrbExtension % 128;

    // CommandTable should be 128 byte aligned
    if (Offset != 0)
        Offset = 128 - Offset;

    return (PAHCI_SRB_EXTENSION)(SrbExtension + Offset);
}// -- PAHCI_SRB_EXTENSION();

/**
 * @name AhciGetLba
 * @implemented
 *
 * Find the logical address of demand block from Cdb
 *
 * @param Srb
 *
 * @return
 * return Logical Address of the block
 *
 */
FORCEINLINE ULONG64 AhciGetLba(__in PCDB Cdb, __in ULONG CdbLength)
{
    ULONG64 lba = 0;

    NT_ASSERT(Cdb != NULL);
    NT_ASSERT(CdbLength != 0);

    if (CdbLength == 0x10)
    {
        REVERSE_BYTES_QUAD(&lba, Cdb->CDB16.LogicalBlock);
    }
    else
    {
        lba |= Cdb->CDB10.LogicalBlockByte3 << 0;
        lba |= Cdb->CDB10.LogicalBlockByte2 << 8;
        lba |= Cdb->CDB10.LogicalBlockByte1 << 16;
        lba |= Cdb->CDB10.LogicalBlockByte0 << 24;
    }

    return lba;
}// -- AhciGetLba();
