/*
 * PROJECT:     ReactOS Storport NVMe miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SCSI to NVMe command translation
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "stornvme.h"

VOID
NvmeCompleteSrb(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb, _In_ UCHAR SrbStatus)
{
    Srb->SrbStatus = SrbStatus;
    if (SRB_STATUS(SrbStatus) == SRB_STATUS_SUCCESS)
        Srb->ScsiStatus = SCSISTAT_GOOD;
    StorPortNotification(RequestComplete, Device, Srb);
}

VOID
NvmeSetSenseAndComplete(_In_ PNVME_DEVICE_EXTENSION Device,
                        _In_ PSCSI_REQUEST_BLOCK Srb,
                        _In_ UCHAR SenseKey,
                        _In_ UCHAR AdditionalSenseCode)
{
    if (Srb->SenseInfoBuffer != NULL && Srb->SenseInfoBufferLength >= 14)
    {
        PUCHAR Sense = (PUCHAR)Srb->SenseInfoBuffer;

        RtlZeroMemory(Sense, Srb->SenseInfoBufferLength);
        Sense[0] = 0x70;
        Sense[2] = SenseKey;
        Sense[7] = 6;
        Sense[12] = AdditionalSenseCode;
        Srb->SrbStatus = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
    }
    else
    {
        Srb->SrbStatus = SRB_STATUS_ERROR;
    }
    Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    StorPortNotification(RequestComplete, Device, Srb);
}

static
BOOLEAN
NvmeCopyToDataBuffer(_In_ PSCSI_REQUEST_BLOCK Srb, _In_ PVOID Data, _In_ ULONG Length)
{
    ULONG Copied = min(Length, Srb->DataTransferLength);

    if (Copied != 0 && Srb->DataBuffer == NULL)
        return FALSE;
    if (Copied != 0)
        RtlCopyMemory(Srb->DataBuffer, Data, Copied);
    Srb->DataTransferLength = Copied;
    return TRUE;
}

static
PNVME_NAMESPACE
NvmeGetNamespace(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PNVME_NAMESPACE Namespace;

    if (Srb->PathId != 0 || Srb->TargetId != 0 || Srb->Lun >= Device->NamespaceCount)
        return NULL;
    Namespace = &Device->Namespaces[Srb->Lun];
    return Namespace->Ready ? Namespace : NULL;
}

static
ULONGLONG
NvmeSrbExtensionPhysical(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PNVME_SRB_EXTENSION SrbExtension)
{
    STOR_PHYSICAL_ADDRESS Physical;
    ULONG Length;

    Physical = StorPortGetPhysicalAddress(Device, NULL, &SrbExtension->Dma, &Length);
    return (ULONGLONG)Physical.QuadPart;
}

static
VOID
NvmeHandleInquiry(_In_ PNVME_DEVICE_EXTENSION Device,
                  _In_ PNVME_NAMESPACE Namespace,
                  _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PCDB Cdb = (PCDB)Srb->Cdb;
    UCHAR Buffer[96];
    ULONG Length;
    ULONG AllocationLength;
    ULONG StringLength;
    ULONG Index;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    AllocationLength = Cdb->CDB6INQUIRY3.AllocationLength;
    if (Cdb->CDB6INQUIRY3.EnableVitalProductData)
    {
        switch (Cdb->CDB6INQUIRY3.PageCode)
        {
            case VPD_SUPPORTED_PAGES:
            {
                ULONG Count = 0;

                Buffer[1] = VPD_SUPPORTED_PAGES;
                Buffer[4 + Count++] = VPD_SUPPORTED_PAGES;
                Buffer[4 + Count++] = VPD_SERIAL_NUMBER;
                Buffer[4 + Count++] = VPD_DEVICE_IDENTIFIERS;
                Buffer[4 + Count++] = VPD_BLOCK_LIMITS;
                Buffer[4 + Count++] = VPD_BLOCK_DEVICE_CHARACTERISTICS;
                if (Device->Oncs & NVME_ONCS_DSM)
                    Buffer[4 + Count++] = VPD_LOGICAL_BLOCK_PROVISIONING;
                Buffer[3] = (UCHAR)Count;
                Length = 4 + Count;
                break;
            }

            case VPD_SERIAL_NUMBER:
                for (StringLength = 0; StringLength < 20 && Device->Serial[StringLength] != 0; StringLength++)
                    NOTHING;
                Buffer[1] = VPD_SERIAL_NUMBER;
                Buffer[3] = (UCHAR)StringLength;
                RtlCopyMemory(&Buffer[4], Device->Serial, StringLength);
                Length = 4 + StringLength;
                break;

            case VPD_DEVICE_IDENTIFIERS:
            {
                BOOLEAN HaveEui = FALSE;
                BOOLEAN HaveGuid = FALSE;

                for (Index = 0; Index < 8; Index++)
                    HaveEui |= Namespace->Eui64[Index] != 0;
                for (Index = 0; Index < 16; Index++)
                    HaveGuid |= Namespace->Nguid[Index] != 0;

                Buffer[1] = VPD_DEVICE_IDENTIFIERS;
                Length = 4;
                Buffer[Length + 0] = 0x02;      /* ASCII, T10 vendor identification */
                Buffer[Length + 1] = 0x01;
                Buffer[Length + 3] = 28;
                RtlFillMemory(&Buffer[Length + 4], 28, ' ');
                RtlCopyMemory(&Buffer[Length + 4], "NVMe    ", 8);
                for (StringLength = 0; StringLength < 20 && Device->Serial[StringLength] != 0; StringLength++)
                    NOTHING;
                RtlCopyMemory(&Buffer[Length + 12], Device->Serial, StringLength);
                Length += 32;
                if (HaveEui || HaveGuid)
                {
                    /* Binary EUI-64 designator from the namespace identity. */
                    Buffer[Length + 0] = 0x01;
                    Buffer[Length + 1] = 0x02;
                    Buffer[Length + 3] = HaveEui ? 8 : 16;
                    if (HaveEui)
                        RtlCopyMemory(&Buffer[Length + 4], Namespace->Eui64, 8);
                    else
                        RtlCopyMemory(&Buffer[Length + 4], Namespace->Nguid, 16);
                    Length += 4 + Buffer[Length + 3];
                }
                Buffer[2] = (UCHAR)((Length - 4) >> 8);
                Buffer[3] = (UCHAR)(Length - 4);
                break;
            }

            case VPD_BLOCK_LIMITS:
            {
                ULONG MaximumBlocks = Device->MaximumTransferLength >> Namespace->BlockShift;

                Buffer[1] = VPD_BLOCK_LIMITS;
                Buffer[3] = 0x3C;
                Buffer[7] = 1;
                Buffer[8] = (UCHAR)(MaximumBlocks >> 24);
                Buffer[9] = (UCHAR)(MaximumBlocks >> 16);
                Buffer[10] = (UCHAR)(MaximumBlocks >> 8);
                Buffer[11] = (UCHAR)MaximumBlocks;
                Buffer[12] = Buffer[8];
                Buffer[13] = Buffer[9];
                Buffer[14] = Buffer[10];
                Buffer[15] = Buffer[11];
                if (Device->Oncs & NVME_ONCS_DSM)
                {
                    Buffer[20] = 0xFF;
                    Buffer[21] = 0xFF;
                    Buffer[22] = 0xFF;
                    Buffer[23] = 0xFF;
                    Buffer[27] = NVME_MAX_DSM_RANGES;
                }
                Length = 64;
                break;
            }

            case VPD_BLOCK_DEVICE_CHARACTERISTICS:
                Buffer[1] = VPD_BLOCK_DEVICE_CHARACTERISTICS;
                Buffer[3] = 0x3C;
                Buffer[4] = 0x00;
                Buffer[5] = 0x01;   /* non-rotating medium */
                Length = 64;
                break;

            case VPD_LOGICAL_BLOCK_PROVISIONING:
                if ((Device->Oncs & NVME_ONCS_DSM) == 0)
                {
                    NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
                    return;
                }
                Buffer[1] = VPD_LOGICAL_BLOCK_PROVISIONING;
                Buffer[3] = 0x04;
                Buffer[5] = 0x80;   /* LBPU */
                Buffer[6] = 0x02;   /* thin provisioned */
                Length = 8;
                break;

            default:
                NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
                return;
        }
    }
    else
    {
        PINQUIRYDATA Inquiry = (PINQUIRYDATA)Buffer;

        if (Cdb->CDB6INQUIRY3.PageCode != 0)
        {
            NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
            return;
        }
        Inquiry->DeviceType = DIRECT_ACCESS_DEVICE;
        Inquiry->RemovableMedia = FALSE;
        Inquiry->Versions = 5;
        Inquiry->ResponseDataFormat = 2;
        Inquiry->AdditionalLength = INQUIRYDATABUFFERSIZE - 5;
        Inquiry->CommandQueue = 1;
        RtlCopyMemory(Inquiry->VendorId, "NVMe    ", 8);
        RtlFillMemory(Inquiry->ProductId, 16, ' ');
        for (StringLength = 0; StringLength < 16 && Device->Model[StringLength] != 0; StringLength++)
            NOTHING;
        RtlCopyMemory(Inquiry->ProductId, Device->Model, StringLength);
        RtlCopyMemory(Inquiry->ProductRevisionLevel, "1.0 ", 4);
        Length = INQUIRYDATABUFFERSIZE;
    }

    Length = min(Length, AllocationLength);
    if (!NvmeCopyToDataBuffer(Srb, Buffer, Length))
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
        return;
    }
    NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
}

static
VOID
NvmeHandleReadCapacity(_In_ PNVME_DEVICE_EXTENSION Device,
                       _In_ PNVME_NAMESPACE Namespace,
                       _In_ PSCSI_REQUEST_BLOCK Srb,
                       _In_ BOOLEAN Extended)
{
    PCDB Cdb = (PCDB)Srb->Cdb;
    UCHAR Buffer[32];
    ULONGLONG LastBlock = Namespace->Blocks - 1;
    ULONG AllocationLength;
    ULONG Length;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    if (Extended)
    {
        Buffer[0] = (UCHAR)(LastBlock >> 56);
        Buffer[1] = (UCHAR)(LastBlock >> 48);
        Buffer[2] = (UCHAR)(LastBlock >> 40);
        Buffer[3] = (UCHAR)(LastBlock >> 32);
        Buffer[4] = (UCHAR)(LastBlock >> 24);
        Buffer[5] = (UCHAR)(LastBlock >> 16);
        Buffer[6] = (UCHAR)(LastBlock >> 8);
        Buffer[7] = (UCHAR)LastBlock;
        Buffer[8] = (UCHAR)(Namespace->BlockSize >> 24);
        Buffer[9] = (UCHAR)(Namespace->BlockSize >> 16);
        Buffer[10] = (UCHAR)(Namespace->BlockSize >> 8);
        Buffer[11] = (UCHAR)Namespace->BlockSize;
        if (Device->Oncs & NVME_ONCS_DSM)
            Buffer[14] = 0x80;  /* LBPME */
        AllocationLength = ((ULONG)Srb->Cdb[10] << 24) |
                           ((ULONG)Srb->Cdb[11] << 16) |
                           ((ULONG)Srb->Cdb[12] << 8) |
                           Srb->Cdb[13];
        Length = min(32, AllocationLength);
    }
    else
    {
        ULONG ReportedLast = LastBlock > MAXULONG ? MAXULONG : (ULONG)LastBlock;

        Buffer[0] = (UCHAR)(ReportedLast >> 24);
        Buffer[1] = (UCHAR)(ReportedLast >> 16);
        Buffer[2] = (UCHAR)(ReportedLast >> 8);
        Buffer[3] = (UCHAR)ReportedLast;
        Buffer[4] = (UCHAR)(Namespace->BlockSize >> 24);
        Buffer[5] = (UCHAR)(Namespace->BlockSize >> 16);
        Buffer[6] = (UCHAR)(Namespace->BlockSize >> 8);
        Buffer[7] = (UCHAR)Namespace->BlockSize;
        Length = 8;
    }

    if (!NvmeCopyToDataBuffer(Srb, Buffer, Length))
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
        return;
    }
    NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
}

static
VOID
NvmeHandleModeSense(_In_ PNVME_DEVICE_EXTENSION Device,
                    _In_ PNVME_NAMESPACE Namespace,
                    _In_ PSCSI_REQUEST_BLOCK Srb,
                    _In_ BOOLEAN Ten)
{
    UCHAR Buffer[36];
    ULONG HeaderLength = Ten ? 8 : 4;
    ULONG Length = HeaderLength;
    PCDB Cdb = (PCDB)Srb->Cdb;
    UCHAR PageCode = Ten ? Cdb->MODE_SENSE10.PageCode : Cdb->MODE_SENSE.PageCode;
    UCHAR PageControl = Ten ? Cdb->MODE_SENSE10.Pc : Cdb->MODE_SENSE.Pc;
    BOOLEAN DisableBlockDescriptors = Ten ? Cdb->MODE_SENSE10.Dbd : Cdb->MODE_SENSE.Dbd;
    ULONG AllocationLength = Ten ? ((ULONG)Srb->Cdb[7] << 8) | Srb->Cdb[8] : Srb->Cdb[4];

    RtlZeroMemory(Buffer, sizeof(Buffer));
    if (Srb->Cdb[3] != 0 || (PageCode != MODE_PAGE_CACHING && PageCode != MODE_SENSE_RETURN_ALL))
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
        return;
    }
    if (Ten)
        Buffer[3] = MODE_DSP_FUA_SUPPORTED;
    else
        Buffer[2] = MODE_DSP_FUA_SUPPORTED;

    if (!DisableBlockDescriptors)
    {
        PUCHAR Descriptor = &Buffer[HeaderLength];
        ULONG Blocks = Namespace->Blocks > 0xFFFFFF ? 0xFFFFFF : (ULONG)Namespace->Blocks;

        Descriptor[1] = (UCHAR)(Blocks >> 16);
        Descriptor[2] = (UCHAR)(Blocks >> 8);
        Descriptor[3] = (UCHAR)Blocks;
        Descriptor[5] = (UCHAR)(Namespace->BlockSize >> 16);
        Descriptor[6] = (UCHAR)(Namespace->BlockSize >> 8);
        Descriptor[7] = (UCHAR)Namespace->BlockSize;
        Length += 8;
        if (Ten)
            Buffer[7] = 8;
        else
            Buffer[3] = 8;
    }
    if (PageCode == MODE_PAGE_CACHING || PageCode == MODE_SENSE_RETURN_ALL)
    {
        PUCHAR Page = &Buffer[Length];

        Page[0] = MODE_PAGE_CACHING;
        Page[1] = 0x12;
        if (PageControl == 1)
            Page[2] = Device->VolatileWriteCache ? 0x04 : 0x00;
        else if (Device->WriteCacheEnabled)
            Page[2] = 0x04;
        Length += 0x14;
    }
    if (Ten)
    {
        Buffer[0] = (UCHAR)((Length - 2) >> 8);
        Buffer[1] = (UCHAR)(Length - 2);
    }
    else
    {
        Buffer[0] = (UCHAR)(Length - 1);
    }

    Length = min(Length, AllocationLength);
    if (!NvmeCopyToDataBuffer(Srb, Buffer, Length))
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
        return;
    }
    NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
}

static
VOID
NvmeHandleModeSelect(_In_ PNVME_DEVICE_EXTENSION Device,
                     _In_ PSCSI_REQUEST_BLOCK Srb,
                     _In_ BOOLEAN Ten)
{
    PNVME_SRB_EXTENSION SrbExtension = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    PUCHAR Data = (PUCHAR)Srb->DataBuffer;
    ULONG DataLength = Srb->DataTransferLength;
    ULONG HeaderLength = Ten ? 8 : 4;
    ULONG BlockDescriptorLength;
    ULONG Offset;

    if (Data == NULL || DataLength < HeaderLength)
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
        return;
    }

    BlockDescriptorLength = Ten ? (((ULONG)Data[6] << 8) | Data[7]) : Data[3];
    Offset = HeaderLength + BlockDescriptorLength;
    while (Offset + 2 <= DataLength)
    {
        UCHAR PageCode = Data[Offset] & 0x3F;
        ULONG PageLength = Data[Offset + 1];

        if (Offset + 2 + PageLength > DataLength)
            break;
        if (PageCode == MODE_PAGE_CACHING && PageLength >= 2)
        {
            BOOLEAN WantWce = (Data[Offset + 2] & 0x04) != 0;

            if (!Device->VolatileWriteCache || WantWce == Device->WriteCacheEnabled)
                break;
            if (SrbExtension == NULL)
            {
                NvmeCompleteSrb(Device, Srb, SRB_STATUS_ERROR);
                return;
            }
            else
            {
                NVME_COMMAND Command;
                NVME_LOCK Lock;
                ULONG Slot;

                RtlZeroMemory(&Command, sizeof(Command));
                NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_SET_FEATURES, 0);
                Command.CDW10 = NVME_FEATURE_VOLATILE_WC;
                Command.CDW11 = WantWce ? 1 : 0;
                SrbExtension->FeatureIsWce = TRUE;
                SrbExtension->FeatureValue = WantWce ? 1 : 0;
                NvmeAcquireLock(Device, 0, &Lock);
                Slot = NvmeAdminSubmitLocked(Device, &Command, NVME_SLOT_SRB_FEATURE, Srb);
                NvmeReleaseLock(Device, &Lock);
                if (Slot == MAXULONG)
                    NvmeCompleteSrb(Device, Srb, SRB_STATUS_BUSY);
                return;
            }
        }
        Offset += 2 + PageLength;
    }
    NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
}

static
VOID
NvmeHandleRequestSense(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR Buffer[18];

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Buffer[0] = 0x70;
    Buffer[7] = 10;
    NvmeCopyToDataBuffer(Srb, Buffer, min(sizeof(Buffer), Srb->Cdb[4]));
    NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
}

static
VOID
NvmeHandleReportLuns(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR Buffer[8 + 8 * NVME_MAX_NAMESPACES];
    ULONG Count = 0;
    ULONG Index;
    ULONG Length;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    for (Index = 0; Index < Device->NamespaceCount; Index++)
    {
        if (!Device->Namespaces[Index].Ready)
            continue;
        Buffer[8 + Count * 8 + 1] = (UCHAR)Index;
        Count++;
    }
    Buffer[3] = (UCHAR)(Count * 8);
    Length = 8 + Count * 8;
    NvmeCopyToDataBuffer(Srb, Buffer, Length);
    NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
}

static
VOID
NvmeHandleLogSense(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PNVME_SRB_EXTENSION SrbExtension = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    UCHAR PageCode = Srb->Cdb[2] & 0x3F;
    ULONG AllocationLength = ((ULONG)Srb->Cdb[7] << 8) | Srb->Cdb[8];
    NVME_COMMAND Command;
    NVME_LOCK Lock;
    ULONG Slot;

    if (Srb->Cdb[3] != 0)
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
        return;
    }

    if (PageCode == LOG_PAGE_CODE_SUPPORTED_LOG_PAGES)
    {
        UCHAR Buffer[7];

        RtlZeroMemory(Buffer, sizeof(Buffer));
        Buffer[3] = 3;
        Buffer[4] = LOG_PAGE_CODE_SUPPORTED_LOG_PAGES;
        Buffer[5] = LOG_PAGE_CODE_TEMPERATURE;
        Buffer[6] = 0x2F;
        NvmeCopyToDataBuffer(Srb, Buffer, min(sizeof(Buffer), AllocationLength));
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
        return;
    }
    if ((PageCode != LOG_PAGE_CODE_TEMPERATURE && PageCode != 0x2F) || SrbExtension == NULL)
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
        return;
    }

    /* Both pages are views of the SMART log; fetch a fresh copy. */
    SrbExtension->ScsiLogPage = PageCode;
    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_ADMIN_GET_LOG_PAGE, 0);
    Command.NSID = 0xFFFFFFFF;
    Command.PRP1 = NvmeSrbExtensionPhysical(Device, SrbExtension);
    Command.CDW10 = NVME_LOG_SMART | (((sizeof(NVME_SMART_LOG) / 4) - 1) << 16);

    NvmeAcquireLock(Device, 0, &Lock);
    Slot = NvmeAdminSubmitLocked(Device, &Command, NVME_SLOT_SRB_LOG, Srb);
    NvmeReleaseLock(Device, &Lock);
    if (Slot == MAXULONG)
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_BUSY);
}

VOID
NvmeCompleteLogSenseSrb(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb, _In_ USHORT NvmeStatus)
{
    PNVME_SRB_EXTENSION SrbExtension = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    PNVME_SMART_LOG Smart = (PNVME_SMART_LOG)SrbExtension->Dma.LogPage;
    ULONG AllocationLength = ((ULONG)Srb->Cdb[7] << 8) | Srb->Cdb[8];
    UCHAR Buffer[16];
    UCHAR Celsius;
    ULONG Length;

    if (NvmeStatus != 0)
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_HARDWARE_ERROR, 0x00);
        return;
    }

    Celsius = Smart->CompositeTemperature > 528
                  ? MAXUCHAR
                  : Smart->CompositeTemperature > 273
                        ? (UCHAR)(Smart->CompositeTemperature - 273)
                        : 0;
    RtlZeroMemory(Buffer, sizeof(Buffer));
    if (SrbExtension->ScsiLogPage == LOG_PAGE_CODE_TEMPERATURE)
    {
        Buffer[0] = LOG_PAGE_CODE_TEMPERATURE;
        Buffer[3] = 12;
        Buffer[5] = 0x00;
        Buffer[6] = 0x03;
        Buffer[7] = 2;
        Buffer[9] = Celsius;
        Buffer[11] = 0x01;
        Buffer[12] = 0x03;
        Buffer[13] = 2;
        Buffer[15] = Device->Wctemp > 273 ? (UCHAR)(Device->Wctemp - 273) : 0;
        Length = 16;
    }
    else
    {
        Buffer[0] = 0x2F;
        Buffer[3] = 8;
        Buffer[6] = 0x03;
        Buffer[7] = 4;
        if (Smart->CriticalWarning != 0)
        {
            Buffer[8] = 0x5D;   /* failure prediction threshold exceeded */
            Buffer[9] = 0x00;
        }
        Buffer[10] = Celsius;
        Length = 12;
    }

    if (!NvmeCopyToDataBuffer(Srb, Buffer, min(Length, AllocationLength)))
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
        return;
    }
    NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
}

VOID
NvmeCompleteFeatureSrb(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb, _In_ USHORT NvmeStatus)
{
    PNVME_SRB_EXTENSION SrbExtension = (PNVME_SRB_EXTENSION)Srb->SrbExtension;

    if (NvmeStatus != 0)
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_HARDWARE_ERROR, 0x00);
        return;
    }
    if (SrbExtension != NULL && SrbExtension->FeatureIsWce)
        Device->WriteCacheEnabled = SrbExtension->FeatureValue != 0;
    NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
}

static
BOOLEAN
NvmeDecodeLba(_In_ PSCSI_REQUEST_BLOCK Srb, _Out_ PULONGLONG Lba, _Out_ PULONG Blocks)
{
    PCDB Cdb = (PCDB)Srb->Cdb;

    switch (Srb->Cdb[0])
    {
        case SCSIOP_READ6:
        case SCSIOP_WRITE6:
            *Lba = ((ULONGLONG)(Cdb->CDB6READWRITE.LogicalBlockMsb1 & 0x1F) << 16) |
                   ((ULONGLONG)Cdb->CDB6READWRITE.LogicalBlockMsb0 << 8) |
                   Cdb->CDB6READWRITE.LogicalBlockLsb;
            *Blocks = Cdb->CDB6READWRITE.TransferBlocks ? Cdb->CDB6READWRITE.TransferBlocks : 256;
            return TRUE;

        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_VERIFY:
            *Lba = ((ULONGLONG)Cdb->CDB10.LogicalBlockByte0 << 24) |
                   ((ULONGLONG)Cdb->CDB10.LogicalBlockByte1 << 16) |
                   ((ULONGLONG)Cdb->CDB10.LogicalBlockByte2 << 8) |
                   Cdb->CDB10.LogicalBlockByte3;
            *Blocks = ((ULONG)Cdb->CDB10.TransferBlocksMsb << 8) |
                      Cdb->CDB10.TransferBlocksLsb;
            return TRUE;

        case SCSIOP_READ12:
        case SCSIOP_WRITE12:
            *Lba = ((ULONGLONG)Cdb->CDB12.LogicalBlock[0] << 24) |
                   ((ULONGLONG)Cdb->CDB12.LogicalBlock[1] << 16) |
                   ((ULONGLONG)Cdb->CDB12.LogicalBlock[2] << 8) |
                   Cdb->CDB12.LogicalBlock[3];
            *Blocks = ((ULONG)Cdb->CDB12.TransferLength[0] << 24) |
                      ((ULONG)Cdb->CDB12.TransferLength[1] << 16) |
                      ((ULONG)Cdb->CDB12.TransferLength[2] << 8) |
                      Cdb->CDB12.TransferLength[3];
            return TRUE;

        case SCSIOP_READ16:
        case SCSIOP_WRITE16:
        {
            ULONG Index;

            *Lba = 0;
            for (Index = 0; Index < 8; Index++)
                *Lba = (*Lba << 8) | Cdb->CDB16.LogicalBlock[Index];
            *Blocks = ((ULONG)Cdb->CDB16.TransferLength[0] << 24) |
                      ((ULONG)Cdb->CDB16.TransferLength[1] << 16) |
                      ((ULONG)Cdb->CDB16.TransferLength[2] << 8) |
                      Cdb->CDB16.TransferLength[3];
            return TRUE;
        }
    }
    return FALSE;
}

static
BOOLEAN
NvmeSubmitReadWrite(_In_ PNVME_DEVICE_EXTENSION Device,
                    _In_ PNVME_NAMESPACE Namespace,
                    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PNVME_SRB_EXTENSION SrbExtension = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    PSTOR_SCATTER_GATHER_LIST Sgl;
    NVME_COMMAND Command;
    PULONGLONG PrpList;
    ULONGLONG Lba;
    ULONG Blocks;
    ULONG PrpCount = 0;
    ULONG SglIndex;
    ULONG TotalLength = 0;
    UCHAR Opcode = Srb->Cdb[0];
    BOOLEAN Write = Opcode == SCSIOP_WRITE6 || Opcode == SCSIOP_WRITE ||
                    Opcode == SCSIOP_WRITE12 || Opcode == SCSIOP_WRITE16;
    BOOLEAN CanFua = Opcode != SCSIOP_READ6 && Opcode != SCSIOP_WRITE6;

    if (!NvmeDecodeLba(Srb, &Lba, &Blocks))
        return FALSE;
    if (Blocks == 0)
    {
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
        return TRUE;
    }
    if (Lba >= Namespace->Blocks || Blocks > Namespace->Blocks - Lba ||
        ((ULONGLONG)Blocks << Namespace->BlockShift) != Srb->DataTransferLength ||
        Srb->DataTransferLength > Device->MaximumTransferLength)
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x21);
        return TRUE;
    }
    if (SrbExtension == NULL)
    {
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_ERROR);
        return TRUE;
    }

    Sgl = StorPortGetScatterGatherList(Device, Srb);
    if (Sgl == NULL || Sgl->NumberOfElements == 0)
    {
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_ERROR);
        return TRUE;
    }

    /*
     * PRP entries are page-sized: the first may start unaligned, everything
     * after it must be page-aligned. Storport's list gives physically
     * contiguous runs, so each run is cut into pages here.
     */
    PrpList = SrbExtension->Dma.PrpList;
    RtlZeroMemory(&Command, sizeof(Command));
    RtlZeroMemory(PrpList, sizeof(SrbExtension->Dma.PrpList));
    for (SglIndex = 0; SglIndex < Sgl->NumberOfElements; SglIndex++)
    {
        ULONGLONG Address = (ULONGLONG)Sgl->List[SglIndex].PhysicalAddress.QuadPart;
        ULONG Remaining = Sgl->List[SglIndex].Length;

        while (Remaining != 0)
        {
            ULONG Chunk = PAGE_SIZE - (ULONG)(Address & (PAGE_SIZE - 1));

            if (Chunk > Remaining)
                Chunk = Remaining;
            if ((PrpCount == 0 && (Address & 0x3) != 0) ||
                (PrpCount != 0 && (Address & (PAGE_SIZE - 1)) != 0))
            {
                goto InvalidMapping;
            }
            if (PrpCount == 0)
            {
                Command.PRP1 = Address;
            }
            else
            {
                if (PrpCount - 1 >= NVME_MAX_PRP_ENTRIES)
                    goto InvalidMapping;
                PrpList[PrpCount - 1] = Address;
            }
            PrpCount++;
            TotalLength += Chunk;
            Address += Chunk;
            Remaining -= Chunk;
        }
    }
    if (TotalLength != Srb->DataTransferLength)
        goto InvalidMapping;
    if (PrpCount == 2)
        Command.PRP2 = PrpList[0];
    else if (PrpCount > 2)
        Command.PRP2 = NvmeSrbExtensionPhysical(Device, SrbExtension);

    NVME_COMMAND_SET_OPCODE(&Command, Write ? NVME_NVM_WRITE : NVME_NVM_READ, 0);
    Command.NSID = Namespace->Nsid;
    Command.CDW10 = (ULONG)Lba;
    Command.CDW11 = (ULONG)(Lba >> 32);
    Command.CDW12 = Blocks - 1;
    if (CanFua && (Srb->Cdb[1] & CDB_FORCE_MEDIA_ACCESS) != 0)
        Command.CDW12 |= 1UL << 30;

    return NvmeSubmitIoCommand(Device, NvmeSelectQueue(Device), Srb, &Command);

InvalidMapping:
    NvmeCompleteSrb(Device, Srb, SRB_STATUS_ERROR);
    return TRUE;
}

static
BOOLEAN
NvmeSubmitFlush(_In_ PNVME_DEVICE_EXTENSION Device,
                _In_ PNVME_NAMESPACE Namespace,
                _In_ PSCSI_REQUEST_BLOCK Srb)
{
    NVME_COMMAND Command;

    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_NVM_FLUSH, 0);
    Command.NSID = Namespace->Nsid;
    return NvmeSubmitIoCommand(Device, NvmeSelectQueue(Device), Srb, &Command);
}

static
VOID
NvmeHandleUnmap(_In_ PNVME_DEVICE_EXTENSION Device,
                _In_ PNVME_NAMESPACE Namespace,
                _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PNVME_SRB_EXTENSION SrbExtension = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    PUCHAR Data = (PUCHAR)Srb->DataBuffer;
    NVME_COMMAND Command;
    ULONG DataLength = Srb->DataTransferLength;
    ULONG DescriptorLength;
    ULONG Count = 0;
    ULONG Offset;

    if ((Device->Oncs & NVME_ONCS_DSM) == 0 || (Srb->Cdb[1] & 0x01) != 0)
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x20);
        return;
    }
    if (Data == NULL || DataLength < 8 || SrbExtension == NULL)
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x24);
        return;
    }

    DescriptorLength = ((ULONG)Data[2] << 8) | Data[3];
    if (DescriptorLength > DataLength - 8 || (DescriptorLength % 16) != 0)
    {
        NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x26);
        return;
    }

    for (Offset = 8; Offset + 16 <= 8 + DescriptorLength; Offset += 16)
    {
        ULONGLONG Lba = 0;
        ULONG Blocks;
        ULONG Index;

        for (Index = 0; Index < 8; Index++)
            Lba = (Lba << 8) | Data[Offset + Index];
        Blocks = ((ULONG)Data[Offset + 8] << 24) |
                 ((ULONG)Data[Offset + 9] << 16) |
                 ((ULONG)Data[Offset + 10] << 8) |
                 Data[Offset + 11];
        if (Blocks == 0)
            continue;
        if (Count >= NVME_MAX_DSM_RANGES ||
            Lba >= Namespace->Blocks || Blocks > Namespace->Blocks - Lba)
        {
            NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x21);
            return;
        }
        SrbExtension->Dma.DsmRanges[Count].ContextAttributes = 0;
        SrbExtension->Dma.DsmRanges[Count].LengthInBlocks = Blocks;
        SrbExtension->Dma.DsmRanges[Count].StartingLba = Lba;
        Count++;
    }
    if (Count == 0)
    {
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
        return;
    }

    RtlZeroMemory(&Command, sizeof(Command));
    NVME_COMMAND_SET_OPCODE(&Command, NVME_NVM_DATASET_MGMT, 0);
    Command.NSID = Namespace->Nsid;
    Command.PRP1 = NvmeSrbExtensionPhysical(Device, SrbExtension);
    Command.CDW10 = Count - 1;
    Command.CDW11 = NVME_DSM_ATTRIBUTE_DEALLOCATE;
    NvmeSubmitIoCommand(Device, NvmeSelectQueue(Device), Srb, &Command);
}

static
VOID
NvmeHandleStartStopUnit(_In_ PNVME_DEVICE_EXTENSION Device, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    UCHAR PowerCondition = Srb->Cdb[4] >> 4;
    BOOLEAN Start = (Srb->Cdb[4] & 0x01) != 0;

    switch (PowerCondition)
    {
        case 0x0:
            NvmeSubmitPowerStateSrb(Device, Srb, Start);
            break;
        case 0x1:
            NvmeSubmitPowerStateSrb(Device, Srb, TRUE);
            break;
        case 0x2:
        case 0x3:
            NvmeSubmitPowerStateSrb(Device, Srb, FALSE);
            break;
        default:
            NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
            break;
    }
}

BOOLEAN
NTAPI
NvmeHwStartIo(_In_ PVOID DeviceExtension, _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)DeviceExtension;
    PNVME_NAMESPACE Namespace;

    if (Srb->Function != SRB_FUNCTION_EXECUTE_SCSI)
    {
        if (Srb->Function == SRB_FUNCTION_FLUSH || Srb->Function == SRB_FUNCTION_SHUTDOWN)
        {
            Namespace = NvmeGetNamespace(Device, Srb);
            if (Namespace != NULL && Device->WriteCacheEnabled)
            {
                NvmeSubmitFlush(Device, Namespace, Srb);
                return TRUE;
            }
            NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
            return TRUE;
        }
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_INVALID_REQUEST);
        return TRUE;
    }

    if (Srb->Cdb[0] == SCSIOP_REPORT_LUNS)
    {
        if (Srb->PathId != 0 || Srb->TargetId != 0)
        {
            NvmeCompleteSrb(Device, Srb, SRB_STATUS_NO_DEVICE);
            return TRUE;
        }
        NvmeHandleReportLuns(Device, Srb);
        return TRUE;
    }

    Namespace = NvmeGetNamespace(Device, Srb);
    if (Namespace == NULL)
    {
        NvmeCompleteSrb(Device, Srb, SRB_STATUS_NO_DEVICE);
        return TRUE;
    }

    switch (Srb->Cdb[0])
    {
        case SCSIOP_TEST_UNIT_READY:
        case SCSIOP_MEDIUM_REMOVAL:
        case SCSIOP_RESERVE_UNIT:
        case SCSIOP_RELEASE_UNIT:
            NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
            return TRUE;

        case SCSIOP_START_STOP_UNIT:
            NvmeHandleStartStopUnit(Device, Srb);
            return TRUE;

        case SCSIOP_REQUEST_SENSE:
            NvmeHandleRequestSense(Device, Srb);
            return TRUE;

        case SCSIOP_INQUIRY:
            NvmeHandleInquiry(Device, Namespace, Srb);
            return TRUE;

        case SCSIOP_READ_CAPACITY:
            NvmeHandleReadCapacity(Device, Namespace, Srb, FALSE);
            return TRUE;

        case SCSIOP_SERVICE_ACTION_IN16:
            if ((Srb->Cdb[1] & 0x1F) == 0x10)
            {
                NvmeHandleReadCapacity(Device, Namespace, Srb, TRUE);
                return TRUE;
            }
            NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x20);
            return TRUE;

        case SCSIOP_MODE_SENSE:
            NvmeHandleModeSense(Device, Namespace, Srb, FALSE);
            return TRUE;

        case SCSIOP_MODE_SENSE10:
            NvmeHandleModeSense(Device, Namespace, Srb, TRUE);
            return TRUE;

        case SCSIOP_MODE_SELECT:
            NvmeHandleModeSelect(Device, Srb, FALSE);
            return TRUE;

        case SCSIOP_MODE_SELECT10:
            NvmeHandleModeSelect(Device, Srb, TRUE);
            return TRUE;

        case SCSIOP_LOG_SENSE:
            NvmeHandleLogSense(Device, Srb);
            return TRUE;

        case SCSIOP_VERIFY:
        case SCSIOP_VERIFY12:
        case SCSIOP_VERIFY16:
            NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
            return TRUE;

        case SCSIOP_SYNCHRONIZE_CACHE:
        case SCSIOP_SYNCHRONIZE_CACHE16:
            if (!Device->WriteCacheEnabled)
            {
                NvmeCompleteSrb(Device, Srb, SRB_STATUS_SUCCESS);
                return TRUE;
            }
            return NvmeSubmitFlush(Device, Namespace, Srb);

        case SCSIOP_UNMAP:
            NvmeHandleUnmap(Device, Namespace, Srb);
            return TRUE;

        case SCSIOP_READ6:
        case SCSIOP_WRITE6:
        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_READ12:
        case SCSIOP_WRITE12:
        case SCSIOP_READ16:
        case SCSIOP_WRITE16:
            if (!NvmeSubmitReadWrite(Device, Namespace, Srb))
                NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x20);
            return TRUE;

        default:
            NvmeSetSenseAndComplete(Device, Srb, SCSI_SENSE_ILLEGAL_REQUEST, 0x20);
            return TRUE;
    }
}
