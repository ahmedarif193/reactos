/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     FDO PnP dispatch for the SD host controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"

#define NDEBUG
#include <debug.h>

#include "hardware.h"

/**
 * @brief IoCompletion routine that signals an event when the lower driver finishes.
 *
 * Used by SdBusForwardIrpAndWait to synchronously wait for the lower
 * driver to complete the IRP.
 *
 * @param[in] DeviceObject  Pointer to the device object (unused).
 * @param[in] Irp           Pointer to the completed IRP (unused directly).
 * @param[in] Context       Pointer to a KEVENT to be signalled.
 *
 * @return STATUS_MORE_PROCESSING_REQUIRED to prevent further completion.
 */
static NTSTATUS
NTAPI
SdBusFdoStartCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT Event = (PKEVENT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/**
 * @brief Check whether any physical RAM range extends above 4 GB.
 *
 * ADMA2 in this driver currently uses 32-bit descriptors only, so pages above
 * 4 GB cannot be described directly.
 *
 * @return TRUE if memory above 4 GB is present, FALSE otherwise.
 */
static BOOLEAN
SdBusHasMemoryAbove4Gb(VOID)
{
    PPHYSICAL_MEMORY_RANGE MemoryRanges;
    PPHYSICAL_MEMORY_RANGE Range;
    BOOLEAN HasAbove4Gb = FALSE;
    const ULONGLONG Limit4Gb = 0x100000000ULL;

    MemoryRanges = MmGetPhysicalMemoryRanges();
    if (MemoryRanges == NULL)
    {
        /*
         * If query fails, keep ADMA2 enabled and rely on runtime fallback logic.
         */
        return FALSE;
    }

    for (Range = MemoryRanges;
         Range->BaseAddress.QuadPart || Range->NumberOfBytes.QuadPart;
         Range++)
    {
        ULONGLONG Base = (ULONGLONG)Range->BaseAddress.QuadPart;
        ULONGLONG Size = (ULONGLONG)Range->NumberOfBytes.QuadPart;

        if (Base >= Limit4Gb || (Size > 0 && Size > (Limit4Gb - Base)))
        {
            HasAbove4Gb = TRUE;
            break;
        }
    }

    ExFreePool(MemoryRanges);
    return HasAbove4Gb;
}

NTSTATUS
SdBusResetHost(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR ResetMask)
{
    ULONG Timeout;

    SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, ResetMask);

    Timeout = SD_RESET_TIMEOUT_MS;
    while (Timeout > 0)
    {
        if (!(SdBusReadReg8(FdoExtension, SDHCI_SOFTWARE_RESET) & ResetMask))
        {
            InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
            return STATUS_SUCCESS;
        }

        if (KeGetCurrentIrql() > APC_LEVEL)
        {
            KeStallExecutionProcessor(1000);
        }
        else
        {
            LARGE_INTEGER Delay;

            Delay.QuadPart = -10000LL;
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        }

        Timeout--;
    }

    DPRINT1("SdBusResetHost: reset 0x%02x timed out\n", ResetMask);
    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
    return STATUS_IO_TIMEOUT;
}

/**
 * @brief Release DMA adapter object if previously acquired.
 *
 * @param[in] FdoExtension  Pointer to the host controller FDO extension.
 */
static VOID
SdBusReleaseDmaAdapter(
    _In_ PFDO_EXTENSION FdoExtension)
{
    if (FdoExtension->DmaAdapter != NULL)
    {
        if (FdoExtension->DmaAdapter->DmaOperations &&
            FdoExtension->DmaAdapter->DmaOperations->PutDmaAdapter)
        {
            FdoExtension->DmaAdapter->DmaOperations->PutDmaAdapter(
                FdoExtension->DmaAdapter);
        }
        FdoExtension->DmaAdapter = NULL;
        FdoExtension->DmaMapRegisters = 0;
    }

    FdoExtension->UseDmaMappingForAdma2 = FALSE;
}

/**
 * @brief Initialize DMA adapter used for MDL -> bus-address mapping.
 *
 * The current ADMA implementation uses 32-bit descriptors. Configure the DMA
 * adapter for 32-bit addresses so HAL map-register translation keeps transfer
 * segments below 4 GB when needed.
 *
 * @param[in] FdoExtension  Pointer to the host controller FDO extension.
 *
 * @return STATUS_SUCCESS on success, STATUS_INSUFFICIENT_RESOURCES if no
 *         DMA adapter is available.
 */
static NTSTATUS
SdBusInitializeDmaAdapter(
    _In_ PFDO_EXTENSION FdoExtension)
{
    DEVICE_DESCRIPTION DeviceDescription;
    BOOLEAN ControllerSupports64BitDma;
    INTERFACE_TYPE InterfaceType;

    if (FdoExtension->DmaAdapter != NULL)
    {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&DeviceDescription, sizeof(DeviceDescription));
    ControllerSupports64BitDma =
        ((FdoExtension->HostCapabilities & SDHCI_CAP_64BIT_SYSTEM_BUS) != 0);
    InterfaceType = FdoExtension->BusInterfaceType;
    if (InterfaceType == InterfaceTypeUndefined)
    {
        InterfaceType = PCIBus;
    }

    DeviceDescription.Version = DEVICE_DESCRIPTION_VERSION;
    DeviceDescription.Master = TRUE;
    DeviceDescription.ScatterGather = TRUE;
    /*
     * Our ADMA descriptor format is currently 32-bit only. Request 32-bit
     * DMA addresses from HAL so map-register translation/bounce keeps SG
     * elements below 4GB even on controllers that advertise 64-bit DMA.
     */
    DeviceDescription.Dma32BitAddresses = TRUE;
    DeviceDescription.Dma64BitAddresses = FALSE;
    DeviceDescription.InterfaceType = InterfaceType;
    DeviceDescription.BusNumber = FdoExtension->BusNumber;
    DeviceDescription.DmaWidth = Width32Bits;
    DeviceDescription.DmaSpeed = Compatible;
    DeviceDescription.MaximumLength = MAXULONG;

    FdoExtension->DmaAdapter = IoGetDmaAdapter(FdoExtension->PhysicalDevice,
                                               &DeviceDescription,
                                               &FdoExtension->DmaMapRegisters);
    if (FdoExtension->DmaAdapter == NULL)
    {
        FdoExtension->DmaMapRegisters = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DPRINT1("SDHCI: DMA adapter initialized (requested 32-bit addresses, "
            "controller 64-bit capability: %s, map registers: %lu)\n",
            ControllerSupports64BitDma ? "yes" : "no",
            FdoExtension->DmaMapRegisters);

    return STATUS_SUCCESS;
}

/**
 * @brief Forward an IRP to the lower driver and wait synchronously for completion.
 *
 * @param[in]     FdoExtension  Pointer to the FDO device extension.
 * @param[in,out] Irp           Pointer to the IRP to forward.
 *
 * @return The NTSTATUS returned by the lower driver.
 */
static NTSTATUS
SdBusForwardIrpAndWait(
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           SdBusFdoStartCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = Irp->IoStatus.Status;
    }

    return Status;
}

/**
 * @brief Perform a full SDHCI controller software reset and initialization.
 *
 * Reads controller capabilities, performs a software reset, configures the
 * initial 400 kHz identification-mode clock, sets bus power, configures
 * the data timeout, clears pending interrupts, and enables interrupt
 * status reporting and signal delivery for card detect events.
 *
 * @param[in] FdoExtension  Pointer to the FDO device extension with mapped registers.
 *
 * @return STATUS_SUCCESS on success, or STATUS_IO_TIMEOUT / STATUS_SD_BUS_POWER_ERROR.
 */
static NTSTATUS
SdBusInitializeController(
    _In_ PFDO_EXTENSION FdoExtension)
{
    ULONG Caps;
    ULONG Caps2;
    USHORT Version;
    ULONG Timeout;
    USHORT ClockControl;
    UCHAR PowerControl;
    NTSTATUS Status;
    BOOLEAN HasMemoryAbove4Gb;
    BOOLEAN NeedDmaMappingForAdma2;
    LARGE_INTEGER Delay;

    DPRINT1("SdBusInitializeController: RegisterBase %p\n",
           FdoExtension->RegisterBase);

    /* Read host controller version */
    Version = SdBusReadReg16(FdoExtension, SDHCI_HOST_VERSION);
    FdoExtension->SpecVersion = Version & SDHCI_VERSION_MASK;

    /* Read capabilities */
    Caps = SdBusReadReg32(FdoExtension, SDHCI_CAPABILITIES);
    Caps2 = SdBusReadReg32(FdoExtension, SDHCI_CAPABILITIES2);
    Caps &= ~SDHCI_CAP_ADMA2_SUPPORT;
    FdoExtension->HostCapabilities = Caps;
    FdoExtension->HostCapabilities2 = Caps2;

    /* Extract base clock frequency */
    FdoExtension->MaxClockFrequency = SDHCI_BASE_CLK_MHZ(Caps) * 1000; /* kHz */

    DPRINT1("SDHCI: Version %u.%02u, Caps 0x%08lx, Caps2 0x%08lx, BaseClock %lu kHz\n",
           (FdoExtension->SpecVersion >> 4) + 1,
           FdoExtension->SpecVersion & 0x0F,
           Caps, Caps2,
           FdoExtension->MaxClockFrequency);

    HasMemoryAbove4Gb = SdBusHasMemoryAbove4Gb();
    NeedDmaMappingForAdma2 = HasMemoryAbove4Gb;
    FdoExtension->UseDmaMappingForAdma2 = FALSE;

    /*
     * Initialize DMA adapter only when ADMA2 needs map-register translation.
     * On <=4GB systems, direct PFN ADMA2 is used to avoid unnecessary HAL
     * mapping involvement in the steady state.
     */
    if ((Caps & SDHCI_CAP_ADMA2_SUPPORT) && NeedDmaMappingForAdma2)
    {
        Status = SdBusInitializeDmaAdapter(FdoExtension);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SDHCI: DMA adapter init failed (0x%08lx), ADMA mapping unavailable\n",
                    Status);
        }
    }

    /* Perform a full software reset */
    SdBusWriteReg8(FdoExtension, SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL);

    /* Wait for reset to complete */
    Timeout = SD_RESET_TIMEOUT_MS;
    while (Timeout > 0)
    {
        if (!(SdBusReadReg8(FdoExtension, SDHCI_SOFTWARE_RESET) & SDHCI_RESET_ALL))
        {
            break;
        }
        KeStallExecutionProcessor(1000); /* 1 ms */
        Timeout--;
    }

    if (Timeout == 0)
    {
        DPRINT1("SDHCI: Software reset timed out\n");
        return STATUS_IO_TIMEOUT;
    }

    SdBusHardwareInitializeController(FdoExtension);

    /* Set initial clock to 400 kHz for identification mode */
    if (FdoExtension->MaxClockFrequency > 0)
    {
        USHORT Divisor;
        USHORT DivisorHigh;

        Divisor = (USHORT)SDHCI_CALC_CLK_DIVIDER(FdoExtension->MaxClockFrequency,
                                              SD_INIT_CLOCK_KHZ);
        /* Clamp to 8-bit + 2-bit extended divisor */
        if (Divisor > 0x3FF)
        {
            Divisor = 0x3FF;
        }

        DivisorHigh = (Divisor & 0x300) >> 2; /* bits 9:8 -> bits 7:6 */
        ClockControl = (USHORT)((Divisor & 0xFF) << SDHCI_CLK_FREQ_SEL_SHIFT);
        ClockControl |= (USHORT)DivisorHigh;
        ClockControl |= SDHCI_CLK_INT_CLK_ENABLE;

        SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockControl);

        /* Wait for internal clock to stabilize */
        Timeout = 200;
        while (Timeout > 0)
        {
            ClockControl = SdBusReadReg16(FdoExtension, SDHCI_CLOCK_CONTROL);
            if (ClockControl & SDHCI_CLK_INT_CLK_STABLE)
            {
                break;
            }
            KeStallExecutionProcessor(1000);
            Timeout--;
        }

        if (Timeout == 0)
        {
            DPRINT1("SDHCI: Internal clock failed to stabilize\n");
            return STATUS_IO_TIMEOUT;
        }

        /* Enable SD clock output */
        ClockControl |= SDHCI_CLK_SD_CLK_ENABLE;
        SdBusWriteReg16(FdoExtension, SDHCI_CLOCK_CONTROL, ClockControl);
    }

    /* Reset leaves the host in 1-bit mode until card enumeration widens it. */
    FdoExtension->CurrentBusWidth = 1;

    /* Set bus power to 3.3V if supported */
    if (Caps & SDHCI_CAP_VOLTAGE_330)
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_330 | SDHCI_PC_BUS_POWER_ON;
    }
    else if (Caps & SDHCI_CAP_VOLTAGE_300)
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_300 | SDHCI_PC_BUS_POWER_ON;
    }
    else if (Caps & SDHCI_CAP_VOLTAGE_180)
    {
        PowerControl = SDHCI_PC_BUS_VOLTAGE_180 | SDHCI_PC_BUS_POWER_ON;
    }
    else
    {
        DPRINT1("SDHCI: No supported voltage found in capabilities\n");
        return STATUS_SD_BUS_POWER_ERROR;
    }
    SdBusWriteReg8(FdoExtension, SDHCI_POWER_CONTROL, PowerControl);

    Delay.QuadPart = -(LONGLONG)SD_POWER_UP_DELAY_MS * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    /* Set data timeout to maximum */
    SdBusWriteReg8(FdoExtension, SDHCI_TIMEOUT_CONTROL, 0x0E);

    /* Clear all pending interrupts */
    SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS, SDHCI_INT_ALL_MASK);

    /* Enable status reporting for command/data completion and card detect */
    SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS_ENABLE,
                    SDHCI_INT_CMD_COMPLETE |
                    SDHCI_INT_XFER_COMPLETE |
                    SDHCI_INT_DMA |
                    SDHCI_INT_BUFFER_WRITE_READY |
                    SDHCI_INT_BUFFER_READ_READY |
                    SDHCI_INT_CARD_INSERTION |
                    SDHCI_INT_CARD_REMOVAL |
                    SDHCI_INT_CARD_INTERRUPT |
                    SDHCI_INT_ERROR |
                    SDHCI_INT_CMD_ERROR_MASK |
                    SDHCI_INT_DATA_ERROR_MASK);

    /*
     * Transfer/error signaling only. Card-detect signaling is deferred to
     * SdBusFdoStartDevice (after DeviceState==Started): the ISR drops interrupts
     * unacknowledged until Started, so a level-triggered card interrupt from an
     * already-powered SDIO card (on-board Wi-Fi) would storm here and wedge boot.
     */
    SdBusWriteReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE,
                    SDHCI_INT_CMD_COMPLETE |
                    SDHCI_INT_XFER_COMPLETE |
                    SDHCI_INT_DMA |
                    SDHCI_INT_BUFFER_WRITE_READY |
                    SDHCI_INT_BUFFER_READ_READY |
                    SDHCI_INT_ERROR |
                    SDHCI_INT_CMD_ERROR_MASK |
                    SDHCI_INT_DATA_ERROR_MASK);

    /*
     * Try ADMA2 first (scatter-gather directly to caller pages, no bounce buffer).
     *
     * Only when no DMA address mapping is required. Direct ADMA2 puts the
     * caller's buffer pages into 32-bit descriptor addresses, which truncates
     * any page above 4 GB. On platforms whose RAM lives above 4 GB the HAL
     * DMA adapter provides no map registers / bounce,
     * so direct ADMA2 silently targets the wrong physical address and the data
     * transfer never completes. In that case fall through to the SDMA path,
     * which DMAs into the guaranteed-below-4 GB bounce buffer and copies up.
     */
    FdoExtension->UseAdma2 = FALSE;
    if ((Caps & SDHCI_CAP_ADMA2_SUPPORT) && !NeedDmaMappingForAdma2)
    {
        PHYSICAL_ADDRESS HighestAcceptable;
        HighestAcceptable.QuadPart = 0xFFFFFFFF; /* 32-bit addressable */

        FdoExtension->Adma2MaxDescriptors = 512;
        FdoExtension->Adma2Table = (PSDHCI_ADMA2_DESCRIPTOR_32)
            MmAllocateContiguousMemory(
                FdoExtension->Adma2MaxDescriptors * sizeof(SDHCI_ADMA2_DESCRIPTOR_32),
                HighestAcceptable);

        if (FdoExtension->Adma2Table != NULL)
        {
            FdoExtension->Adma2TablePhysical =
                MmGetPhysicalAddress(FdoExtension->Adma2Table);
            FdoExtension->UseAdma2 = TRUE;
            FdoExtension->UseDmaMappingForAdma2 = NeedDmaMappingForAdma2;

            DPRINT1("SDHCI: ADMA2 table at VA=%p PA=0x%08lx (%lu descriptors)\n",
                    FdoExtension->Adma2Table,
                    FdoExtension->Adma2TablePhysical.LowPart,
                    FdoExtension->Adma2MaxDescriptors);
            DPRINT1("SDHCI: ADMA2 path uses %s\n",
                    FdoExtension->UseDmaMappingForAdma2 ?
                    "direct descriptors with DMA mapping fallback for >4GB MDLs" :
                    "direct PFN descriptors (<=4GB RAM)");
        }
        else
        {
            DPRINT1("SDHCI: ADMA2 table allocation failed, trying SDMA\n");
        }
    }
    else if ((Caps & SDHCI_CAP_ADMA2_SUPPORT) && NeedDmaMappingForAdma2)
    {
        DPRINT1("SDHCI: Memory above 4GB and no DMA mapping support, disabling ADMA2-32 and using SDMA\n");
    }

    /* Allocate SDMA bounce buffer as fallback (and backup path for ADMA2) */
    FdoExtension->UseSdma = FALSE;
    if (Caps & SDHCI_CAP_SDMA_SUPPORT)
    {
        PHYSICAL_ADDRESS HighestAcceptable;
        PHYSICAL_ADDRESS LowestAcceptable;
        PHYSICAL_ADDRESS BoundaryMultiple;
        HighestAcceptable.QuadPart = 0xFFFFFFFF; /* 32-bit addressable */
        LowestAcceptable.QuadPart = 0;
        BoundaryMultiple.QuadPart = 0;

        FdoExtension->DmaBufferSize = 256 * 1024;
        FdoExtension->DmaBuffer = MmAllocateContiguousMemorySpecifyCache(
            FdoExtension->DmaBufferSize, LowestAcceptable, HighestAcceptable,
            BoundaryMultiple, MmNonCached);
        if (FdoExtension->DmaBuffer == NULL)
        {
            FdoExtension->DmaBufferSize = 64 * 1024;
            FdoExtension->DmaBuffer = MmAllocateContiguousMemorySpecifyCache(
                FdoExtension->DmaBufferSize, LowestAcceptable, HighestAcceptable,
                BoundaryMultiple, MmNonCached);
        }

        if (FdoExtension->DmaBuffer != NULL)
        {
            FdoExtension->DmaPhysical =
                MmGetPhysicalAddress(FdoExtension->DmaBuffer);
            FdoExtension->UseSdma = TRUE;

            DPRINT1("SDHCI: SDMA buffer at VA=%p PA=0x%08lx (%lu bytes)\n",
                    FdoExtension->DmaBuffer,
                    FdoExtension->DmaPhysical.LowPart,
                    FdoExtension->DmaBufferSize);
        }
        else
        {
            DPRINT1("SDHCI: SDMA buffer allocation failed, using PIO\n");
            FdoExtension->DmaBufferSize = 0;
        }
    }
    else if (!FdoExtension->UseAdma2)
    {
        DPRINT1("SDHCI: Controller does not support DMA, using PIO\n");
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_START_DEVICE for the FDO.
 *
 * Forwards the start IRP to the lower driver, parses translated resources
 * to locate the SDHCI memory-mapped register block and interrupt, maps the
 * registers, connects the ISR, initializes the controller hardware, and
 * triggers initial card-detect enumeration if a card is already inserted.
 *
 * @param[in]     FdoExtension  Pointer to the FDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_START_DEVICE IRP.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
static NTSTATUS
SdBusFdoStartDevice(
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PCM_RESOURCE_LIST ResourceList;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDesc;
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG i;
    NTSTATUS Status;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Length = 0;
    BOOLEAN FoundMemory = FALSE;
    ULONG InterruptVector = 0;
    KIRQL InterruptLevel = 0;
    KAFFINITY InterruptAffinity = 0;
    KINTERRUPT_MODE InterruptMode = LevelSensitive;
    BOOLEAN FoundInterrupt = FALSE;
    BOOLEAN InterruptShareable = FALSE;

    /* Forward START_DEVICE to the lower driver first */
    Status = SdBusForwardIrpAndWait(FdoExtension, Irp);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusFdoStartDevice: Lower driver failed start (0x%08lx)\n", Status);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    /* Parse the translated resource list */
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    ResourceList = IoStack->Parameters.StartDevice.AllocatedResourcesTranslated;

    if (ResourceList == NULL || ResourceList->Count == 0)
    {
        DPRINT1("SdBusFdoStartDevice: No resources assigned\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    FullDesc = &ResourceList->List[0];
    PartialList = &FullDesc->PartialResourceList;
    FdoExtension->BusInterfaceType = FullDesc->InterfaceType;
    FdoExtension->BusNumber = FullDesc->BusNumber;

    for (i = 0; i < PartialList->Count; i++)
    {
        Descriptor = &PartialList->PartialDescriptors[i];

        switch (Descriptor->Type)
        {
            case CmResourceTypeMemory:
                if (!FoundMemory)
                {
                    PhysicalAddress = Descriptor->u.Memory.Start;
                    Length = Descriptor->u.Memory.Length;
                    FoundMemory = TRUE;

                    DPRINT1("SDHCI: Memory resource at 0x%I64x length 0x%lx\n",
                           PhysicalAddress.QuadPart, Length);
                }
                break;

            case CmResourceTypeInterrupt:
                if (!FoundInterrupt)
                {
                    InterruptVector = Descriptor->u.Interrupt.Vector;
                    InterruptLevel = (KIRQL)Descriptor->u.Interrupt.Level;
                    InterruptAffinity = Descriptor->u.Interrupt.Affinity;
                    InterruptShareable = (Descriptor->ShareDisposition == CmResourceShareShared);
                    InterruptMode = (Descriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ?
                                    Latched : LevelSensitive;
                    FoundInterrupt = TRUE;

                    DPRINT1("SDHCI: Interrupt vector %lu level %u\n",
                           InterruptVector, InterruptLevel);
                }
                break;

            default:
                break;
        }
    }

    if (!FoundMemory)
    {
        DPRINT1("SdBusFdoStartDevice: No memory resource found\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    /* Map the SDHCI registers */
    FdoExtension->RegisterBase = MmMapIoSpace(PhysicalAddress,
                                               Length,
                                               MmNonCached);
    if (FdoExtension->RegisterBase == NULL)
    {
        DPRINT1("SdBusFdoStartDevice: MmMapIoSpace failed\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    FdoExtension->RegisterLength = Length;
    FdoExtension->RegistersMapped = TRUE;

    Status = SdBusHardwareMapResources(FdoExtension,
                                       PhysicalAddress,
                                       PartialList);
    if (!NT_SUCCESS(Status))
    {
        MmUnmapIoSpace(FdoExtension->RegisterBase, FdoExtension->RegisterLength);
        FdoExtension->RegisterBase = NULL;
        FdoExtension->RegistersMapped = FALSE;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    /* Connect the interrupt if available */
    if (FoundInterrupt)
    {
        Status = IoConnectInterrupt(&FdoExtension->InterruptObject,
                                    SdBusInterruptService,
                                    FdoExtension,
                                    NULL,
                                    InterruptVector,
                                    InterruptLevel,
                                    InterruptLevel,
                                    InterruptMode,
                                    InterruptShareable,
                                    InterruptAffinity,
                                    FALSE);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdBusFdoStartDevice: IoConnectInterrupt failed (0x%08lx)\n", Status);
            SdBusHardwareRelease(FdoExtension);
            MmUnmapIoSpace(FdoExtension->RegisterBase, FdoExtension->RegisterLength);
            FdoExtension->RegisterBase = NULL;
            FdoExtension->RegistersMapped = FALSE;
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
    }

    /* Initialize the SDHCI controller */
    Status = SdBusInitializeController(FdoExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusFdoStartDevice: Controller init failed (0x%08lx)\n", Status);
        if (FdoExtension->InterruptObject)
        {
            IoDisconnectInterrupt(FdoExtension->InterruptObject);
            FdoExtension->InterruptObject = NULL;
        }

        if (FdoExtension->Adma2Table != NULL)
        {
            MmFreeContiguousMemory(FdoExtension->Adma2Table);
            FdoExtension->Adma2Table = NULL;
            FdoExtension->UseAdma2 = FALSE;
        }

        if (FdoExtension->DmaBuffer != NULL)
        {
            MmFreeContiguousMemory(FdoExtension->DmaBuffer);
            FdoExtension->DmaBuffer = NULL;
            FdoExtension->UseSdma = FALSE;
        }

        SdBusReleaseDmaAdapter(FdoExtension);
        SdBusHardwareRelease(FdoExtension);
        MmUnmapIoSpace(FdoExtension->RegisterBase, FdoExtension->RegisterLength);
        FdoExtension->RegisterBase = NULL;
        FdoExtension->RegistersMapped = FALSE;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    FdoExtension->Common.DeviceState = SdBusDeviceStateStarted;

    Status = SdBusInitializeRequestQueue(FdoExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusFdoStartDevice: SdBusInitializeRequestQueue failed (0x%08lx)\n",
                Status);
    }

    (VOID)SdBusHardwareAttach(FdoExtension);

    {
        ULONG PresentState;
        ULONG SettleWait = 100;
        LARGE_INTEGER SettleDelay;

        do
        {
            PresentState = SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE);
            if (PresentState & (SDHCI_PS_CARD_STATE_STABLE | SDHCI_PS_CARD_INSERTED))
                break;
            SettleDelay.QuadPart = -50000;
            KeDelayExecutionThread(KernelMode, FALSE, &SettleDelay);
        } while (--SettleWait);

        PresentState = SdBusReadReg32(FdoExtension, SDHCI_PRESENT_STATE);

        DPRINT1("SdBusFdoStartDevice: PresentState=0x%08lx HostControl=0x%02x settle=%lu\n",
                PresentState,
                SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL),
                SettleWait);

        if ((PresentState & SDHCI_PS_CARD_INSERTED) || FdoExtension->NonRemovable)
        {
            Status = SdBusEnumerateInsertedCard(FdoExtension, FALSE, FALSE);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("SdBusFdoStartDevice: initial card enumeration failed "
                        "(0x%08lx)\n", Status);
            }
        }
    }

    if (!FdoExtension->NonRemovable)
    {
        /*
         * The synchronous startup scan consumed the initial card-present state.
         * Drop that latched insertion/removal edge before enabling hotplug
         * signals, otherwise level-triggered card-detect can fire during PnP
         * start and race the startup path.
         */
        SdBusWriteReg32(FdoExtension,
                        SDHCI_INT_STATUS,
                        SDHCI_INT_CARD_INSERTION | SDHCI_INT_CARD_REMOVAL);
        SdBusUpdateInterruptSignalEnable(FdoExtension,
                                         SDHCI_INT_CARD_INSERTION |
                                         SDHCI_INT_CARD_REMOVAL,
                                         0);
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_QUERY_DEVICE_RELATIONS for BusRelations.
 *
 * Allocates and populates a DEVICE_RELATIONS structure containing
 * references to all currently present child PDOs (enumerated SD cards).
 *
 * @param[in]     FdoExtension  Pointer to the FDO device extension.
 * @param[in,out] Irp           Pointer to the query-relations IRP.
 *
 * @return STATUS_SUCCESS on success, or STATUS_INSUFFICIENT_RESOURCES.
 */
static NTSTATUS
SdBusFdoQueryBusRelations(
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp)
{
    PDEVICE_RELATIONS Relations;
    PPDO_EXTENSION PdoExtension;
    PLIST_ENTRY Entry;
    ULONG Capacity;
    ULONG Index;
    KIRQL OldIrql;
    BOOLEAN Retry;

    do
    {
        KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
        Capacity = FdoExtension->ChildPdoCount;
        KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

        Relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(
            PagedPool,
            sizeof(DEVICE_RELATIONS) +
            (Capacity > 0 ? (Capacity - 1) : 0) * sizeof(PDEVICE_OBJECT),
            TAG_SDBUS);
        if (Relations == NULL)
        {
            Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Retry = FALSE;
        Index = 0;

        KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);
        for (Entry = FdoExtension->ChildPdoList.Flink;
             Entry != &FdoExtension->ChildPdoList;
             Entry = Entry->Flink)
        {
            PdoExtension = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
            if (!PdoExtension->Present)
            {
                continue;
            }

            if (Index >= Capacity)
            {
                Retry = TRUE;
                break;
            }

            Relations->Objects[Index] = PdoExtension->Common.Self;
            ObReferenceObject(PdoExtension->Common.Self);
            Index++;
        }
        KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

        if (Retry)
        {
            while (Index > 0)
            {
                Index--;
                ObDereferenceObject(Relations->Objects[Index]);
            }

            ExFreePoolWithTag(Relations, TAG_SDBUS);
        }
        else
        {
            Relations->Count = Index;
        }
    } while (Retry);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_STOP_DEVICE for the FDO.
 *
 * Disables all SDHCI interrupts, disconnects the ISR, marks the device
 * as stopped, and passes the IRP to the lower driver.
 *
 * @param[in]     FdoExtension  Pointer to the FDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_STOP_DEVICE IRP.
 *
 * @return NTSTATUS from the lower driver.
 */
static NTSTATUS
SdBusFdoStopDevice(
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp)
{
    FdoExtension->Common.DeviceState = SdBusDeviceStateStopped;

    /* Disable all interrupts */
    if (FdoExtension->RegistersMapped)
    {
        SdBusWriteReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE, 0);
        SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS_ENABLE, 0);
    }

    /* Disconnect interrupt */
    if (FdoExtension->InterruptObject)
    {
        IoDisconnectInterrupt(FdoExtension->InterruptObject);
        FdoExtension->InterruptObject = NULL;
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(FdoExtension->LowerDevice, Irp);
}

/**
 * @brief Handle IRP_MN_REMOVE_DEVICE for the FDO.
 *
 * Disables interrupts, disconnects the ISR, cancels the card-detect DPC,
 * marks all child PDOs as absent, passes the IRP down, waits for pending
 * I/O via IoReleaseRemoveLockAndWait, unmaps registers, detaches from
 * the device stack, and deletes the FDO.
 *
 * @param[in]     FdoExtension  Pointer to the FDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_REMOVE_DEVICE IRP.
 *
 * @return STATUS_SUCCESS.
 */
static NTSTATUS
SdBusFdoRemoveDevice(
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp)
{
    PLIST_ENTRY Entry;
    PPDO_EXTENSION PdoExtension;
    KIRQL OldIrql;

    FdoExtension->Common.DeviceState = SdBusDeviceStateRemoved;

    /* Disable all interrupts and disconnect */
    if (FdoExtension->RegistersMapped)
    {
        SdBusWriteReg32(FdoExtension, SDHCI_INT_SIGNAL_ENABLE, 0);
        SdBusWriteReg32(FdoExtension, SDHCI_INT_STATUS_ENABLE, 0);
    }

    if (FdoExtension->InterruptObject)
    {
        IoDisconnectInterrupt(FdoExtension->InterruptObject);
        FdoExtension->InterruptObject = NULL;
    }

    /* Cancel pending DPCs */
    KeRemoveQueueDpc(&FdoExtension->CardDetectDpc);

    /* Mark all child PDOs as no longer present */
    KeAcquireSpinLock(&FdoExtension->Lock, &OldIrql);

    for (Entry = FdoExtension->ChildPdoList.Flink;
         Entry != &FdoExtension->ChildPdoList;
         Entry = Entry->Flink)
    {
        PdoExtension = CONTAINING_RECORD(Entry, PDO_EXTENSION, ListEntry);
        PdoExtension->Present = FALSE;
        PdoExtension->ReportedMissing = TRUE;
    }

    KeReleaseSpinLock(&FdoExtension->Lock, OldIrql);

    SdBusTearDownRequestQueue(FdoExtension);

    /* Pass the IRP down */
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    IoCallDriver(FdoExtension->LowerDevice, Irp);

    /* Wait for pending I/O and queued work items to drain */
    IoReleaseRemoveLockAndWait(&FdoExtension->RemoveLock, Irp);

    /* Release DMA adapter */
    SdBusReleaseDmaAdapter(FdoExtension);

    /* Free ADMA2 descriptor table */
    if (FdoExtension->Adma2Table != NULL)
    {
        MmFreeContiguousMemory(FdoExtension->Adma2Table);
        FdoExtension->Adma2Table = NULL;
        FdoExtension->UseAdma2 = FALSE;
    }

    /* Free SDMA bounce buffer */
    if (FdoExtension->DmaBuffer != NULL)
    {
        MmFreeContiguousMemory(FdoExtension->DmaBuffer);
        FdoExtension->DmaBuffer = NULL;
        FdoExtension->UseSdma = FALSE;
    }

    /* Unmap registers after all in-flight work has completed */
    SdBusHardwareRelease(FdoExtension);

    if (FdoExtension->RegistersMapped)
    {
        MmUnmapIoSpace(FdoExtension->RegisterBase, FdoExtension->RegisterLength);
        FdoExtension->RegisterBase = NULL;
        FdoExtension->RegistersMapped = FALSE;
    }

    /* Detach and delete */
    IoDetachDevice(FdoExtension->LowerDevice);
    IoDeleteDevice(FdoExtension->Common.Self);

    return STATUS_SUCCESS;
}

/**
 * @brief Handle IRP_MN_SURPRISE_REMOVAL for the FDO.
 *
 * Marks the device state as surprise-removed and passes the IRP to the
 * lower driver.
 *
 * @param[in]     FdoExtension  Pointer to the FDO device extension.
 * @param[in,out] Irp           Pointer to the IRP_MN_SURPRISE_REMOVAL IRP.
 *
 * @return NTSTATUS from the lower driver.
 */
static NTSTATUS
SdBusFdoSurpriseRemoval(
    _In_ PFDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp)
{
    FdoExtension->Common.DeviceState = SdBusDeviceStateSurpriseRemoved;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(FdoExtension->LowerDevice, Irp);
}

/**
 * @brief Top-level PnP dispatch for the SD bus FDO.
 *
 * Acquires the remove lock, dispatches to minor-function-specific handlers
 * (start, stop, remove, surprise removal, bus relations), and passes
 * unhandled PnP IRPs to the lower driver.
 *
 * @param[in]     DeviceObject  Pointer to the FDO device object.
 * @param[in,out] Irp           Pointer to the PnP IRP.
 *
 * @return NTSTATUS from the specific PnP handler or the lower driver.
 */
NTSTATUS
SdBusFdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFDO_EXTENSION FdoExtension;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    FdoExtension = (PFDO_EXTENSION)DeviceObject->DeviceExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    Status = IoAcquireRemoveLock(&FdoExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            DPRINT("SdBusFdoPnp: IRP_MN_START_DEVICE\n");
            Status = SdBusFdoStartDevice(FdoExtension, Irp);
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (IoStack->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                DPRINT("SdBusFdoPnp: IRP_MN_QUERY_DEVICE_RELATIONS (BusRelations)\n");
                Status = SdBusFdoQueryBusRelations(FdoExtension, Irp);
            }
            else
            {
                /* Not our type, pass down */
                IoSkipCurrentIrpStackLocation(Irp);
                Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
            }
            break;

        case IRP_MN_STOP_DEVICE:
            DPRINT("SdBusFdoPnp: IRP_MN_STOP_DEVICE\n");
            Status = SdBusFdoStopDevice(FdoExtension, Irp);
            break;

        case IRP_MN_REMOVE_DEVICE:
            DPRINT("SdBusFdoPnp: IRP_MN_REMOVE_DEVICE\n");
            Status = SdBusFdoRemoveDevice(FdoExtension, Irp);
            /* RemoveLock released inside */
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
            DPRINT("SdBusFdoPnp: IRP_MN_SURPRISE_REMOVAL\n");
            Status = SdBusFdoSurpriseRemoval(FdoExtension, Irp);
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
            break;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            DPRINT("SdBusFdoPnp: IRP_MN_FILTER_RESOURCE_REQUIREMENTS\n");
            IoCopyCurrentIrpStackLocationToNext(Irp);
            Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
            break;

        default:
            /* Pass all unhandled PnP IRPs down to the lower driver */
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
            break;
    }

    IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
    return Status;
}
