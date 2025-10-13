/*
 * PROJECT:        ReactOS Kernel
 * LICENSE:        GNU GPLv2 only as published by the Free Software Foundation
 * PURPOSE:        AHCI Miniport (SCSIPORT skeleton)
 */

#include "ahciport.h"

static ULONG NTAPI
AhciHwFindAdapter(
    _In_ PVOID DeviceExtension,
    _In_ PVOID HwContext,
    _In_ PVOID BusInformation,
    _In_ PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _Out_ PBOOLEAN Again)
{
    PAHCI_ADAPTER_EXTENSION adapter = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;
    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "ahciport: HwFindAdapter called! Bus=%lu\n",
               ConfigInfo->SystemIoBusNumber));

    *Again = FALSE;

    /* Minimal configuration: one bus; leave ranges for SCSIPORT */
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->Master = TRUE;
    ConfigInfo->MapBuffers = TRUE;
    ConfigInfo->NeedPhysicalAddresses = TRUE;
    ConfigInfo->MaximumNumberOfTargets = 32; /* AHCI max ports */
    ConfigInfo->MaximumTransferLength = 128 * 1024; /* conservative */
    ConfigInfo->AlignmentMask = 0x1; /* word alignment */
    ConfigInfo->Dma32BitAddresses = TRUE;

    /* Phase 3: Probe access ranges to locate ABAR (memory BAR) */
    if (ConfigInfo->NumberOfAccessRanges > 0 && ConfigInfo->AccessRanges)
    {
        ACCESS_RANGE (*ranges)[] = ConfigInfo->AccessRanges;
        ULONG i;
        for (i = 0; i < ConfigInfo->NumberOfAccessRanges; ++i)
        {
            ACCESS_RANGE *ar = &(*ranges)[i];
            if (ar->RangeInMemory && ar->RangeLength >= 0x1000)
            {
                BOOLEAN inIoSpace = FALSE; /* memory */
                PVOID base = ScsiPortGetDeviceBase(DeviceExtension,
                                                   ConfigInfo->AdapterInterfaceType,
                                                   ConfigInfo->SystemIoBusNumber,
                                                   ar->RangeStart,
                                                   ar->RangeLength,
                                                   inIoSpace);
                if (base != NULL)
                {
                    PAHCI_HBA_MEM hba = (PAHCI_HBA_MEM)base;
                    ULONG vs = hba->VS;
                    ULONG cap = hba->CAP;
                    ULONG pi = hba->PI;

                    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                               "ahciport: Checking ABAR at %p: VS=0x%08x CAP=0x%08x PI=0x%08x\n",
                               hba, vs, cap, pi));

                    if (vs != 0 && cap != 0 && pi != 0)
                    {
                        adapter->AbBase = hba;
                        adapter->Version = vs;
                        adapter->Cap = cap;
                        adapter->PortsImplemented = pi;

                        /* Count ports by highest set bit */
                        if (pi)
                        {
                            UCHAR idx;
                            for (idx = 31; idx > 0; --idx)
                            {
                                if (pi & (1u << idx)) { adapter->PortCount = idx + 1; break; }
                            }
                            if (adapter->PortCount == 0) adapter->PortCount = 1;
                        }

                        /* Allocate non-cached memory for per-port CL and FIS (phase scaffold) */
                        {
                            ULONG clSize = 1024;   /* AHCI: 1KB aligned */
                            ULONG fisSize = 256;  /* AHCI: 256-byte align */
                            ULONG perPort = AHCI_ALIGN_UP(clSize, 1024) + AHCI_ALIGN_UP(fisSize, 256);
                            ULONG total = perPort * adapter->PortCount;
                            PVOID base = ScsiPortGetUncachedExtension(DeviceExtension, ConfigInfo, total);
                            if (base)
                            {
                                ULONG off = 0;
                                adapter->NonCachedBase = base;
                                adapter->NonCachedBytes = total;
                                for (i = 0; i < adapter->PortCount; ++i)
                                {
                                    PVOID cl = (PVOID)((PUCHAR)base + off);
                                    off += AHCI_ALIGN_UP(clSize, 1024);
                                    PVOID rfis = (PVOID)((PUCHAR)base + off);
                                    off += AHCI_ALIGN_UP(fisSize, 256);
                                    adapter->CommandList[i] = cl;
                                    adapter->ReceivedFIS[i] = rfis;
                                }
                            }
                        }

                        /* Claim the device */
                        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                                   "ahciport: Found AHCI controller! Ports=%u Version=0x%08x\n",
                                   adapter->PortCount, adapter->Version));
                        return SP_RETURN_FOUND;
                    }
                    /* Not a valid AHCI BAR, release */
                    ScsiPortFreeDeviceBase(DeviceExtension, base);
                }
            }
        }
    }

    /* We don’t claim a device yet */
    return SP_RETURN_NOT_FOUND;
}

static BOOLEAN NTAPI
AhciHwInitialize(
    _In_ PVOID DeviceExtension)
{
    PAHCI_ADAPTER_EXTENSION adapter = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;
    ULONG pi;
    ULONG port;

    if (adapter == NULL || adapter->AbBase == NULL)
        return FALSE;

    /* Clear global interrupts and enable GHC.IE */
    adapter->AbBase->IS = 0xFFFFFFFF; /* clear */
    adapter->AbBase->GHC |= AHCI_GHC_IE;

    pi = adapter->PortsImplemented;
    for (port = 0; port < 32; ++port)
    {
        if (pi & (1u << port))
        {
            volatile ULONG *pxclb  = (volatile ULONG *)((PUCHAR)adapter->AbBase + AHCI_PORT_REG_BASE + port * AHCI_PORT_STRIDE + AHCI_PxCLB);
            volatile ULONG *pxfb   = (volatile ULONG *)((PUCHAR)adapter->AbBase + AHCI_PORT_REG_BASE + port * AHCI_PORT_STRIDE + AHCI_PxFB);
            volatile ULONG *pxis   = (volatile ULONG *)((PUCHAR)adapter->AbBase + AHCI_PORT_REG_BASE + port * AHCI_PORT_STRIDE + AHCI_PxIS);
            volatile ULONG *pxserr = (volatile ULONG *)((PUCHAR)adapter->AbBase + AHCI_PORT_REG_BASE + port * AHCI_PORT_STRIDE + AHCI_PxSERR);

            if (adapter->CommandList[port] && adapter->ReceivedFIS[port])
            {
                ULONG ml;
                SCSI_PHYSICAL_ADDRESS phys;
                phys = ScsiPortGetPhysicalAddress(adapter, NULL, adapter->CommandList[port], &ml);
                *pxclb = phys.LowPart;
                phys = ScsiPortGetPhysicalAddress(adapter, NULL, adapter->ReceivedFIS[port], &ml);
                *pxfb = phys.LowPart;
            }

            *pxis = 0xFFFFFFFF; /* clear port interrupts */
            *pxserr = 0xFFFFFFFF; /* clear errors */
        }
    }

    /* Notify bus change so SCSIPORT probes targets */
    ScsiPortNotification(BusChangeDetected, adapter, 0);
    return TRUE;
}

static BOOLEAN NTAPI
AhciHwStartIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PAHCI_ADAPTER_EXTENSION adapter = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;
    PCDB cdb;

    if (Srb == NULL)
        return FALSE;

    /* Only PathId 0 supported; target is AHCI port index; Lun 0 */
    if (Srb->PathId != 0 || Srb->Lun != 0)
    {
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        goto complete;
    }

    if (adapter == NULL || adapter->AbBase == NULL)
    {
        Srb->SrbStatus = SRB_STATUS_ERROR;
        goto complete;
    }

    if (!(adapter->PortsImplemented & (1u << Srb->TargetId)))
    {
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        goto complete;
    }

    if (Srb->Function != SRB_FUNCTION_EXECUTE_SCSI)
    {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        goto complete;
    }

    cdb = (PCDB)Srb->Cdb;
    switch (cdb->CDB6GENERIC.OperationCode)
    {
        case SCSIOP_TEST_UNIT_READY:
        {
            if (AhciIsPortDevicePresent(adapter, Srb->TargetId))
            {
                Srb->ScsiStatus = SCSISTAT_GOOD;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
            }
            else
            {
                Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
            }
            break;
        }
        case SCSIOP_INQUIRY:
        {
            PINQUIRYDATA inq;
            ULONG len = Srb->DataTransferLength;
            if (Srb->DataBuffer == NULL || len < sizeof(INQUIRYDATA))
            {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                break;
            }
            RtlZeroMemory(Srb->DataBuffer, len);
            inq = (PINQUIRYDATA)Srb->DataBuffer;
            if (AhciIsPortDevicePresent(adapter, Srb->TargetId))
            {
                inq->DeviceType = DIRECT_ACCESS_DEVICE;
                inq->RemovableMedia = 0;
                inq->Versions = 5; /* SPC-ish */
                inq->ResponseDataFormat = 2;
                inq->AdditionalLength = 0x1F; /* 36 - 5 */
                RtlCopyMemory(inq->VendorId, "ReactOS ", 8);
                RtlCopyMemory(inq->ProductId, "AHCI Disk       ", 16);
                RtlCopyMemory(inq->ProductRevisionLevel, "0001", 4);
                Srb->ScsiStatus = SCSISTAT_GOOD;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
            }
            else
            {
                /* Report no device */
                inq->DeviceType = 0x1F; /* unknown */
                Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
            }
            break;
        }
        case SCSIOP_REQUEST_SENSE:
        {
            PSENSE_DATA sd = (PSENSE_DATA)Srb->DataBuffer;
            ULONG len = Srb->DataTransferLength;
            if (sd && len >= sizeof(SENSE_DATA))
            {
                RtlZeroMemory(sd, sizeof(SENSE_DATA));
                sd->ErrorCode = 0x70; /* current errors */
                sd->Valid = 0;
                Srb->ScsiStatus = SCSISTAT_GOOD;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
            }
            else
            {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            }
            break;
        }
        case SCSIOP_READ_CAPACITY:
        {
            /* Return a small fake capacity with 512-byte blocks if present */
            PUCHAR buf = (PUCHAR)Srb->DataBuffer;
            if (AhciIsPortDevicePresent(adapter, Srb->TargetId) && buf && Srb->DataTransferLength >= 8)
            {
                ULONG lastLba = 0x0000FFFF; /* 64MB approx */
                ULONG blkLen = 512;
                buf[0] = (UCHAR)((lastLba >> 24) & 0xFF);
                buf[1] = (UCHAR)((lastLba >> 16) & 0xFF);
                buf[2] = (UCHAR)((lastLba >> 8) & 0xFF);
                buf[3] = (UCHAR)((lastLba) & 0xFF);
                buf[4] = (UCHAR)((blkLen >> 24) & 0xFF);
                buf[5] = (UCHAR)((blkLen >> 16) & 0xFF);
                buf[6] = (UCHAR)((blkLen >> 8) & 0xFF);
                buf[7] = (UCHAR)((blkLen) & 0xFF);
                Srb->ScsiStatus = SCSISTAT_GOOD;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
            }
            else
            {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            }
            break;
        }
        case SCSIOP_READ:
        {
            /* Temporary stub: zero-fill read for basic bring-up */
            if (AhciIsPortDevicePresent(adapter, Srb->TargetId) &&
                Srb->DataBuffer && Srb->DataTransferLength)
            {
                RtlZeroMemory(Srb->DataBuffer, Srb->DataTransferLength);
                Srb->ScsiStatus = SCSISTAT_GOOD;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
            }
            else
            {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            }
            break;
        }
        case SCSIOP_WRITE:
        {
            /* Temporary stub: drop writes, report success */
            if (AhciIsPortDevicePresent(adapter, Srb->TargetId))
            {
                Srb->ScsiStatus = SCSISTAT_GOOD;
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
            }
            else
            {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            }
            break;
        }
        default:
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }

complete:
    ScsiPortNotification(RequestComplete, DeviceExtension, Srb);
    ScsiPortNotification(NextRequest, DeviceExtension, 0);
    return TRUE;
}

static BOOLEAN NTAPI
AhciHwInterrupt(
    _In_ PVOID DeviceExtension)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    /* No interrupt handled */
    return FALSE;
}

static BOOLEAN NTAPI
AhciHwResetBus(
    _In_ PVOID DeviceExtension,
    _In_ ULONG PathId)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(PathId);
    return TRUE;
}

ULONG NTAPI
DriverEntry(
    _In_ PVOID DriverObject,
    _In_ PVOID RegistryPath)
{
    HW_INITIALIZATION_DATA hw = {0};
    /* VendorId/DeviceId strings for SCSIPORT PCI matching
     * These are used by SCSIPORT to find PCI devices
     * SCSIPORT expects lowercase hex strings (e.g., "8086" for Intel)
     * For Intel ICH9 AHCI Controller: VendorID=0x8086, DeviceID=0x2922 */
    static const CHAR VendorIdString[] = "8086";  /* Intel */
    static const CHAR DeviceIdString[] = "2922";  /* ICH9 AHCI Controller */

    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "ahciport: DriverEntry(DriverObject=%p, RegistryPath=%p)\n",
               DriverObject, RegistryPath));

    hw.HwInitializationDataSize = sizeof(hw);
    hw.AdapterInterfaceType = PCIBus;
    hw.NumberOfAccessRanges = 6; /* AHCI typically uses BAR5 (MMIO) */
    hw.HwFindAdapter = AhciHwFindAdapter;
    hw.HwInitialize = AhciHwInitialize;
    hw.HwStartIo = AhciHwStartIo;
    hw.HwInterrupt = AhciHwInterrupt;
    hw.HwResetBus = AhciHwResetBus;

    /* Set VendorId/DeviceId for SCSIPORT PCI matching */
    hw.VendorId = (PVOID)VendorIdString;
    hw.VendorIdLength = sizeof(VendorIdString) - 1;
    hw.DeviceId = (PVOID)DeviceIdString;
    hw.DeviceIdLength = sizeof(DeviceIdString) - 1;

    /* Basic capabilities */
    hw.TaggedQueuing = FALSE;         /* enable later */
    hw.AutoRequestSense = TRUE;
    hw.MultipleRequestPerLu = TRUE;
    hw.NeedPhysicalAddresses = TRUE;
    hw.MapBuffers = TRUE;

    /* Extensions */
    hw.SrbExtensionSize = sizeof(AHCI_SRB_EXTENSION);
    hw.DeviceExtensionSize = sizeof(AHCI_ADAPTER_EXTENSION);

    {
        ULONG status = ScsiPortInitialize(DriverObject, RegistryPath, &hw, NULL);
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "ahciport: ScsiPortInitialize -> 0x%08lx\n", status));
        return status;
    }
}
