/*
 * PROJECT:        ReactOS Kernel
 * LICENSE:        GNU GPLv2 only as published by the Free Software Foundation
 * PURPOSE:        AHCI Miniport (SCSIPORT implementation)
 */

#include "ahciport.h"

#define AHCI_CMD_SLOT                0
#define AHCI_DEFAULT_SECTOR_SIZE     512
#define AHCI_DEFAULT_PACKET_SECTOR   2048
#define AHCI_MAX_TRANSFER_LENGTH     (1024 * 1024)
#define AHCI_COMMAND_TIMEOUT_US      (5 * 1000 * 1000)
#define AHCI_RESET_TIMEOUT_US        (500 * 1000)
#define AHCI_NO_DEVICE_DEBOUNCE_US   (100 * 1000)
#define AHCI_TASKFILE_TIMEOUT_US     (500 * 1000)
#define AHCI_COMRESET_DELAY_US       1000

#define AHCI_ALIGN_PTR(Ptr, Alignment) \
    ((PUCHAR)(((ULONG_PTR)(Ptr) + ((Alignment) - 1)) & ~((ULONG_PTR)(Alignment) - 1)))

#define AHCI_QUIRK_ALLOW_TINY_PRDT_CROSS 0x00000001
#define AHCI_PCI_ID_ANY                0xFFFF

typedef struct _AHCI_PCI_QUIRK {
    USHORT VendorId;
    USHORT DeviceId;
    USHORT SubVendorId;
    USHORT SubDeviceId;
    UCHAR RevisionId;
    UCHAR RevisionMask;
    ULONG Flags;
} AHCI_PCI_QUIRK, *PAHCI_PCI_QUIRK;

static const AHCI_PCI_QUIRK AhciPciQuirks[] =
{
    /* Populate with controllers that mis-handle sub-sector PRDT entries. */
    { 0, 0, 0, 0, 0, 0, 0 }
};

static BOOLEAN
AhciReadPciConfig(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _Out_ PPCI_COMMON_CONFIG PciConfig)
{
    ULONG length;

    if (ConfigInfo->AdapterInterfaceType != PCIBus)
        return FALSE;

#ifdef AHCI_USE_STORPORT
    length = StorPortGetBusData(Adapter,
                                PCIConfiguration,
                                ConfigInfo->SystemIoBusNumber,
                                ConfigInfo->SlotNumber,
                                PciConfig,
                                sizeof(*PciConfig));
#else
    length = ScsiPortGetBusData(Adapter,
                                PCIConfiguration,
                                ConfigInfo->SystemIoBusNumber,
                                ConfigInfo->SlotNumber,
                                PciConfig,
                                sizeof(*PciConfig));
#endif

    return (length == sizeof(*PciConfig));
}

static ULONG
AhciGetPciQuirks(
    _In_ const PCI_COMMON_CONFIG *PciConfig)
{
    ULONG quirks = 0;

    for (ULONG i = 0; i < RTL_NUMBER_OF(AhciPciQuirks); ++i)
    {
        const AHCI_PCI_QUIRK *entry = &AhciPciQuirks[i];
        BOOLEAN revMatch;

        if (entry->Flags == 0)
            continue;

        if ((entry->VendorId != AHCI_PCI_ID_ANY) && (entry->VendorId != PciConfig->VendorID))
            continue;
        if ((entry->DeviceId != AHCI_PCI_ID_ANY) && (entry->DeviceId != PciConfig->DeviceID))
            continue;
        if ((entry->SubVendorId != AHCI_PCI_ID_ANY) && (entry->SubVendorId != PciConfig->u.type0.SubVendorID))
            continue;
        if ((entry->SubDeviceId != AHCI_PCI_ID_ANY) && (entry->SubDeviceId != PciConfig->u.type0.SubSystemID))
            continue;

        if (entry->RevisionMask == 0)
            revMatch = TRUE;
        else
            revMatch = ((PciConfig->RevisionID & entry->RevisionMask) ==
                        (entry->RevisionId & entry->RevisionMask));

        if (!revMatch)
            continue;

        quirks |= entry->Flags;
    }

    return quirks;
}

#ifdef AHCI_USE_STORPORT
#ifndef STOR_STATUS_SUCCESS
#define STOR_STATUS_SUCCESS 0
#endif
#endif

static __inline PVOID
AhciGetSrbDataBuffer(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    UNREFERENCED_PARAMETER(Adapter);
    return (Srb != NULL) ? Srb->DataBuffer : NULL;
}

#ifndef AHCI_USE_STORPORT
static VOID
AhciFinishRequest(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ BOOLEAN AsyncCompletion)
{
    if (Srb == NULL)
        return;

    UNREFERENCED_PARAMETER(AsyncCompletion);
    ScsiPortNotification(RequestComplete, Adapter, Srb);
    ScsiPortNotification(NextRequest, Adapter, 0);
}
#endif

static BOOLEAN
AhciIssuePacketCommand(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_bytes_(CdbLength) PUCHAR Cdb,
    _In_ ULONG CdbLength,
    _In_ BOOLEAN DataOut,
    _In_opt_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesTransferred,
    _Out_opt_ PBOOLEAN TaskfileError);

#ifdef AHCI_USE_STORPORT
static BOOLEAN
AhciHandleIoControl(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG PortNumber,
    _Inout_ PSCSI_REQUEST_BLOCK Srb)
{
    PSRB_IO_CONTROL control;
    ULONG payloadLength;
    PUCHAR payload;

    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(PortNumber);

    control = (PSRB_IO_CONTROL)Srb->DataBuffer;
    if (control == NULL ||
        Srb->DataTransferLength < sizeof(SRB_IO_CONTROL) ||
        control->HeaderLength > Srb->DataTransferLength)
    {
        Srb->SrbStatus = SRB_STATUS_ERROR;
        return TRUE;
    }

    payloadLength = control->Length;
    payload = (PUCHAR)control + control->HeaderLength;

    switch (control->ControlCode)
    {
        case IOCTL_SCSI_GET_ADDRESS:
        {
            PSCSI_ADDRESS address;

            AHCI_TRACE_PRINT("ahciport: IOCTL_SCSI_GET_ADDRESS path=%u target=%u lun=%u\n",
                     Srb->PathId,
                     Srb->TargetId,
                     Srb->Lun);

            if (payloadLength < sizeof(SCSI_ADDRESS) ||
                Srb->DataTransferLength < control->HeaderLength + sizeof(SCSI_ADDRESS))
            {
                control->ReturnCode = SRB_STATUS_INVALID_REQUEST;
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                break;
            }

            address = (PSCSI_ADDRESS)payload;
            RtlZeroMemory(address, sizeof(SCSI_ADDRESS));
            address->Length = sizeof(SCSI_ADDRESS);
            address->PortNumber = 0;
            address->PathId = Srb->PathId;
            address->TargetId = Srb->TargetId;
            address->Lun = Srb->Lun;

            control->ReturnCode = SRB_STATUS_SUCCESS;
            control->Length = sizeof(SCSI_ADDRESS);
            Srb->DataTransferLength = control->HeaderLength + control->Length;
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;
        }

        case IOCTL_SCSI_GET_CAPABILITIES:
        {
            PIO_SCSI_CAPABILITIES caps;

            AHCI_TRACE_PRINT("ahciport: IOCTL_SCSI_GET_CAPABILITIES\n");

            if (payloadLength < sizeof(IO_SCSI_CAPABILITIES) ||
                Srb->DataTransferLength < control->HeaderLength + sizeof(IO_SCSI_CAPABILITIES))
            {
                control->ReturnCode = SRB_STATUS_INVALID_REQUEST;
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                break;
            }

            caps = (PIO_SCSI_CAPABILITIES)payload;
            RtlZeroMemory(caps, sizeof(IO_SCSI_CAPABILITIES));
            caps->Length = sizeof(IO_SCSI_CAPABILITIES);
            caps->MaximumTransferLength = AHCI_MAX_TRANSFER_LENGTH;
            caps->MaximumPhysicalPages = AHCI_MAX_PRDT_ENTRIES;
            caps->SupportedAsynchronousEvents = 0;
            caps->AlignmentMask = sizeof(ULONG) - 1;
            caps->TaggedQueuing = TRUE;
            caps->AdapterScansDown = FALSE;
            caps->AdapterUsesPio = FALSE;

            control->ReturnCode = SRB_STATUS_SUCCESS;
            control->Length = sizeof(IO_SCSI_CAPABILITIES);
            Srb->DataTransferLength = control->HeaderLength + control->Length;
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            break;
        }

        default:
            control->ReturnCode = SRB_STATUS_INVALID_REQUEST;
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }

    return TRUE;
}

static VOID
AhciNotifyBusChange(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId)
{
    StorPortNotification(BusChangeDetected, Adapter, PathId, TargetId, 0);
}

#else
static VOID
AhciNotifyBusChange(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId)
{
    UNREFERENCED_PARAMETER(TargetId);
    ScsiPortNotification(BusChangeDetected, Adapter, PathId);
}
#endif

#ifdef AHCI_USE_STORPORT
static SCSI_ADAPTER_CONTROL_STATUS NTAPI
AhciHwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    UNREFERENCED_PARAMETER(DeviceExtension);

    switch (ControlType)
    {
        case ScsiQuerySupportedControlTypes:
        {
            PSCSI_SUPPORTED_CONTROL_TYPE_LIST list = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
            ULONG maxType;

            if (list == NULL)
                return ScsiAdapterControlUnsuccessful;

            maxType = list->MaxControlType;
            for (ULONG index = 0; index <= maxType; ++index)
            {
                BOOLEAN supported = FALSE;

                switch (index)
                {
                    case ScsiQuerySupportedControlTypes:
                    case ScsiStopAdapter:
                    case ScsiRestartAdapter:
                        supported = TRUE;
                        break;
                    default:
                        break;
                }

                list->SupportedTypeList[index] = supported;
            }

            return ScsiAdapterControlSuccess;
        }

        case ScsiStopAdapter:
        case ScsiRestartAdapter:
            return ScsiAdapterControlSuccess;

        default:
            break;
    }

    return ScsiAdapterControlUnsuccessful;
}
#endif

static volatile ULONG *
AhciPortRegPtr(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_ ULONG Offset)
{
    return (volatile ULONG *)((PUCHAR)Adapter->AbBase + AHCI_PORT_REG_BASE + Port * AHCI_PORT_STRIDE + Offset);
}

static ULONG
AhciReadPort(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_ ULONG Offset)
{
    volatile ULONG *reg = AhciPortRegPtr(Adapter, Port, Offset);
    return AHCI_READ_REG32(Adapter, reg);
}

static VOID
AhciWritePort(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    volatile ULONG *reg = AhciPortRegPtr(Adapter, Port, Offset);
    AHCI_WRITE_REG32(Adapter, reg, Value);
}

static BOOLEAN
AhciWaitForPortClear(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_ ULONG Offset,
    _In_ ULONG Mask,
    _In_ ULONG TimeoutUs)
{
    volatile ULONG *reg = AhciPortRegPtr(Adapter, Port, Offset);
    ULONG iterations = (TimeoutUs / 10) + 1;

    while (iterations--)
    {
        if ((AHCI_READ_REG32(Adapter, reg) & Mask) == 0)
            return TRUE;

        ScsiPortStallExecution(10);
    }

    return FALSE;
}

static BOOLEAN
AhciWaitForPortSet(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_ ULONG Offset,
    _In_ ULONG Mask,
    _In_ ULONG ExpectedBits,
    _In_ ULONG TimeoutUs)
{
    volatile ULONG *reg = AhciPortRegPtr(Adapter, Port, Offset);
    ULONG iterations = (TimeoutUs / 10) + 1;

    while (iterations--)
    {
        ULONG value = AHCI_READ_REG32(Adapter, reg);
        if ((value & Mask) == ExpectedBits)
            return TRUE;

        ScsiPortStallExecution(10);
    }

    return FALSE;
}

static BOOLEAN
AhciWaitForPortDevicePresence(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_ ULONG TimeoutUs,
    _Out_ PBOOLEAN DeviceAbsent)
{
    volatile ULONG *reg = AhciPortRegPtr(Adapter, Port, AHCI_PxSSTS);
    ULONG iterations = (TimeoutUs / 10) + 1;
    ULONG absentStableUs = 0;

    *DeviceAbsent = FALSE;

    while (iterations--)
    {
        ULONG value = AHCI_READ_REG32(Adapter, reg);
        ULONG det = value & AHCI_PxSSTS_DET_MASK;

        if (det == AHCI_PxSSTS_DET_PRESENT)
            return TRUE;

        if (det == AHCI_PxSSTS_DET_NO_DEVICE && (value & AHCI_PxSSTS_SPD_MASK) == 0)
        {
            absentStableUs += 10;
            if (absentStableUs >= AHCI_NO_DEVICE_DEBOUNCE_US)
            {
                *DeviceAbsent = TRUE;
                return FALSE;
            }
        }
        else
        {
            absentStableUs = 0;
        }

        ScsiPortStallExecution(10);
    }

    return FALSE;
}

static BOOLEAN
AhciWaitForPortMaskAny(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_ ULONG Offset,
    _In_ ULONG Mask,
    _In_ ULONG TimeoutUs)
{
    volatile ULONG *reg = AhciPortRegPtr(Adapter, Port, Offset);
    ULONG iterations = (TimeoutUs / 10) + 1;

    while (iterations--)
    {
        if (AHCI_READ_REG32(Adapter, reg) & Mask)
            return TRUE;

        ScsiPortStallExecution(10);
    }

    return FALSE;
}

static VOID
AhciDumpPortState(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_opt_z_ PCSTR Reason)
{
    ULONG ssts = AhciReadPort(Adapter, Port, AHCI_PxSSTS);
    ULONG sctl = AhciReadPort(Adapter, Port, AHCI_PxSCTL);
    ULONG cmd = AhciReadPort(Adapter, Port, AHCI_PxCMD);
    ULONG tfd = AhciReadPort(Adapter, Port, AHCI_PxTFD);
    ULONG serr = AhciReadPort(Adapter, Port, AHCI_PxSERR);
    ULONG is = AhciReadPort(Adapter, Port, AHCI_PxIS);
    ULONG ci = AhciReadPort(Adapter, Port, AHCI_PxCI);

    AHCI_WARN("Port %lu state%s%s%s SSTS=0x%08lx SCTL=0x%08lx CMD=0x%08lx TFD=0x%08lx SERR=0x%08lx IS=0x%08lx CI=0x%08lx",
              Port,
              Reason ? " (" : "",
              Reason ? Reason : "",
              Reason ? ")" : "",
              ssts,
              sctl,
              cmd,
              tfd,
              serr,
              is,
              ci);

    AHCI_TRACE_PRINT("ahciport: Port %lu state%s%s%s SSTS=0x%08lx SCTL=0x%08lx CMD=0x%08lx TFD=0x%08lx SERR=0x%08lx IS=0x%08lx CI=0x%08lx\n",
             Port,
             Reason ? " (" : "",
             Reason ? Reason : "",
             Reason ? ")" : "",
             ssts,
             sctl,
             cmd,
             tfd,
             serr,
             is,
             ci);
}

static BOOLEAN
AhciWaitForTaskfileReady(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_ ULONG Mask,
    _In_ ULONG TimeoutUs)
{
    volatile ULONG *pxtfd = AhciPortRegPtr(Adapter, Port, AHCI_PxTFD);
    ULONG iterations = (TimeoutUs / 10) + 1;

    while (iterations--)
    {
        ULONG tfd = AHCI_READ_REG32(Adapter, pxtfd);
        if ((tfd & Mask) == 0)
            return TRUE;

        ScsiPortStallExecution(10);
    }

    return FALSE;
}

static VOID
AhciClearSense(
    _Inout_ PAHCI_PORT_CONTEXT Port)
{
    Port->SenseValid = FALSE;
    RtlZeroMemory(&Port->SenseData, sizeof(SENSE_DATA));
}

static VOID
AhciRecordSense(
    _Inout_ PAHCI_PORT_CONTEXT Port,
    _In_ UCHAR SenseKey,
    _In_ UCHAR AdditionalSenseCode,
    _In_ UCHAR AdditionalSenseQualifier)
{
    RtlZeroMemory(&Port->SenseData, sizeof(SENSE_DATA));
    Port->SenseData.ErrorCode = 0x70;
    Port->SenseData.Valid = 1;
    Port->SenseData.SenseKey = SenseKey;
    Port->SenseData.AdditionalSenseLength = sizeof(SENSE_DATA) - offsetof(SENSE_DATA, AdditionalSenseCode);
    Port->SenseData.AdditionalSenseCode = AdditionalSenseCode;
    Port->SenseData.AdditionalSenseCodeQualifier = AdditionalSenseQualifier;
    Port->SenseValid = TRUE;
}

static VOID
AhciStoreSenseData(
    _Inout_ PAHCI_PORT_CONTEXT Port,
    _In_reads_bytes_(Length) PSENSE_DATA Sense,
    _In_ ULONG Length)
{
    ULONG copyLength;

    RtlZeroMemory(&Port->SenseData, sizeof(SENSE_DATA));

    if (Sense == NULL || Length == 0)
    {
        Port->SenseData.ErrorCode = 0x70;
        Port->SenseValid = TRUE;
        return;
    }

    copyLength = (Length < sizeof(SENSE_DATA)) ? Length : sizeof(SENSE_DATA);
    RtlCopyMemory(&Port->SenseData, Sense, copyLength);

    if (copyLength < sizeof(SENSE_DATA))
    {
        ULONG offset = offsetof(SENSE_DATA, AdditionalSenseCode);

        Port->SenseData.ErrorCode = 0x70;
        if (copyLength > offset)
        {
            ULONG additional = copyLength - offset;
            Port->SenseData.AdditionalSenseLength = (additional > 0xFF) ? 0xFF : (UCHAR)additional;
        }
    }

    Port->SenseValid = TRUE;
}

static VOID
AhciCopySenseToSrb(
    _Inout_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_bytes_(Length) PSENSE_DATA Sense,
    _In_ ULONG Length)
{
    if (Srb == NULL || Sense == NULL || Length == 0)
        return;

    if (Srb->SenseInfoBuffer != NULL && Srb->SenseInfoBufferLength != 0)
    {
        ULONG copy = Srb->SenseInfoBufferLength;
        if (copy > Length)
            copy = Length;
        RtlCopyMemory(Srb->SenseInfoBuffer, Sense, copy);
    }
}

static BOOLEAN
AhciPerformPacketRequestSense(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _Out_writes_bytes_(SenseBufferLength) PSENSE_DATA SenseBuffer,
    _Inout_ PULONG SenseBufferLength)
{
    ULONG allocation;
    UCHAR senseCdb[12] = {0};
    ULONG bytesTransferred = 0;
    BOOLEAN taskfileError = FALSE;

    if (SenseBuffer == NULL || SenseBufferLength == NULL || *SenseBufferLength == 0)
        return FALSE;

    allocation = *SenseBufferLength;
    if (allocation == 0)
        allocation = sizeof(SENSE_DATA);
    if (allocation > 0xFF)
        allocation = 0xFF;

    RtlZeroMemory(SenseBuffer, allocation);

    senseCdb[0] = SCSIOP_REQUEST_SENSE;
    senseCdb[4] = (UCHAR)allocation;

    if (!AhciIssuePacketCommand(Adapter,
                                Port,
                                NULL,
                                senseCdb,
                                sizeof(senseCdb),
                                FALSE,
                                SenseBuffer,
                                allocation,
                                &bytesTransferred,
                                &taskfileError))
    {
        return FALSE;
    }

    if (bytesTransferred == 0 || bytesTransferred > allocation)
        bytesTransferred = allocation;

    *SenseBufferLength = bytesTransferred;
    return TRUE;
}

static BOOLEAN
AhciStopPort(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port)
{
    ULONG cmd;

    cmd = AhciReadPort(Adapter, Port, AHCI_PxCMD);
    if (cmd & AHCI_PxCMD_ST)
    {
        cmd &= ~AHCI_PxCMD_ST;
        AhciWritePort(Adapter, Port, AHCI_PxCMD, cmd);
        if (!AhciWaitForPortClear(Adapter, Port, AHCI_PxCMD, AHCI_PxCMD_CR, AHCI_RESET_TIMEOUT_US))
            return FALSE;
    }

    cmd = AhciReadPort(Adapter, Port, AHCI_PxCMD);
    if (cmd & AHCI_PxCMD_FRE)
    {
        cmd &= ~AHCI_PxCMD_FRE;
        AhciWritePort(Adapter, Port, AHCI_PxCMD, cmd);
        if (!AhciWaitForPortClear(Adapter, Port, AHCI_PxCMD, AHCI_PxCMD_FR, AHCI_RESET_TIMEOUT_US))
            return FALSE;
    }

    return TRUE;
}

static BOOLEAN
AhciResetPort(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port)
{
    PAHCI_PORT_CONTEXT port = &Adapter->Ports[Port];
    ULONG sctl;
    BOOLEAN deviceAbsent = FALSE;

    if (!AhciStopPort(Adapter, Port))
        return FALSE;

    /* Issue COMRESET */
    sctl = AhciReadPort(Adapter, Port, AHCI_PxSCTL);
    sctl &= ~AHCI_PxSCTL_DET_MASK;
    AhciWritePort(Adapter, Port, AHCI_PxSCTL, sctl | AHCI_PxSCTL_DET_RESET);
    ScsiPortStallExecution(AHCI_COMRESET_DELAY_US);
    AhciWritePort(Adapter, Port, AHCI_PxSCTL, sctl | AHCI_PxSCTL_DET_NONE);

    if (!AhciWaitForPortDevicePresence(Adapter, Port, AHCI_RESET_TIMEOUT_US, &deviceAbsent))
    {
        if (deviceAbsent)
        {
            AHCI_TRACE("ResetPort: port %lu reports no device present after COMRESET", Port);
            AhciWritePort(Adapter, Port, AHCI_PxIS, 0xFFFFFFFF);
            AhciWritePort(Adapter, Port, AHCI_PxSERR, 0xFFFFFFFF);
            AhciWritePort(Adapter, Port, AHCI_PxSACT, 0);
            AhciWritePort(Adapter, Port, AHCI_PxCI, 0);
            return TRUE;
        }

        AhciDumpPortState(Adapter, Port, "reset wait DET");
        return FALSE;
    }

    if (!AhciWaitForPortMaskAny(Adapter, Port, AHCI_PxSSTS, AHCI_PxSSTS_SPD_MASK, AHCI_RESET_TIMEOUT_US))
    {
        AhciDumpPortState(Adapter, Port, "reset wait SPD");
        return FALSE;
    }

    if (!AhciWaitForPortSet(Adapter, Port, AHCI_PxSSTS, AHCI_PxSSTS_IPM_MASK, AHCI_PxSSTS_IPM_ACTIVE, AHCI_RESET_TIMEOUT_US))
    {
        AhciDumpPortState(Adapter, Port, "reset wait IPM active");
        return FALSE;
    }

    /* Program memory structures */
    if (port->CommandList)
    {
        AhciWritePort(Adapter, Port, AHCI_PxCLB, port->CommandListPhys.LowPart);
        AhciWritePort(Adapter, Port, AHCI_PxCLBU, port->CommandListPhys.HighPart);
    }

    if (port->ReceivedFis)
    {
        AhciWritePort(Adapter, Port, AHCI_PxFB, port->ReceivedFisPhys.LowPart);
        AhciWritePort(Adapter, Port, AHCI_PxFBU, port->ReceivedFisPhys.HighPart);
    }

    AhciWritePort(Adapter, Port, AHCI_PxIS, 0xFFFFFFFF);
    AhciWritePort(Adapter, Port, AHCI_PxIE, 0);
    AhciWritePort(Adapter, Port, AHCI_PxSERR, 0xFFFFFFFF);
    AhciWritePort(Adapter, Port, AHCI_PxSACT, 0);
    AhciWritePort(Adapter, Port, AHCI_PxCI, 0);

    /* Enable FIS reception and command processing */
    {
        ULONG cmd = AhciReadPort(Adapter, Port, AHCI_PxCMD);

        cmd |= AHCI_PxCMD_SUD | AHCI_PxCMD_POD | AHCI_PxCMD_FRE;
        AhciWritePort(Adapter, Port, AHCI_PxCMD, cmd);
        if (!AhciWaitForPortSet(Adapter, Port, AHCI_PxCMD, AHCI_PxCMD_FR, AHCI_PxCMD_FR, AHCI_RESET_TIMEOUT_US))
        {
            AhciDumpPortState(Adapter, Port, "reset wait FRE");
            return FALSE;
        }

        cmd |= AHCI_PxCMD_ST;
        AhciWritePort(Adapter, Port, AHCI_PxCMD, cmd);
        if (!AhciWaitForPortSet(Adapter, Port, AHCI_PxCMD, AHCI_PxCMD_CR, AHCI_PxCMD_CR, AHCI_RESET_TIMEOUT_US))
        {
            AhciDumpPortState(Adapter, Port, "reset wait CR");
            return FALSE;
        }
    }

    port->Busy = FALSE;
    return TRUE;
}

static ULONG
AhciBuildPrdt(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_ PVOID Buffer,
    _In_ ULONG Length,
    _Inout_updates_(MaxEntries) PAHCI_PRDT_ENTRY Prdt,
    _In_ ULONG MaxEntries)
{
    ULONG offset = 0;
    ULONG entry = 0;

    if (Length == 0)
        return 0;

    while (offset < Length && entry < MaxEntries)
    {
        ULONG chunkLength;
        PVOID virtualAddress = (PVOID)((PUCHAR)Buffer + offset);
        SCSI_PHYSICAL_ADDRESS phys = ScsiPortGetPhysicalAddress(Adapter, Srb, virtualAddress, &chunkLength);
        ULONGLONG physAddress;
        BOOLEAN firstInChunk = TRUE;
        const ULONG prdtByteCountMask = 0x3FFFFF;

        if (chunkLength == 0)
        {
            AHCI_TRACE("BuildPrdt: zero chunk (VA=%p Buffer=%p Length=%lu Offset=%lu Srb=%p DataBuffer=%p Transfer=%lu)",
                       virtualAddress,
                       Buffer,
                       Length,
                       offset,
                       Srb,
                       (Srb != NULL) ? Srb->DataBuffer : NULL,
                       (Srb != NULL) ? (ULONG)Srb->DataTransferLength : 0);
            break;
        }

        if (!(Adapter->Cap & AHCI_CAP_S64A) && phys.HighPart)
        {
            AHCI_ERROR("BuildPrdt: HBA lacks 64-bit DMA but got phys=%08lx%08lx",
                       (ULONG)phys.HighPart, (ULONG)phys.LowPart);
            return 0;
        }

        if (chunkLength > (Length - offset))
            chunkLength = Length - offset;
        else if (chunkLength < (Length - offset))
            AHCI_TRACE("BuildPrdt: chunkLength=%lu remaining=%lu offset=%lu entries=%lu", chunkLength, Length - offset, offset, entry);

        physAddress = ((ULONGLONG)(ULONG)phys.HighPart << 32) | phys.LowPart;

        while (chunkLength > 0 && entry < MaxEntries)
        {
            ULONGLONG nextBoundary = (physAddress + 0x400000ULL) & ~0x3FFFFFULL;
            ULONG bytes = (chunkLength > (4 * 1024 * 1024)) ? (4 * 1024 * 1024) : chunkLength;
            PAHCI_PRDT_ENTRY prdt;

            if (physAddress + bytes > nextBoundary)
            {
                ULONGLONG boundaryBytes = nextBoundary - physAddress;
                if (boundaryBytes > 0 && boundaryBytes < bytes)
                {
                    /* Avoid sub-sector PRDT entries by borrowing from the previous entry in this chunk. */
                    if (boundaryBytes < AHCI_DEFAULT_SECTOR_SIZE && !firstInChunk && entry > 0)
                    {
                        ULONG adjust = AHCI_DEFAULT_SECTOR_SIZE - (ULONG)boundaryBytes;
                        PAHCI_PRDT_ENTRY prev = &Prdt[entry - 1];
                        ULONGLONG prevPhysBase = ((ULONGLONG)prev->DBAU << 32) | prev->DBA;
                        ULONG prevBytes = (prev->DBC_I & prdtByteCountMask) + 1;
                        ULONGLONG prevEnd = prevPhysBase + prevBytes;

                        if (prevEnd == physAddress &&
                            prevBytes >= adjust + AHCI_DEFAULT_SECTOR_SIZE)
                        {
                            ULONG prevFlags = prev->DBC_I & ~prdtByteCountMask;

                            prevFlags &= ~(1u << 31);
                            prevBytes -= adjust;
                            prev->DBC_I = prevFlags | (prevBytes - 1);
                            physAddress = prevPhysBase + prevBytes;
                            chunkLength += adjust;
                            if (offset >= adjust)
                                offset -= adjust;
                            boundaryBytes += adjust;
                        }
                    }

                    if (boundaryBytes < AHCI_DEFAULT_SECTOR_SIZE &&
                        (Adapter->Quirks & AHCI_QUIRK_ALLOW_TINY_PRDT_CROSS))
                    {
                        /* Controller quirk: allow a short cross-boundary segment. */
                    }
                    else
                    {
                        bytes = (ULONG)boundaryBytes;
                    }
                }
            }

            if (bytes == 0)
                bytes = (chunkLength > (4 * 1024 * 1024)) ? (4 * 1024 * 1024) : chunkLength;

            prdt = &Prdt[entry++];
            prdt->DBA = (ULONG)(physAddress & 0xFFFFFFFF);
            prdt->DBAU = (ULONG)(physAddress >> 32);
            prdt->Reserved = 0;
            prdt->DBC_I = bytes - 1;

            physAddress += bytes;
            chunkLength -= bytes;
            offset += bytes;
            firstInChunk = FALSE;
        }
    }

    if (offset < Length)
    {
        AHCI_TRACE("BuildPrdt: incomplete mapping offset=%lu length=%lu entries=%lu", offset, Length, entry);
        return 0;
    }

    if (entry > 0)
        Prdt[entry - 1].DBC_I |= (1u << 31);

    return entry;
}

typedef enum _AHCI_CMD_STATUS
{
    AhciCmdStatusSuccess,
    AhciCmdStatusError,
    AhciCmdStatusTimeout
} AHCI_CMD_STATUS;

static AHCI_CMD_STATUS
AhciWaitForCommandComplete(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_ ULONG SlotMask,
    _In_ ULONG TimeoutUs,
    _In_ BOOLEAN IsFpdma)
{
    ULONG iterations = (TimeoutUs / 50) + 1;
    volatile ULONG *pxci = AhciPortRegPtr(Adapter, Port, AHCI_PxCI);
    volatile ULONG *pxis = AhciPortRegPtr(Adapter, Port, AHCI_PxIS);
    volatile ULONG *pxsact = AhciPortRegPtr(Adapter, Port, AHCI_PxSACT);

    while (iterations--)
    {
        ULONG ci = AHCI_READ_REG32(Adapter, pxci);
        ULONG is = AHCI_READ_REG32(Adapter, pxis);

        if (is)
        {
            AhciWritePort(Adapter, Port, AHCI_PxIS, is);

            if (is & (AHCI_PxIS_TFES | AHCI_PxIS_HBFS | AHCI_PxIS_HBDS))
            {
                return AhciCmdStatusError;
            }
        }

        if (IsFpdma)
        {
            ULONG sact = AHCI_READ_REG32(Adapter, pxsact);
            /* For NCQ, completion is indicated when SACT bit clears. 
               CI bit clears as soon as command is transmitted, so do not check it. */
            if ((sact & SlotMask) == 0)
                return AhciCmdStatusSuccess;
        }
        else
        {
            if ((ci & SlotMask) == 0)
                return AhciCmdStatusSuccess;
        }

        ScsiPortStallExecution(50);
    }

    return AhciCmdStatusTimeout;
}

static BOOLEAN
AhciIssueAtaCommand(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR Command,
    _In_ ULONGLONG Lba,
    _In_ USHORT SectorCount,
    _In_ BOOLEAN Write,
    _In_opt_ PVOID Buffer,
    _In_ ULONG Length)
{
    PAHCI_PORT_CONTEXT port = &Adapter->Ports[Port];
    PAHCI_CMD_HEADER header;
    PAHCI_CMD_TABLE table;
    ULONG slotMask = 1u << AHCI_CMD_SLOT;
    UCHAR tag = (UCHAR)AHCI_CMD_SLOT;
    BOOLEAN isFpdma = (Command == ATA_CMD_FPDMA_READ || Command == ATA_CMD_FPDMA_WRITE);
    BOOLEAN sactArmed = FALSE;
    ULONG tfd;
    ULONG prdtEntries = 0;

    if (port->CommandList == NULL || port->CommandTable == NULL)
    {
        AHCI_ERROR("IssueAta: missing buffers for port %lu", Port);
        return FALSE;
    }

    AHCI_TRACE_PRINT("ahciport: IssueAtaCommand port=%lu cmd=0x%02x LBA=%llu sectors=%u write=%u buffer=%p length=%lu\n",
             Port, Command, Lba, (unsigned)SectorCount, Write, Buffer, (unsigned long)Length);

    if (!AhciWaitForTaskfileReady(Adapter, Port, AHCI_TFD_STS_BSY | AHCI_TFD_STS_DRQ, AHCI_TASKFILE_TIMEOUT_US))
    {
        AHCI_ERROR("IssueAta: taskfile busy timeout on port %lu", Port);
        AhciDumpPortState(Adapter, Port, "taskfile busy");
        return FALSE;
    }

    header = &port->CommandList[AHCI_CMD_SLOT];
    table = port->CommandTable;

    RtlZeroMemory(header, sizeof(AHCI_CMD_HEADER));
    RtlZeroMemory(table, AHCI_CMD_TABLE_ALLOC_SIZE);

    if (Length)
    {
        prdtEntries = AhciBuildPrdt(Adapter, Srb, Buffer, Length, table->PRDT, AHCI_MAX_PRDT_ENTRIES);
        if (prdtEntries == 0)
        {
            AHCI_ERROR("IssueAta: PRDT build failed (Length=%lu) on port %lu", Length, Port);
            return FALSE;
        }
    }

    header->Flags = (5 & AHCI_CMDH_CFL_MASK); /* 20-byte Register H2D FIS */
    if (Write)
        header->Flags |= AHCI_CMDH_W;
    header->PRDTL = (USHORT)prdtEntries;
    header->PRDBC = 0;
    header->CTBA = port->CommandTablePhys.LowPart;
    header->CTBAU = port->CommandTablePhys.HighPart;

    {
        UCHAR *fis = table->CFIS;
        RtlZeroMemory(fis, 64);
        fis[0] = AHCI_FIS_TYPE_REG_H2D;
        fis[1] = 1 << 7;
        fis[2] = Command;
        fis[3] = isFpdma ? (UCHAR)(SectorCount & 0xFF) : 0;
        fis[4] = (UCHAR)(Lba & 0xFF);
        fis[5] = (UCHAR)((Lba >> 8) & 0xFF);
        fis[6] = (UCHAR)((Lba >> 16) & 0xFF);
        fis[7] = 0x40;
        fis[8] = (UCHAR)((Lba >> 24) & 0xFF);
        fis[9] = (UCHAR)((Lba >> 32) & 0xFF);
        fis[10] = (UCHAR)((Lba >> 40) & 0xFF);
        fis[11] = isFpdma ? (UCHAR)((SectorCount >> 8) & 0xFF) : 0;
        if (isFpdma)
        {
            fis[12] = (UCHAR)((tag & 0x1F) << 3);
            fis[13] = 0;
        }
        else
        {
            fis[12] = (UCHAR)(SectorCount & 0xFF);
            fis[13] = (UCHAR)((SectorCount >> 8) & 0xFF);
        }
        fis[14] = 0;
        fis[15] = 0;
    }

    AhciWritePort(Adapter, Port, AHCI_PxIS, 0xFFFFFFFF);
    AhciWritePort(Adapter, Port, AHCI_PxSERR, 0xFFFFFFFF);

    {
        ULONG ci = AhciReadPort(Adapter, Port, AHCI_PxCI);
        if (ci & slotMask)
        {
            AHCI_WARN("IssueAta: slot busy (CI=0x%08lx) on port %lu", ci, Port);
            return FALSE;
        }
    }

    if (isFpdma)
    {
        AhciWritePort(Adapter, Port, AHCI_PxSACT, slotMask);
        sactArmed = TRUE;
    }

    port->Busy = TRUE;

    AhciWritePort(Adapter, Port, AHCI_PxCI, slotMask);

    {
        AHCI_CMD_STATUS cmdStatus = AhciWaitForCommandComplete(Adapter, Port, slotMask, AHCI_COMMAND_TIMEOUT_US, isFpdma);

        if (cmdStatus == AhciCmdStatusTimeout)
        {
            port->Busy = FALSE;
            if (sactArmed)
            {
                AhciWritePort(Adapter, Port, AHCI_PxSACT, 0);
                sactArmed = FALSE;
            }
            AHCI_ERROR("IssueAta: command timeout (op=0x%02x) on port %lu", Command, Port);
            AhciDumpPortState(Adapter, Port, "command timeout");
            if (!AhciStopPort(Adapter, Port))
                AHCI_ERROR("IssueAta: soft stop failed after timeout on port %lu", Port);
            else
                AhciDumpPortState(Adapter, Port, "after stop");
            return FALSE;
        }

        tfd = AhciReadPort(Adapter, Port, AHCI_PxTFD);
        port->Busy = FALSE;

        if (cmdStatus == AhciCmdStatusError || (tfd & AHCI_TFD_STS_ERR))
        {
            if (sactArmed)
            {
                AhciWritePort(Adapter, Port, AHCI_PxSACT, 0);
                sactArmed = FALSE;
            }
            AHCI_WARN("IssueAta: taskfile error (TFD=0x%08lx) on port %lu", tfd, Port);
            AhciDumpPortState(Adapter, Port, "taskfile error");
            return FALSE;
        }
    }

    if (sactArmed)
    {
        AhciWritePort(Adapter, Port, AHCI_PxSACT, 0);
        sactArmed = FALSE;
    }

    Adapter->AbBase->IS = (1u << Port);
    return TRUE;
}

static BOOLEAN
AhciIssuePacketCommand(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_bytes_(CdbLength) PUCHAR Cdb,
    _In_ ULONG CdbLength,
    _In_ BOOLEAN DataOut,
    _In_opt_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesTransferred,
    _Out_opt_ PBOOLEAN TaskfileError)
{
    PAHCI_PORT_CONTEXT port = &Adapter->Ports[Port];
    PAHCI_CMD_HEADER header;
    PAHCI_CMD_TABLE table;
    ULONG slotMask = 1u << AHCI_CMD_SLOT;
    ULONG byteCount;
    ULONG prdtEntries = 0;
    BOOLEAN requestErrored = FALSE;

    if (TaskfileError)
        *TaskfileError = FALSE;

    if (Cdb == NULL || CdbLength == 0)
        return FALSE;

    if (Length != 0 && Buffer == NULL)
        return FALSE;

    if (port->CommandList == NULL || port->CommandTable == NULL)
    {
        AHCI_ERROR("IssuePacket: missing buffers for port %lu", Port);
        return FALSE;
    }

    if (!AhciWaitForTaskfileReady(Adapter, Port, AHCI_TFD_STS_BSY | AHCI_TFD_STS_DRQ, AHCI_TASKFILE_TIMEOUT_US))
    {
        AHCI_ERROR("IssuePacket: taskfile busy timeout on port %lu", Port);
        AhciDumpPortState(Adapter, Port, "packet taskfile busy");
        return FALSE;
    }

    header = &port->CommandList[AHCI_CMD_SLOT];
    table = port->CommandTable;

    RtlZeroMemory(header, sizeof(AHCI_CMD_HEADER));
    RtlZeroMemory(table, AHCI_CMD_TABLE_ALLOC_SIZE);

    if (Length)
    {
        prdtEntries = AhciBuildPrdt(Adapter, Srb, Buffer, Length, table->PRDT, AHCI_MAX_PRDT_ENTRIES);
        if (prdtEntries == 0)
        {
            AHCI_ERROR("IssuePacket: PRDT build failed (Length=%lu) on port %lu", Length, Port);
            return FALSE;
        }
    }

    header->Flags = (5 & AHCI_CMDH_CFL_MASK) | AHCI_CMDH_A;
    if (DataOut)
        header->Flags |= AHCI_CMDH_W;
    header->PRDTL = (USHORT)prdtEntries;
    header->PRDBC = 0;
    header->CTBA = port->CommandTablePhys.LowPart;
    header->CTBAU = port->CommandTablePhys.HighPart;

    {
        UCHAR *fis = table->CFIS;

        RtlZeroMemory(fis, 64);
        fis[0] = AHCI_FIS_TYPE_REG_H2D;
        fis[1] = 1 << 7;
        fis[2] = 0xA0; /* PACKET command */
        fis[7] = 0x40; /* device register: LBA mode */

        if (Length == 0)
        {
            byteCount = 0;
        }
        else
        {
            ULONG wordCount = (Length + 1) >> 1; /* ATAPI byte count is in words */
            if (wordCount > 0xFFFF)
                wordCount = 0xFFFF;
            byteCount = wordCount;
        }

        fis[5] = (UCHAR)(byteCount & 0xFF);       /* Cylinder Low / LBA Mid */
        fis[6] = (UCHAR)((byteCount >> 8) & 0xFF);/* Cylinder High / LBA High */
        fis[9] = 0;   /* Upper byte count */
        fis[10] = 0;
        fis[12] = 0;  /* Sector count */
        fis[13] = 0;
    }

    {
        UCHAR acmd[16] = {0};
        ULONG packetLength = (CdbLength > sizeof(acmd)) ? sizeof(acmd) : CdbLength;

        RtlCopyMemory(acmd, Cdb, packetLength);
        RtlCopyMemory(table->ACMD, acmd, sizeof(acmd));
    }

    AhciWritePort(Adapter, Port, AHCI_PxIS, 0xFFFFFFFF);
    AhciWritePort(Adapter, Port, AHCI_PxSERR, 0xFFFFFFFF);

    {
        ULONG ci = AhciReadPort(Adapter, Port, AHCI_PxCI);
        if (ci & slotMask)
        {
            AHCI_WARN("IssuePacket: slot busy (CI=0x%08lx) on port %lu", ci, Port);
            return FALSE;
        }
    }

    port->Busy = TRUE;

    AhciWritePort(Adapter, Port, AHCI_PxCI, slotMask);

    {
        AHCI_CMD_STATUS cmdStatus = AhciWaitForCommandComplete(Adapter, Port, slotMask, AHCI_COMMAND_TIMEOUT_US, FALSE);

        if (cmdStatus == AhciCmdStatusTimeout)
        {
            port->Busy = FALSE;
            AHCI_ERROR("IssuePacket: command timeout on port %lu", Port);
            AhciDumpPortState(Adapter, Port, "packet command timeout");
            if (!AhciStopPort(Adapter, Port))
                AHCI_ERROR("IssuePacket: soft stop failed after timeout on port %lu", Port);
            else
                AhciDumpPortState(Adapter, Port, "packet after stop");
            return FALSE;
        }

        {
            ULONG tfd = AhciReadPort(Adapter, Port, AHCI_PxTFD);

            AHCI_TRACE_PRINT("ahciport: Port %lu packet completion TFD=0x%08lx\n",
                             Port,
                             tfd);

            if (cmdStatus == AhciCmdStatusError || (tfd & AHCI_TFD_STS_ERR))
            {
                requestErrored = TRUE;

                if (TaskfileError != NULL)
                {
                    *TaskfileError = TRUE;
                }
                else
                {
                    AhciDumpPortState(Adapter, Port, "packet taskfile error");
                    port->Busy = FALSE;
                    Adapter->AbBase->IS = (1u << Port);
                    return FALSE;
                }
            }
        }
    }

    if (BytesTransferred != NULL)
    {
        ULONG transferred = header->PRDBC;
        if (requestErrored)
        {
            transferred = 0;
        }
        else if (transferred == 0 && Length != 0 && (port->Atapi || !DataOut))
        {
            /* Some controllers leave PRDBC zero for PIO; fall back to caller length */
            transferred = Length;
        }
        *BytesTransferred = transferred;
    }

    port->Busy = FALSE;
    Adapter->AbBase->IS = (1u << Port);
    return TRUE;
}

static BOOLEAN
AhciIdentifyDevice(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port)
{
    PAHCI_PORT_CONTEXT port = &Adapter->Ports[Port];
    USHORT *id;
    ULONGLONG sectors = 0;

    if (port->IdentifyBuffer == NULL)
        return FALSE;

    RtlZeroMemory(port->IdentifyBuffer, 512);

    if (!AhciIssueAtaCommand(Adapter, Port, NULL, 0xEC, 0, 1, FALSE, port->IdentifyBuffer, 512))
    {
        AHCI_WARN("IDENTIFY DEVICE command failed on port %lu", Port);
        return FALSE;
    }

    id = (USHORT *)port->IdentifyBuffer;

    if (id[83] & (1 << 10))
    {
        sectors = ((ULONGLONG)id[103] << 48) |
                  ((ULONGLONG)id[102] << 32) |
                  ((ULONGLONG)id[101] << 16) |
                  id[100];
    }

    if (sectors == 0)
        sectors = ((ULONGLONG)id[61] << 16) | id[60];

    if (sectors == 0)
        return FALSE;

    if (!port->Atapi)
        port->SectorSize = AHCI_DEFAULT_SECTOR_SIZE;
    port->SectorCount = sectors;
    port->Present = TRUE;
    port->Atapi = FALSE;
    port->SupportsNCQ = FALSE;
    port->MaxQueueDepth = 1;
    if ((Adapter->Cap & AHCI_CAP_SNCQ) && (id[76] & 0x0100))
    {
        UCHAR depthField = (UCHAR)(id[75] & 0x1F);
        UCHAR depth = (depthField & 0x1F) + 1;
        if (depth == 0)
            depth = 1;
        if (depth > 32)
            depth = 32;
        port->SupportsNCQ = TRUE;
        port->MaxQueueDepth = depth;
        AHCI_TRACE("Port %lu: NCQ supported (depth=%u)", Port, depth);
    }
    AhciClearSense(port);
    AHCI_TRACE("Port %lu: IDENTIFY reports %llu sectors", Port, sectors);
    return TRUE;
}

static BOOLEAN
AhciIdentifyPacketDevice(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port)
{
    PAHCI_PORT_CONTEXT port = &Adapter->Ports[Port];
#if AHCI_ENABLE_TRACE
    USHORT *id;
#endif

    if (port->IdentifyBuffer == NULL)
        return FALSE;

    RtlZeroMemory(port->IdentifyBuffer, 512);

    if (!AhciIssueAtaCommand(Adapter, Port, NULL, 0xA1, 0, 1, FALSE, port->IdentifyBuffer, 512))
    {
        AHCI_WARN("IDENTIFY PACKET DEVICE command failed on port %lu", Port);
        return FALSE;
    }

#if AHCI_ENABLE_TRACE
    id = (USHORT *)port->IdentifyBuffer;
#endif

    port->SectorSize = AHCI_DEFAULT_PACKET_SECTOR;
    port->SectorCount = 0;
    port->Present = TRUE;
    port->Atapi = TRUE;
    port->SupportsNCQ = FALSE;
    port->MaxQueueDepth = 1;
    AhciClearSense(port);

#if AHCI_ENABLE_TRACE
    AHCI_TRACE("Port %lu: IDENTIFY PACKET device detected (Config=0x%04x)",
               Port,
               id[0]);
#else
    AHCI_TRACE("Port %lu: IDENTIFY PACKET device detected", Port);
#endif
    return TRUE;
}

static BOOLEAN
AhciReadWriteSectors(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG Port,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_opt_ PVOID Buffer,
    _In_ ULONGLONG Lba,
    _In_ ULONG TransferLength,
    _In_ BOOLEAN Write)
{
    PAHCI_PORT_CONTEXT port = &Adapter->Ports[Port];
    ULONG sectorSize;
    ULONGLONG sectors;
    BOOLEAN success;

    if (!port->Present || port->Atapi || port->SectorSize == 0 || Buffer == NULL)
        return FALSE;

    sectorSize = port->SectorSize;
    if (sectorSize == 0 || (TransferLength % sectorSize) != 0)
        return FALSE;

    sectors = TransferLength / sectorSize;
    if (sectors == 0 || sectors > 0xFFFFULL)
        return FALSE;

    if ((Lba + sectors) > port->SectorCount)
        return FALSE;

    {
        BOOLEAN overallSuccess = TRUE;
        ULONGLONG currentLba = Lba;
        PUCHAR currentBuffer = (PUCHAR)Buffer;
        ULONG remaining = TransferLength;
        const ULONG maxChunk = AHCI_MAX_PRDT_ENTRIES * PAGE_SIZE;

        while (remaining > 0)
        {
            ULONG chunk = remaining;
            if (chunk > maxChunk)
            {
                chunk = maxChunk;
                chunk -= (chunk % sectorSize);
                if (chunk == 0)
                    chunk = sectorSize;
            }

            USHORT chunkSectors = (USHORT)(chunk / sectorSize);
            if (chunkSectors == 0)
                chunkSectors = 1;

            {
                UCHAR opcode = port->SupportsNCQ ?
                               (Write ? ATA_CMD_FPDMA_WRITE : ATA_CMD_FPDMA_READ) :
                               (Write ? 0x35 : 0x25);

                success = AhciIssueAtaCommand(Adapter,
                                              Port,
                                              Srb,
                                              opcode,
                                              currentLba,
                                              chunkSectors,
                                              Write,
                                              currentBuffer,
                                              chunk);
            }

            if (!success)
            {
                overallSuccess = FALSE;
                break;
            }

            currentLba += chunkSectors;
            currentBuffer += chunk;
            remaining -= chunk;
        }

        success = overallSuccess;
    }

    if (success)
    {
        AHCI_TRACE("Port %lu: %s %llu sectors @ LBA %llu", Port, Write ? "Wrote" : "Read", sectors, Lba);
        AhciClearSense(port);
    }
    else
    {
        AHCI_WARN("Port %lu: %s command failed (sectors=%llu LBA=%llu)", Port, Write ? "Write" : "Read", sectors, Lba);
        AhciRecordSense(port, SCSI_SENSE_MEDIUM_ERROR, SCSI_ADSENSE_NO_SENSE, 0);
    }

    return success;
}

static BOOLEAN
AhciHandleExecuteScsiPacket(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG PortNumber,
    _Inout_ PSCSI_REQUEST_BLOCK Srb)
{
    PAHCI_PORT_CONTEXT port = &Adapter->Ports[PortNumber];
    PVOID dataBuffer = AhciGetSrbDataBuffer(Adapter, Srb);
    ULONG transferLength = Srb->DataTransferLength;
    BOOLEAN dataOut = (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) != 0;
    BOOLEAN dataIn = (Srb->SrbFlags & SRB_FLAGS_DATA_IN) != 0;

    if (transferLength != 0 && dataBuffer == NULL)
    {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return TRUE;
    }

    if (!dataOut && !dataIn && transferLength != 0)
    {
        dataIn = TRUE;
    }

    if (Srb->Cdb[0] == SCSIOP_GET_CONFIGURATION)
    {
        PUCHAR buffer = (PUCHAR)dataBuffer;
        ULONG responseLength = (transferLength < 16) ? transferLength : 16;

        if (buffer == NULL || transferLength == 0)
        {
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            return TRUE;
        }

        RtlZeroMemory(buffer, transferLength);

        if (responseLength >= 8)
        {
            ULONG dataLength = (responseLength >= 8) ? (responseLength - 4) : 0;

            buffer[0] = (UCHAR)((dataLength >> 24) & 0xFF);
            buffer[1] = (UCHAR)((dataLength >> 16) & 0xFF);
            buffer[2] = (UCHAR)((dataLength >> 8) & 0xFF);
            buffer[3] = (UCHAR)(dataLength & 0xFF);

            buffer[6] = 0x00;
            buffer[7] = 0x08; /* Current profile: CD-ROM */
        }

        if (responseLength >= 16)
        {
            PUCHAR feature = buffer + 8;

            feature[0] = 0x00;
            feature[1] = 0x08; /* Profile list feature */
            feature[2] = 0x00; /* Current, Version 0 */
            feature[3] = 0x02; /* Additional length */
            feature[4] = 0x00;
            feature[5] = 0x08; /* Profile: CD-ROM */
            feature[6] = 0x00;
            feature[7] = 0x00;
        }

        Srb->DataTransferLength = responseLength;
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        Srb->ScsiStatus = SCSISTAT_GOOD;
        AhciClearSense(port);
        return TRUE;
    }

    if (Srb->Cdb[0] == SCSIOP_INQUIRY)
    {
        ULONG length = Srb->DataTransferLength;
        PUCHAR inquiry;
        const ULONG minimumLength = 36;

        if (dataBuffer == NULL || length < minimumLength)
        {
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            return TRUE;
        }

        RtlZeroMemory(dataBuffer, length);
        inquiry = (PUCHAR)dataBuffer;

        inquiry[0] = READ_ONLY_DIRECT_ACCESS_DEVICE;
        inquiry[1] = 0x80; /* Removable */
        inquiry[2] = 5;    /* SPC-3 */
        inquiry[3] = 2;    /* Response data format */
        inquiry[4] = 0x1F; /* Additional length */

        RtlCopyMemory(&inquiry[8], "ReactOS ", 8);
        RtlCopyMemory(&inquiry[16], "AHCI Optical     ", 16);
        RtlCopyMemory(&inquiry[32], "0001", 4);

        AhciClearSense(port);
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        Srb->ScsiStatus = SCSISTAT_GOOD;
        Srb->DataTransferLength = (length < minimumLength) ? length : minimumLength;
        return TRUE;
    }

    if (Srb->Cdb[0] == SCSIOP_REQUEST_SENSE)
    {
        PSENSE_DATA sense = (PSENSE_DATA)dataBuffer;
        ULONG length = Srb->DataTransferLength;

        if (sense == NULL || length < sizeof(SENSE_DATA))
        {
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            return TRUE;
        }

        if (port->SenseValid)
        {
            ULONG used = (length < sizeof(SENSE_DATA)) ? length : sizeof(SENSE_DATA);
            RtlCopyMemory(sense, &port->SenseData, used);
            port->SenseValid = FALSE;
        }
        else
        {
            ULONG used = (length < sizeof(SENSE_DATA)) ? length : sizeof(SENSE_DATA);
            RtlZeroMemory(sense, used);
            sense->ErrorCode = 0x70;
        }

        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        Srb->ScsiStatus = SCSISTAT_GOOD;
        Srb->DataTransferLength = (length < sizeof(SENSE_DATA)) ? length : sizeof(SENSE_DATA);
        return TRUE;
    }

    {
        ULONG bytesTransferred = 0;

        BOOLEAN taskfileError = FALSE;

        if (!AhciIssuePacketCommand(Adapter,
                                     PortNumber,
                                     Srb,
                                     Srb->Cdb,
                                     Srb->CdbLength,
                                     dataOut,
                                     dataBuffer,
                                     transferLength,
                                     &bytesTransferred,
                                     &taskfileError))
        {
            ULONG senseLength = sizeof(SENSE_DATA);

            RtlZeroMemory(&port->SenseData, sizeof(SENSE_DATA));
            port->SenseData.ErrorCode = 0x70;
            port->SenseData.Valid = 1;
            port->SenseData.SenseKey = SCSI_SENSE_HARDWARE_ERROR;
            port->SenseData.AdditionalSenseCode = SCSI_ADSENSE_NO_SENSE;
            port->SenseData.AdditionalSenseCodeQualifier = 0;
            port->SenseData.AdditionalSenseLength = (UCHAR)(senseLength - 8);
            port->SenseValid = TRUE;

            Srb->SrbStatus = SRB_STATUS_ERROR;
            Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
            return TRUE;
        }

        if (taskfileError)
        {
            SENSE_DATA senseData;
            ULONG senseLength = sizeof(SENSE_DATA);

            if (AhciPerformPacketRequestSense(Adapter, PortNumber, &senseData, &senseLength))
            {
                AhciStoreSenseData(port, &senseData, senseLength);
                AhciCopySenseToSrb(Srb, &senseData, senseLength);
                Srb->SrbStatus = SRB_STATUS_AUTOSENSE_VALID | SRB_STATUS_ERROR;
                Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
            }
            else
            {
                AhciRecordSense(port, SCSI_SENSE_HARDWARE_ERROR, SCSI_ADSENSE_NO_SENSE, 0);
                AhciCopySenseToSrb(Srb, &port->SenseData, sizeof(SENSE_DATA));
                Srb->SrbStatus = SRB_STATUS_ERROR;
                Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
            }

            Srb->DataTransferLength = 0;
            return TRUE;
        }

        if (dataIn)
        {
            if (bytesTransferred != 0 && bytesTransferred < transferLength)
                Srb->DataTransferLength = bytesTransferred;
            else
                Srb->DataTransferLength = transferLength;
        }
        else if (!dataOut)
        {
            Srb->DataTransferLength = 0;
        }
    }

    Srb->SrbStatus = SRB_STATUS_SUCCESS;
    Srb->ScsiStatus = SCSISTAT_GOOD;

    AhciClearSense(port);
    return TRUE;
}

static BOOLEAN
AhciHandleExecuteScsi(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG PortNumber,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PAHCI_PORT_CONTEXT port = &Adapter->Ports[PortNumber];
    PUCHAR cdb = Srb->Cdb;
    UCHAR opcode = cdb[0];
    BOOLEAN success = FALSE;
    PVOID dataBuffer = AhciGetSrbDataBuffer(Adapter, Srb);

    Srb->ScsiStatus = SCSISTAT_GOOD;
    Srb->SrbStatus = SRB_STATUS_SUCCESS;

    if (!port->Present)
    {
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        return TRUE;
    }

    if (port->Atapi)
        return AhciHandleExecuteScsiPacket(Adapter, PortNumber, Srb);

    switch (opcode)
    {
        case SCSIOP_TEST_UNIT_READY:
            AhciClearSense(port);
            success = TRUE;
            Srb->DataTransferLength = 0;
            break;

        case SCSIOP_INQUIRY:
        {
            ULONG length = Srb->DataTransferLength;
            PUCHAR inquiry;
            const ULONG minimumLength = 36; /* Standard inquiry data length */

            AHCI_TRACE_PRINT("ahciport: INQUIRY buffer=%p length=%lu\n",
                     dataBuffer,
                     (unsigned long)length);

            if (dataBuffer == NULL || length < minimumLength)
            {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                break;
            }

            RtlZeroMemory(dataBuffer, length);
            inquiry = (PUCHAR)dataBuffer;

            inquiry[0] = DIRECT_ACCESS_DEVICE;
            inquiry[2] = 5; /* SPC-3 */
            inquiry[3] = 2; /* Response data format */
            inquiry[4] = 0x1F; /* 31 additional bytes follow */

            RtlCopyMemory(&inquiry[8], "ReactOS ", 8);
            RtlCopyMemory(&inquiry[16], "AHCI Disk       ", 16);
            RtlCopyMemory(&inquiry[32], "0001", 4);

            AhciClearSense(port);
            success = TRUE;
            Srb->DataTransferLength = (length < minimumLength) ? length : minimumLength;
            break;
        }

        case SCSIOP_REQUEST_SENSE:
        {
            PSENSE_DATA sense = (PSENSE_DATA)dataBuffer;
            ULONG length = Srb->DataTransferLength;

            AHCI_TRACE_PRINT("ahciport: REQUEST_SENSE buffer=%p length=%lu senseValid=%u\n",
                     dataBuffer,
                     (unsigned long)length,
                     port->SenseValid);

            if (sense == NULL || length < sizeof(SENSE_DATA))
            {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                break;
            }

            if (port->SenseValid)
            {
                ULONG used = (length < sizeof(SENSE_DATA)) ? length : sizeof(SENSE_DATA);
                RtlCopyMemory(sense, &port->SenseData, used);
                port->SenseValid = FALSE;
            }
            else
            {
                ULONG used = (length < sizeof(SENSE_DATA)) ? length : sizeof(SENSE_DATA);
                RtlZeroMemory(sense, used);
                sense->ErrorCode = 0x70;
            }

            success = TRUE;
            Srb->DataTransferLength = (length < sizeof(SENSE_DATA)) ? length : sizeof(SENSE_DATA);
            break;
        }

        case SCSIOP_READ_CAPACITY:
        {
            PUCHAR buffer = (PUCHAR)dataBuffer;

            AHCI_TRACE_PRINT("ahciport: READ_CAPACITY buffer=%p length=%lu\n",
                     buffer,
                     (unsigned long)Srb->DataTransferLength);

            if (buffer == NULL || Srb->DataTransferLength < 8)
            {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                break;
            }

            if (port->SectorCount == 0)
            {
                Srb->SrbStatus = SRB_STATUS_ERROR;
                break;
            }

            {
                ULONGLONG lastLba = (port->SectorCount > 0) ? (port->SectorCount - 1) : 0;
                ULONG sectorSize = port->SectorSize ? port->SectorSize : AHCI_DEFAULT_SECTOR_SIZE;

                if (lastLba > 0xFFFFFFFFULL)
                    lastLba = 0xFFFFFFFFULL;

                buffer[0] = (UCHAR)((lastLba >> 24) & 0xFF);
                buffer[1] = (UCHAR)((lastLba >> 16) & 0xFF);
                buffer[2] = (UCHAR)((lastLba >> 8) & 0xFF);
                buffer[3] = (UCHAR)(lastLba & 0xFF);
                buffer[4] = (UCHAR)((sectorSize >> 24) & 0xFF);
                buffer[5] = (UCHAR)((sectorSize >> 16) & 0xFF);
                buffer[6] = (UCHAR)((sectorSize >> 8) & 0xFF);
                buffer[7] = (UCHAR)(sectorSize & 0xFF);
            }

            AhciClearSense(port);
            success = TRUE;
            Srb->DataTransferLength = (Srb->DataTransferLength < 8) ? Srb->DataTransferLength : 8;
            break;
        }

        case SCSIOP_SERVICE_ACTION_IN16:
        {
            if (cdb[1] == SERVICE_ACTION_READ_CAPACITY16)
            {
                PUCHAR buffer = (PUCHAR)dataBuffer;

                AHCI_TRACE_PRINT("ahciport: READ_CAPACITY16 buffer=%p length=%lu\n",
                         buffer,
                         (unsigned long)Srb->DataTransferLength);

                if (buffer == NULL || Srb->DataTransferLength < 32)
                {
                    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                    break;
                }

                RtlZeroMemory(buffer, Srb->DataTransferLength);
                if (port->SectorCount)
                {
                    ULONGLONG lastLba = port->SectorCount - 1;
                    ULONG sectorSize = port->SectorSize ? port->SectorSize : AHCI_DEFAULT_SECTOR_SIZE;

                    buffer[0] = (UCHAR)((lastLba >> 56) & 0xFF);
                    buffer[1] = (UCHAR)((lastLba >> 48) & 0xFF);
                    buffer[2] = (UCHAR)((lastLba >> 40) & 0xFF);
                    buffer[3] = (UCHAR)((lastLba >> 32) & 0xFF);
                    buffer[4] = (UCHAR)((lastLba >> 24) & 0xFF);
                    buffer[5] = (UCHAR)((lastLba >> 16) & 0xFF);
                    buffer[6] = (UCHAR)((lastLba >> 8) & 0xFF);
                    buffer[7] = (UCHAR)(lastLba & 0xFF);
                    buffer[8] = (UCHAR)((sectorSize >> 24) & 0xFF);
                    buffer[9] = (UCHAR)((sectorSize >> 16) & 0xFF);
                    buffer[10] = (UCHAR)((sectorSize >> 8) & 0xFF);
                    buffer[11] = (UCHAR)(sectorSize & 0xFF);
                }

                AhciClearSense(port);
                success = TRUE;
                Srb->DataTransferLength = (Srb->DataTransferLength < 32) ? Srb->DataTransferLength : 32;
                break;
            }

            /* Unsupported service action */
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
        }

        case SCSIOP_READ:
        case SCSIOP_READ12:
        case SCSIOP_READ16:
        {
            ULONGLONG lba;
            ULONG length;

            if (dataBuffer == NULL || Srb->DataTransferLength == 0)
            {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                break;
            }

            if (opcode == SCSIOP_READ16)
            {
                lba = ((ULONGLONG)cdb[2] << 56) |
                      ((ULONGLONG)cdb[3] << 48) |
                      ((ULONGLONG)cdb[4] << 40) |
                      ((ULONGLONG)cdb[5] << 32) |
                      ((ULONGLONG)cdb[6] << 24) |
                      ((ULONGLONG)cdb[7] << 16) |
                      ((ULONGLONG)cdb[8] << 8) |
                       (ULONGLONG)cdb[9];
                length = (cdb[10] << 24) |
                         (cdb[11] << 16) |
                         (cdb[12] << 8) |
                          cdb[13];
            }
            else if (opcode == SCSIOP_READ12)
            {
                lba = (cdb[2] << 24) |
                      (cdb[3] << 16) |
                      (cdb[4] << 8) |
                       cdb[5];
                length = (cdb[6] << 24) |
                         (cdb[7] << 16) |
                         (cdb[8] << 8) |
                          cdb[9];
            }
            else
            {
                lba = (cdb[2] << 24) |
                      (cdb[3] << 16) |
                      (cdb[4] << 8) |
                       cdb[5];
                length = (cdb[7] << 8) | cdb[8];
            }

            if (length == 0)
            {
                success = TRUE;
                break;
            }

            success = AhciReadWriteSectors(Adapter,
                                            PortNumber,
                                            Srb,
                                            dataBuffer,
                                            lba,
                                            Srb->DataTransferLength,
                                            FALSE);
            break;
        }

        case SCSIOP_WRITE:
        case SCSIOP_WRITE12:
        case SCSIOP_WRITE16:
        {
            ULONGLONG lba;
            ULONG length;

            if (dataBuffer == NULL || Srb->DataTransferLength == 0)
            {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                break;
            }

            if (opcode == SCSIOP_WRITE16)
            {
                lba = ((ULONGLONG)cdb[2] << 56) |
                      ((ULONGLONG)cdb[3] << 48) |
                      ((ULONGLONG)cdb[4] << 40) |
                      ((ULONGLONG)cdb[5] << 32) |
                      ((ULONGLONG)cdb[6] << 24) |
                      ((ULONGLONG)cdb[7] << 16) |
                      ((ULONGLONG)cdb[8] << 8) |
                       (ULONGLONG)cdb[9];
                length = (cdb[10] << 24) |
                         (cdb[11] << 16) |
                         (cdb[12] << 8) |
                          cdb[13];
            }
            else if (opcode == SCSIOP_WRITE12)
            {
                lba = (cdb[2] << 24) |
                      (cdb[3] << 16) |
                      (cdb[4] << 8) |
                       cdb[5];
                length = (cdb[6] << 24) |
                         (cdb[7] << 16) |
                         (cdb[8] << 8) |
                          cdb[9];
            }
            else
            {
                lba = (cdb[2] << 24) |
                      (cdb[3] << 16) |
                      (cdb[4] << 8) |
                       cdb[5];
                length = (cdb[7] << 8) | cdb[8];
            }

            if (length == 0)
            {
                success = TRUE;
                break;
            }

            success = AhciReadWriteSectors(Adapter,
                                            PortNumber,
                                            Srb,
                                            dataBuffer,
                                            lba,
                                            Srb->DataTransferLength,
                                            TRUE);
            break;
        }

        case SCSIOP_SYNCHRONIZE_CACHE:
        case SCSIOP_SYNCHRONIZE_CACHE16:
        {
            success = AhciIssueAtaCommand(Adapter,
                                          PortNumber,
                                          Srb,
                                          (opcode == SCSIOP_SYNCHRONIZE_CACHE16) ? 0xEA : 0xE7,
                                          0,
                                          0,
                                          FALSE,
                                          NULL,
                                          0);
            if (success)
                AhciClearSense(port);
            else
                AhciRecordSense(port, SCSI_SENSE_HARDWARE_ERROR, SCSI_ADSENSE_NO_SENSE, 0);
            Srb->DataTransferLength = 0;
            break;
        }

        default:
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }

    if (!success && Srb->SrbStatus == SRB_STATUS_SUCCESS)
    {
        Srb->SrbStatus = SRB_STATUS_ERROR;
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    }

    return TRUE;
}

static VOID
AhciProcessExecuteSrb(
    _In_ PAHCI_ADAPTER_EXTENSION Adapter,
    _In_ ULONG PortNumber,
    _Inout_ PSCSI_REQUEST_BLOCK Srb)
{
    if (Adapter == NULL || Srb == NULL)
        return;

    AHCI_TRACE("HwStartIo: Executing opcode 0x%02x on port %u (Length=%lu)",
               Srb->Cdb[0], PortNumber, Srb->DataTransferLength);
    AHCI_TRACE_PRINT("ahciport: opcode=0x%02x transfer=%lu\n",
             Srb->Cdb[0], (unsigned long)Srb->DataTransferLength);

    if (Srb->CdbLength > 0)
    {
        AHCI_TRACE_PRINT("ahciport: CDB bytes:");
        for (ULONG i = 0; i < Srb->CdbLength && i < 16; ++i)
        {
            AHCI_TRACE_PRINT(" %02x", Srb->Cdb[i]);
        }
        if (Srb->CdbLength > 16)
            AHCI_TRACE_PRINT(" ...");
        AHCI_TRACE_PRINT("\n");
    }

    AhciHandleExecuteScsi(Adapter, PortNumber, Srb);

    AHCI_TRACE_PRINT("ahciport: completion status=0x%x scsi=0x%x data=%lu\n",
             Srb->SrbStatus,
             Srb->ScsiStatus,
             (unsigned long)Srb->DataTransferLength);
}

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
    ACCESS_RANGE (*ranges)[];
    ULONG i;

    AHCI_TRACE_PRINT("ahciport: HwFindAdapter bus=%lu interface=%u ranges=%lu\n",
             ConfigInfo ? ConfigInfo->SystemIoBusNumber : 0,
             ConfigInfo ? ConfigInfo->AdapterInterfaceType : 0,
             ConfigInfo ? (ULONG)ConfigInfo->NumberOfAccessRanges : 0);

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);

    AHCI_TRACE("HwFindAdapter: Bus=%lu Interface=%d AccessRanges=%lu",
               ConfigInfo->SystemIoBusNumber,
               ConfigInfo->AdapterInterfaceType,
               (ULONG)ConfigInfo->NumberOfAccessRanges);

    *Again = FALSE;

    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->Master = TRUE;
    ConfigInfo->MapBuffers = TRUE;
    ConfigInfo->NeedPhysicalAddresses = TRUE;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->NumberOfPhysicalBreaks = AHCI_MAX_PRDT_ENTRIES - 1;
    ConfigInfo->MaximumNumberOfTargets = AHCI_MAX_PORTS;
    ConfigInfo->MaximumNumberOfLogicalUnits = 1;
    ConfigInfo->MaximumTransferLength = AHCI_MAX_TRANSFER_LENGTH;
    ConfigInfo->AlignmentMask = sizeof(ULONG) - 1;
    /* Final DMA addressing policy is selected after reading CAP.S64A. */
    ConfigInfo->Dma32BitAddresses = TRUE;
    ConfigInfo->SrbExtensionSize = 0;
#ifdef AHCI_USE_STORPORT
    ConfigInfo->Dma64BitAddresses = 0;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
#else
    ConfigInfo->Dma64BitAddresses = 0;
#endif

    RtlZeroMemory(adapter, sizeof(*adapter));

    if (ConfigInfo->NumberOfAccessRanges == 0 || ConfigInfo->AccessRanges == NULL)
        return SP_RETURN_ERROR;

    ranges = ConfigInfo->AccessRanges;

    for (i = 0; i < ConfigInfo->NumberOfAccessRanges; ++i)
    {
        ACCESS_RANGE *range = &(*ranges)[i];
        PVOID base;

        if (!range->RangeInMemory)
            continue;

        if (range->RangeLength < sizeof(AHCI_HBA_MEM))
            continue;

        base = ScsiPortGetDeviceBase(DeviceExtension,
                                     ConfigInfo->AdapterInterfaceType,
                                     ConfigInfo->SystemIoBusNumber,
                                     range->RangeStart,
                                     range->RangeLength,
                                     FALSE);
        if (base == NULL)
        {
            AHCI_ERROR("HwFindAdapter: ScsiPortGetDeviceBase failed for range %lu", i);
            continue;
        }

        adapter->AbBase = (PAHCI_HBA_MEM)base;
        adapter->Version = adapter->AbBase->VS;
        adapter->Cap = adapter->AbBase->CAP;
        adapter->PortsImplemented = adapter->AbBase->PI;
        adapter->Quirks = 0;

        {
            PCI_COMMON_CONFIG pciConfig;

            if (AhciReadPciConfig(adapter, ConfigInfo, &pciConfig))
            {
                adapter->Quirks = AhciGetPciQuirks(&pciConfig);
                if (adapter->Quirks != 0)
                {
                    AHCI_WARN("HwFindAdapter: PCI %04x:%04x rev=%02x quirks=0x%08lx",
                              pciConfig.VendorID,
                              pciConfig.DeviceID,
                              pciConfig.RevisionID,
                              adapter->Quirks);
                }
            }
        }

        if (adapter->Cap & AHCI_CAP_S64A)
        {
            ConfigInfo->Dma32BitAddresses = FALSE;
            ConfigInfo->Dma64BitAddresses = SCSI_DMA64_MINIPORT_SUPPORTED;
        }
        else
        {
            ConfigInfo->Dma32BitAddresses = TRUE;
            ConfigInfo->Dma64BitAddresses = 0;
        }

        if (adapter->PortsImplemented == 0)
        {
            AHCI_WARN("HwFindAdapter: PortsImplemented=0, releasing base");
            ScsiPortFreeDeviceBase(DeviceExtension, base);
            adapter->AbBase = NULL;
            continue;
        }

        AHCI_TRACE("HwFindAdapter: ABAR=%p VS=0x%08lx CAP=0x%08lx PI=0x%08lx",
                   adapter->AbBase,
                   adapter->Version,
                   adapter->Cap,
                   adapter->PortsImplemented);

        adapter->PortCount = 0;
        for (ULONG portIndex = 0; portIndex < AHCI_MAX_PORTS; ++portIndex)
        {
            if (adapter->PortsImplemented & (1u << portIndex))
                adapter->PortCount = (UCHAR)(portIndex + 1);
        }

        if (adapter->PortCount == 0)
            adapter->PortCount = 1;

        ConfigInfo->MaximumNumberOfTargets = adapter->PortCount;

        {
            ULONG clSize = AHCI_ALIGN_UP(sizeof(AHCI_CMD_HEADER) * 32, AHCI_CMD_LIST_ALIGN);
            ULONG fisSize = AHCI_ALIGN_UP(256, AHCI_RECEIVED_FIS_ALIGN);
            ULONG tableSize = AHCI_ALIGN_UP(AHCI_CMD_TABLE_ALLOC_SIZE, AHCI_CMD_TABLE_ALIGN);
            ULONG idSize = AHCI_ALIGN_UP(512, AHCI_IDENTIFY_ALIGN);
            ULONG alignmentSlack = (AHCI_CMD_LIST_ALIGN - 1) +
                                   (AHCI_RECEIVED_FIS_ALIGN - 1) +
                                   (AHCI_CMD_TABLE_ALIGN - 1) +
                                   (AHCI_IDENTIFY_ALIGN - 1);
            ULONG perPort = clSize + fisSize + tableSize + idSize + alignmentSlack;
            ULONG total = perPort * adapter->PortCount;
            PUCHAR block;

            block = ScsiPortGetUncachedExtension(DeviceExtension, ConfigInfo, total);
            if (block == NULL)
            {
                AHCI_ERROR("HwFindAdapter: ScsiPortGetUncachedExtension failed (%lu bytes)", total);
                return SP_RETURN_NOT_FOUND;
            }

            RtlZeroMemory(block, total);

            adapter->NonCachedBase = block;
            adapter->NonCachedBytes = total;

            for (ULONG portIndex = 0; portIndex < adapter->PortCount; ++portIndex)
            {
                PAHCI_PORT_CONTEXT port = &adapter->Ports[portIndex];
                ULONG chunk;
                PUCHAR base = block + (perPort * portIndex);
                PUCHAR ptr = base;

                RtlZeroMemory(port, sizeof(*port));

                ptr = AHCI_ALIGN_PTR(ptr, AHCI_CMD_LIST_ALIGN);
                port->CommandList = (PAHCI_CMD_HEADER)ptr;
                chunk = clSize;
                port->CommandListPhys = ScsiPortGetPhysicalAddress(adapter, NULL, port->CommandList, &chunk);
                ptr += clSize;

                ptr = AHCI_ALIGN_PTR(ptr, AHCI_RECEIVED_FIS_ALIGN);
                port->ReceivedFis = ptr;
                chunk = fisSize;
                port->ReceivedFisPhys = ScsiPortGetPhysicalAddress(adapter, NULL, port->ReceivedFis, &chunk);
                ptr += fisSize;

                ptr = AHCI_ALIGN_PTR(ptr, AHCI_CMD_TABLE_ALIGN);
                port->CommandTable = (PAHCI_CMD_TABLE)ptr;
                chunk = tableSize;
                port->CommandTablePhys = ScsiPortGetPhysicalAddress(adapter, NULL, port->CommandTable, &chunk);
                ptr += tableSize;

                ptr = AHCI_ALIGN_PTR(ptr, AHCI_IDENTIFY_ALIGN);
                port->IdentifyBuffer = ptr;
                chunk = idSize;
                port->IdentifyBufferPhys = ScsiPortGetPhysicalAddress(adapter, NULL, port->IdentifyBuffer, &chunk);

                /* No bounce path for HBA command structures; require <=4GB on 32-bit-only HBAs. */
                if (!(adapter->Cap & AHCI_CAP_S64A) &&
                    (port->CommandListPhys.HighPart ||
                     port->ReceivedFisPhys.HighPart ||
                     port->CommandTablePhys.HighPart ||
                     port->IdentifyBufferPhys.HighPart))
                {
                    AHCI_ERROR("HwFindAdapter: HBA lacks 64-bit DMA but non-cached extension is above 4GB");
                    return SP_RETURN_NOT_FOUND;
                }

                port->SectorSize = AHCI_DEFAULT_SECTOR_SIZE;

                AHCI_TRACE_PRINT("ahciport: Port %lu buffers CLB=%p FB=%p CT=%p\n",
                                 portIndex,
                                 port->CommandList,
                                 port->ReceivedFis,
                                 port->CommandTable);
            }
        }

        AHCI_TRACE("HwFindAdapter: Controller ready (Ports=%u)", adapter->PortCount);

        return SP_RETURN_FOUND;
    }

    return SP_RETURN_NOT_FOUND;
}

static BOOLEAN NTAPI
AhciHwInitialize(
    _In_ PVOID DeviceExtension)
{
    PAHCI_ADAPTER_EXTENSION adapter = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;
    ULONG port;

    AHCI_TRACE_PRINT("ahciport: HwInitialize\n");

    if (adapter == NULL || adapter->AbBase == NULL)
        return FALSE;

    AHCI_TRACE("HwInitialize: enabling HBA (PortsImplemented=0x%08lx)", adapter->PortsImplemented);

    adapter->AbBase->IS = 0xFFFFFFFF;
    adapter->AbBase->GHC |= AHCI_GHC_AE;
    adapter->AbBase->GHC &= ~AHCI_GHC_IE; /* we poll */

    for (port = 0; port < adapter->PortCount && port < AHCI_MAX_PORTS; ++port)
    {
        if (!(adapter->PortsImplemented & (1u << port)))
            continue;

        AHCI_TRACE_PRINT("ahciport: HwInitialize resetting port=%lu\n", port);

        if (!AhciResetPort(adapter, port))
        {
            AHCI_WARN("HwInitialize: Port %lu reset failed", port);
            continue;
        }

        adapter->Ports[port].Signature = AhciReadPort(adapter, port, AHCI_PxSIG);

        {
            BOOLEAN present = AhciIsPortDevicePresent(adapter, port);
#if DBG && AHCI_ENABLE_TRACE
            ULONG ssts = AhciReadPort(adapter, port, AHCI_PxSSTS);

            AHCI_TRACE("HwInitialize: port=%lu signature=0x%08lx SSTS=0x%08lx (DET=%lu SPD=%lu IPM=%lu) present=%u",
                       port,
                       adapter->Ports[port].Signature,
                       ssts,
                       ssts & AHCI_PxSSTS_DET_MASK,
                       (ssts & AHCI_PxSSTS_SPD_MASK) >> 4,
                       (ssts & AHCI_PxSSTS_IPM_MASK) >> 8,
                       present);
#else
            AHCI_TRACE("HwInitialize: port=%lu signature=0x%08lx present=%u",
                       port,
                       adapter->Ports[port].Signature,
                       present);
#endif

            if (!present)
            {
                AhciDumpPortState(adapter, port, "HwInitialize no device");
                continue;
            }
        }

        switch (adapter->Ports[port].Signature)
        {
            case AHCI_SIGNATURE_SATA:
                if (!AhciIdentifyDevice(adapter, port))
                {
                    AHCI_WARN("HwInitialize: IDENTIFY failed on port %lu", port);
                    adapter->Ports[port].Present = FALSE;
                }
                break;

            case AHCI_SIGNATURE_ATAPI:
                adapter->Ports[port].Atapi = TRUE;
                adapter->Ports[port].Present = TRUE;
                if (adapter->Ports[port].SectorSize == 0)
                    adapter->Ports[port].SectorSize = 2048;
                adapter->Ports[port].SectorCount = 0;
                AhciClearSense(&adapter->Ports[port]);

                if (!AhciIdentifyPacketDevice(adapter, port))
                {
                    AHCI_WARN("HwInitialize: IDENTIFY PACKET failed on port %lu", port);
                }
                break;

            default:
                AHCI_WARN("HwInitialize: Unsupported signature 0x%08lx on port %lu",
                          adapter->Ports[port].Signature,
                          port);
                adapter->Ports[port].Present = FALSE;
                break;
        }

        if (adapter->Ports[port].Present)
            AhciNotifyBusChange(adapter, 0, (UCHAR)port);
    }

    AhciNotifyBusChange(adapter, 0, 0xFF);
    return TRUE;
}

static BOOLEAN NTAPI
AhciHwStartIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PAHCI_ADAPTER_EXTENSION adapter = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;
    ULONG portNumber;

    if (Srb == NULL)
        return FALSE;

    AHCI_TRACE_PRINT("ahciport: HwStartIo path=%u target=%u lun=%u function=0x%x\n",
             Srb->PathId, Srb->TargetId, Srb->Lun, Srb->Function);
    AHCI_TRACE_PRINT("ahciport: HwStartIo entry=%p\n", AhciHwStartIo);

    if (Srb->PathId != 0 || Srb->TargetId >= adapter->PortCount || Srb->Lun != 0)
    {
        AHCI_WARN("HwStartIo: Invalid Path/LUN (Path=%u Target=%u LUN=%u)",
                   Srb->PathId, Srb->TargetId, Srb->Lun);
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        goto complete;
    }

    portNumber = Srb->TargetId;

#ifdef AHCI_USE_STORPORT
    if (Srb->Function == SRB_FUNCTION_PNP)
    {
        PSCSI_PNP_REQUEST_BLOCK pnp = (PSCSI_PNP_REQUEST_BLOCK)Srb;
        BOOLEAN adapterRequest = (pnp->SrbPnPFlags & SRB_PNP_FLAGS_ADAPTER_REQUEST) != 0;

        AHCI_TRACE_PRINT("ahciport: PNP action=%u flags=0x%lx\n",
                 pnp->PnPAction,
                 pnp->SrbPnPFlags);

        switch (pnp->PnPAction)
        {
            case StorStartDevice:
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                break;

            case StorStopDevice:
            case StorRemoveDevice:
            case StorSurpriseRemoval:
                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                break;

            case StorQueryCapabilities:
            {
                PDEVICE_CAPABILITIES caps = (PDEVICE_CAPABILITIES)pnp->DataBuffer;

                if (caps != NULL &&
                    pnp->DataTransferLength >= sizeof(DEVICE_CAPABILITIES))
                {
                    RtlZeroMemory(caps, sizeof(*caps));
                    caps->Size = sizeof(*caps);
                    caps->Version = 1;
                    caps->Address = pnp->TargetId;
                    caps->UINumber = pnp->TargetId;
                    for (ULONG state = 0; state < PowerSystemMaximum; ++state)
                    {
                        caps->DeviceState[state] = (state == PowerSystemWorking) ? PowerDeviceD0 : PowerDeviceD3;
                    }
                    caps->DeviceWake = PowerDeviceUnspecified;
                    caps->SystemWake = PowerSystemUnspecified;
                    caps->Removable = FALSE;
                    caps->SurpriseRemovalOK = TRUE;
                    caps->UniqueID = FALSE;
                    Srb->SrbStatus = SRB_STATUS_SUCCESS;
                }
                else
                {
                    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                }
                break;
            }

            default:
                Srb->SrbStatus = adapterRequest ? SRB_STATUS_INVALID_REQUEST : SRB_STATUS_SUCCESS;
                break;
        }

        goto complete;
    }

    if (Srb->Function == SRB_FUNCTION_RELEASE_DEVICE ||
        Srb->Function == SRB_FUNCTION_SHUTDOWN ||
        Srb->Function == SRB_FUNCTION_FLUSH ||
        Srb->Function == SRB_FUNCTION_RESET_LOGICAL_UNIT)
    {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        Srb->DataTransferLength = 0;
        goto complete;
    }

    if (Srb->Function == SRB_FUNCTION_IO_CONTROL)
    {
        AhciHandleIoControl(adapter, portNumber, Srb);
        goto complete;
    }

    if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI)
    {
        AhciProcessExecuteSrb(adapter, portNumber, Srb);
        goto complete;
    }
#endif

    if (Srb->Function != SRB_FUNCTION_EXECUTE_SCSI)
    {
        AHCI_WARN("HwStartIo: Unsupported SRB function 0x%02x", Srb->Function);
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        goto complete;
    }

    AhciProcessExecuteSrb(adapter, portNumber, Srb);

complete:
#ifdef AHCI_USE_STORPORT
    return TRUE;
#else
    AhciFinishRequest(adapter, Srb, FALSE);
    return TRUE;
#endif
}

static BOOLEAN NTAPI
AhciHwInterrupt(
    _In_ PVOID DeviceExtension)
{
    PAHCI_ADAPTER_EXTENSION adapter = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;
    ULONG is;
    ULONG port;
    BOOLEAN handled = FALSE;

    if (adapter == NULL || adapter->AbBase == NULL)
        return FALSE;

    is = adapter->AbBase->IS;
    if (is == 0)
        return FALSE;

    adapter->AbBase->IS = is;

    for (port = 0; port < AHCI_MAX_PORTS; ++port)
    {
        if (!(is & (1u << port)))
            continue;

        AhciWritePort(adapter, port, AHCI_PxIS, 0xFFFFFFFF);
        handled = TRUE;
    }

    return handled;
}

static BOOLEAN NTAPI
AhciHwResetBus(
    _In_ PVOID DeviceExtension,
    _In_ ULONG PathId)
{
    PAHCI_ADAPTER_EXTENSION adapter = (PAHCI_ADAPTER_EXTENSION)DeviceExtension;
    ULONG port;

    if (PathId != 0)
        return TRUE;

    for (port = 0; port < adapter->PortCount && port < AHCI_MAX_PORTS; ++port)
    {
        if (!(adapter->PortsImplemented & (1u << port)))
            continue;

        if (!AhciResetPort(adapter, port))
            continue;

        if (!AhciIsPortDevicePresent(adapter, port))
        {
            adapter->Ports[port].Present = FALSE;
            continue;
        }

        switch (adapter->Ports[port].Signature)
        {
            case AHCI_SIGNATURE_SATA:
                AhciIdentifyDevice(adapter, port);
                break;

            case AHCI_SIGNATURE_ATAPI:
                adapter->Ports[port].Atapi = TRUE;
                adapter->Ports[port].Present = TRUE;
                if (adapter->Ports[port].SectorSize == 0)
                    adapter->Ports[port].SectorSize = 2048;
                adapter->Ports[port].SectorCount = 0;
                AhciClearSense(&adapter->Ports[port]);

                AhciIdentifyPacketDevice(adapter, port);
                break;

            default:
                break;
        }

        AhciNotifyBusChange(adapter, 0, (UCHAR)port);
    }

    AhciNotifyBusChange(adapter, 0, 0xFF);
    return TRUE;
}

ULONG NTAPI
DriverEntry(
    _In_ PVOID DriverObject,
    _In_ PVOID RegistryPath)
{
    HW_INITIALIZATION_DATA hw;
    static const CHAR VendorIdString[] = "8086";
    static const CHAR DeviceIdString[] = "2922";

    AHCI_TRACE_PRINT("ahciport: DriverEntry invoked\n");
    AHCI_TRACE_PRINT("ahciport: DriverEntry=%p HwInitialize=%p HwStartIo=%p\n",
             DriverEntry,
             AhciHwInitialize,
             AhciHwStartIo);
#ifdef AHCI_USE_STORPORT
    AHCI_TRACE_PRINT("ahciport: StorPortInitialize=%p StorPortGetPhysicalAddress=%p\n",
             StorPortInitialize,
             StorPortGetPhysicalAddress);
#endif
    RtlZeroMemory(&hw, sizeof(hw));
    hw.HwInitializationDataSize = sizeof(hw);
    hw.AdapterInterfaceType = PCIBus;
    hw.NumberOfAccessRanges = 6;
    hw.HwFindAdapter = AhciHwFindAdapter;
    hw.HwInitialize = AhciHwInitialize;
    hw.HwStartIo = AhciHwStartIo;
    hw.HwInterrupt = AhciHwInterrupt;
    hw.HwResetBus = AhciHwResetBus;
#ifdef AHCI_USE_STORPORT
    hw.HwAdapterControl = AhciHwAdapterControl;
#endif

    hw.VendorId = (PVOID)VendorIdString;
    hw.VendorIdLength = sizeof(VendorIdString) - 1;
    hw.DeviceId = (PVOID)DeviceIdString;
    hw.DeviceIdLength = sizeof(DeviceIdString) - 1;

    hw.TaggedQueuing = FALSE;
    hw.AutoRequestSense = TRUE;
    hw.MultipleRequestPerLu = FALSE;
    hw.NeedPhysicalAddresses = TRUE;
#ifdef AHCI_USE_STORPORT
    hw.MapBuffers = STOR_MAP_ALL_BUFFERS;
#else
    hw.MapBuffers = TRUE;
#endif

    hw.DeviceExtensionSize = sizeof(AHCI_ADAPTER_EXTENSION);
    hw.SrbExtensionSize = sizeof(AHCI_SRB_EXTENSION);

#ifdef AHCI_USE_STORPORT
    {
        ULONG status = StorPortInitialize(DriverObject, RegistryPath, &hw, NULL);
        AHCI_TRACE("DriverEntry: PortInitialize -> 0x%08lx", status);
        return status;
    }
#else
    {
        ULONG status = ScsiPortInitialize(DriverObject, RegistryPath, &hw, NULL);
        AHCI_TRACE("DriverEntry: PortInitialize -> 0x%08lx", status);
        return status;
    }
#endif
}
