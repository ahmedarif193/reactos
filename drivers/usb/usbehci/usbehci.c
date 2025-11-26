/*
 * PROJECT:     ReactOS USB EHCI Miniport Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     USBEHCI main driver functions
 * COPYRIGHT:   Copyright 2017-2018 Vadim Galyant <vgal@rambler.ru>
 */

#include "usbehci.h"

#define NDEBUG
#include <debug.h>

/* Enable EHCI trace channel for noisy chatty logs in DBG builds */
/* Intentionally NOT defining NDEBUG_EHCI_TRACE so DPRINT_EHCI is active */
#define NDEBUG_EHCI_TRACE
#include "dbg_ehci.h"

#if DBG
static VOID
EHCI_DumpSetupPacket(IN PUSB_DEFAULT_PIPE_SETUP_PACKET Setup)
{
    if (!Setup) return;
    DPRINT_EHCI("EHCI SETUP: bmReq=0x%02x bReq=0x%02x wVal=0x%04x wIdx=0x%04x wLen=%u\n",
            Setup->bmRequestType.B,
            Setup->bRequest,
            Setup->wValue.W,
            Setup->wIndex.W,
            Setup->wLength);
}
#endif

USBPORT_REGISTRATION_PACKET RegPacket;

/* Runtime trace control (DBG builds):
 *  bit0: general EHCI logs (DPRINT_EHCI)
 *  bit1: root hub logs (DPRINT_RH)
 *  bit2: poll tick logs
 */
#if DBG
/* Default: enable general + root hub traces; poll logs via registry */
ULONG g_EhciTraceMask = 0x3;
ULONG g_EhciPollLogDiv = 0x400; /* default: log every 1024 polls */
#endif

/* Forward declarations for local routines referenced before definition */
VOID
NTAPI
EHCI_EnableInterrupts(IN PVOID ehciExtension);

/* Forward decls for helpers used before definition */
VOID
NTAPI
EHCI_RemoveQhFromAsyncList(IN PEHCI_EXTENSION EhciExtension,
                           IN PEHCI_HCD_QH QH);

#if DBG
static VOID
EHCI_HexDump(IN PCSTR Tag, IN const VOID* Buf, IN ULONG Length)
{
    const UCHAR* p = (const UCHAR*)Buf;
    CHAR Line[64];
    const char *Hex = "0123456789ABCDEF";
    ULONG i, j, Chunk;

    if (!Buf || !Length) return;

    DbgPrint("%s (Length %u):\n", Tag, Length);

    for (i = 0; i < Length; i += 16)
    {
        PCHAR d = Line;
        Chunk = Length - i;
        if (Chunk > 16) Chunk = 16;

        for (j = 0; j < Chunk; j++)
        {
            UCHAR v = p[i + j];
            *d++ = Hex[(v >> 4) & 0x0F];
            *d++ = Hex[v & 0x0F];
            *d++ = ' ';
        }
        *d = '\0';

        DbgPrint("  +%04x: %s\n", i, Line);
    }
}
#endif

#if DBG
static
VOID
EHCI_DumpScatterGatherList(IN PCSTR Tag,
                           IN PUSBPORT_TRANSFER_PARAMETERS TransferParameters,
                           IN PUSBPORT_SCATTER_GATHER_LIST SgList)
{
    ULONG Index;

    if (!TransferParameters)
    {
        DPRINT_EHCI("EHCI_SG_DUMP: %s: TransferParameters NULL\n", Tag);
        return;
    }

    if (!TransferParameters->TransferBufferLength)
    {
        DPRINT_EHCI("EHCI_SG_DUMP: %s: TransferBufferLength=0\n", Tag);
        return;
    }

    if (!SgList)
    {
        DPRINT_EHCI("EHCI_SG_DUMP: %s: SgList NULL (Length=%lu)\n",
                Tag,
                TransferParameters->TransferBufferLength);
        return;
    }

    DPRINT_EHCI("EHCI_SG_DUMP: %s: Len=%lu Flags=0x%lx Elements=%lu CurrentVa=%p\n",
            Tag,
            TransferParameters->TransferBufferLength,
            TransferParameters->TransferFlags,
            SgList->SgElementCount,
            (PVOID)SgList->CurrentVa);

    for (Index = 0; Index < SgList->SgElementCount; Index++)
    {
        ULONGLONG PhysicalAddress = SgList->SgElement[Index].SgPhysicalAddress.QuadPart;

        DPRINT_EHCI("EHCI_SG_DUMP: %s: SG[%lu] PA=0x%I64x Len=%lu Offset=%lu\n",
                Tag,
                Index,
                PhysicalAddress,
                SgList->SgElement[Index].SgTransferLength,
                SgList->SgElement[Index].SgOffset);
    }
}
#endif

static const UCHAR ClassicPeriod[8] = {
    ENDPOINT_INTERRUPT_1ms - 1,
    ENDPOINT_INTERRUPT_2ms - 1,
    ENDPOINT_INTERRUPT_4ms - 1,
    ENDPOINT_INTERRUPT_8ms - 1,
    ENDPOINT_INTERRUPT_16ms - 1,
    ENDPOINT_INTERRUPT_32ms - 1,
    ENDPOINT_INTERRUPT_32ms - 1,
    ENDPOINT_INTERRUPT_32ms - 1
};

static const EHCI_PERIOD pTable[] = {
    { ENDPOINT_INTERRUPT_1ms, 0x00, 0xFF },
    { ENDPOINT_INTERRUPT_2ms, 0x00, 0x55 },
    { ENDPOINT_INTERRUPT_2ms, 0x00, 0xAA },
    { ENDPOINT_INTERRUPT_4ms, 0x00, 0x11 },
    { ENDPOINT_INTERRUPT_4ms, 0x00, 0x44 },
    { ENDPOINT_INTERRUPT_4ms, 0x00, 0x22 },
    { ENDPOINT_INTERRUPT_4ms, 0x00, 0x88 },
    { ENDPOINT_INTERRUPT_8ms, 0x00, 0x01 },
    { ENDPOINT_INTERRUPT_8ms, 0x00, 0x10 },
    { ENDPOINT_INTERRUPT_8ms, 0x00, 0x04 },
    { ENDPOINT_INTERRUPT_8ms, 0x00, 0x40 },
    { ENDPOINT_INTERRUPT_8ms, 0x00, 0x02 },
    { ENDPOINT_INTERRUPT_8ms, 0x00, 0x20 },
    { ENDPOINT_INTERRUPT_8ms, 0x00, 0x08 },
    { ENDPOINT_INTERRUPT_8ms, 0x00, 0x80 },
    { ENDPOINT_INTERRUPT_16ms, 0x01, 0x01 },
    { ENDPOINT_INTERRUPT_16ms, 0x02, 0x01 },
    { ENDPOINT_INTERRUPT_16ms, 0x01, 0x10 },
    { ENDPOINT_INTERRUPT_16ms, 0x02, 0x10 },
    { ENDPOINT_INTERRUPT_16ms, 0x01, 0x04 },
    { ENDPOINT_INTERRUPT_16ms, 0x02, 0x04 },
    { ENDPOINT_INTERRUPT_16ms, 0x01, 0x40 },
    { ENDPOINT_INTERRUPT_16ms, 0x02, 0x40 },
    { ENDPOINT_INTERRUPT_16ms, 0x01, 0x02 },
    { ENDPOINT_INTERRUPT_16ms, 0x02, 0x02 },
    { ENDPOINT_INTERRUPT_16ms, 0x01, 0x20 },
    { ENDPOINT_INTERRUPT_16ms, 0x02, 0x20 },
    { ENDPOINT_INTERRUPT_16ms, 0x01, 0x08 },
    { ENDPOINT_INTERRUPT_16ms, 0x02, 0x08 },
    { ENDPOINT_INTERRUPT_16ms, 0x01, 0x80 },
    { ENDPOINT_INTERRUPT_16ms, 0x02, 0x80 },
    { ENDPOINT_INTERRUPT_32ms, 0x03, 0x01 },
    { ENDPOINT_INTERRUPT_32ms, 0x05, 0x01 },
    { ENDPOINT_INTERRUPT_32ms, 0x04, 0x01 },
    { ENDPOINT_INTERRUPT_32ms, 0x06, 0x01 },
    { ENDPOINT_INTERRUPT_32ms, 0x03, 0x10 },
    { ENDPOINT_INTERRUPT_32ms, 0x05, 0x10 },
    { ENDPOINT_INTERRUPT_32ms, 0x04, 0x10 },
    { ENDPOINT_INTERRUPT_32ms, 0x06, 0x10 },
    { ENDPOINT_INTERRUPT_32ms, 0x03, 0x04 },
    { ENDPOINT_INTERRUPT_32ms, 0x05, 0x04 },
    { ENDPOINT_INTERRUPT_32ms, 0x04, 0x04 },
    { ENDPOINT_INTERRUPT_32ms, 0x06, 0x04 },
    { ENDPOINT_INTERRUPT_32ms, 0x03, 0x40 },
    { ENDPOINT_INTERRUPT_32ms, 0x05, 0x40 },
    { ENDPOINT_INTERRUPT_32ms, 0x04, 0x40 },
    { ENDPOINT_INTERRUPT_32ms, 0x06, 0x40 },
    { ENDPOINT_INTERRUPT_32ms, 0x03, 0x02 },
    { ENDPOINT_INTERRUPT_32ms, 0x05, 0x02 },
    { ENDPOINT_INTERRUPT_32ms, 0x04, 0x02 },
    { ENDPOINT_INTERRUPT_32ms, 0x06, 0x02 },
    { ENDPOINT_INTERRUPT_32ms, 0x03, 0x20 },
    { ENDPOINT_INTERRUPT_32ms, 0x05, 0x20 },
    { ENDPOINT_INTERRUPT_32ms, 0x04, 0x20 },
    { ENDPOINT_INTERRUPT_32ms, 0x06, 0x20 },
    { ENDPOINT_INTERRUPT_32ms, 0x03, 0x08 },
    { ENDPOINT_INTERRUPT_32ms, 0x05, 0x08 },
    { ENDPOINT_INTERRUPT_32ms, 0x04, 0x08 },
    { ENDPOINT_INTERRUPT_32ms, 0x06, 0x08 },
    { ENDPOINT_INTERRUPT_32ms, 0x03, 0x80 },
    { ENDPOINT_INTERRUPT_32ms, 0x05, 0x80 },
    { ENDPOINT_INTERRUPT_32ms, 0x04, 0x80 },
    { ENDPOINT_INTERRUPT_32ms, 0x06, 0x80 },
    { 0x00, 0x00, 0x00 }
};
C_ASSERT(RTL_NUMBER_OF(pTable) == INTERRUPT_ENDPOINTs + 1);

static const UCHAR Balance[] = {
    0, 16, 8, 24, 4, 20, 12, 28, 2, 18, 10, 26, 6, 22, 14, 30,
    1, 17, 9, 25, 5, 21, 13, 29, 3, 19, 11, 27, 7, 23, 15, 31
};
C_ASSERT(RTL_NUMBER_OF(Balance) == EHCI_FRAMES);

static const UCHAR LinkTable[] = {
    255, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,  9, 9,
    10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19,
    20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29,
    30, 30, 0
};
C_ASSERT(RTL_NUMBER_OF(LinkTable) == INTERRUPT_ENDPOINTs + 1);

#if DBG
static PCSTR
EHCI_DecodeConditionCode(UCHAR Status)
{
    switch (Status)
    {
        case EHCI_TOKEN_STATUS_HALTED:
            return "HALTED";
        case EHCI_TOKEN_STATUS_DATA_BUFFER_ERROR:
            return "DATA_BUFFER_ERROR";
        case EHCI_TOKEN_STATUS_BABBLE_DETECTED:
            return "BABBLE";
        case EHCI_TOKEN_STATUS_TRANSACTION_ERROR:
            return "XACT_ERROR";
        case EHCI_TOKEN_STATUS_MISSED_MICROFRAME:
            return "MISSED_MICROFRAME";
        default:
            return "STATUS_OK";
    }
}
#endif

PEHCI_HCD_TD
NTAPI
EHCI_AllocTd(IN PEHCI_EXTENSION EhciExtension,
             IN PEHCI_ENDPOINT EhciEndpoint)
{
    PEHCI_HCD_TD TD;
    ULONG ix;

    DPRINT_EHCI("EHCI_AllocTd: ... \n");

    if (EhciEndpoint->MaxTDs == 0)
    {
        RegPacket.UsbPortBugCheck(EhciExtension);
        return NULL;
    }

    TD = EhciEndpoint->FirstTD;

    for (ix = 1; TD->TdFlags & EHCI_HCD_TD_FLAG_ALLOCATED; ix++)
    {
        TD += 1;

        if (ix >= EhciEndpoint->MaxTDs)
        {
            RegPacket.UsbPortBugCheck(EhciExtension);
            return NULL;
        }
    }

    TD->TdFlags |= EHCI_HCD_TD_FLAG_ALLOCATED;

    EhciEndpoint->RemainTDs--;

    return TD;
}

PEHCI_HCD_QH
NTAPI
EHCI_InitializeQH(IN PEHCI_EXTENSION EhciExtension,
                  IN PEHCI_ENDPOINT EhciEndpoint,
                  IN PEHCI_HCD_QH QH,
                  IN ULONG QhPA)
{
    PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties;
    ULONG DeviceSpeed;

    DPRINT_EHCI("EHCI_InitializeQH: EhciEndpoint=%p QH=%p QhPA=%p\n",
                EhciEndpoint,
                QH,
                QhPA);

    EndpointProperties = &EhciEndpoint->EndpointProperties;

    RtlZeroMemory(QH, sizeof(EHCI_HCD_QH));

    ASSERT((QhPA & ~LINK_POINTER_MASK) == 0);

    QH->sqh.PhysicalAddress = QhPA;

    QH->sqh.HwQH.EndpointParams.DeviceAddress = EndpointProperties->DeviceAddress;
    QH->sqh.HwQH.EndpointParams.EndpointNumber = EndpointProperties->EndpointAddress;

    DeviceSpeed = EndpointProperties->DeviceSpeed;

    switch (DeviceSpeed)
    {
        case UsbLowSpeed:
            QH->sqh.HwQH.EndpointParams.EndpointSpeed = EHCI_QH_EP_LOW_SPEED;
            break;

        case UsbFullSpeed:
            QH->sqh.HwQH.EndpointParams.EndpointSpeed = EHCI_QH_EP_FULL_SPEED;
            break;

        case UsbHighSpeed:
            QH->sqh.HwQH.EndpointParams.EndpointSpeed = EHCI_QH_EP_HIGH_SPEED;
            break;

        default:
            DPRINT_EHCI("EHCI_InitializeQH: Unknown DeviceSpeed=0x%x\n", DeviceSpeed);
            ASSERT(FALSE);
            break;
    }

    QH->sqh.HwQH.EndpointParams.MaximumPacketLength = EndpointProperties->MaxPacketSize;
    QH->sqh.HwQH.EndpointCaps.PipeMultiplier = 1;

    if (DeviceSpeed == UsbHighSpeed)
    {
        QH->sqh.HwQH.EndpointCaps.HubAddr = 0;
        QH->sqh.HwQH.EndpointCaps.PortNumber = 0;
    }
    else
    {
        QH->sqh.HwQH.EndpointCaps.HubAddr = EndpointProperties->HubAddr;
        QH->sqh.HwQH.EndpointCaps.PortNumber = EndpointProperties->PortNumber;

        if (EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_CONTROL)
            QH->sqh.HwQH.EndpointParams.ControlEndpointFlag = 1;
    }

    QH->sqh.HwQH.NextTD = TERMINATE_POINTER;
    QH->sqh.HwQH.AlternateNextTD = TERMINATE_POINTER;

    QH->sqh.HwQH.Token.Status &= (UCHAR)~(EHCI_TOKEN_STATUS_ACTIVE |
                                          EHCI_TOKEN_STATUS_HALTED);

    DPRINT_EHCI("EHCI_InitializeQH: EP=%u DevAddr=%u Speed=%u MPS=%u CEF=%u Hub=%u Port=%u\n",
                EndpointProperties->EndpointAddress,
                EndpointProperties->DeviceAddress,
                QH->sqh.HwQH.EndpointParams.EndpointSpeed,
                QH->sqh.HwQH.EndpointParams.MaximumPacketLength,
                QH->sqh.HwQH.EndpointParams.ControlEndpointFlag,
                QH->sqh.HwQH.EndpointCaps.HubAddr,
                QH->sqh.HwQH.EndpointCaps.PortNumber);

    return QH;
}

MPSTATUS
NTAPI
EHCI_OpenBulkOrControlEndpoint(IN PEHCI_EXTENSION EhciExtension,
                               IN PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                               IN PEHCI_ENDPOINT EhciEndpoint,
                               IN BOOLEAN IsControl)
{
    PEHCI_HCD_QH QH;
    ULONG QhPA;
    PEHCI_HCD_TD TdVA;
    ULONG TdPA;
    PEHCI_HCD_TD TD;
    ULONG TdCount;
    ULONG ix;

    DPRINT_EHCI("EHCI_OpenBulkOrControlEndpoint: EhciEndpoint=%p IsControl=%u\n",
           EhciEndpoint,
           IsControl);

    InitializeListHead(&EhciEndpoint->ListTDs);

    EhciEndpoint->DmaBufferVA = (PVOID)EndpointProperties->BufferVA;
    EhciEndpoint->DmaBufferPA = EndpointProperties->BufferPA;

    RtlZeroMemory(EhciEndpoint->DmaBufferVA, sizeof(EHCI_HCD_TD));

    QH = (PEHCI_HCD_QH)EhciEndpoint->DmaBufferVA + 1;
    QhPA = EhciEndpoint->DmaBufferPA + sizeof(EHCI_HCD_TD);

    EhciEndpoint->FirstTD = (PEHCI_HCD_TD)(QH + 1);

    TdCount = (EndpointProperties->BufferLength -
               (sizeof(EHCI_HCD_TD) + sizeof(EHCI_HCD_QH))) /
               sizeof(EHCI_HCD_TD);

    if (EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_CONTROL)
        EhciEndpoint->EndpointStatus |= USBPORT_ENDPOINT_CONTROL;

    EhciEndpoint->MaxTDs = TdCount;
    EhciEndpoint->RemainTDs = TdCount;

    TdVA = EhciEndpoint->FirstTD;
    TdPA = QhPA + sizeof(EHCI_HCD_QH);

    DPRINT_EHCI("EHCI_OpenBulkOrControlEndpoint: BufferLen=%lu TDs=%lu QH_VA=%p TD0_VA=%p\n",
            EndpointProperties->BufferLength,
            TdCount,
            QH,
            TdVA);

    for (ix = 0; ix < TdCount; ix++)
    {
        DPRINT_EHCI("EHCI_OpenBulkOrControlEndpoint: TdVA - %p, TdPA - %p\n",
                    TdVA,
                    TdPA);

        RtlZeroMemory(TdVA, sizeof(EHCI_HCD_TD));

        ASSERT((TdPA & ~LINK_POINTER_MASK) == 0);

        TdVA->PhysicalAddress = TdPA;
        TdVA->EhciEndpoint = EhciEndpoint;
        TdVA->EhciTransfer = NULL;

        TdPA += sizeof(EHCI_HCD_TD);
        TdVA += 1;
    }

    EhciEndpoint->QH = EHCI_InitializeQH(EhciExtension,
                                         EhciEndpoint,
                                         QH,
                                         QhPA);

    if (IsControl)
    {
        QH->sqh.HwQH.EndpointParams.DataToggleControl = 1;
        EhciEndpoint->HcdHeadP = NULL;
    }
    else
    {
        QH->sqh.HwQH.EndpointParams.DataToggleControl = 0;
    }

    TD = EHCI_AllocTd(EhciExtension, EhciEndpoint);

    if (!TD)
        return MP_STATUS_NO_RESOURCES;

    TD->TdFlags |= EHCI_HCD_TD_FLAG_DUMMY;
    TD->HwTD.Token.Status &= (UCHAR)~EHCI_TOKEN_STATUS_ACTIVE;

    TD->HwTD.NextTD = TERMINATE_POINTER;
    TD->HwTD.AlternateNextTD = TERMINATE_POINTER;

    TD->NextHcdTD = NULL;
    TD->AltNextHcdTD = NULL;

    EhciEndpoint->HcdTailP = TD;
    EhciEndpoint->HcdHeadP = TD;

    QH->sqh.HwQH.CurrentTD = TD->PhysicalAddress;
    QH->sqh.HwQH.NextTD = TERMINATE_POINTER;
    QH->sqh.HwQH.AlternateNextTD = TERMINATE_POINTER;

    QH->sqh.HwQH.Token.Status &= (UCHAR)~EHCI_TOKEN_STATUS_ACTIVE;
    QH->sqh.HwQH.Token.TransferBytes = 0;

    EhciEndpoint->NextDataToggle = 0;

    DPRINT_EHCI("EHCI_OpenBulkOrControlEndpoint: completed EP=%p TDs=%lu\n",
            EhciEndpoint,
            TdCount);
    return MP_STATUS_SUCCESS;
}

MPSTATUS
NTAPI
EHCI_OpenInterruptEndpoint(IN PEHCI_EXTENSION EhciExtension,
                           IN PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                           IN PEHCI_ENDPOINT EhciEndpoint)
{
    PEHCI_HCD_QH QH;
    ULONG QhPA;
    PEHCI_HCD_TD FirstTD;
    ULONG FirstTdPA;
    PEHCI_HCD_TD TD;
    PEHCI_HCD_TD DummyTD;
    ULONG TdCount;
    ULONG ix;
    const EHCI_PERIOD * PeriodTable = NULL;
    ULONG ScheduleOffset;
    ULONG Idx = 0;
    UCHAR Period;

    DPRINT("EHCI_OpenInterruptEndpoint: EhciExtension - %p, EndpointProperties - %p, EhciEndpoint - %p\n",
           EhciExtension,
           EndpointProperties,
           EhciEndpoint);

    Period = EndpointProperties->Period;
    ScheduleOffset = EndpointProperties->ScheduleOffset;

    ASSERT(Period < (INTERRUPT_ENDPOINTs + 1));

    while (!(Period & 1))
    {
        Idx++;
        Period >>= 1;
    }

    ASSERT(Idx < 8);

    InitializeListHead(&EhciEndpoint->ListTDs);

    if (EhciEndpoint->EndpointProperties.DeviceSpeed == UsbHighSpeed)
    {
        PeriodTable = &pTable[ClassicPeriod[Idx] + ScheduleOffset];
        EhciEndpoint->PeriodTable = PeriodTable;

        DPRINT("EHCI_OpenInterruptEndpoint: EhciEndpoint - %p, ScheduleMask - %X, Index - %X\n",
               EhciEndpoint,
               PeriodTable->ScheduleMask,
               ClassicPeriod[Idx]);

        EhciEndpoint->StaticQH = EhciExtension->PeriodicHead[PeriodTable->PeriodIdx];
    }
    else
    {
        EhciEndpoint->PeriodTable = NULL;

        DPRINT("EHCI_OpenInterruptEndpoint: EhciEndpoint - %p, Index - %X\n",
               EhciEndpoint,
               ClassicPeriod[Idx]);

        EhciEndpoint->StaticQH = EhciExtension->PeriodicHead[ClassicPeriod[Idx] +
                                                             ScheduleOffset];
    }

    EhciEndpoint->DmaBufferVA = (PVOID)EndpointProperties->BufferVA;
    EhciEndpoint->DmaBufferPA = EndpointProperties->BufferPA;

    RtlZeroMemory((PVOID)EndpointProperties->BufferVA, sizeof(EHCI_HCD_TD));

    QH = (PEHCI_HCD_QH)(EndpointProperties->BufferVA + sizeof(EHCI_HCD_TD));
    QhPA = EndpointProperties->BufferPA + sizeof(EHCI_HCD_TD);

    FirstTD = (PEHCI_HCD_TD)(EndpointProperties->BufferVA +
                             sizeof(EHCI_HCD_TD) +
                             sizeof(EHCI_HCD_QH));

    FirstTdPA = EndpointProperties->BufferPA +
                sizeof(EHCI_HCD_TD) +
                sizeof(EHCI_HCD_QH);

    TdCount = (EndpointProperties->BufferLength -
               (sizeof(EHCI_HCD_TD) + sizeof(EHCI_HCD_QH))) /
               sizeof(EHCI_HCD_TD);

    ASSERT(TdCount >= EHCI_MAX_INTERRUPT_TD_COUNT + 1);

    EhciEndpoint->FirstTD = FirstTD;
    EhciEndpoint->MaxTDs = TdCount;

    for (ix = 0; ix < TdCount; ix++)
    {
        TD = EhciEndpoint->FirstTD + ix;

        RtlZeroMemory(TD, sizeof(EHCI_HCD_TD));

        ASSERT((FirstTdPA & ~LINK_POINTER_MASK) == 0);

        TD->PhysicalAddress = FirstTdPA;
        TD->EhciEndpoint = EhciEndpoint;
        TD->EhciTransfer = NULL;

        FirstTdPA += sizeof(EHCI_HCD_TD);
    }

    EhciEndpoint->RemainTDs = TdCount;

    EhciEndpoint->QH = EHCI_InitializeQH(EhciExtension,
                                         EhciEndpoint,
                                         QH,
                                         QhPA);

    if (EhciEndpoint->EndpointProperties.DeviceSpeed == UsbHighSpeed)
    {
        QH->sqh.HwQH.EndpointCaps.InterruptMask = PeriodTable->ScheduleMask;
    }
    else
    {
        QH->sqh.HwQH.EndpointCaps.InterruptMask =
        EndpointProperties->InterruptScheduleMask;

        QH->sqh.HwQH.EndpointCaps.SplitCompletionMask =
        EndpointProperties->SplitCompletionMask;
    }

    DummyTD = EHCI_AllocTd(EhciExtension, EhciEndpoint);

    DummyTD->TdFlags |= EHCI_HCD_TD_FLAG_DUMMY;
    DummyTD->NextHcdTD = NULL;
    DummyTD->AltNextHcdTD = NULL;

    DummyTD->HwTD.Token.Status &= ~EHCI_TOKEN_STATUS_ACTIVE;

    DummyTD->HwTD.NextTD = TERMINATE_POINTER;
    DummyTD->HwTD.AlternateNextTD = TERMINATE_POINTER;

    EhciEndpoint->HcdTailP = DummyTD;
    EhciEndpoint->HcdHeadP = DummyTD;

    QH->sqh.HwQH.CurrentTD = DummyTD->PhysicalAddress;
    QH->sqh.HwQH.NextTD = TERMINATE_POINTER;
    QH->sqh.HwQH.AlternateNextTD = TERMINATE_POINTER;

    QH->sqh.HwQH.Token.Status &= ~EHCI_TOKEN_STATUS_ACTIVE;
    QH->sqh.HwQH.Token.TransferBytes = 0;

    EhciEndpoint->NextDataToggle = 0;

    return MP_STATUS_SUCCESS;
}

MPSTATUS
NTAPI
EHCI_OpenHsIsoEndpoint(IN PEHCI_EXTENSION EhciExtension,
                       IN PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                       IN PEHCI_ENDPOINT EhciEndpoint)
{
    DPRINT_EHCI("EHCI_OpenHsIsoEndpoint: UNIMPLEMENTED. FIXME\n");
    return MP_STATUS_NOT_SUPPORTED;
}

MPSTATUS
NTAPI
EHCI_OpenIsoEndpoint(IN PEHCI_EXTENSION EhciExtension,
                     IN PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                     IN PEHCI_ENDPOINT EhciEndpoint)
{
    DPRINT_EHCI("EHCI_OpenIsoEndpoint: UNIMPLEMENTED. FIXME\n");
    return MP_STATUS_NOT_SUPPORTED;
}

MPSTATUS
NTAPI
EHCI_OpenEndpoint(IN PVOID ehciExtension,
                  IN PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                  IN PVOID ehciEndpoint)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_ENDPOINT EhciEndpoint = ehciEndpoint;
    ULONG TransferType;
    MPSTATUS MPStatus;

    DPRINT("EHCI_OpenEndpoint: ... \n");

    RtlCopyMemory(&EhciEndpoint->EndpointProperties,
                  EndpointProperties,
                  sizeof(EhciEndpoint->EndpointProperties));

    TransferType = EndpointProperties->TransferType;

    switch (TransferType)
    {
        case USBPORT_TRANSFER_TYPE_ISOCHRONOUS:
            if (EndpointProperties->DeviceSpeed == UsbHighSpeed)
            {
                MPStatus = EHCI_OpenHsIsoEndpoint(EhciExtension,
                                                  EndpointProperties,
                                                  EhciEndpoint);
            }
            else
            {
                MPStatus = EHCI_OpenIsoEndpoint(EhciExtension,
                                                EndpointProperties,
                                                EhciEndpoint);
            }

            break;

        case USBPORT_TRANSFER_TYPE_CONTROL:
            MPStatus = EHCI_OpenBulkOrControlEndpoint(EhciExtension,
                                                      EndpointProperties,
                                                      EhciEndpoint,
                                                      TRUE);
            break;

        case USBPORT_TRANSFER_TYPE_BULK:
            MPStatus = EHCI_OpenBulkOrControlEndpoint(EhciExtension,
                                                      EndpointProperties,
                                                      EhciEndpoint,
                                                      FALSE);
            break;

        case USBPORT_TRANSFER_TYPE_INTERRUPT:
            MPStatus = EHCI_OpenInterruptEndpoint(EhciExtension,
                                                  EndpointProperties,
                                                  EhciEndpoint);
            break;

        default:
            MPStatus = MP_STATUS_NOT_SUPPORTED;
            break;
    }

    return MPStatus;
}

MPSTATUS
NTAPI
EHCI_ReopenEndpoint(IN PVOID ehciExtension,
                    IN PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                    IN PVOID ehciEndpoint)
{
    PEHCI_ENDPOINT EhciEndpoint;
    ULONG TransferType;
    PEHCI_HCD_QH QH;
    MPSTATUS MPStatus;

    EhciEndpoint = ehciEndpoint;

    TransferType = EndpointProperties->TransferType;

    DPRINT("EHCI_ReopenEndpoint: EhciEndpoint - %p, TransferType - %x\n",
           EhciEndpoint,
           TransferType);

    switch (TransferType)
    {
        case USBPORT_TRANSFER_TYPE_ISOCHRONOUS:
            if (EndpointProperties->DeviceSpeed == UsbHighSpeed)
            {
                DPRINT_EHCI("EHCI_ReopenEndpoint: HS Iso. UNIMPLEMENTED. FIXME\n");
                MPStatus = MP_STATUS_NOT_SUPPORTED;
            }
            else
            {
                DPRINT_EHCI("EHCI_ReopenEndpoint: Iso. UNIMPLEMENTED. FIXME\n");
                MPStatus = MP_STATUS_NOT_SUPPORTED;
            }

            break;

        case USBPORT_TRANSFER_TYPE_CONTROL:
        case USBPORT_TRANSFER_TYPE_BULK:
        case USBPORT_TRANSFER_TYPE_INTERRUPT:
            RtlCopyMemory(&EhciEndpoint->EndpointProperties,
                          EndpointProperties,
                          sizeof(EhciEndpoint->EndpointProperties));

            QH = EhciEndpoint->QH;

            QH->sqh.HwQH.EndpointParams.DeviceAddress = EndpointProperties->DeviceAddress;
            QH->sqh.HwQH.EndpointParams.MaximumPacketLength = EndpointProperties->MaxPacketSize;

            QH->sqh.HwQH.EndpointCaps.HubAddr = EndpointProperties->HubAddr;

            MPStatus = MP_STATUS_SUCCESS;
            break;

        default:
            DPRINT_EHCI("EHCI_ReopenEndpoint: Unknown TransferType\n");
            MPStatus = MP_STATUS_SUCCESS;
            break;
    }

    return MPStatus;
}

VOID
NTAPI
EHCI_QueryEndpointRequirements(IN PVOID ehciExtension,
                               IN PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                               IN PUSBPORT_ENDPOINT_REQUIREMENTS EndpointRequirements)
{
    ULONG TransferType;

    DPRINT_EHCI("EHCI_QueryEndpointRequirements: DevAddr=%u EpAddr=0x%02x Type=%u Speed=%u MPS=%u\n",
            EndpointProperties->DeviceAddress,
            EndpointProperties->EndpointAddress,
            EndpointProperties->TransferType,
            EndpointProperties->DeviceSpeed,
            EndpointProperties->MaxPacketSize);

    TransferType = EndpointProperties->TransferType;

    switch (TransferType)
    {
        case USBPORT_TRANSFER_TYPE_ISOCHRONOUS:
            DPRINT_EHCI("EHCI_QueryEndpointRequirements: IsoTransfer\n");

            if (EndpointProperties->DeviceSpeed == UsbHighSpeed)
            {
                EndpointRequirements->HeaderBufferSize = EHCI_MAX_HS_ISO_HEADER_BUFFER_SIZE;
                EndpointRequirements->MaxTransferSize = EHCI_MAX_HS_ISO_TRANSFER_SIZE;
            }
            else
            {
                EndpointRequirements->HeaderBufferSize = EHCI_MAX_FS_ISO_HEADER_BUFFER_SIZE;
                EndpointRequirements->MaxTransferSize = EHCI_MAX_FS_ISO_TRANSFER_SIZE;
            }
            break;

        case USBPORT_TRANSFER_TYPE_CONTROL:
            DPRINT_EHCI("EHCI_QueryEndpointRequirements: ControlTransfer\n");
            EndpointRequirements->HeaderBufferSize = sizeof(EHCI_HCD_TD) +
                                                     sizeof(EHCI_HCD_QH) +
                                                     EHCI_MAX_CONTROL_TD_COUNT * sizeof(EHCI_HCD_TD);

            EndpointRequirements->MaxTransferSize = EHCI_MAX_CONTROL_TRANSFER_SIZE;
            break;

        case USBPORT_TRANSFER_TYPE_BULK:
            DPRINT_EHCI("EHCI_QueryEndpointRequirements: BulkTransfer\n");
            EndpointRequirements->HeaderBufferSize = sizeof(EHCI_HCD_TD) +
                                                     sizeof(EHCI_HCD_QH) +
                                                     EHCI_MAX_BULK_TD_COUNT * sizeof(EHCI_HCD_TD);

            EndpointRequirements->MaxTransferSize = EHCI_MAX_BULK_TRANSFER_SIZE;
            break;

        case USBPORT_TRANSFER_TYPE_INTERRUPT:
            DPRINT_EHCI("EHCI_QueryEndpointRequirements: InterruptTransfer\n");
            EndpointRequirements->HeaderBufferSize = sizeof(EHCI_HCD_TD) +
                                                     sizeof(EHCI_HCD_QH) +
                                                     EHCI_MAX_INTERRUPT_TD_COUNT * sizeof(EHCI_HCD_TD);

            EndpointRequirements->MaxTransferSize = EHCI_MAX_INTERRUPT_TRANSFER_SIZE;
            break;

        default:
            DPRINT_EHCI("EHCI_QueryEndpointRequirements: Unknown TransferType=0x%x\n",
                    TransferType);
            DbgBreakPoint();
            break;
    }

    DPRINT_EHCI("EHCI_QueryEndpointRequirements: HeaderBufferSize=%lu MaxTransferSize=%lu\n",
            EndpointRequirements->HeaderBufferSize,
            EndpointRequirements->MaxTransferSize);
}

VOID
NTAPI
EHCI_DisablePeriodicList(IN PEHCI_EXTENSION EhciExtension)
{
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_COMMAND Command;

    DPRINT("EHCI_DisablePeriodicList: ... \n");

    if (EhciExtension->Flags & EHCI_FLAGS_IDLE_SUPPORT)
    {
        OperationalRegs = EhciExtension->OperationalRegs;

        Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
        Command.PeriodicEnable = 0;
        WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);
    }
}

VOID
NTAPI
EHCI_CloseEndpoint(IN PVOID ehciExtension,
                   IN PVOID ehciEndpoint,
                   IN BOOLEAN DisablePeriodic)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_ENDPOINT EhciEndpoint = ehciEndpoint;
    ULONG TransferType;

    DPRINT_EHCI("EHCI_CloseEndpoint: EhciEndpoint - %p, DisablePeriodic - %X\n",
            EhciEndpoint,
            DisablePeriodic);

    if (DisablePeriodic)
    {
        TransferType = EhciEndpoint->EndpointProperties.TransferType;

        if (TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS ||
            TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
        {
            EHCI_DisablePeriodicList(EhciExtension);
        }
    }
}

PEHCI_STATIC_QH
NTAPI
EHCI_GetQhForFrame(IN PEHCI_EXTENSION EhciExtension,
                   IN ULONG FrameIdx)
{
    //DPRINT_EHCI("EHCI_GetQhForFrame: FrameIdx - %x, Balance[FrameIdx] - %x\n",
    //            FrameIdx,
    //            Balance[FrameIdx & 0x1F]);

    return EhciExtension->PeriodicHead[Balance[FrameIdx & (EHCI_FRAMES - 1)]];
}

PEHCI_HCD_QH
NTAPI
EHCI_GetDummyQhForFrame(IN PEHCI_EXTENSION EhciExtension,
                        IN ULONG Idx)
{
    return (PEHCI_HCD_QH)((ULONG_PTR)EhciExtension->IsoDummyQHListVA +
                          Idx * sizeof(EHCI_HCD_QH));
}

VOID
NTAPI
EHCI_AlignHwStructure(IN PEHCI_EXTENSION EhciExtension,
                      IN PULONG PhysicalAddress,
                      IN PULONG_PTR VirtualAddress,
                      IN ULONG Alignment)
{
    ULONG PAddress;
    ULONG NewPAddress;
    ULONG_PTR VAddress;

    //DPRINT_EHCI("EHCI_AlignHwStructure: *PhysicalAddress - %X, *VirtualAddress - %X, Alignment - %x\n",
    //             *PhysicalAddress,
    //             *VirtualAddress,
    //             Alignment);

    PAddress = *PhysicalAddress;
    VAddress = *VirtualAddress;

    NewPAddress = (ULONG)(ULONG_PTR)PAGE_ALIGN(*PhysicalAddress + Alignment - 1);

    if (NewPAddress != (ULONG)(ULONG_PTR)PAGE_ALIGN(*PhysicalAddress))
    {
        VAddress += NewPAddress - PAddress;
        PAddress = NewPAddress;

        DPRINT("EHCI_AlignHwStructure: VAddress - %X, PAddress - %X\n",
               VAddress,
               PAddress);
    }

    *VirtualAddress = VAddress;
    *PhysicalAddress = PAddress;
}

VOID
NTAPI
EHCI_AddDummyQHs(IN PEHCI_EXTENSION EhciExtension)
{
    PEHCI_HC_RESOURCES HcResourcesVA;
    PEHCI_HCD_QH DummyQH;
    ULONG DummyQhPA;
    EHCI_QH_EP_PARAMS EndpointParams;
    EHCI_LINK_POINTER PAddress;
    ULONG Frame;

    DPRINT("EHCI_AddDummyQueueHeads: EhciExtension - %p\n", EhciExtension);

    HcResourcesVA = EhciExtension->HcResourcesVA;

    DummyQH = EhciExtension->IsoDummyQHListVA;
    DummyQhPA = EhciExtension->IsoDummyQHListPA;

    for (Frame = 0; Frame < EHCI_FRAME_LIST_MAX_ENTRIES; Frame++)
    {
        RtlZeroMemory(DummyQH, sizeof(EHCI_HCD_QH));

        PAddress.AsULONG = HcResourcesVA->PeriodicFrameList[Frame];

        DummyQH->sqh.HwQH.HorizontalLink.AsULONG = PAddress.AsULONG;
        DummyQH->sqh.HwQH.CurrentTD = 0;
        DummyQH->sqh.HwQH.NextTD = TERMINATE_POINTER;
        DummyQH->sqh.HwQH.AlternateNextTD = TERMINATE_POINTER;

        EndpointParams = DummyQH->sqh.HwQH.EndpointParams;
        EndpointParams.DeviceAddress = 0;
        EndpointParams.EndpointSpeed = 0;
        EndpointParams.MaximumPacketLength = EHCI_DUMMYQH_MAX_PACKET_LENGTH;
        DummyQH->sqh.HwQH.EndpointParams = EndpointParams;

        DummyQH->sqh.HwQH.EndpointCaps.AsULONG = 0;
        DummyQH->sqh.HwQH.EndpointCaps.InterruptMask = 0;
        DummyQH->sqh.HwQH.EndpointCaps.SplitCompletionMask = 0;
        DummyQH->sqh.HwQH.EndpointCaps.PipeMultiplier = 1;

        DummyQH->sqh.HwQH.Token.Status &= (UCHAR)~EHCI_TOKEN_STATUS_ACTIVE;

        DummyQH->sqh.PhysicalAddress = DummyQhPA;
        DummyQH->sqh.StaticQH = EHCI_GetQhForFrame(EhciExtension, Frame);

        PAddress.AsULONG = DummyQhPA;
        PAddress.Reserved = 0;
        PAddress.Type = EHCI_LINK_TYPE_QH;

        HcResourcesVA->PeriodicFrameList[Frame] = PAddress.AsULONG;

        DummyQH++;
        DummyQhPA += sizeof(EHCI_HCD_QH);
    }
}

VOID
NTAPI
EHCI_InitializeInterruptSchedule(IN PEHCI_EXTENSION EhciExtension)
{
    PEHCI_STATIC_QH StaticQH;
    ULONG ix;

    DPRINT("EHCI_InitializeInterruptSchedule: ... \n");

    for (ix = 0; ix < INTERRUPT_ENDPOINTs; ix++)
    {
        StaticQH = EhciExtension->PeriodicHead[ix];

        StaticQH->HwQH.EndpointParams.HeadReclamationListFlag = 0;
        StaticQH->HwQH.NextTD |= TERMINATE_POINTER;
        StaticQH->HwQH.Token.Status |= (UCHAR)EHCI_TOKEN_STATUS_HALTED;
    }

    for (ix = 1; ix < INTERRUPT_ENDPOINTs; ix++)
    {
        StaticQH = EhciExtension->PeriodicHead[ix];

        StaticQH->PrevHead = NULL;
        StaticQH->NextHead = (PEHCI_HCD_QH)EhciExtension->PeriodicHead[LinkTable[ix]];

        StaticQH->HwQH.HorizontalLink.AsULONG =
            EhciExtension->PeriodicHead[LinkTable[ix]]->PhysicalAddress;

        StaticQH->HwQH.HorizontalLink.Type = EHCI_LINK_TYPE_QH;
        StaticQH->HwQH.EndpointCaps.InterruptMask = 0xFF;

        StaticQH->QhFlags |= EHCI_QH_FLAG_STATIC;

        if (ix < (ENDPOINT_INTERRUPT_8ms - 1))
            StaticQH->QhFlags |= EHCI_QH_FLAG_STATIC_FAST;
    }

    EhciExtension->PeriodicHead[0]->HwQH.HorizontalLink.Terminate = 1;

    EhciExtension->PeriodicHead[0]->QhFlags |= (EHCI_QH_FLAG_STATIC |
                                                EHCI_QH_FLAG_STATIC_FAST);
}

MPSTATUS
NTAPI
EHCI_InitializeSchedule(IN PEHCI_EXTENSION EhciExtension,
                        IN ULONG_PTR BaseVA,
                        IN ULONGLONG BasePA)
{
    PEHCI_HW_REGISTERS OperationalRegs;
    PEHCI_HC_RESOURCES HcResourcesVA;
    ULONGLONG HcResourcesPA;
    PEHCI_STATIC_QH AsyncHead;
    ULONG AsyncHeadPA;
    PEHCI_STATIC_QH PeriodicHead;
    ULONG PeriodicHeadPA;
    PEHCI_STATIC_QH StaticQH;
    EHCI_LINK_POINTER NextLink;
    EHCI_LINK_POINTER StaticHeadPA;
    ULONG Frame;
    ULONG ix;

    DPRINT("EHCI_InitializeSchedule: BaseVA - %p, BasePA - %p\n",
           BaseVA,
           BasePA);

    OperationalRegs = EhciExtension->OperationalRegs;

    HcResourcesVA = (PEHCI_HC_RESOURCES)BaseVA;
    HcResourcesPA = BasePA;

    EhciExtension->HcResourcesVA = HcResourcesVA;
    EhciExtension->HcResourcesPA = BasePA;

    /* Asynchronous Schedule */

    AsyncHead = &HcResourcesVA->AsyncHead;
    AsyncHeadPA = HcResourcesPA + FIELD_OFFSET(EHCI_HC_RESOURCES, AsyncHead);

    RtlZeroMemory(AsyncHead, sizeof(EHCI_STATIC_QH));

    NextLink.AsULONG = AsyncHeadPA;
    NextLink.Type = EHCI_LINK_TYPE_QH;

    AsyncHead->HwQH.HorizontalLink = NextLink;
    AsyncHead->HwQH.EndpointParams.HeadReclamationListFlag = 1;
    AsyncHead->HwQH.EndpointCaps.PipeMultiplier = 1;
    AsyncHead->HwQH.NextTD |= TERMINATE_POINTER;
    AsyncHead->HwQH.Token.Status = (UCHAR)EHCI_TOKEN_STATUS_HALTED;

    AsyncHead->PhysicalAddress = (ULONG)AsyncHeadPA;
    AsyncHead->PrevHead = AsyncHead->NextHead = (PEHCI_HCD_QH)AsyncHead;

    EhciExtension->AsyncHead = AsyncHead;

    /* Periodic Schedule */

    PeriodicHead = &HcResourcesVA->PeriodicHead[0];
    PeriodicHeadPA = HcResourcesPA + FIELD_OFFSET(EHCI_HC_RESOURCES, PeriodicHead[0]);

    for (ix = 0; ix < (INTERRUPT_ENDPOINTs + 1); ix++)
    {
        EHCI_AlignHwStructure(EhciExtension,
                              &PeriodicHeadPA,
                              (PULONG_PTR)&PeriodicHead,
                              80);

        EhciExtension->PeriodicHead[ix] = PeriodicHead;
        EhciExtension->PeriodicHead[ix]->PhysicalAddress = (ULONG)PeriodicHeadPA;

        PeriodicHead += 1;
        PeriodicHeadPA += sizeof(EHCI_STATIC_QH);
    }

    EHCI_InitializeInterruptSchedule(EhciExtension);

    for (Frame = 0; Frame < EHCI_FRAME_LIST_MAX_ENTRIES; Frame++)
    {
        StaticQH = EHCI_GetQhForFrame(EhciExtension, Frame);

        StaticHeadPA.AsULONG = StaticQH->PhysicalAddress;
        StaticHeadPA.Type = EHCI_LINK_TYPE_QH;

        //DPRINT_EHCI("EHCI_InitializeSchedule: StaticHeadPA[%x] - %X\n",
        //            Frame,
        //            StaticHeadPA);

        HcResourcesVA->PeriodicFrameList[Frame] = StaticHeadPA.AsULONG;
    }

    EhciExtension->IsoDummyQHListVA = &HcResourcesVA->IsoDummyQH[0];
    EhciExtension->IsoDummyQHListPA = (ULONG)(HcResourcesPA + FIELD_OFFSET(EHCI_HC_RESOURCES, IsoDummyQH[0]));

    EHCI_AddDummyQHs(EhciExtension);

    /* Force 32-bit addressing for schedule structures */
    WRITE_REGISTER_ULONG(&OperationalRegs->SegmentSelector, 0);
    WRITE_REGISTER_ULONG(&OperationalRegs->PeriodicListBase,
                         (ULONG)(EhciExtension->HcResourcesPA + FIELD_OFFSET(EHCI_HC_RESOURCES, PeriodicFrameList)));

    WRITE_REGISTER_ULONG(&OperationalRegs->AsyncListBase,
                         NextLink.AsULONG);

#if DBG
    {
        ULONG seg = READ_REGISTER_ULONG(&OperationalRegs->SegmentSelector);
        ULONG plb = READ_REGISTER_ULONG(&OperationalRegs->PeriodicListBase);
        ULONG alb = READ_REGISTER_ULONG(&OperationalRegs->AsyncListBase);
        DPRINT_EHCI("EHCI_InitializeSchedule: CTRLDSSegment=0x%08lx PLB=0x%08lx ALB=0x%08lx\n", seg, plb, alb);
    }
#endif

    return MP_STATUS_SUCCESS;
}

MPSTATUS
NTAPI
EHCI_InitializeHardware(IN PEHCI_EXTENSION EhciExtension)
{
    PEHCI_HC_CAPABILITY_REGISTERS CapabilityRegisters;
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_COMMAND Command;
    LARGE_INTEGER EndTime;
    LARGE_INTEGER CurrentTime;
    EHCI_HC_STRUCTURAL_PARAMS StructuralParams;

    DPRINT("EHCI_InitializeHardware: ... \n");

    OperationalRegs = EhciExtension->OperationalRegs;
    CapabilityRegisters = EhciExtension->CapabilityRegisters;

    Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    Command.Reset = 1;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);

    KeQuerySystemTime(&EndTime);
    EndTime.QuadPart += 100 * 10000; // 100 msec

    DPRINT("EHCI_InitializeHardware: Start reset ... \n");

    while (TRUE)
    {
        KeQuerySystemTime(&CurrentTime);
        Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);

        if (Command.Reset != 1)
            break;

        if (CurrentTime.QuadPart >= EndTime.QuadPart)
        {
            if (Command.Reset == 1)
            {
                DPRINT_EHCI("EHCI_InitializeHardware: Reset failed!\n");
                return MP_STATUS_HW_ERROR;
            }

            break;
        }
    }

    DPRINT("EHCI_InitializeHardware: Reset - OK\n");

    StructuralParams.AsULONG = READ_REGISTER_ULONG(&CapabilityRegisters->StructParameters.AsULONG);

    EhciExtension->NumberOfPorts = StructuralParams.PortCount;
    EhciExtension->PortPowerControl = StructuralParams.PortPowerControl;

    DPRINT("EHCI_InitializeHardware: StructuralParams - %X\n", StructuralParams.AsULONG);
    DPRINT("EHCI_InitializeHardware: PortPowerControl - %x\n", EhciExtension->PortPowerControl);
    DPRINT("EHCI_InitializeHardware: N_PORTS          - %x\n", EhciExtension->NumberOfPorts);

    WRITE_REGISTER_ULONG(&OperationalRegs->PeriodicListBase, 0);
    WRITE_REGISTER_ULONG(&OperationalRegs->AsyncListBase, 0);
    /* Force 32-bit addressing for schedule structures (EHCI CTRLDSSegment) */
    WRITE_REGISTER_ULONG(&OperationalRegs->SegmentSelector, 0);
#if DBG
    {
        ULONG seg = READ_REGISTER_ULONG(&OperationalRegs->SegmentSelector);
        DPRINT_EHCI("EHCI_InitializeHardware: CTRLDSSegment after reset=0x%08lx\n", seg);
    }
#endif

    EhciExtension->InterruptMask.AsULONG = 0;
    EhciExtension->InterruptMask.Interrupt = 1;
    EhciExtension->InterruptMask.ErrorInterrupt = 1;
    /* Enable port-change interrupts by default to reduce polling */
    EhciExtension->InterruptMask.PortChangeInterrupt = 1;
    EhciExtension->InterruptMask.FrameListRollover = 1;
    EhciExtension->InterruptMask.HostSystemError = 1;
    EhciExtension->InterruptMask.InterruptOnAsyncAdvance = 1;

    /* Initialize cached connect state bitmap */
    EhciExtension->LastConnectStatusBits = 0;

    return MP_STATUS_SUCCESS;
}

UCHAR
NTAPI
EHCI_GetOffsetEECP(IN PEHCI_EXTENSION EhciExtension,
                   IN UCHAR CapabilityID)
{
    EHCI_LEGACY_EXTENDED_CAPABILITY LegacyCapability;
    EHCI_HC_CAPABILITY_PARAMS CapParameters;
    UCHAR OffsetEECP;

    DPRINT("EHCI_GetOffsetEECP: CapabilityID - %x\n", CapabilityID);

    CapParameters = EhciExtension->CapabilityRegisters->CapParameters;

    OffsetEECP = CapParameters.ExtCapabilitiesPointer;

    if (!OffsetEECP)
        return 0;

    while (TRUE)
    {
        RegPacket.UsbPortReadWriteConfigSpace(EhciExtension,
                                              TRUE,
                                              &LegacyCapability.AsULONG,
                                              OffsetEECP,
                                              sizeof(LegacyCapability));

        DPRINT("EHCI_GetOffsetEECP: OffsetEECP - %x\n", OffsetEECP);

        if (LegacyCapability.CapabilityID == CapabilityID)
            break;

        OffsetEECP = LegacyCapability.NextCapabilityPointer;

        if (!OffsetEECP)
            return 0;
    }

    return OffsetEECP;
}

MPSTATUS
NTAPI
EHCI_TakeControlHC(IN PEHCI_EXTENSION EhciExtension)
{
    LARGE_INTEGER EndTime;
    LARGE_INTEGER CurrentTime;
    EHCI_LEGACY_EXTENDED_CAPABILITY LegacyCapability;
    UCHAR OffsetEECP;

    DPRINT("EHCI_TakeControlHC: EhciExtension - %p\n", EhciExtension);

    OffsetEECP = EHCI_GetOffsetEECP(EhciExtension, 1);

    if (OffsetEECP == 0)
        return MP_STATUS_SUCCESS;

    DPRINT("EHCI_TakeControlHC: OffsetEECP - %X\n", OffsetEECP);

    RegPacket.UsbPortReadWriteConfigSpace(EhciExtension,
                                          TRUE,
                                          &LegacyCapability.AsULONG,
                                          OffsetEECP,
                                          sizeof(LegacyCapability));

    if (LegacyCapability.BiosOwnedSemaphore == 0)
        return MP_STATUS_SUCCESS;

    LegacyCapability.OsOwnedSemaphore = 1;

    RegPacket.UsbPortReadWriteConfigSpace(EhciExtension,
                                          FALSE,
                                          &LegacyCapability.AsULONG,
                                          OffsetEECP,
                                          sizeof(LegacyCapability));

    KeQuerySystemTime(&EndTime);
    EndTime.QuadPart += 100 * 10000;

    do
    {
        RegPacket.UsbPortReadWriteConfigSpace(EhciExtension,
                                              TRUE,
                                              &LegacyCapability.AsULONG,
                                              OffsetEECP,
                                              sizeof(LegacyCapability));
        KeQuerySystemTime(&CurrentTime);

        if (LegacyCapability.BiosOwnedSemaphore)
        {
            DPRINT("EHCI_TakeControlHC: Ownership is ok\n");
            break;
        }
    }
    while (CurrentTime.QuadPart <= EndTime.QuadPart);

    return MP_STATUS_SUCCESS;
}

static const WCHAR EHCI_REG_FRAME_LENGTH_ADJ[] = L"FrameLengthAdjustment";
static const WCHAR EHCI_REG_IDLE_SUPPORT[] = L"EnableIdleSupport";
static const WCHAR EHCI_REG_TRACE_MASK[]   = L"EhciTraceMask";
static const WCHAR EHCI_REG_POLL_DIV[]     = L"EhciPollLogDiv";

VOID
NTAPI
EHCI_GetRegistryParameters(IN PEHCI_EXTENSION EhciExtension)
{
    ULONG ParameterValue;
    MPSTATUS MpStatus;

    DPRINT("EHCI_GetRegistryParameters: EhciExtension - %p\n", EhciExtension);

    /* Optional override for the FLADJ value (defaults to PCI config byte) */
    ParameterValue = EhciExtension->FrameLengthAdjustment;

    MpStatus = RegPacket.UsbPortGetMiniportRegistryKeyValue(EhciExtension,
                                                            TRUE,
                                                            EHCI_REG_FRAME_LENGTH_ADJ,
                                                            sizeof(EHCI_REG_FRAME_LENGTH_ADJ),
                                                            &ParameterValue,
                                                            sizeof(ParameterValue));

    if (MpStatus == MP_STATUS_SUCCESS)
    {
        DPRINT("EHCI_GetRegistryParameters: overriding FLADJ with %lu\n", ParameterValue);
        EhciExtension->FrameLengthAdjustment = (UCHAR)(ParameterValue & 0xFF);
    }

    /* Enable/disable idle support (controller selective suspend) */
    ParameterValue = 0;

    MpStatus = RegPacket.UsbPortGetMiniportRegistryKeyValue(EhciExtension,
                                                            TRUE,
                                                            EHCI_REG_IDLE_SUPPORT,
                                                            sizeof(EHCI_REG_IDLE_SUPPORT),
                                                            &ParameterValue,
                                                            sizeof(ParameterValue));

    if (MpStatus == MP_STATUS_SUCCESS)
    {
        if (ParameterValue)
        {
            EhciExtension->Flags |= EHCI_FLAGS_IDLE_SUPPORT;
            DPRINT("EHCI_GetRegistryParameters: idle support enabled via registry\n");
        }
        else
        {
            EhciExtension->Flags &= ~EHCI_FLAGS_IDLE_SUPPORT;
            DPRINT("EHCI_GetRegistryParameters: idle support disabled via registry\n");
        }
    }

#if DBG
    /* Optional runtime trace mask */
    ParameterValue = 0xFFFFFFFF;
    MpStatus = RegPacket.UsbPortGetMiniportRegistryKeyValue(EhciExtension,
                                                            TRUE,
                                                            EHCI_REG_TRACE_MASK,
                                                            sizeof(EHCI_REG_TRACE_MASK),
                                                            &ParameterValue,
                                                            sizeof(ParameterValue));
    if (MpStatus == MP_STATUS_SUCCESS)
    {
        g_EhciTraceMask = ParameterValue;
        DPRINT("EHCI_GetRegistryParameters: EhciTraceMask=0x%08lx\n", g_EhciTraceMask);
    }

    /* Optional poll log divisor */
    ParameterValue = g_EhciPollLogDiv;
    MpStatus = RegPacket.UsbPortGetMiniportRegistryKeyValue(EhciExtension,
                                                            TRUE,
                                                            EHCI_REG_POLL_DIV,
                                                            sizeof(EHCI_REG_POLL_DIV),
                                                            &ParameterValue,
                                                            sizeof(ParameterValue));
    if (MpStatus == MP_STATUS_SUCCESS)
    {
        if (ParameterValue == 0) ParameterValue = 1;
        g_EhciPollLogDiv = ParameterValue;
        DPRINT("EHCI_GetRegistryParameters: EhciPollLogDiv=%lu\n", g_EhciPollLogDiv);
    }
#endif
}

MPSTATUS
NTAPI
EHCI_StartController(IN PVOID ehciExtension,
                     IN PUSBPORT_RESOURCES Resources)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_HC_CAPABILITY_REGISTERS CapabilityRegisters;
    PEHCI_HW_REGISTERS OperationalRegs;
    MPSTATUS MPStatus;
    EHCI_USB_COMMAND Command;
    UCHAR CapabilityRegLength;
    UCHAR Fladj;

    DPRINT_EHCI("EHCI_StartController: ResourcesTypes=0x%lx\n", Resources->ResourcesTypes);

    if ((Resources->ResourcesTypes & (USBPORT_RESOURCES_MEMORY | USBPORT_RESOURCES_INTERRUPT)) !=
                                     (USBPORT_RESOURCES_MEMORY | USBPORT_RESOURCES_INTERRUPT))
    {
        DPRINT_EHCI("EHCI_StartController: Resources->ResourcesTypes - %x\n",
                Resources->ResourcesTypes);

        return MP_STATUS_ERROR;
    }

    CapabilityRegisters = (PEHCI_HC_CAPABILITY_REGISTERS)Resources->ResourceBase;
    EhciExtension->CapabilityRegisters = CapabilityRegisters;

    CapabilityRegLength = READ_REGISTER_UCHAR(&CapabilityRegisters->RegistersLength);

    OperationalRegs = (PEHCI_HW_REGISTERS)((ULONG_PTR)CapabilityRegisters +
                                                      CapabilityRegLength);

    EhciExtension->OperationalRegs = OperationalRegs;

    DPRINT_EHCI("EHCI_StartController: CapRegs=%p OpRegs=%p\n", CapabilityRegisters, OperationalRegs);
    DPRINT_EHCI("EHCI_StartController: HCSParams=0x%08lx HCCParams=0x%08lx Ports=%u\n",
            CapabilityRegisters->StructParameters.AsULONG,
            CapabilityRegisters->CapParameters.AsULONG,
            CapabilityRegisters->StructParameters.PortCount);

    RegPacket.UsbPortReadWriteConfigSpace(EhciExtension,
                                          TRUE,
                                          &Fladj,
                                          EHCI_FLADJ_PCI_CONFIG_OFFSET,
                                          sizeof(Fladj));

    EhciExtension->FrameLengthAdjustment = Fladj;
    DPRINT_EHCI("EHCI_StartController: PCI FLADJ=0x%02x\n", Fladj);

    EHCI_GetRegistryParameters(EhciExtension);

    MPStatus = EHCI_TakeControlHC(EhciExtension);

    if (MPStatus)
    {
        DPRINT_EHCI("EHCI_StartController: Unsuccessful TakeControlHC()\n");
        return MPStatus;
    }
    DPRINT_EHCI("EHCI_StartController: TakeControlHC OK\n");

    MPStatus = EHCI_InitializeHardware(EhciExtension);

    if (MPStatus)
    {
        DPRINT_EHCI("EHCI_StartController: Unsuccessful InitializeHardware()\n");
        return MPStatus;
    }
    DPRINT_EHCI("EHCI_StartController: InitializeHardware OK\n");

    MPStatus = EHCI_InitializeSchedule(EhciExtension,
                                       Resources->StartVA,
                                       Resources->StartPA);

    if (MPStatus)
    {
        DPRINT_EHCI("EHCI_StartController: Unsuccessful InitializeSchedule()\n");
        return MPStatus;
    }
    DPRINT_EHCI("EHCI_StartController: InitializeSchedule OK\n");

    RegPacket.UsbPortReadWriteConfigSpace(EhciExtension,
                                          TRUE,
                                          &Fladj,
                                          EHCI_FLADJ_PCI_CONFIG_OFFSET,
                                          sizeof(Fladj));

    if (Fladj != EhciExtension->FrameLengthAdjustment)
    {
        Fladj = EhciExtension->FrameLengthAdjustment;

        RegPacket.UsbPortReadWriteConfigSpace(EhciExtension,
                                              FALSE, // write
                                              &Fladj,
                                              EHCI_FLADJ_PCI_CONFIG_OFFSET,
                                              sizeof(Fladj));
    }

    /* Port routing control logic default-routes all ports to this HC */
    EhciExtension->PortRoutingControl = EHCI_CONFIG_FLAG_CONFIGURED;
    WRITE_REGISTER_ULONG(&OperationalRegs->ConfigFlag,
                         EhciExtension->PortRoutingControl);
    DPRINT_EHCI("EHCI_StartController: ConfigFlag=0x%08lx\n", EhciExtension->PortRoutingControl);

    Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    Command.InterruptThreshold = 1; // one micro-frame
    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);

    /* Proactively enable interrupts (USBPORT may also call EnableInterrupts) */
    EHCI_EnableInterrupts(EhciExtension);
    DPRINT_EHCI("EHCI_StartController: HcCommand=0x%08lx (InterruptThreshold=1)\n", Command.AsULONG);

    Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    Command.Run = 1; // execution of the schedule
    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);

    EhciExtension->IsStarted = TRUE;

    if (Resources->IsChirpHandled)
    {
        ULONG Port;

        for (Port = 1; Port <= EhciExtension->NumberOfPorts; Port++)
        {
            EHCI_RH_SetFeaturePortPower(EhciExtension, Port);
        }

        RegPacket.UsbPortWait(EhciExtension, 200);

        for (Port = 1; Port <= EhciExtension->NumberOfPorts; Port++)
        {
            EHCI_RH_ChirpRootPort(EhciExtension, Port++);
        }
    }

    return MPStatus;
}

VOID
NTAPI
EHCI_StopController(IN PVOID ehciExtension,
                    IN BOOLEAN DisableInterrupts)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_COMMAND Command;
    EHCI_USB_STATUS Status;
    LARGE_INTEGER EndTime, Now;
    PEHCI_HCD_QH Qh, NextQh;
    ULONG ix;

    DPRINT_EHCI("EHCI_StopController: entry DisableInterrupts=%u\n", DisableInterrupts);

    OperationalRegs = EhciExtension->OperationalRegs;

    if (DisableInterrupts)
    {
        WRITE_REGISTER_ULONG(&OperationalRegs->HcInterruptEnable.AsULONG, 0);
    }

    /* Disable schedules */
    Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    Command.AsynchronousEnable = 0;
    Command.PeriodicEnable = 0;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);

    /* Wait for schedules to quiesce */
    KeQuerySystemTime(&EndTime);
    EndTime.QuadPart += 200 * 10000; // 200 ms
    do
    {
        Status.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);
        KeQuerySystemTime(&Now);
        if (!Status.AsynchronousStatus && !Status.PeriodicStatus)
            break;
    }
    while (Now.QuadPart < EndTime.QuadPart);

    /* Halt the controller */
    Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    Command.Run = 0;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);

    KeQuerySystemTime(&EndTime);
    EndTime.QuadPart += 200 * 10000; // 200 ms
    do
    {
        Status.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);
        KeQuerySystemTime(&Now);
        if (Status.HCHalted)
            break;
    }
    while (Now.QuadPart < EndTime.QuadPart);

    if (!Status.HCHalted)
    {
        DPRINT_EHCI("EHCI_StopController: controller did not halt in time (STS=0x%08lx)\n", Status.AsULONG);
    }

    /* Acknowledge any pending status bits */
    if (Status.AsULONG)
    {
        WRITE_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG, Status.AsULONG & EHCI_INTERRUPT_MASK);
    }

    /* Defensive sweep: unlink any remaining QHs from schedules */
    /* Async list */
    if (EhciExtension->AsyncHead)
    {
        Qh = ((PEHCI_HCD_QH)EhciExtension->AsyncHead)->sqh.NextHead;
        while (Qh && Qh != (PEHCI_HCD_QH)EhciExtension->AsyncHead)
        {
            NextQh = Qh->sqh.NextHead;
            if (Qh->sqh.QhFlags & EHCI_QH_FLAG_IN_SCHEDULE)
            {
                DPRINT_EHCI("EHCI_StopController: unlinking ASYNC QH %p\n", Qh);
                EHCI_RemoveQhFromAsyncList(EhciExtension, Qh);
            }
            Qh = NextQh;
        }
    }

    /* Periodic lists (walk each static head chain) */
    for (ix = 0; ix < RTL_NUMBER_OF(EhciExtension->PeriodicHead); ix++)
    {
        PEHCI_STATIC_QH StaticQH = EhciExtension->PeriodicHead[ix];
        if (!StaticQH) continue;

        Qh = StaticQH->NextHead;
        while (Qh && !(Qh->sqh.QhFlags & EHCI_QH_FLAG_STATIC))
        {
            PEHCI_HCD_QH PrevHead;
            ULONG NextQhPA;

            NextQh = Qh->sqh.NextHead;

            if (Qh->sqh.QhFlags & EHCI_QH_FLAG_IN_SCHEDULE)
            {
                PrevHead = Qh->sqh.PrevHead;

                DPRINT_EHCI("EHCI_StopController: unlinking PERIODIC QH %p (prev=%p next=%p)\n",
                        Qh, PrevHead, NextQh);

                /* Relink neighbors */
                PrevHead->sqh.NextHead = NextQh;
                if (NextQh)
                {
                    NextQh->sqh.PrevHead = PrevHead;

                    NextQhPA = NextQh->sqh.PhysicalAddress;
                    NextQhPA &= LINK_POINTER_MASK + TERMINATE_POINTER;
                    NextQhPA |= (EHCI_LINK_TYPE_QH << 1);
                    PrevHead->sqh.HwQH.HorizontalLink.AsULONG = NextQhPA;
                }
                else
                {
                    PrevHead->sqh.HwQH.HorizontalLink.Terminate = 1;
                }

                Qh->sqh.QhFlags &= ~EHCI_QH_FLAG_IN_SCHEDULE;
                Qh->sqh.NextHead = NULL;
                Qh->sqh.PrevHead = NULL;
            }

            Qh = NextQh;
        }
    }

    EhciExtension->IsStarted = FALSE;
    DPRINT_EHCI("EHCI_StopController: exit\n");
}

VOID
NTAPI
EHCI_SuspendController(IN PVOID ehciExtension)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_COMMAND Command;
    EHCI_USB_STATUS Status;
    EHCI_INTERRUPT_ENABLE IntrEn;
    ULONG ix;

    DPRINT("EHCI_SuspendController: ... \n");

    OperationalRegs = EhciExtension->OperationalRegs;

    EhciExtension->BackupPeriodiclistbase = READ_REGISTER_ULONG(&OperationalRegs->PeriodicListBase);
    EhciExtension->BackupAsynclistaddr = READ_REGISTER_ULONG(&OperationalRegs->AsyncListBase);
    EhciExtension->BackupCtrlDSSegment = READ_REGISTER_ULONG(&OperationalRegs->SegmentSelector);
    EhciExtension->BackupUSBCmd = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);

    /* Stop async/periodic engines before halting */
    Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    Command.InterruptAdvanceDoorbell = 0;
    Command.AsynchronousEnable = 0;
    Command.PeriodicEnable = 0;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);

    /* Wait for schedules to quiesce */
    for (ix = 0; ix < 200; ix++)
    {
        Status.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);
        if (!Status.AsynchronousStatus && !Status.PeriodicStatus)
            break;
        RegPacket.UsbPortWait(EhciExtension, 1);
    }

    /* Halt the controller */
    Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    Command.Run = 0;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);

    KeStallExecutionProcessor(125);

    Status.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);

    Status.HCHalted = 0;
    Status.Reclamation = 0;
    Status.PeriodicStatus = 0;
    Status.AsynchronousStatus = 0;

    if (Status.AsULONG)
        WRITE_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG, Status.AsULONG);

    /* Mask all interrupts during suspend */
    WRITE_REGISTER_ULONG(&OperationalRegs->HcInterruptEnable.AsULONG, 0);

    for (ix = 0; ix < 10; ix++)
    {
        Status.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);

        if (Status.HCHalted)
            break;

        RegPacket.UsbPortWait(EhciExtension, 1);
    }

    if (!Status.HCHalted)
        DbgBreakPoint();

    /* Keep PortChange enabled to detect wake events */
    IntrEn.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcInterruptEnable.AsULONG);
    IntrEn.PortChangeInterrupt = 1;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcInterruptEnable.AsULONG, IntrEn.AsULONG);

    EhciExtension->Flags |= EHCI_FLAGS_CONTROLLER_SUSPEND;
}

MPSTATUS
NTAPI
EHCI_ResumeController(IN PVOID ehciExtension)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_HW_REGISTERS OperationalRegs;
    ULONG RoutingControl;
    EHCI_USB_COMMAND Command;
    EHCI_USB_STATUS Status;
    ULONG ix;

    DPRINT("EHCI_ResumeController: ... \n");

    OperationalRegs = EhciExtension->OperationalRegs;

    RoutingControl = EhciExtension->PortRoutingControl;

    if (!(RoutingControl & EHCI_CONFIG_FLAG_CONFIGURED))
    {
        EhciExtension->PortRoutingControl = RoutingControl | EHCI_CONFIG_FLAG_CONFIGURED;
        WRITE_REGISTER_ULONG(&OperationalRegs->ConfigFlag,
                             EhciExtension->PortRoutingControl);

        return MP_STATUS_HW_ERROR;
    }

    /* Keep 32-bit addressing across resume as well */
    WRITE_REGISTER_ULONG(&OperationalRegs->SegmentSelector, 0);
#if DBG
    {
        ULONG seg = READ_REGISTER_ULONG(&OperationalRegs->SegmentSelector);
        DPRINT_EHCI("EHCI_ResumeController: CTRLDSSegment restored=0x%08lx (forced 0)\n", seg);
    }
#endif

    WRITE_REGISTER_ULONG(&OperationalRegs->PeriodicListBase,
                         EhciExtension->BackupPeriodiclistbase);

    WRITE_REGISTER_ULONG(&OperationalRegs->AsyncListBase,
                         EhciExtension->BackupAsynclistaddr);

    /* Restore command register from backup, re-enable run and saved schedules */
    Command.AsULONG = EhciExtension->BackupUSBCmd;
    Command.Reset = 0;
    Command.InterruptAdvanceDoorbell = 0;
    Command.LightResetHC = 0;
    Command.AsynchronousParkModeCount = 0;
    Command.AsynchronousParkModeEnable = 0;
    Command.Run = 1;

    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);

    WRITE_REGISTER_ULONG(&OperationalRegs->HcInterruptEnable.AsULONG,
                         EhciExtension->InterruptMask.AsULONG);

    /* Optionally wait for schedules to indicate active if they were enabled */
    Status.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);
    for (ix = 0; ix < 100; ix++)
    {
        Status.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);
        if (((! (EhciExtension->BackupUSBCmd & (1 << 5))) || Status.AsynchronousStatus) &&
            ((! (EhciExtension->BackupUSBCmd & (1 << 4))) || Status.PeriodicStatus))
        {
            break;
        }
        RegPacket.UsbPortWait(EhciExtension, 1);
    }

    /* If HC supports PortPowerControl, power ports after resume */
    if (EhciExtension->PortPowerControl)
    {
        USHORT Port;
        for (Port = 1; Port <= EhciExtension->NumberOfPorts; Port++)
        {
            EHCI_RH_SetFeaturePortPower(EhciExtension, Port);
        }
        /* Allow power to settle to PowerOnToPowerGood (2*2ms) */
        RegPacket.UsbPortWait(EhciExtension, 10);
    }

    /* Invalidate the root hub once to refresh state after resume */
    RegPacket.UsbPortInvalidateRootHub(EhciExtension);

    EhciExtension->Flags &= ~EHCI_FLAGS_CONTROLLER_SUSPEND;

    return MP_STATUS_SUCCESS;
}

BOOLEAN
NTAPI
EHCI_HardwarePresent(IN PEHCI_EXTENSION EhciExtension,
                     IN BOOLEAN IsInvalidateController)
{
    PEHCI_HW_REGISTERS OperationalRegs = EhciExtension->OperationalRegs;

    if (READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG) != -1)
        return TRUE;

    DPRINT_EHCI("EHCI_HardwarePresent: IsInvalidateController - %x\n",
            IsInvalidateController);

    if (!IsInvalidateController)
        return FALSE;

    RegPacket.UsbPortInvalidateController(EhciExtension,
                                          USBPORT_INVALIDATE_CONTROLLER_SURPRISE_REMOVE);
    return FALSE;
}

BOOLEAN
NTAPI
EHCI_InterruptService(IN PVOID ehciExtension)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_HW_REGISTERS OperationalRegs;
    BOOLEAN Result = FALSE;
    EHCI_USB_STATUS IntrSts;
    EHCI_INTERRUPT_ENABLE IntrEn;
    EHCI_INTERRUPT_ENABLE iStatus;
    EHCI_USB_COMMAND Command;
    ULONG FrameIndex;

    OperationalRegs = EhciExtension->OperationalRegs;

    DPRINT_EHCI("EHCI_InterruptService: ... \n");

    Result = EHCI_HardwarePresent(EhciExtension, FALSE);

    if (!Result)
        return FALSE;

    IntrEn.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcInterruptEnable.AsULONG);
    IntrSts.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);

    iStatus.AsULONG = (IntrEn.AsULONG & IntrSts.AsULONG) & EHCI_INTERRUPT_MASK;

    if (!iStatus.AsULONG)
        return FALSE;

    EhciExtension->InterruptStatus = iStatus;

    DPRINT_EHCI("EHCI_InterruptService: Mask=0x%08lx Status=0x%08lx iStatus=0x%08lx\n",
                IntrEn.AsULONG,
                IntrSts.AsULONG,
                iStatus.AsULONG);

    WRITE_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG, iStatus.AsULONG);

    if (iStatus.HostSystemError)
    {
        EhciExtension->HcSystemErrors++;

        if (EhciExtension->HcSystemErrors < EHCI_MAX_HC_SYSTEM_ERRORS)
        {
            Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
            Command.Run = 1;
            WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);
        }
    }

    FrameIndex = READ_REGISTER_ULONG(&OperationalRegs->FrameIndex) / EHCI_MICROFRAMES;
    FrameIndex &= EHCI_FRINDEX_FRAME_MASK;

    if ((FrameIndex ^ EhciExtension->FrameIndex) & EHCI_FRAME_LIST_MAX_ENTRIES)
    {
        EhciExtension->FrameHighPart += 2 * EHCI_FRAME_LIST_MAX_ENTRIES;

        EhciExtension->FrameHighPart -= (FrameIndex ^ EhciExtension->FrameHighPart) &
                                        EHCI_FRAME_LIST_MAX_ENTRIES;
    }

    EhciExtension->FrameIndex = FrameIndex;

    return TRUE;
}

VOID
NTAPI
EHCI_InterruptDpc(IN PVOID ehciExtension,
                  IN BOOLEAN EnableInterrupts)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_INTERRUPT_ENABLE iStatus;

    OperationalRegs = EhciExtension->OperationalRegs;

    DPRINT_EHCI("EHCI_InterruptDpc: [%p] EnableInterrupts=%u\n",
                EhciExtension, EnableInterrupts);

    iStatus = EhciExtension->InterruptStatus;
    EhciExtension->InterruptStatus.AsULONG = 0;

    if (iStatus.Interrupt == 1 ||
        iStatus.ErrorInterrupt == 1 ||
        iStatus.InterruptOnAsyncAdvance == 1)
    {
        DPRINT_EHCI("EHCI_InterruptDpc: [%p] InterruptStatus=0x%08lx\n", EhciExtension, iStatus.AsULONG);
        RegPacket.UsbPortInvalidateEndpoint(EhciExtension, NULL);
    }

    if (iStatus.PortChangeInterrupt == 1)
    {
        DPRINT_EHCI("EHCI_InterruptDpc: [%p] PortChangeInterrupt\n", EhciExtension);
        RegPacket.UsbPortInvalidateRootHub(EhciExtension);
    }

    if (EnableInterrupts)
    {
        WRITE_REGISTER_ULONG(&OperationalRegs->HcInterruptEnable.AsULONG,
                             EhciExtension->InterruptMask.AsULONG);
        DPRINT_EHCI("EHCI_InterruptDpc: re-enabled interrupts mask=0x%08lx\n",
                    EhciExtension->InterruptMask.AsULONG);
    }
}

ULONG
NTAPI
EHCI_MapAsyncTransferToTd(IN PEHCI_EXTENSION EhciExtension,
                          IN ULONG MaxPacketSize,
                          IN ULONG TransferedLen,
                          IN PULONG DataToggle,
                          IN PEHCI_TRANSFER EhciTransfer,
                          IN PEHCI_HCD_TD TD,
                          IN PUSBPORT_SCATTER_GATHER_LIST SgList)
{
    PUSBPORT_TRANSFER_PARAMETERS TransferParameters;
    PUSBPORT_SCATTER_GATHER_ELEMENT SgElement;
    ULONG SgIdx;
    ULONG LengthThisTD;
    ULONG ix;
    ULONG SgRemain;
    ULONG DiffLength;
    ULONG NumPackets;

    DPRINT_EHCI("EHCI_MapAsyncTransferToTd: Xfer=%p TD=%p Xfered=%lu MaxPkt=%lu Toggle=%p SgCount=%lu\n",
                EhciTransfer,
                TD,
                TransferedLen,
                MaxPacketSize,
                DataToggle,
                SgList ? SgList->SgElementCount : 0);

    TransferParameters = EhciTransfer->TransferParameters;

    SgElement = &SgList->SgElement[0];

    for (SgIdx = 0; SgIdx < SgList->SgElementCount; SgIdx++)
    {
        if (TransferedLen >= SgElement->SgOffset &&
            TransferedLen < SgElement->SgOffset + SgElement->SgTransferLength)
        {
            break;
        }

        SgElement += 1;
    }

    DPRINT_EHCI("EHCI_MapAsyncTransferToTd: Using SG[%lu] PA=%08lx Len=%lu Off=%lu StartXfered=%lu\n",
                SgIdx,
                SgList->SgElement[SgIdx].SgPhysicalAddress.LowPart,
                SgList->SgElement[SgIdx].SgTransferLength,
                SgList->SgElement[SgIdx].SgOffset,
                TransferedLen);

    SgRemain = SgList->SgElementCount - SgIdx;

    if (SgRemain > EHCI_MAX_QTD_BUFFER_PAGES)
    {
        TD->HwTD.Buffer[0] = SgList->SgElement[SgIdx].SgPhysicalAddress.LowPart -
                             SgList->SgElement[SgIdx].SgOffset +
                             TransferedLen;

        LengthThisTD = EHCI_MAX_QTD_BUFFER_PAGES * PAGE_SIZE -
                       (TD->HwTD.Buffer[0] & (PAGE_SIZE - 1));

        for (ix = 1; ix < EHCI_MAX_QTD_BUFFER_PAGES; ix++)
        {
            TD->HwTD.Buffer[ix] = SgList->SgElement[SgIdx + ix].SgPhysicalAddress.LowPart;
        }

        NumPackets = LengthThisTD / MaxPacketSize;

        DPRINT_EHCI("EHCI_MapAsyncTransferToTd: TD Buf0=%08lx Buf1=%08lx Buf2=%08lx Buf3=%08lx Buf4=%08lx LengthThisTD=%lu (paged)\n",
                    TD->HwTD.Buffer[0], TD->HwTD.Buffer[1], TD->HwTD.Buffer[2], TD->HwTD.Buffer[3], TD->HwTD.Buffer[4],
                    LengthThisTD);
        DiffLength = LengthThisTD - MaxPacketSize * (LengthThisTD / MaxPacketSize);

        if (LengthThisTD != MaxPacketSize * (LengthThisTD / MaxPacketSize))
            LengthThisTD -= DiffLength;

        if (DataToggle && (NumPackets & 1))
            *DataToggle = !(*DataToggle);
    }
    else
    {
        LengthThisTD = TransferParameters->TransferBufferLength - TransferedLen;

        TD->HwTD.Buffer[0] = TransferedLen +
                             SgList->SgElement[SgIdx].SgPhysicalAddress.LowPart -
                             SgList->SgElement[SgIdx].SgOffset;

        for (ix = 1; ix < EHCI_MAX_QTD_BUFFER_PAGES; ix++)
        {
            if ((SgIdx + ix) >= SgList->SgElementCount)
                break;

            TD->HwTD.Buffer[ix] = SgList->SgElement[SgIdx + ix].SgPhysicalAddress.LowPart;
        }

        DPRINT_EHCI("EHCI_MapAsyncTransferToTd: TD Buf0=%08lx Buf1=%08lx Buf2=%08lx Buf3=%08lx Buf4=%08lx LengthThisTD=%lu (contig)\n",
                    TD->HwTD.Buffer[0], TD->HwTD.Buffer[1], TD->HwTD.Buffer[2], TD->HwTD.Buffer[3], TD->HwTD.Buffer[4],
                    LengthThisTD);
    }

    TD->HwTD.Token.TransferBytes = LengthThisTD;
    TD->LengthThisTD = LengthThisTD;

    /* debug: remember start PA and length for TD completion correlation */
    TD->Pad[0] = TD->HwTD.Buffer[0];
    TD->Pad[1] = LengthThisTD;
    /* also remember a mapped VA for debug hexdump at completion */
#if DBG
    {
        ULONG_PTR va = (ULONG_PTR)SgList->MappedSystemVa + TransferedLen;
        TD->Pad[2] = (ULONG)(va & 0xFFFFFFFF);
        TD->Pad[3] = (ULONG)((va >> 32) & 0xFFFFFFFF);
    }
#endif

    {
        ULONG expected = SgList->SgElement[SgIdx].SgPhysicalAddress.LowPart -
                         SgList->SgElement[SgIdx].SgOffset +
                         TransferedLen;
        if ((TD->HwTD.Buffer[0] ^ expected) != 0)
        {
            DPRINT_EHCI("EHCI_MapAsyncTransferToTd: WARNING Buf0 mismatch: set=0x%08lx expected=0x%08lx delta=%ld\n",
                    TD->HwTD.Buffer[0], expected, (LONG)TD->HwTD.Buffer[0] - (LONG)expected);
        }
    }

    DPRINT_EHCI("EHCI_MapAsyncTransferToTd: EXIT XferedNext=%lu ToggleNext=%u\n",
                LengthThisTD + TransferedLen,
                DataToggle ? (*DataToggle & 1) : 0);
    return LengthThisTD + TransferedLen;
}

VOID
NTAPI
EHCI_EnableAsyncList(IN PEHCI_EXTENSION EhciExtension)
{
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_COMMAND UsbCmd;
    EHCI_INTERRUPT_ENABLE IntrEn;

    DPRINT_EHCI("EHCI_EnableAsyncList: enabling async schedule\n");

    OperationalRegs = EhciExtension->OperationalRegs;

    /* Re-enable async advance interrupt when bringing async back */
    IntrEn.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcInterruptEnable.AsULONG);
    IntrEn.InterruptOnAsyncAdvance = 1;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcInterruptEnable.AsULONG, IntrEn.AsULONG);

    UsbCmd.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    UsbCmd.AsynchronousEnable = 1;
    WRITE_REGISTER_ULONG((&OperationalRegs->HcCommand.AsULONG), UsbCmd.AsULONG);
    DPRINT_EHCI("EHCI_EnableAsyncList: HcCommand=0x%08lx\n", UsbCmd.AsULONG);
}

VOID
NTAPI
EHCI_DisableAsyncList(IN PEHCI_EXTENSION EhciExtension)
{
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_COMMAND UsbCmd;
    EHCI_INTERRUPT_ENABLE IntrEn;

    DPRINT_EHCI("EHCI_DisableAsyncList: disabling async schedule\n");

    OperationalRegs = EhciExtension->OperationalRegs;

    UsbCmd.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    UsbCmd.AsynchronousEnable = 0;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, UsbCmd.AsULONG);
    DPRINT_EHCI("EHCI_DisableAsyncList: HcCommand=0x%08lx\n", UsbCmd.AsULONG);

    /* While idling async, drop IOAA to reduce interrupts; PortChange stays on */
    IntrEn.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcInterruptEnable.AsULONG);
    IntrEn.InterruptOnAsyncAdvance = 0;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcInterruptEnable.AsULONG, IntrEn.AsULONG);
}

VOID
NTAPI
EHCI_EnablePeriodicList(IN PEHCI_EXTENSION EhciExtension)
{
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_COMMAND Command;

    DPRINT_EHCI("EHCI_EnablePeriodicList: enabling periodic schedule\n");

    OperationalRegs = EhciExtension->OperationalRegs;

    Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    Command.PeriodicEnable = 1;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);
    DPRINT_EHCI("EHCI_EnablePeriodicList: HcCommand=0x%08lx\n", Command.AsULONG);
}

VOID
NTAPI
EHCI_FlushAsyncCache(IN PEHCI_EXTENSION EhciExtension)
{
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_COMMAND Command;
    EHCI_USB_STATUS Status;
    LARGE_INTEGER CurrentTime;
    LARGE_INTEGER EndTime;
    EHCI_USB_COMMAND Cmd;

    DPRINT_EHCI("EHCI_FlushAsyncCache: EhciExtension=%p\n", EhciExtension);

    OperationalRegs = EhciExtension->OperationalRegs;
    Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    Status.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);

    if (!Status.AsynchronousStatus && !Command.AsynchronousEnable)
    {
        DPRINT_EHCI("EHCI_FlushAsyncCache: nothing to flush (AsyncEnable=0, AsyncStatus=0)\n");
        return;
    }

    if (Status.AsynchronousStatus && !Command.AsynchronousEnable)
    {
        KeQuerySystemTime(&EndTime);
        EndTime.QuadPart += 100 * 10000;  //100 ms

        do
        {
            Status.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);
            Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
            KeQuerySystemTime(&CurrentTime);

            if (CurrentTime.QuadPart > EndTime.QuadPart)
                RegPacket.UsbPortBugCheck(EhciExtension);
        }
        while (Status.AsynchronousStatus && Command.AsULONG != -1 && Command.Run);

        DPRINT_EHCI("EHCI_FlushAsyncCache: waited for async when disabled\n");
        return;
    }

    if (!Status.AsynchronousStatus && Command.AsynchronousEnable)
    {
        KeQuerySystemTime(&EndTime);
        EndTime.QuadPart += 100 * 10000;  //100 ms

        do
        {
            Status.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);
            Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
            KeQuerySystemTime(&CurrentTime);
        }
        while (!Status.AsynchronousStatus && Command.AsULONG != -1 && Command.Run);
        DPRINT_EHCI("EHCI_FlushAsyncCache: async engine active\n");
    }

    Command.InterruptAdvanceDoorbell = 1;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);
    DPRINT_EHCI("EHCI_FlushAsyncCache: doorbell rung\n");

    KeQuerySystemTime(&EndTime);
    EndTime.QuadPart += 100 * 10000;  //100 ms

    Cmd.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);

    if (Cmd.InterruptAdvanceDoorbell)
    {
        while (Cmd.Run)
        {
            if (Cmd.AsULONG == (ULONG)-1)
                break;

            KeStallExecutionProcessor(1);
            Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
            KeQuerySystemTime(&CurrentTime);

            if (!Command.InterruptAdvanceDoorbell)
                break;

            if (CurrentTime.QuadPart > EndTime.QuadPart)
            {
                DPRINT_EHCI("EHCI_FlushAsyncCache: doorbell timeout, Cmd=0x%08lx\n",
                        Command.AsULONG);
                break;
            }

            Cmd = Command;
        }
    }

    /* InterruptOnAsyncAdvance */
    WRITE_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG, 0x20);
    DPRINT_EHCI("EHCI_FlushAsyncCache: InterruptOnAsyncAdvance acked\n");
}

VOID
NTAPI
EHCI_LockQH(IN PEHCI_EXTENSION EhciExtension,
            IN PEHCI_HCD_QH QH,
            IN ULONG TransferType)
{
    PEHCI_HCD_QH PrevQH;
    PEHCI_HCD_QH NextQH;
    ULONG QhPA;
    ULONG FrameIndexReg;
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_COMMAND Command;

    DPRINT_EHCI("EHCI_LockQH: QH=%p TransferType=%u\n",
                QH,
                TransferType);

    OperationalRegs = EhciExtension->OperationalRegs;

    ASSERT((QH->sqh.QhFlags & EHCI_QH_FLAG_UPDATING) == 0);
    ASSERT(EhciExtension->LockQH == NULL);

    PrevQH = QH->sqh.PrevHead;
    QH->sqh.QhFlags |= EHCI_QH_FLAG_UPDATING;

    ASSERT(PrevQH);

    NextQH = QH->sqh.NextHead;

    EhciExtension->PrevQH = PrevQH;
    EhciExtension->NextQH = NextQH;
    EhciExtension->LockQH = QH;

    if (NextQH)
    {
        QhPA = NextQH->sqh.PhysicalAddress;
        QhPA &= LINK_POINTER_MASK + TERMINATE_POINTER;
        QhPA |= (EHCI_LINK_TYPE_QH << 1);
    }
    else
    {
        QhPA = TERMINATE_POINTER;
    }

    PrevQH->sqh.HwQH.HorizontalLink.AsULONG = QhPA;

    FrameIndexReg = READ_REGISTER_ULONG(&OperationalRegs->FrameIndex);

    if (TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
    {
        do
        {
            Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
        }
        while (READ_REGISTER_ULONG(&OperationalRegs->FrameIndex) ==
               FrameIndexReg && (Command.AsULONG != -1) && Command.Run);
    }
    else
    {
        EHCI_FlushAsyncCache(EhciExtension);
    }
}

VOID
NTAPI
EHCI_UnlockQH(IN PEHCI_EXTENSION EhciExtension,
              IN PEHCI_HCD_QH QH)
{
    ULONG QhPA;

    DPRINT_EHCI("EHCI_UnlockQH: QH=%p\n", QH);

    ASSERT(QH->sqh.QhFlags & EHCI_QH_FLAG_UPDATING);
    ASSERT(EhciExtension->LockQH);
    ASSERT(EhciExtension->LockQH == QH);

    QH->sqh.QhFlags &= ~EHCI_QH_FLAG_UPDATING;

    EhciExtension->LockQH = NULL;

    QhPA = QH->sqh.PhysicalAddress;
    QhPA &= LINK_POINTER_MASK + TERMINATE_POINTER;
    QhPA |= (EHCI_LINK_TYPE_QH << 1);

    EhciExtension->PrevQH->sqh.HwQH.HorizontalLink.AsULONG = QhPA;
    DPRINT_EHCI("EHCI_UnlockQH: PrevQH=%p NewLink=0x%08lx\n",
                EhciExtension->PrevQH,
                QhPA);
}

VOID
NTAPI
EHCI_LinkTransferToQueue(IN PEHCI_EXTENSION EhciExtension,
                         IN PEHCI_ENDPOINT EhciEndpoint,
                         IN PEHCI_HCD_TD NextTD)
{
    PEHCI_HCD_QH QH;
    PEHCI_HCD_TD TD;
    PEHCI_TRANSFER Transfer;
    PEHCI_HCD_TD LinkTD;
    BOOLEAN IsPresent;
    ULONG ix;

    DPRINT_EHCI("EHCI_LinkTransferToQueue: EP=%p NextTD=%p\n",
                EhciEndpoint,
                NextTD);

    ASSERT(EhciEndpoint->HcdHeadP != NULL);
    IsPresent = EHCI_HardwarePresent(EhciExtension, 0);

    QH = EhciEndpoint->QH;
    TD = EhciEndpoint->HcdHeadP;

    if (TD == EhciEndpoint->HcdTailP)
    {
        if (IsPresent)
        {
            EHCI_LockQH(EhciExtension,
                        QH,
                        EhciEndpoint->EndpointProperties.TransferType);
        }

        QH->sqh.HwQH.CurrentTD = EhciEndpoint->DmaBufferPA;
        QH->sqh.HwQH.NextTD = NextTD->PhysicalAddress;
        QH->sqh.HwQH.AlternateNextTD = NextTD->HwTD.AlternateNextTD;

        QH->sqh.HwQH.Token.Status = (UCHAR)~(EHCI_TOKEN_STATUS_ACTIVE |
                                             EHCI_TOKEN_STATUS_HALTED);

        QH->sqh.HwQH.Token.TransferBytes = 0;

        if (IsPresent)
            EHCI_UnlockQH(EhciExtension, QH);

        EhciEndpoint->HcdHeadP = NextTD;
        DPRINT_EHCI("EHCI_LinkTransferToQueue: Primed QH=%p NextTD=0x%08lx AltNext=0x%08lx\n",
                    QH,
                    QH->sqh.HwQH.NextTD,
                    QH->sqh.HwQH.AlternateNextTD);
    }
    else
    {
        DPRINT_EHCI("EHCI_LinkTransferToQueue: TD=%p HcdTailP=%p\n",
                    EhciEndpoint->HcdHeadP,
                    EhciEndpoint->HcdTailP);

        LinkTD = EhciEndpoint->HcdHeadP;

        while (TD != EhciEndpoint->HcdTailP)
        {
            LinkTD = TD;
            TD = TD->NextHcdTD;
        }

        ASSERT(LinkTD != EhciEndpoint->HcdTailP);

        Transfer = LinkTD->EhciTransfer;

        TD = EhciEndpoint->FirstTD;

        for (ix = 0; ix < EhciEndpoint->MaxTDs; ix++)
        {
            if (TD->EhciTransfer == Transfer)
            {
                TD->AltNextHcdTD = NextTD;
                TD->HwTD.AlternateNextTD = NextTD->PhysicalAddress;
            }

            TD += 1;
        }

        LinkTD->HwTD.NextTD = NextTD->PhysicalAddress;
        LinkTD->NextHcdTD = NextTD;

        if (QH->sqh.HwQH.CurrentTD == LinkTD->PhysicalAddress)
        {
            QH->sqh.HwQH.NextTD = NextTD->PhysicalAddress;
            QH->sqh.HwQH.AlternateNextTD = TERMINATE_POINTER;
        }

        DPRINT_EHCI("EHCI_LinkTransferToQueue: Linked NextTD=0x%08lx at LinkTD=%p\n",
                    NextTD->PhysicalAddress,
                    LinkTD);
    }
}

MPSTATUS
NTAPI
EHCI_ControlTransfer(IN PEHCI_EXTENSION EhciExtension,
                     IN PEHCI_ENDPOINT EhciEndpoint,
                     IN PUSBPORT_TRANSFER_PARAMETERS TransferParameters,
                     IN PEHCI_TRANSFER EhciTransfer,
                     IN PUSBPORT_SCATTER_GATHER_LIST SgList)
{
    PEHCI_HCD_TD FirstTD;
    PEHCI_HCD_TD LastTD;
    PEHCI_HCD_TD TD;
    PEHCI_HCD_TD PrevTD;
    PEHCI_HCD_TD LinkTD;
    ULONG TransferedLen = 0;
    EHCI_TD_TOKEN Token;
    ULONG DataToggle = 1;

    DPRINT_EHCI("EHCI_ControlTransfer: EP=%p Xfer=%p Len=%lu Flags=0x%lx\n",
                EhciEndpoint,
                EhciTransfer,
                TransferParameters ? TransferParameters->TransferBufferLength : 0,
                TransferParameters ? TransferParameters->TransferFlags : 0);

    if (EhciEndpoint->RemainTDs < EHCI_MAX_CONTROL_TD_COUNT)
        return MP_STATUS_FAILURE;

#if DBG
    EHCI_DumpScatterGatherList("EHCI_ControlTransfer",
                               TransferParameters,
                               SgList);
    EHCI_DumpSetupPacket(&TransferParameters->SetupPacket);
#endif

    EhciExtension->PendingTransfers++;
    EhciEndpoint->PendingTDs++;

    EhciTransfer->TransferOnAsyncList = 1;

    FirstTD = EHCI_AllocTd(EhciExtension, EhciEndpoint);

    if (!FirstTD)
    {
        RegPacket.UsbPortBugCheck(EhciExtension);
        return MP_STATUS_FAILURE;
    }

    EhciTransfer->PendingTDs++;

    FirstTD->TdFlags |= EHCI_HCD_TD_FLAG_PROCESSED;
    FirstTD->EhciTransfer = EhciTransfer;

    FirstTD->HwTD.Buffer[0] = FirstTD->PhysicalAddress + FIELD_OFFSET(EHCI_HCD_TD, SetupPacket);
    FirstTD->HwTD.Buffer[1] = 0;
    FirstTD->HwTD.Buffer[2] = 0;
    FirstTD->HwTD.Buffer[3] = 0;
    FirstTD->HwTD.Buffer[4] = 0;

    FirstTD->NextHcdTD = NULL;

    FirstTD->HwTD.NextTD = TERMINATE_POINTER;
    FirstTD->HwTD.AlternateNextTD = TERMINATE_POINTER;

    FirstTD->HwTD.Token.AsULONG = 0;
    FirstTD->HwTD.Token.ErrorCounter = 3;
    FirstTD->HwTD.Token.PIDCode = EHCI_TD_TOKEN_PID_SETUP;
    FirstTD->HwTD.Token.Status = (UCHAR)EHCI_TOKEN_STATUS_ACTIVE;
    FirstTD->HwTD.Token.TransferBytes = sizeof(FirstTD->SetupPacket);

    RtlCopyMemory(&FirstTD->SetupPacket,
                  &TransferParameters->SetupPacket,
                  sizeof(FirstTD->SetupPacket));
    DPRINT_EHCI("EHCI_ControlTransfer: SETUP TD=%p Buf0=0x%08lx Bytes=%lu\n",
                FirstTD,
                FirstTD->HwTD.Buffer[0],
                (ULONG)sizeof(FirstTD->SetupPacket));

    LastTD = EHCI_AllocTd(EhciExtension, EhciEndpoint);

    if (!LastTD)
    {
        RegPacket.UsbPortBugCheck(EhciExtension);
        return MP_STATUS_FAILURE;
    }

    EhciTransfer->PendingTDs++;

    LastTD->TdFlags |= EHCI_HCD_TD_FLAG_PROCESSED;
    LastTD->EhciTransfer = EhciTransfer;

    LastTD->HwTD.Buffer[0] = 0;
    LastTD->HwTD.Buffer[1] = 0;
    LastTD->HwTD.Buffer[2] = 0;
    LastTD->HwTD.Buffer[3] = 0;
    LastTD->HwTD.Buffer[4] = 0;

    LastTD->NextHcdTD = NULL;
    LastTD->HwTD.NextTD = TERMINATE_POINTER;
    LastTD->HwTD.AlternateNextTD = TERMINATE_POINTER;

    LastTD->HwTD.Token.AsULONG = 0;
    LastTD->HwTD.Token.ErrorCounter = 3;

    FirstTD->AltNextHcdTD = LastTD;
    FirstTD->HwTD.AlternateNextTD = LastTD->PhysicalAddress;

    PrevTD = FirstTD;
    LinkTD = FirstTD;

    while (TransferedLen < TransferParameters->TransferBufferLength)
    {
        TD = EHCI_AllocTd(EhciExtension, EhciEndpoint);

        if (!TD)
        {
            RegPacket.UsbPortBugCheck(EhciExtension);
            return MP_STATUS_FAILURE;
        }

        LinkTD = TD;

        EhciTransfer->PendingTDs++;

        TD->TdFlags |= EHCI_HCD_TD_FLAG_PROCESSED;
        TD->EhciTransfer = EhciTransfer;

        TD->HwTD.Buffer[0] = 0;
        TD->HwTD.Buffer[1] = 0;
        TD->HwTD.Buffer[2] = 0;
        TD->HwTD.Buffer[3] = 0;
        TD->HwTD.Buffer[4] = 0;

        TD->NextHcdTD = NULL;

        TD->HwTD.NextTD = TERMINATE_POINTER;
        TD->HwTD.AlternateNextTD = TERMINATE_POINTER;

        TD->HwTD.Token.AsULONG = 0;
        TD->HwTD.Token.ErrorCounter = 3;

        PrevTD->NextHcdTD = TD;
        PrevTD->HwTD.NextTD = TD->PhysicalAddress;

        if (TransferParameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
            TD->HwTD.Token.PIDCode = EHCI_TD_TOKEN_PID_IN;
        else
            TD->HwTD.Token.PIDCode = EHCI_TD_TOKEN_PID_OUT;

        TD->HwTD.Token.DataToggle = DataToggle;
        TD->HwTD.Token.Status = (UCHAR)EHCI_TOKEN_STATUS_ACTIVE;

        if (DataToggle)
            TD->HwTD.Token.DataToggle = 1;
        else
            TD->HwTD.Token.DataToggle = 0;

        TD->AltNextHcdTD = LastTD;
        TD->HwTD.AlternateNextTD = LastTD->PhysicalAddress;

        TransferedLen = EHCI_MapAsyncTransferToTd(EhciExtension,
                                                  EhciEndpoint->EndpointProperties.MaxPacketSize,
                                                  TransferedLen,
                                                  &DataToggle,
                                                  EhciTransfer,
                                                  TD,
                                                  SgList);

        DPRINT_EHCI("EHCI_ControlTransfer: DATA TD=%p PID=%u Toggle=%u\n",
                    TD,
                    TD->HwTD.Token.PIDCode,
                    TD->HwTD.Token.DataToggle);

        PrevTD = TD;
    }

    LinkTD->NextHcdTD = LastTD;
    LinkTD->HwTD.NextTD = LastTD->PhysicalAddress;

    LastTD->HwTD.Buffer[0] = 0;
    LastTD->LengthThisTD = 0;

    Token.AsULONG = 0;
    Token.Status = (UCHAR)EHCI_TOKEN_STATUS_ACTIVE;
    Token.InterruptOnComplete = 1;
    Token.DataToggle = 1;

    if (TransferParameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
        Token.PIDCode = EHCI_TD_TOKEN_PID_OUT;
    else
        Token.PIDCode = EHCI_TD_TOKEN_PID_IN;

    LastTD->HwTD.Token = Token;
    DPRINT_EHCI("EHCI_ControlTransfer: STATUS TD=%p PID=%u\n",
                LastTD,
                LastTD->HwTD.Token.PIDCode);

    LastTD->NextHcdTD = EhciEndpoint->HcdTailP;
    LastTD->HwTD.NextTD = EhciEndpoint->HcdTailP->PhysicalAddress;

    EHCI_EnableAsyncList(EhciExtension);
    EHCI_LinkTransferToQueue(EhciExtension, EhciEndpoint, FirstTD);

    ASSERT(EhciEndpoint->HcdTailP->NextHcdTD == NULL);
    ASSERT(EhciEndpoint->HcdTailP->AltNextHcdTD == NULL);

    DPRINT_EHCI("EHCI_ControlTransfer: queued EP=%p FirstTD=%p LastTD=%p\n",
                EhciEndpoint,
                FirstTD,
                LastTD);
    return MP_STATUS_SUCCESS;
}

MPSTATUS
NTAPI
EHCI_BulkTransfer(IN PEHCI_EXTENSION EhciExtension,
                  IN PEHCI_ENDPOINT EhciEndpoint,
                  IN PUSBPORT_TRANSFER_PARAMETERS TransferParameters,
                  IN PEHCI_TRANSFER EhciTransfer,
                  IN PUSBPORT_SCATTER_GATHER_LIST SgList)
{
    PEHCI_HCD_TD PrevTD;
    PEHCI_HCD_TD FirstTD = NULL;
    PEHCI_HCD_TD TD;
    ULONG TransferedLen;
    ULONG DataToggle;

    DPRINT_EHCI("EHCI_BulkTransfer: EP=%p Xfer=%p Len=%lu Flags=0x%lx\n",
                EhciEndpoint,
                EhciTransfer,
                TransferParameters ? TransferParameters->TransferBufferLength : 0,
                TransferParameters ? TransferParameters->TransferFlags : 0);

    if (((TransferParameters->TransferBufferLength /
        ((EHCI_MAX_QTD_BUFFER_PAGES - 1) * PAGE_SIZE)) + 1) > EhciEndpoint->RemainTDs)
    {
        DPRINT_EHCI("EHCI_BulkTransfer: return MP_STATUS_FAILURE\n");
        return MP_STATUS_FAILURE;
    }

    EhciExtension->PendingTransfers++;
    EhciEndpoint->PendingTDs++;

    EhciTransfer->TransferOnAsyncList = 1;

    TransferedLen = 0;
    PrevTD = NULL;
    DataToggle = EhciEndpoint->NextDataToggle & 1;

    if (TransferParameters->TransferBufferLength)
    {
#if DBG
        EHCI_DumpScatterGatherList("EHCI_BulkTransfer",
                                   TransferParameters,
                                   SgList);
#endif
        while (TransferedLen < TransferParameters->TransferBufferLength)
        {
            TD = EHCI_AllocTd(EhciExtension, EhciEndpoint);

            if (!TD)
            {
                RegPacket.UsbPortBugCheck(EhciExtension);
                return MP_STATUS_FAILURE;
            }

            EhciTransfer->PendingTDs++;

            TD->TdFlags |= EHCI_HCD_TD_FLAG_PROCESSED;
            TD->EhciTransfer = EhciTransfer;

            TD->HwTD.Buffer[0] = 0;
            TD->HwTD.Buffer[1] = 0;
            TD->HwTD.Buffer[2] = 0;
            TD->HwTD.Buffer[3] = 0;
            TD->HwTD.Buffer[4] = 0;

            TD->NextHcdTD = NULL;
            TD->HwTD.NextTD = TERMINATE_POINTER;
            TD->HwTD.AlternateNextTD = TERMINATE_POINTER;

            TD->HwTD.Token.AsULONG = 0;
            TD->HwTD.Token.ErrorCounter = 3;

            if (EhciTransfer->PendingTDs == 1)
            {
                FirstTD = TD;
            }
            else
            {
                PrevTD->HwTD.NextTD = TD->PhysicalAddress;
                PrevTD->NextHcdTD = TD;
            }

            TD->HwTD.AlternateNextTD = EhciEndpoint->HcdTailP->PhysicalAddress;
            TD->AltNextHcdTD = EhciEndpoint->HcdTailP;

            TD->HwTD.Token.InterruptOnComplete = 1;

            if (TransferParameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
                TD->HwTD.Token.PIDCode = EHCI_TD_TOKEN_PID_IN;
            else
                TD->HwTD.Token.PIDCode = EHCI_TD_TOKEN_PID_OUT;

            TD->HwTD.Token.Status = (UCHAR)EHCI_TOKEN_STATUS_ACTIVE;
            TD->HwTD.Token.DataToggle = (UCHAR)DataToggle;

            TransferedLen = EHCI_MapAsyncTransferToTd(EhciExtension,
                                                      EhciEndpoint->EndpointProperties.MaxPacketSize,
                                                      TransferedLen,
                                                      &DataToggle,
                                                      EhciTransfer,
                                                      TD,
                                                      SgList);

            DPRINT_EHCI("EHCI_BulkTransfer: DATA TD=%p PID=%u Toggle=%u LenThis=%lu Buf0=0x%08lx\n",
                        TD,
                        TD->HwTD.Token.PIDCode,
                        TD->HwTD.Token.DataToggle,
                        TD->LengthThisTD,
                        TD->HwTD.Buffer[0]);

            PrevTD = TD;
        }
    }
    else
    {
        TD = EHCI_AllocTd(EhciExtension, EhciEndpoint);

        if (!TD)
        {
            RegPacket.UsbPortBugCheck(EhciExtension);
            return MP_STATUS_FAILURE;
        }

        EhciTransfer->PendingTDs++;

        TD->TdFlags |= EHCI_HCD_TD_FLAG_PROCESSED;
        TD->EhciTransfer = EhciTransfer;

        TD->HwTD.Buffer[0] = 0;
        TD->HwTD.Buffer[1] = 0;
        TD->HwTD.Buffer[2] = 0;
        TD->HwTD.Buffer[3] = 0;
        TD->HwTD.Buffer[4] = 0;

        TD->HwTD.NextTD = TERMINATE_POINTER;
        TD->HwTD.AlternateNextTD = TERMINATE_POINTER;

        TD->HwTD.Token.AsULONG = 0;
        TD->HwTD.Token.ErrorCounter = 3;

        TD->NextHcdTD = NULL;

        ASSERT(EhciTransfer->PendingTDs == 1);

        FirstTD = TD;

        TD->HwTD.AlternateNextTD = EhciEndpoint->HcdTailP->PhysicalAddress;
        TD->AltNextHcdTD = EhciEndpoint->HcdTailP;

        TD->HwTD.Token.InterruptOnComplete = 1;

        if (TransferParameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
            TD->HwTD.Token.PIDCode = EHCI_TD_TOKEN_PID_IN;
        else
            TD->HwTD.Token.PIDCode = EHCI_TD_TOKEN_PID_OUT;

        TD->HwTD.Buffer[0] = TD->PhysicalAddress;

        TD->HwTD.Token.Status = (UCHAR)EHCI_TOKEN_STATUS_ACTIVE;
        TD->HwTD.Token.DataToggle = (UCHAR)DataToggle;

        TD->LengthThisTD = 0;
        DPRINT_EHCI("EHCI_BulkTransfer: zero-length TD=%p PID=%u\n",
                    TD,
                    TD->HwTD.Token.PIDCode);
    }

    TD->HwTD.NextTD = EhciEndpoint->HcdTailP->PhysicalAddress;
    TD->NextHcdTD = EhciEndpoint->HcdTailP;

    EhciEndpoint->NextDataToggle = DataToggle & 1;

    EHCI_EnableAsyncList(EhciExtension);
    EHCI_LinkTransferToQueue(EhciExtension, EhciEndpoint, FirstTD);

    ASSERT(EhciEndpoint->HcdTailP->NextHcdTD == 0);
    ASSERT(EhciEndpoint->HcdTailP->AltNextHcdTD == 0);

    DPRINT_EHCI("EHCI_BulkTransfer: queued EP=%p FirstTD=%p TD=%p\n",
                EhciEndpoint,
                FirstTD,
                TD);
    return MP_STATUS_SUCCESS;
}

MPSTATUS
NTAPI
EHCI_InterruptTransfer(IN PEHCI_EXTENSION EhciExtension,
                       IN PEHCI_ENDPOINT EhciEndpoint,
                       IN PUSBPORT_TRANSFER_PARAMETERS TransferParameters,
                       IN PEHCI_TRANSFER EhciTransfer,
                       IN PUSBPORT_SCATTER_GATHER_LIST SgList)
{
    PEHCI_HCD_TD TD;
    PEHCI_HCD_TD FirstTD = NULL;
    PEHCI_HCD_TD PrevTD = NULL;
    ULONG TransferedLen = 0;
    ULONG DataToggle;

    DPRINT_EHCI("EHCI_InterruptTransfer: EP=%p Xfer=%p Len=%lu Flags=0x%lx\n",
                EhciEndpoint,
                EhciTransfer,
                TransferParameters ? TransferParameters->TransferBufferLength : 0,
                TransferParameters ? TransferParameters->TransferFlags : 0);

    if (!EhciEndpoint->RemainTDs)
    {
        DPRINT_EHCI("EHCI_InterruptTransfer: EhciEndpoint - %p\n", EhciEndpoint);
        DbgBreakPoint();
        return MP_STATUS_FAILURE;
    }

    EhciEndpoint->PendingTDs++;

    if (!TransferParameters->TransferBufferLength)
    {
        DPRINT_EHCI("EHCI_InterruptTransfer: EhciEndpoint - %p\n", EhciEndpoint);
        DbgBreakPoint();
        return MP_STATUS_FAILURE;
    }

    DataToggle = EhciEndpoint->NextDataToggle & 1;

    while (TransferedLen < TransferParameters->TransferBufferLength)
    {
#if DBG
        if (TransferedLen == 0)
        {
            EHCI_DumpScatterGatherList("EHCI_InterruptTransfer",
                                       TransferParameters,
                                       SgList);
        }
#endif
        TD = EHCI_AllocTd(EhciExtension, EhciEndpoint);

        if (!TD)
        {
            DPRINT_EHCI("EHCI_InterruptTransfer: EhciEndpoint - %p\n", EhciEndpoint);
            RegPacket.UsbPortBugCheck(EhciExtension);
            return MP_STATUS_FAILURE;
        }

        EhciTransfer->PendingTDs++;

        TD->TdFlags |= EHCI_HCD_TD_FLAG_PROCESSED;
        TD->EhciTransfer = EhciTransfer;

        TD->HwTD.Buffer[0] = 0;
        TD->HwTD.Buffer[1] = 0;
        TD->HwTD.Buffer[2] = 0;
        TD->HwTD.Buffer[3] = 0;
        TD->HwTD.Buffer[4] = 0;

        TD->HwTD.NextTD = TERMINATE_POINTER;
        TD->HwTD.AlternateNextTD = TERMINATE_POINTER;

        TD->HwTD.Token.AsULONG = 0;
        TD->HwTD.Token.ErrorCounter = 3;

        TD->NextHcdTD = NULL;

        if (EhciTransfer->PendingTDs == 1)
        {
            FirstTD = TD;
        }
        else if (PrevTD)
        {
            PrevTD->HwTD.NextTD = TD->PhysicalAddress;
            PrevTD->NextHcdTD = TD;
        }

        if (TransferParameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN)
            TD->HwTD.Token.PIDCode = EHCI_TD_TOKEN_PID_IN;
        else
            TD->HwTD.Token.PIDCode = EHCI_TD_TOKEN_PID_OUT;

        TD->HwTD.Token.Status = (UCHAR)EHCI_TOKEN_STATUS_ACTIVE;
        TD->HwTD.Token.DataToggle = (UCHAR)DataToggle;

        TransferedLen = EHCI_MapAsyncTransferToTd(EhciExtension,
                                                  EhciEndpoint->EndpointProperties.TotalMaxPacketSize,
                                                  TransferedLen,
                                                  &DataToggle,
                                                  EhciTransfer,
                                                  TD,
                                                  SgList);

        DPRINT_EHCI("EHCI_InterruptTransfer: DATA TD=%p PID=%u Toggle=%u LenThis=%lu Buf0=0x%08lx\n",
                    TD,
                    TD->HwTD.Token.PIDCode,
                    TD->HwTD.Token.DataToggle,
                    TD->LengthThisTD,
                    TD->HwTD.Buffer[0]);

        PrevTD = TD;
    }

    TD->HwTD.Token.InterruptOnComplete = 1;

    EhciEndpoint->NextDataToggle = DataToggle & 1;

    DPRINT_EHCI("EHCI_InterruptTransfer: PendingTDs - %x, TD->PhysicalAddress - %p, FirstTD - %p\n",
                EhciTransfer->PendingTDs,
                TD->PhysicalAddress,
                FirstTD);

    TD->HwTD.NextTD = EhciEndpoint->HcdTailP->PhysicalAddress;
    TD->NextHcdTD = EhciEndpoint->HcdTailP;

    EHCI_LinkTransferToQueue(EhciExtension, EhciEndpoint, FirstTD);
    DPRINT_EHCI("EHCI_InterruptTransfer: queued EP=%p FirstTD=%p TD=%p\n",
                EhciEndpoint,
                FirstTD,
                TD);

    ASSERT(EhciEndpoint->HcdTailP->NextHcdTD == NULL);
    ASSERT(EhciEndpoint->HcdTailP->AltNextHcdTD == NULL);

    EHCI_EnablePeriodicList(EhciExtension);

    return MP_STATUS_SUCCESS;
}

MPSTATUS
NTAPI
EHCI_SubmitTransfer(IN PVOID ehciExtension,
                    IN PVOID ehciEndpoint,
                    IN PUSBPORT_TRANSFER_PARAMETERS TransferParameters,
                    IN PVOID ehciTransfer,
                    IN PUSBPORT_SCATTER_GATHER_LIST SgList)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_ENDPOINT EhciEndpoint = ehciEndpoint;
    PEHCI_TRANSFER EhciTransfer = ehciTransfer;
    MPSTATUS MPStatus;

    DPRINT_EHCI("EHCI_SubmitTransfer: EhciEndpoint - %p, EhciTransfer - %p\n",
                EhciEndpoint,
                EhciTransfer);

    RtlZeroMemory(EhciTransfer, sizeof(EHCI_TRANSFER));

    EhciTransfer->TransferParameters = TransferParameters;
    EhciTransfer->USBDStatus = USBD_STATUS_SUCCESS;
    EhciTransfer->EhciEndpoint = EhciEndpoint;

    switch (EhciEndpoint->EndpointProperties.TransferType)
    {
        case USBPORT_TRANSFER_TYPE_CONTROL:
            MPStatus = EHCI_ControlTransfer(EhciExtension,
                                            EhciEndpoint,
                                            TransferParameters,
                                            EhciTransfer,
                                            SgList);
            break;

        case USBPORT_TRANSFER_TYPE_BULK:
            MPStatus = EHCI_BulkTransfer(EhciExtension,
                                         EhciEndpoint,
                                         TransferParameters,
                                         EhciTransfer,
                                         SgList);
            break;

        case USBPORT_TRANSFER_TYPE_INTERRUPT:
            MPStatus = EHCI_InterruptTransfer(EhciExtension,
                                              EhciEndpoint,
                                              TransferParameters,
                                              EhciTransfer,
                                              SgList);
            break;

        default:
            DbgBreakPoint();
            MPStatus = MP_STATUS_NOT_SUPPORTED;
            break;
    }

    return MPStatus;
}

MPSTATUS
NTAPI
EHCI_SubmitIsoTransfer(IN PVOID ehciExtension,
                       IN PVOID ehciEndpoint,
                       IN PUSBPORT_TRANSFER_PARAMETERS TransferParameters,
                       IN PVOID ehciTransfer,
                       IN PVOID isoParameters)
{
    UNREFERENCED_PARAMETER(ehciExtension);
    UNREFERENCED_PARAMETER(ehciEndpoint);
    UNREFERENCED_PARAMETER(TransferParameters);
    UNREFERENCED_PARAMETER(ehciTransfer);
    UNREFERENCED_PARAMETER(isoParameters);

    DPRINT_EHCI("EHCI_SubmitIsoTransfer: not supported (ISO transfers not implemented for EHCI)\n");
    return MP_STATUS_NOT_SUPPORTED;
}

VOID
NTAPI
EHCI_AbortIsoTransfer(IN PEHCI_EXTENSION EhciExtension,
                      IN PEHCI_ENDPOINT EhciEndpoint,
                      IN PEHCI_TRANSFER EhciTransfer)
{
    DPRINT_EHCI("EHCI_AbortIsoTransfer: UNIMPLEMENTED. FIXME\n");
}

VOID
NTAPI
EHCI_AbortAsyncTransfer(IN PEHCI_EXTENSION EhciExtension,
                        IN PEHCI_ENDPOINT EhciEndpoint,
                        IN PEHCI_TRANSFER EhciTransfer)
{
    PEHCI_HCD_QH QH;
    PEHCI_HCD_TD TD;
    ULONG TransferLength;
    PEHCI_HCD_TD CurrentTD;
    PEHCI_TRANSFER CurrentTransfer;
    ULONG FirstTdPA;
    PEHCI_HCD_TD LastTD;
    PEHCI_HCD_TD PrevTD = NULL;
    ULONG NextTD;

    DPRINT("EHCI_AbortAsyncTransfer: EhciEndpoint - %p, EhciTransfer - %p\n",
           EhciEndpoint,
           EhciTransfer);

    QH = EhciEndpoint->QH;
    TD = EhciEndpoint->HcdHeadP;

    ASSERT(EhciEndpoint->PendingTDs);
    EhciEndpoint->PendingTDs--;

    if (TD->EhciTransfer == EhciTransfer)
    {
        TransferLength = 0;

        while (TD != EhciEndpoint->HcdTailP &&
               TD->EhciTransfer == EhciTransfer)
        {
            TransferLength += TD->LengthThisTD - TD->HwTD.Token.TransferBytes;

            TD->HwTD.NextTD = 0;
            TD->HwTD.AlternateNextTD = 0;

            TD->TdFlags = 0;
            TD->EhciTransfer = NULL;

            EhciEndpoint->RemainTDs++;

            TD = TD->NextHcdTD;
        }

        if (TransferLength)
            EhciTransfer->TransferLen += TransferLength;

        QH->sqh.HwQH.CurrentTD = EhciEndpoint->DmaBufferPA;
        QH->sqh.HwQH.NextTD = TD->PhysicalAddress;
        QH->sqh.HwQH.AlternateNextTD = TD->HwTD.AlternateNextTD;

        QH->sqh.HwQH.Token.TransferBytes = 0;
        QH->sqh.HwQH.Token.Status = (UCHAR)~(EHCI_TOKEN_STATUS_ACTIVE |
                                             EHCI_TOKEN_STATUS_HALTED);

        EhciEndpoint->HcdHeadP = TD;
    }
    else
    {
        DPRINT("EHCI_AbortAsyncTransfer: TD->EhciTransfer - %p\n", TD->EhciTransfer);

    DPRINT_EHCI("EHCI_AbortAsyncTransfer: map CurrentTD phys=%08lx\n", QH->sqh.HwQH.CurrentTD);
    CurrentTD = RegPacket.UsbPortGetMappedVirtualAddress(QH->sqh.HwQH.CurrentTD,
                                                         EhciExtension,
                                                         EhciEndpoint);

        CurrentTransfer = CurrentTD->EhciTransfer;
        TD = EhciEndpoint->HcdHeadP;

        while (TD && TD->EhciTransfer != EhciTransfer)
        {
            PrevTD = TD;
            TD = TD->NextHcdTD;
        }

        FirstTdPA = TD->PhysicalAddress;

        while (TD && TD->EhciTransfer == EhciTransfer)
        {
            TD->HwTD.NextTD = 0;
            TD->HwTD.AlternateNextTD = 0;

            TD->TdFlags = 0;
            TD->EhciTransfer = NULL;

            EhciEndpoint->RemainTDs++;

            TD = TD->NextHcdTD;
        }

        LastTD = TD;
        NextTD = LastTD->PhysicalAddress + FIELD_OFFSET(EHCI_HCD_TD, HwTD.NextTD);

        PrevTD->HwTD.NextTD = LastTD->PhysicalAddress;
        PrevTD->HwTD.AlternateNextTD = LastTD->PhysicalAddress;

        PrevTD->NextHcdTD = LastTD;
        PrevTD->AltNextHcdTD = LastTD;

        if (CurrentTransfer == EhciTransfer)
        {
            QH->sqh.HwQH.CurrentTD = EhciEndpoint->DmaBufferPA;

            QH->sqh.HwQH.Token.Status = (UCHAR)~EHCI_TOKEN_STATUS_ACTIVE;
            QH->sqh.HwQH.Token.TransferBytes = 0;

            QH->sqh.HwQH.NextTD = NextTD;
            QH->sqh.HwQH.AlternateNextTD = TERMINATE_POINTER;

            return;
        }

        if (PrevTD->EhciTransfer == CurrentTransfer)
        {
            if (QH->sqh.HwQH.NextTD == FirstTdPA)
                QH->sqh.HwQH.NextTD = NextTD;

            if (QH->sqh.HwQH.AlternateNextTD == FirstTdPA)
                QH->sqh.HwQH.AlternateNextTD = NextTD;

            for (TD = EhciEndpoint->HcdHeadP;
                 TD;
                 TD = TD->NextHcdTD)
            {
                if (TD->EhciTransfer == CurrentTransfer)
                {
                    TD->HwTD.AlternateNextTD = NextTD;
                    TD->AltNextHcdTD = LastTD;
                }
            }
        }
    }
}

VOID
NTAPI
EHCI_AbortTransfer(IN PVOID ehciExtension,
                   IN PVOID ehciEndpoint,
                   IN PVOID ehciTransfer,
                   IN PULONG CompletedLength)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_ENDPOINT EhciEndpoint = ehciEndpoint;
    PEHCI_TRANSFER EhciTransfer = ehciTransfer;
    ULONG TransferType;

    DPRINT("EHCI_AbortTransfer: EhciTransfer - %p, CompletedLength - %x\n",
           EhciTransfer,
           CompletedLength);

    TransferType = EhciEndpoint->EndpointProperties.TransferType;

    if (TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
        EHCI_AbortIsoTransfer(EhciExtension, EhciEndpoint, EhciTransfer);
    else
        EHCI_AbortAsyncTransfer(EhciExtension, EhciEndpoint, EhciTransfer);
}

ULONG
NTAPI
EHCI_GetEndpointState(IN PVOID ehciExtension,
                      IN PVOID ehciEndpoint)
{
    PEHCI_ENDPOINT EhciEndpoint = ehciEndpoint;
    UNREFERENCED_PARAMETER(ehciExtension);

    /* Report the cached state maintained by SetEndpointState */
    DPRINT("EHCI_GetEndpointState: EhciEndpoint - %p state=%lu\n",
           EhciEndpoint,
           EhciEndpoint ? EhciEndpoint->EndpointState : 0);

    return EhciEndpoint ? EhciEndpoint->EndpointState : 0;
}

VOID
NTAPI
EHCI_RemoveQhFromPeriodicList(IN PEHCI_EXTENSION EhciExtension,
                              IN PEHCI_ENDPOINT EhciEndpoint)
{
    PEHCI_HCD_QH QH;
    PEHCI_HCD_QH NextHead;
    ULONG NextQhPA;
    PEHCI_HCD_QH PrevHead;

    QH = EhciEndpoint->QH;

    if (!(QH->sqh.QhFlags & EHCI_QH_FLAG_IN_SCHEDULE))
        return;

    DPRINT("EHCI_RemoveQhFromPeriodicList: EhciEndpoint - %p, QH - %X, EhciEndpoint->StaticQH - %p\n",
           EhciEndpoint,
           QH,
           EhciEndpoint->StaticQH);

    NextHead = QH->sqh.NextHead;
    PrevHead = QH->sqh.PrevHead;

    PrevHead->sqh.NextHead = NextHead;

    if (NextHead)
    {
        if (!(NextHead->sqh.QhFlags & EHCI_QH_FLAG_STATIC))
            NextHead->sqh.PrevHead = PrevHead;

        NextQhPA = NextHead->sqh.PhysicalAddress;
        NextQhPA &= LINK_POINTER_MASK + TERMINATE_POINTER;
        NextQhPA |= (EHCI_LINK_TYPE_QH << 1);

        PrevHead->sqh.HwQH.HorizontalLink.AsULONG = NextQhPA;
    }
    else
    {
        PrevHead->sqh.HwQH.HorizontalLink.Terminate = 1;
    }

    QH->sqh.QhFlags &= ~EHCI_QH_FLAG_IN_SCHEDULE;

    QH->sqh.NextHead = NULL;
    QH->sqh.PrevHead = NULL;
}

VOID
NTAPI
EHCI_RemoveQhFromAsyncList(IN PEHCI_EXTENSION EhciExtension,
                           IN PEHCI_HCD_QH QH)
{
    PEHCI_HCD_QH NextHead;
    ULONG NextHeadPA;
    PEHCI_HCD_QH PrevHead;
    PEHCI_STATIC_QH AsyncHead;
    ULONG AsyncHeadPA;

    DPRINT("EHCI_RemoveQhFromAsyncList: QH - %p\n", QH);

    if (QH->sqh.QhFlags & EHCI_QH_FLAG_IN_SCHEDULE)
    {
        NextHead = QH->sqh.NextHead;
        PrevHead = QH->sqh.PrevHead;

        AsyncHead = EhciExtension->AsyncHead;

        AsyncHeadPA = AsyncHead->PhysicalAddress;
        AsyncHeadPA &= LINK_POINTER_MASK + TERMINATE_POINTER;
        AsyncHeadPA |= (EHCI_LINK_TYPE_QH << 1);

        NextHeadPA = NextHead->sqh.PhysicalAddress;
        NextHeadPA &= LINK_POINTER_MASK + TERMINATE_POINTER;
        NextHeadPA |= (EHCI_LINK_TYPE_QH << 1);

        PrevHead->sqh.HwQH.HorizontalLink.AsULONG = NextHeadPA;

        PrevHead->sqh.NextHead = NextHead;
        NextHead->sqh.PrevHead = PrevHead;

        EHCI_FlushAsyncCache(EhciExtension);

        if (READ_REGISTER_ULONG(&EhciExtension->OperationalRegs->AsyncListBase) ==
            QH->sqh.PhysicalAddress)
        {
            WRITE_REGISTER_ULONG(&EhciExtension->OperationalRegs->AsyncListBase,
                                 AsyncHeadPA);
        }

        QH->sqh.QhFlags &= ~EHCI_QH_FLAG_IN_SCHEDULE;
    }
}

VOID
NTAPI
EHCI_InsertQhInPeriodicList(IN PEHCI_EXTENSION EhciExtension,
                            IN PEHCI_ENDPOINT EhciEndpoint)
{
    PEHCI_STATIC_QH StaticQH;
    PEHCI_HCD_QH QH;
    ULONG QhPA;
    PEHCI_HCD_QH NextHead;
    PEHCI_HCD_QH PrevHead;

    QH = EhciEndpoint->QH;
    StaticQH = EhciEndpoint->StaticQH;

    ASSERT((QH->sqh.QhFlags & EHCI_QH_FLAG_IN_SCHEDULE) == 0);
    ASSERT(StaticQH->QhFlags & EHCI_QH_FLAG_STATIC);

    NextHead = StaticQH->NextHead;

    QH->sqh.Period = EhciEndpoint->EndpointProperties.Period;
    QH->sqh.Ordinal = EhciEndpoint->EndpointProperties.Reserved6;

    DPRINT("EHCI_InsertQhInPeriodicList: EhciEndpoint - %p, QH - %X, EhciEndpoint->StaticQH - %p\n",
           EhciEndpoint,
           QH,
           EhciEndpoint->StaticQH);

    PrevHead = (PEHCI_HCD_QH)StaticQH;

    if ((StaticQH->QhFlags & EHCI_QH_FLAG_STATIC) &&
        (!NextHead || (NextHead->sqh.QhFlags & EHCI_QH_FLAG_STATIC)))
    {
        DPRINT("EHCI_InsertQhInPeriodicList: StaticQH - %p, StaticQH->NextHead - %p\n",
               StaticQH,
               StaticQH->NextHead);
    }
    else
    {
        while (NextHead &&
               !(NextHead->sqh.QhFlags & EHCI_QH_FLAG_STATIC) &&
               QH->sqh.Ordinal > NextHead->sqh.Ordinal)
        {
            PrevHead = NextHead;
            NextHead = NextHead->sqh.NextHead;
        }
    }

    QH->sqh.NextHead = NextHead;
    QH->sqh.PrevHead = PrevHead;

    if (NextHead && !(NextHead->sqh.QhFlags & EHCI_QH_FLAG_STATIC))
        NextHead->sqh.PrevHead = QH;

    QH->sqh.QhFlags |= EHCI_QH_FLAG_IN_SCHEDULE;
    QH->sqh.HwQH.HorizontalLink = PrevHead->sqh.HwQH.HorizontalLink;

    PrevHead->sqh.NextHead = QH;

    QhPA = QH->sqh.PhysicalAddress;
    QhPA &= LINK_POINTER_MASK + TERMINATE_POINTER;
    QhPA |= (EHCI_LINK_TYPE_QH << 1);

    PrevHead->sqh.HwQH.HorizontalLink.AsULONG = QhPA;
}

VOID
NTAPI
EHCI_InsertQhInAsyncList(IN PEHCI_EXTENSION EhciExtension,
                         IN PEHCI_HCD_QH QH)
{
    PEHCI_STATIC_QH AsyncHead;
    ULONG QhPA;
    PEHCI_HCD_QH NextHead;

    DPRINT("EHCI_InsertQhInAsyncList: QH - %p\n", QH);

    ASSERT((QH->sqh.QhFlags & EHCI_QH_FLAG_IN_SCHEDULE) == 0);
    ASSERT((QH->sqh.QhFlags & EHCI_QH_FLAG_NUKED) == 0);

    AsyncHead = EhciExtension->AsyncHead;
    NextHead = AsyncHead->NextHead;

    QH->sqh.HwQH.HorizontalLink = AsyncHead->HwQH.HorizontalLink;
    QH->sqh.QhFlags |= EHCI_QH_FLAG_IN_SCHEDULE;
    QH->sqh.NextHead = NextHead;
    QH->sqh.PrevHead = (PEHCI_HCD_QH)AsyncHead;

    NextHead->sqh.PrevHead = QH;

    QhPA = QH->sqh.PhysicalAddress;
    QhPA &= LINK_POINTER_MASK + TERMINATE_POINTER;
    QhPA |= (EHCI_LINK_TYPE_QH << 1);

    AsyncHead->HwQH.HorizontalLink.AsULONG = QhPA;

    AsyncHead->NextHead = QH;
}

VOID
NTAPI
EHCI_SetIsoEndpointState(IN PEHCI_EXTENSION EhciExtension,
                         IN PEHCI_ENDPOINT EhciEndpoint,
                         IN ULONG EndpointState)
{
    DPRINT("EHCI_SetIsoEndpointState: EhciEndpoint=%p state=%lu\n", EhciEndpoint, EndpointState);
    /* Basic state book-keeping; full iTD/sITD schedule management TBD */
    EhciEndpoint->EndpointState = EndpointState;
}

VOID
NTAPI
EHCI_SetAsyncEndpointState(IN PEHCI_EXTENSION EhciExtension,
                           IN PEHCI_ENDPOINT EhciEndpoint,
                           IN ULONG EndpointState)
{
    PEHCI_HCD_QH QH;
    ULONG TransferType;

    DPRINT("EHCI_SetAsyncEndpointState: EhciEndpoint - %p, EndpointState - %x\n",
            EhciEndpoint,
            EndpointState);

    QH = EhciEndpoint->QH;

    TransferType = EhciEndpoint->EndpointProperties.TransferType;

    switch (EndpointState)
    {
        case USBPORT_ENDPOINT_PAUSED:
            if (TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
                EHCI_RemoveQhFromPeriodicList(EhciExtension, EhciEndpoint);
            else
                EHCI_RemoveQhFromAsyncList(EhciExtension, EhciEndpoint->QH);

            break;

        case USBPORT_ENDPOINT_ACTIVE:
            if (TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
                EHCI_InsertQhInPeriodicList(EhciExtension, EhciEndpoint);
            else
                EHCI_InsertQhInAsyncList(EhciExtension, EhciEndpoint->QH);

            break;

        case USBPORT_ENDPOINT_REMOVE:
            QH->sqh.QhFlags |= EHCI_QH_FLAG_CLOSED;

            if (TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
                EHCI_RemoveQhFromPeriodicList(EhciExtension, EhciEndpoint);
            else
                EHCI_RemoveQhFromAsyncList(EhciExtension, EhciEndpoint->QH);

            break;

        default:
            DbgBreakPoint();
            break;
    }

    EhciEndpoint->EndpointState = EndpointState;
}

VOID
NTAPI
EHCI_SetEndpointState(IN PVOID ehciExtension,
                      IN PVOID ehciEndpoint,
                      IN ULONG EndpointState)
{
    PEHCI_ENDPOINT EhciEndpoint;
    ULONG TransferType;

    DPRINT("EHCI_SetEndpointState: ... \n");

    EhciEndpoint = ehciEndpoint;
    TransferType = EhciEndpoint->EndpointProperties.TransferType;

    if (TransferType == USBPORT_TRANSFER_TYPE_CONTROL ||
        TransferType == USBPORT_TRANSFER_TYPE_BULK ||
        TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
    {
         EHCI_SetAsyncEndpointState((PEHCI_EXTENSION)ehciExtension,
                                    EhciEndpoint,
                                    EndpointState);
    }
    else if (TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
    {
        EHCI_SetIsoEndpointState((PEHCI_EXTENSION)ehciExtension,
                                 EhciEndpoint,
                                 EndpointState);
    }
    else
    {
        RegPacket.UsbPortBugCheck(ehciExtension);
    }
}

VOID
NTAPI
EHCI_InterruptNextSOF(IN PVOID ehciExtension)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;

    DPRINT_EHCI("EHCI_InterruptNextSOF: ... \n");

    RegPacket.UsbPortInvalidateController(EhciExtension,
                                          USBPORT_INVALIDATE_CONTROLLER_SOFT_INTERRUPT);
}

USBD_STATUS
NTAPI
EHCI_GetErrorFromTD(IN PEHCI_HCD_TD TD)
{
    EHCI_TD_TOKEN Token;

    DPRINT_EHCI("EHCI_GetErrorFromTD: ... \n");

    ASSERT(TD->HwTD.Token.Status & EHCI_TOKEN_STATUS_HALTED);

    Token = TD->HwTD.Token;

    if (Token.Status & EHCI_TOKEN_STATUS_TRANSACTION_ERROR)
    {
        DPRINT("EHCI_GetErrorFromTD: TD - %p, TRANSACTION_ERROR\n", TD);
        return USBD_STATUS_XACT_ERROR;
    }

    if (Token.Status & EHCI_TOKEN_STATUS_BABBLE_DETECTED)
    {
        DPRINT("EHCI_GetErrorFromTD: TD - %p, BABBLE_DETECTED\n", TD);
        return USBD_STATUS_BABBLE_DETECTED;
    }

    if (Token.Status & EHCI_TOKEN_STATUS_DATA_BUFFER_ERROR)
    {
        DPRINT("EHCI_GetErrorFromTD: TD - %p, DATA_BUFFER_ERROR\n", TD);
        return USBD_STATUS_DATA_BUFFER_ERROR;
    }

    if (Token.Status & EHCI_TOKEN_STATUS_MISSED_MICROFRAME)
    {
        DPRINT("EHCI_GetErrorFromTD: TD - %p, MISSED_MICROFRAME\n", TD);
        return USBD_STATUS_XACT_ERROR;
    }

    DPRINT("EHCI_GetErrorFromTD: TD - %p, STALL_PID\n", TD);
    return USBD_STATUS_STALL_PID;
}

VOID
NTAPI
EHCI_ProcessDoneAsyncTd(IN PEHCI_EXTENSION EhciExtension,
                        IN PEHCI_HCD_TD TD)
{
    PEHCI_TRANSFER EhciTransfer;
    PUSBPORT_TRANSFER_PARAMETERS TransferParameters;
    ULONG TransferType;
    PEHCI_ENDPOINT EhciEndpoint;
    ULONG LengthTransfered;
    USBD_STATUS USBDStatus;
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_COMMAND Command;

    DPRINT_EHCI("EHCI_ProcessDoneAsyncTd: TD - %p\n", TD);

    EhciTransfer = TD->EhciTransfer;

    TransferParameters = EhciTransfer->TransferParameters;
    EhciTransfer->PendingTDs--;

    EhciEndpoint = EhciTransfer->EhciEndpoint;
    TransferType = EhciEndpoint->EndpointProperties.TransferType;
#if DBG
    DPRINT_EHCI("EHCI_TD_DONE: TD=%p Token=0x%08lx Status=0x%02x PID=%u Toggle=%u RemBytes=%u LenThis=%lu Buf0=0x%08lx\n",
            TD,
            TD->HwTD.Token.AsULONG,
            TD->HwTD.Token.Status,
            TD->HwTD.Token.PIDCode,
            TD->HwTD.Token.DataToggle,
            TD->HwTD.Token.TransferBytes,
            TD->LengthThisTD,
            TD->HwTD.Buffer[0]);
#endif

    if (!(TD->TdFlags & EHCI_HCD_TD_FLAG_ACTIVE))
    {
        BOOLEAN TdHalted;
        BOOLEAN TdShort;

        TdHalted = (TD->HwTD.Token.Status & EHCI_TOKEN_STATUS_HALTED) != 0;
        TdShort = (TD->HwTD.Token.TransferBytes != 0);

        if (TdHalted)
        {
            DPRINT_EHCI("EHCI_TD_DONE: error %s (Token=0x%08lx)\n",
                    EHCI_DecodeConditionCode(TD->HwTD.Token.Status),
                    TD->HwTD.Token.AsULONG);
            USBDStatus = EHCI_GetErrorFromTD(TD);
        }
        else
        {
            USBDStatus = USBD_STATUS_SUCCESS;
        }

        LengthTransfered = TD->LengthThisTD - TD->HwTD.Token.TransferBytes;

#if DBG
        if (TD->HwTD.Token.PIDCode != EHCI_TD_TOKEN_PID_SETUP)
        {
            ULONG startPa = TD->Pad[0];
            ULONG lenPa = TD->Pad[1];
            ULONG endPa = startPa + lenPa;
            DPRINT_EHCI("EHCI_TD_DONE: PAstart=0x%08lx PAend=0x%08lx ObservedBuf0=0x%08lx LenXfer=%lu RemBytes=%u\n",
                    startPa,
                    endPa,
                    TD->HwTD.Buffer[0],
                    LengthTransfered,
                    TD->HwTD.Token.TransferBytes);

            /* Optional hexdump of transferred data (first bytes) */
            {
                ULONG_PTR vaLow = TD->Pad[2];
                ULONG_PTR vaHigh = TD->Pad[3];
                ULONG_PTR va = vaLow | (vaHigh << 32);
                if (va && LengthTransfered)
                {
                    ULONG dumpLen = (LengthTransfered < 32) ? LengthTransfered : 32;
                    EHCI_HexDump("EHCI_TD_DONE DATA", (const VOID*)va, dumpLen);
                }
            }
        }
#endif

        if (TD->HwTD.Token.PIDCode != EHCI_TD_TOKEN_PID_SETUP)
        {
            ULONG Remaining = 0;

            if (TransferParameters &&
                EhciTransfer->TransferLen < TransferParameters->TransferBufferLength)
            {
                Remaining = TransferParameters->TransferBufferLength -
                            EhciTransfer->TransferLen;
            }

            if (Remaining && LengthTransfered > Remaining)
            {
                DPRINT_EHCI("EHCI_TD_DONE: clamping length %lu -> %lu (Remaining=%lu)\n",
                        LengthTransfered,
                        Remaining,
                        Remaining);
                LengthTransfered = Remaining;
            }

            EhciTransfer->TransferLen += LengthTransfered;
        }

        if (USBDStatus != USBD_STATUS_SUCCESS)
            EhciTransfer->USBDStatus = USBDStatus;

        if ((TransferType == USBPORT_TRANSFER_TYPE_BULK ||
             TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT) &&
            (TdHalted || TdShort))
        {
            EhciEndpoint->NextDataToggle = TD->HwTD.Token.DataToggle & 1;
        }
    }

    TD->HwTD.NextTD = 0;
    TD->HwTD.AlternateNextTD = 0;

    TD->TdFlags = 0;
    TD->EhciTransfer = NULL;

    EhciEndpoint->RemainTDs++;

    if (EhciTransfer->PendingTDs == 0)
    {
        EhciEndpoint->PendingTDs--;

        if (TransferType == USBPORT_TRANSFER_TYPE_CONTROL ||
            TransferType == USBPORT_TRANSFER_TYPE_BULK)
        {
            EhciExtension->PendingTransfers--;

            if (EhciExtension->PendingTransfers == 0)
            {
                OperationalRegs = EhciExtension->OperationalRegs;
                Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);

                if (!Command.InterruptAdvanceDoorbell &&
                    (EhciExtension->Flags & EHCI_FLAGS_IDLE_SUPPORT))
                {
                    EHCI_DisableAsyncList(EhciExtension);
                }
            }
        }

        RegPacket.UsbPortCompleteTransfer(EhciExtension,
                                          EhciEndpoint,
                                          TransferParameters,
                                          EhciTransfer->USBDStatus,
                                          EhciTransfer->TransferLen);
    }
}

VOID
NTAPI
EHCI_PollActiveAsyncEndpoint(IN PEHCI_EXTENSION EhciExtension,
                             IN PEHCI_ENDPOINT EhciEndpoint)
{
    PEHCI_HCD_QH QH;
    PEHCI_HCD_TD TD;
    PEHCI_HCD_TD CurrentTD;
    ULONG CurrentTDPhys;
    BOOLEAN IsScheduled;

    DPRINT_EHCI("EHCI_PollActiveAsyncEndpoint: ... \n");

    QH = EhciEndpoint->QH;

    CurrentTDPhys = QH->sqh.HwQH.CurrentTD & LINK_POINTER_MASK;
    ASSERT(CurrentTDPhys);

    DPRINT_EHCI("EHCI_PollActiveAsyncEndpoint: map CurrentTD phys=%08lx\n", CurrentTDPhys);
    CurrentTD = RegPacket.UsbPortGetMappedVirtualAddress(CurrentTDPhys,
                                                         EhciExtension,
                                                         EhciEndpoint);

    if (CurrentTD == EhciEndpoint->DmaBufferVA)
        return;

    IsScheduled = QH->sqh.QhFlags & EHCI_QH_FLAG_IN_SCHEDULE;

    if (!EHCI_HardwarePresent(EhciExtension, 0))
        IsScheduled = 0;

    TD = EhciEndpoint->HcdHeadP;

    if (TD == CurrentTD)
    {
        if (TD != EhciEndpoint->HcdTailP &&
            !(TD->HwTD.Token.Status & EHCI_TOKEN_STATUS_ACTIVE))
        {
            if (TD->NextHcdTD && TD->HwTD.NextTD != TD->NextHcdTD->PhysicalAddress)
                TD->HwTD.NextTD = TD->NextHcdTD->PhysicalAddress;

            if (TD->AltNextHcdTD &&
                TD->HwTD.AlternateNextTD != TD->AltNextHcdTD->PhysicalAddress)
            {
                TD->HwTD.AlternateNextTD = TD->AltNextHcdTD->PhysicalAddress;
            }

            if (QH->sqh.HwQH.CurrentTD == TD->PhysicalAddress &&
                !(TD->HwTD.Token.Status & EHCI_TOKEN_STATUS_ACTIVE) &&
                (QH->sqh.HwQH.NextTD != TD->HwTD.NextTD ||
                 QH->sqh.HwQH.AlternateNextTD != TD->HwTD.AlternateNextTD))
            {
                QH->sqh.HwQH.NextTD = TD->HwTD.NextTD;
                QH->sqh.HwQH.AlternateNextTD = TD->HwTD.AlternateNextTD;
            }

            EHCI_InterruptNextSOF(EhciExtension);
        }
    }
    else
    {
        while (TD != CurrentTD)
        {
            ASSERT((TD->TdFlags & EHCI_HCD_TD_FLAG_DUMMY) == 0);

            TD->TdFlags |= EHCI_HCD_TD_FLAG_DONE;

            if (TD->HwTD.Token.Status & EHCI_TOKEN_STATUS_ACTIVE)
                TD->TdFlags |= EHCI_HCD_TD_FLAG_ACTIVE;

            InsertTailList(&EhciEndpoint->ListTDs, &TD->DoneLink);
            TD = TD->NextHcdTD;
        }
    }

    if (CurrentTD->HwTD.Token.Status & EHCI_TOKEN_STATUS_ACTIVE)
    {
        ASSERT(TD != NULL);
        EhciEndpoint->HcdHeadP = TD;
        return;
    }

    if ((CurrentTD->NextHcdTD != EhciEndpoint->HcdTailP) &&
        (CurrentTD->AltNextHcdTD != EhciEndpoint->HcdTailP ||
         CurrentTD->HwTD.Token.TransferBytes == 0))
    {
        ASSERT(TD != NULL);
        EhciEndpoint->HcdHeadP = TD;
        return;
    }

    if (IsScheduled)
    {
        EHCI_LockQH(EhciExtension,
                    QH,
                    EhciEndpoint->EndpointProperties.TransferType);
    }

    QH->sqh.HwQH.CurrentTD = EhciEndpoint->DmaBufferPA;

    CurrentTD->TdFlags |= EHCI_HCD_TD_FLAG_DONE;
    InsertTailList(&EhciEndpoint->ListTDs, &CurrentTD->DoneLink);

    if (CurrentTD->HwTD.Token.TransferBytes &&
        CurrentTD->AltNextHcdTD == EhciEndpoint->HcdTailP)
    {
        TD = CurrentTD->NextHcdTD;

        while (TD != EhciEndpoint->HcdTailP)
        {
            TD->TdFlags |= EHCI_HCD_TD_FLAG_ACTIVE;
            InsertTailList(&EhciEndpoint->ListTDs, &TD->DoneLink);
            TD = TD->NextHcdTD;
        }
    }

    QH->sqh.HwQH.CurrentTD = EhciEndpoint->HcdTailP->PhysicalAddress;
    QH->sqh.HwQH.NextTD = TERMINATE_POINTER;
    QH->sqh.HwQH.AlternateNextTD = TERMINATE_POINTER;
    QH->sqh.HwQH.Token.TransferBytes = 0;

    EhciEndpoint->HcdHeadP = EhciEndpoint->HcdTailP;

    if (IsScheduled)
        EHCI_UnlockQH(EhciExtension, QH);
}

VOID
NTAPI
EHCI_PollHaltedAsyncEndpoint(IN PEHCI_EXTENSION EhciExtension,
                             IN PEHCI_ENDPOINT EhciEndpoint)
{
    PEHCI_HCD_QH QH;
    PEHCI_HCD_TD CurrentTD;
    ULONG CurrentTdPA;
    PEHCI_HCD_TD TD;
    PEHCI_TRANSFER Transfer;
    BOOLEAN IsScheduled;

    DPRINT("EHCI_PollHaltedAsyncEndpoint: EhciEndpoint - %p\n", EhciEndpoint);

    QH = EhciEndpoint->QH;
    EHCI_DumpHwQH(QH);

    CurrentTdPA = QH->sqh.HwQH.CurrentTD & LINK_POINTER_MASK;
    ASSERT(CurrentTdPA);

    IsScheduled = QH->sqh.QhFlags & EHCI_QH_FLAG_IN_SCHEDULE;

    if (!EHCI_HardwarePresent(EhciExtension, 0))
        IsScheduled = 0;

    DPRINT_EHCI("EHCI_PollHaltedAsyncEndpoint: map CurrentTD phys=%08lx\n", CurrentTdPA);
    CurrentTD = RegPacket.UsbPortGetMappedVirtualAddress(CurrentTdPA,
                                                         EhciExtension,
                                                         EhciEndpoint);

    DPRINT("EHCI_PollHaltedAsyncEndpoint: CurrentTD - %p\n", CurrentTD);

    if (CurrentTD == EhciEndpoint->DmaBufferVA)
        return;

    ASSERT(EhciEndpoint->HcdTailP != CurrentTD);

    if (IsScheduled)
    {
        EHCI_LockQH(EhciExtension,
                    QH,
                    EhciEndpoint->EndpointProperties.TransferType);
    }

    TD = EhciEndpoint->HcdHeadP;

    while (TD != CurrentTD)
    {
        DPRINT("EHCI_PollHaltedAsyncEndpoint: TD - %p\n", TD);

        ASSERT((TD->TdFlags & EHCI_HCD_TD_FLAG_DUMMY) == 0);

        if (TD->HwTD.Token.Status & EHCI_TOKEN_STATUS_ACTIVE)
            TD->TdFlags |= EHCI_HCD_TD_FLAG_ACTIVE;

        TD->TdFlags |= EHCI_HCD_TD_FLAG_DONE;

        InsertTailList(&EhciEndpoint->ListTDs, &TD->DoneLink);

        TD = TD->NextHcdTD;
    }

    TD = CurrentTD;

    Transfer = CurrentTD->EhciTransfer;

    do
    {
        DPRINT("EHCI_PollHaltedAsyncEndpoint: TD - %p\n", TD);

        if (TD->HwTD.Token.Status & EHCI_TOKEN_STATUS_ACTIVE)
            TD->TdFlags |= EHCI_HCD_TD_FLAG_ACTIVE;

        TD->TdFlags |= EHCI_HCD_TD_FLAG_DONE;

        InsertTailList(&EhciEndpoint->ListTDs, &TD->DoneLink);

        TD = TD->NextHcdTD;
    }
    while (TD->EhciTransfer == Transfer);

    EhciEndpoint->HcdHeadP = TD;

    QH->sqh.HwQH.CurrentTD = EhciEndpoint->DmaBufferPA;
    QH->sqh.HwQH.NextTD = TD->PhysicalAddress;
    QH->sqh.HwQH.AlternateNextTD = TERMINATE_POINTER;
    QH->sqh.HwQH.Token.TransferBytes = 0;

    if (IsScheduled)
        EHCI_UnlockQH(EhciExtension, QH);

    if (EhciEndpoint->EndpointStatus & USBPORT_ENDPOINT_CONTROL)
    {
        EhciEndpoint->EndpointStatus &= ~USBPORT_ENDPOINT_HALT;
        QH->sqh.HwQH.Token.ErrorCounter = 0;
        QH->sqh.HwQH.Token.Status &= (UCHAR)~(EHCI_TOKEN_STATUS_ACTIVE |
                                              EHCI_TOKEN_STATUS_HALTED);

    }
}

VOID
NTAPI
EHCI_PollAsyncEndpoint(IN PEHCI_EXTENSION EhciExtension,
                       IN PEHCI_ENDPOINT EhciEndpoint)
{
    PEHCI_HCD_QH QH;
    PLIST_ENTRY DoneList;
    PEHCI_HCD_TD TD;

    //DPRINT_EHCI("EHCI_PollAsyncEndpoint: EhciEndpoint - %p\n", EhciEndpoint);

    if (!EhciEndpoint->PendingTDs)
        return;

    QH = EhciEndpoint->QH;

    if (QH->sqh.QhFlags & EHCI_QH_FLAG_CLOSED)
        return;

    if (QH->sqh.HwQH.Token.Status & EHCI_TOKEN_STATUS_ACTIVE ||
        !(QH->sqh.HwQH.Token.Status & EHCI_TOKEN_STATUS_HALTED))
    {
        EHCI_PollActiveAsyncEndpoint(EhciExtension, EhciEndpoint);
    }
    else
    {
        EhciEndpoint->EndpointStatus |= USBPORT_ENDPOINT_HALT;
        EHCI_PollHaltedAsyncEndpoint(EhciExtension, EhciEndpoint);
    }

    DoneList = &EhciEndpoint->ListTDs;

    while (!IsListEmpty(DoneList))
    {
        TD = CONTAINING_RECORD(DoneList->Flink,
                               EHCI_HCD_TD,
                               DoneLink);

        RemoveHeadList(DoneList);

        ASSERT((TD->TdFlags & (EHCI_HCD_TD_FLAG_PROCESSED |
                               EHCI_HCD_TD_FLAG_DONE)));

        EHCI_ProcessDoneAsyncTd(EhciExtension, TD);
    }
}

VOID
NTAPI
EHCI_PollIsoEndpoint(IN PEHCI_EXTENSION EhciExtension,
                     IN PEHCI_ENDPOINT EhciEndpoint)
{
    DPRINT_EHCI("EHCI_PollIsoEndpoint: UNIMPLEMENTED. FIXME\n");
}

VOID
NTAPI
EHCI_PollEndpoint(IN PVOID ehciExtension,
                  IN PVOID ehciEndpoint)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_ENDPOINT EhciEndpoint = ehciEndpoint;
    ULONG TransferType;

    //DPRINT_EHCI("EHCI_PollEndpoint: EhciEndpoint - %p\n", EhciEndpoint);

    TransferType = EhciEndpoint->EndpointProperties.TransferType;

    if (TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
        EHCI_PollIsoEndpoint(EhciExtension, EhciEndpoint);
    else
        EHCI_PollAsyncEndpoint(EhciExtension, EhciEndpoint);
}

VOID
NTAPI
EHCI_CheckController(IN PVOID ehciExtension)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;

    //DPRINT_EHCI("EHCI_CheckController: ... \n");

    if (EhciExtension->IsStarted)
        EHCI_HardwarePresent(EhciExtension, TRUE);
#if DBG
    else
    {
        /* Emit a trace when we are asked to check while not started */
        DPRINT_EHCI("EHCI_CheckController: called while !IsStarted (ext=%p)\n", EhciExtension);
    }
#endif
}

ULONG
NTAPI
EHCI_Get32BitFrameNumber(IN PVOID ehciExtension)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    ULONG FrameIdx;
    ULONG FrameIndex;
    ULONG FrameNumber;

    //DPRINT_EHCI("EHCI_Get32BitFrameNumber: EhciExtension - %p\n", EhciExtension);

    FrameIdx = EhciExtension->FrameIndex;
    FrameIndex = READ_REGISTER_ULONG(&EhciExtension->OperationalRegs->FrameIndex);

    FrameNumber = (USHORT)FrameIdx ^ ((FrameIndex / EHCI_MICROFRAMES) & EHCI_FRINDEX_FRAME_MASK);
    FrameNumber &= EHCI_FRAME_LIST_MAX_ENTRIES;
    FrameNumber += FrameIndex | ((FrameIndex / EHCI_MICROFRAMES) & EHCI_FRINDEX_INDEX_MASK);

    return FrameNumber;
}

VOID
NTAPI
EHCI_EnableInterrupts(IN PVOID ehciExtension)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;

    DPRINT("EHCI_EnableInterrupts: EhciExtension->InterruptMask - %x\n",
           EhciExtension->InterruptMask.AsULONG);

    WRITE_REGISTER_ULONG(&EhciExtension->OperationalRegs->HcInterruptEnable.AsULONG,
                         EhciExtension->InterruptMask.AsULONG);
}

VOID
NTAPI
EHCI_DisableInterrupts(IN PVOID ehciExtension)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;

    DPRINT("EHCI_DisableInterrupts: ... \n");

    WRITE_REGISTER_ULONG(&EhciExtension->OperationalRegs->HcInterruptEnable.AsULONG,
                         0);
}

VOID
NTAPI
EHCI_PollController(IN PVOID ehciExtension)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_HW_REGISTERS OperationalRegs;
    ULONG Port;
    EHCI_PORT_STATUS_CONTROL PortSC;

    /* Optional poll logging (DBG) */
#if DBG
    if (g_EhciTraceMask & 0x4)
    {
        static ULONG s_pollLogTick;
        ULONG div = g_EhciPollLogDiv ? g_EhciPollLogDiv : 1;
        if ((++s_pollLogTick % div) == 0)
            DPRINT_EHCI("EHCI_PollController: tick=%lu div=%lu\n", s_pollLogTick, div);
    }
#endif

    OperationalRegs = EhciExtension->OperationalRegs;
#if DBG
    {
        ULONG seg = READ_REGISTER_ULONG(&OperationalRegs->SegmentSelector);
        if (seg)
            DPRINT_EHCI("EHCI_PollController: CTRLDSSegment nonzero=0x%08lx\n", seg);
    }
#endif

    if (!(EhciExtension->Flags & EHCI_FLAGS_CONTROLLER_SUSPEND))
    {
        RegPacket.UsbPortInvalidateRootHub(EhciExtension);
        return;
    }

    if (EhciExtension->NumberOfPorts)
    {
        for (Port = 0; Port < EhciExtension->NumberOfPorts; Port++)
        {
            PortSC.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->PortControl[Port].AsULONG);

            if (PortSC.ConnectStatusChange)
                RegPacket.UsbPortInvalidateRootHub(EhciExtension);
        }
    }
}

VOID
NTAPI
EHCI_SetEndpointDataToggle(IN PVOID ehciExtension,
                           IN PVOID ehciEndpoint,
                           IN ULONG DataToggle)
{
    PEHCI_ENDPOINT EhciEndpoint;
    ULONG TransferType;

    EhciEndpoint = ehciEndpoint;

    DPRINT("EHCI_SetEndpointDataToggle: EhciEndpoint - %p, DataToggle - %x\n",
                EhciEndpoint,
                DataToggle);

    TransferType = EhciEndpoint->EndpointProperties.TransferType;

    if (TransferType == USBPORT_TRANSFER_TYPE_BULK ||
        TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
    {
        ULONG ToggleBit = DataToggle ? 1 : 0;

        EhciEndpoint->QH->sqh.HwQH.Token.DataToggle = ToggleBit;
        EhciEndpoint->NextDataToggle = ToggleBit;
    }
}

ULONG
NTAPI
EHCI_GetEndpointStatus(IN PVOID ehciExtension,
                       IN PVOID ehciEndpoint)
{
    PEHCI_ENDPOINT EhciEndpoint;
    ULONG TransferType;
    ULONG EndpointStatus = USBPORT_ENDPOINT_RUN;

    EhciEndpoint = ehciEndpoint;

    DPRINT("EHCI_GetEndpointStatus: EhciEndpoint - %p\n", EhciEndpoint);

    TransferType = EhciEndpoint->EndpointProperties.TransferType;

    if (TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
        return EndpointStatus;

    if (EhciEndpoint->EndpointStatus & USBPORT_ENDPOINT_HALT)
        EndpointStatus = USBPORT_ENDPOINT_HALT;

    return EndpointStatus;
}

VOID
NTAPI
EHCI_SetEndpointStatus(IN PVOID ehciExtension,
                       IN PVOID ehciEndpoint,
                       IN ULONG EndpointStatus)
{
    PEHCI_ENDPOINT EhciEndpoint;
    ULONG TransferType;
    PEHCI_HCD_QH QH;

    EhciEndpoint = ehciEndpoint;

    DPRINT("EHCI_SetEndpointStatus: EhciEndpoint - %p, EndpointStatus - %x\n",
                EhciEndpoint,
                EndpointStatus);

    TransferType = EhciEndpoint->EndpointProperties.TransferType;

    if (TransferType != USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
    {

        if (EndpointStatus == USBPORT_ENDPOINT_RUN)
        {
            EhciEndpoint->EndpointStatus &= ~USBPORT_ENDPOINT_HALT;

            QH = EhciEndpoint->QH;
            QH->sqh.HwQH.Token.Status &= (UCHAR)~EHCI_TOKEN_STATUS_HALTED;

            return;
        }

        if (EndpointStatus == USBPORT_ENDPOINT_HALT)
            DbgBreakPoint();
    }
}

VOID
NTAPI
EHCI_ResetController(IN PVOID ehciExtension)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_COMMAND Command;
    LARGE_INTEGER EndTime, Now;

    DPRINT_EHCI("EHCI_ResetController: entry\n");

    OperationalRegs = EhciExtension->OperationalRegs;

    /* Issue HC reset */
    Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
    Command.Reset = 1;
    WRITE_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG, Command.AsULONG);

    KeQuerySystemTime(&EndTime);
    EndTime.QuadPart += 100 * 10000; // 100 ms
    do
    {
        Command.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcCommand.AsULONG);
        KeQuerySystemTime(&Now);
        if (!Command.Reset)
            break;
    }
    while (Now.QuadPart < EndTime.QuadPart);

    if (Command.Reset)
    {
        DPRINT_EHCI("EHCI_ResetController: reset timed out\n");
    }

    DPRINT_EHCI("EHCI_ResetController: exit\n");
}

MPSTATUS
NTAPI
EHCI_StartSendOnePacket(IN PVOID ehciExtension,
                        IN PVOID PacketParameters,
                        IN PVOID Data,
                        IN PULONG pDataLength,
                        IN PVOID BufferVA,
                        IN PVOID BufferPA,
                        IN ULONG BufferLength,
                        IN USBD_STATUS * pUSBDStatus)
{
    DPRINT_EHCI("EHCI_StartSendOnePacket: UNIMPLEMENTED. FIXME\n");
    return MP_STATUS_SUCCESS;
}

MPSTATUS
NTAPI
EHCI_EndSendOnePacket(IN PVOID ehciExtension,
                      IN PVOID PacketParameters,
                      IN PVOID Data,
                      IN PULONG pDataLength,
                      IN PVOID BufferVA,
                      IN PVOID BufferPA,
                      IN ULONG BufferLength,
                      IN USBD_STATUS * pUSBDStatus)
{
    DPRINT_EHCI("EHCI_EndSendOnePacket: UNIMPLEMENTED. FIXME\n");
    return MP_STATUS_SUCCESS;
}

MPSTATUS
NTAPI
EHCI_PassThru(IN PVOID ehciExtension,
              IN PVOID passThruParameters,
              IN ULONG ParameterLength,
              IN PVOID pParameters)
{
    DPRINT_EHCI("EHCI_PassThru: UNIMPLEMENTED. FIXME\n");
    return MP_STATUS_SUCCESS;
}

VOID
NTAPI
EHCI_RebalanceEndpoint(IN PVOID ohciExtension,
                       IN PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
                       IN PVOID ohciEndpoint)
{
    PEHCI_EXTENSION EhciExtension = (PEHCI_EXTENSION)ohciExtension;
    PEHCI_ENDPOINT EhciEndpoint = (PEHCI_ENDPOINT)ohciEndpoint;
    ULONG TransferType;

    if (!EhciExtension || !EhciEndpoint || !EndpointProperties)
        return;

    TransferType = EhciEndpoint->EndpointProperties.TransferType;

    DPRINT("EHCI_RebalanceEndpoint: EP=%p type=%lu period=%u ordinal=%lu\n",
           EhciEndpoint,
           TransferType,
           EndpointProperties->Period,
           EndpointProperties->Reserved6);

    /* Only interrupt endpoints are placed on periodic tree here */
    if (TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
    {
        /* If scheduled, remove, update props, and reinsert */
        if (EhciEndpoint->QH && (EhciEndpoint->QH->sqh.QhFlags & EHCI_QH_FLAG_IN_SCHEDULE))
        {
            EHCI_RemoveQhFromPeriodicList(EhciExtension, EhciEndpoint);
        }

        EhciEndpoint->EndpointProperties.Period = EndpointProperties->Period;
        EhciEndpoint->EndpointProperties.Reserved6 = EndpointProperties->Reserved6; /* Ordinal */

        if (EhciEndpoint->QH)
        {
            EHCI_InsertQhInPeriodicList(EhciExtension, EhciEndpoint);
        }
    }
}

VOID
NTAPI
EHCI_FlushInterrupts(IN PVOID ehciExtension)
{
    PEHCI_EXTENSION EhciExtension = ehciExtension;
    PEHCI_HW_REGISTERS OperationalRegs;
    EHCI_USB_STATUS Status;

    DPRINT("EHCI_FlushInterrupts: ... \n");

    OperationalRegs = EhciExtension->OperationalRegs;

    Status.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG);
    WRITE_REGISTER_ULONG(&OperationalRegs->HcStatus.AsULONG, Status.AsULONG);
}

VOID
NTAPI
EHCI_TakePortControl(IN PVOID ohciExtension)
{
    PEHCI_EXTENSION EhciExtension = (PEHCI_EXTENSION)ohciExtension;
    PEHCI_HW_REGISTERS OperationalRegs;
    ULONG Port;
    EHCI_PORT_STATUS_CONTROL PortSC;

    DPRINT("EHCI_TakePortControl: taking ownership of ports\n");

    if (!EhciExtension)
        return;

    OperationalRegs = EhciExtension->OperationalRegs;

    /* Ensure this HC is configured */
    WRITE_REGISTER_ULONG(&OperationalRegs->ConfigFlag, EHCI_CONFIG_FLAG_CONFIGURED);

    /* Set owner to EHCI and power ports if supported */
    for (Port = 0; Port < EhciExtension->NumberOfPorts; Port++)
    {
        PortSC.AsULONG = READ_REGISTER_ULONG(&OperationalRegs->PortControl[Port].AsULONG);
        PortSC.PortOwner = 0; /* EHCI owns */
        if (EhciExtension->PortPowerControl)
            PortSC.PortPower = 1;
        WRITE_REGISTER_ULONG(&OperationalRegs->PortControl[Port].AsULONG, PortSC.AsULONG);
    }
}

VOID
NTAPI
EHCI_Unload(IN PDRIVER_OBJECT DriverObject)
{
#if DBG
    DPRINT_EHCI("EHCI_Unload: Not supported\n");
#endif
    return;
}

NTSTATUS
NTAPI
DriverEntry(IN PDRIVER_OBJECT DriverObject,
            IN PUNICODE_STRING RegistryPath)
{
    DPRINT("DriverEntry: DriverObject - %p, RegistryPath - %wZ\n",
           DriverObject,
           RegistryPath);

    if (USBPORT_GetHciMn() != USBPORT_HCI_MN)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&RegPacket, sizeof(USBPORT_REGISTRATION_PACKET));

    RegPacket.MiniPortVersion = USB_MINIPORT_VERSION_EHCI;

    /* Enable polling as a fallback so we don't lose port changes on quirky HW */
    RegPacket.MiniPortFlags = USB_MINIPORT_FLAGS_INTERRUPT |
                              USB_MINIPORT_FLAGS_MEMORY_IO |
                              USB_MINIPORT_FLAGS_USB2 |
                              USB_MINIPORT_FLAGS_WAKE_SUPPORT |
                              USB_MINIPORT_FLAGS_POLLING;

    RegPacket.MiniPortBusBandwidth = TOTAL_USB20_BUS_BANDWIDTH;

    RegPacket.MiniPortExtensionSize = sizeof(EHCI_EXTENSION);
    RegPacket.MiniPortEndpointSize = sizeof(EHCI_ENDPOINT);
    RegPacket.MiniPortTransferSize = sizeof(EHCI_TRANSFER);
    RegPacket.MiniPortResourcesSize = sizeof(EHCI_HC_RESOURCES);

    RegPacket.OpenEndpoint = EHCI_OpenEndpoint;
    RegPacket.ReopenEndpoint = EHCI_ReopenEndpoint;
    RegPacket.QueryEndpointRequirements = EHCI_QueryEndpointRequirements;
    RegPacket.CloseEndpoint = EHCI_CloseEndpoint;
    RegPacket.StartController = EHCI_StartController;
    RegPacket.StopController = EHCI_StopController;
    RegPacket.SuspendController = EHCI_SuspendController;
    RegPacket.ResumeController = EHCI_ResumeController;
    RegPacket.InterruptService = EHCI_InterruptService;
    RegPacket.InterruptDpc = EHCI_InterruptDpc;
    RegPacket.SubmitTransfer = EHCI_SubmitTransfer;
    RegPacket.SubmitIsoTransfer = EHCI_SubmitIsoTransfer;
    RegPacket.AbortTransfer = EHCI_AbortTransfer;
    RegPacket.GetEndpointState = EHCI_GetEndpointState;
    RegPacket.SetEndpointState = EHCI_SetEndpointState;
    RegPacket.PollEndpoint = EHCI_PollEndpoint;
    RegPacket.CheckController = EHCI_CheckController;
    RegPacket.Get32BitFrameNumber = EHCI_Get32BitFrameNumber;
    RegPacket.InterruptNextSOF = EHCI_InterruptNextSOF;
    RegPacket.EnableInterrupts = EHCI_EnableInterrupts;
    RegPacket.DisableInterrupts = EHCI_DisableInterrupts;
    RegPacket.PollController = EHCI_PollController;
    RegPacket.SetEndpointDataToggle = EHCI_SetEndpointDataToggle;
    RegPacket.GetEndpointStatus = EHCI_GetEndpointStatus;
    RegPacket.SetEndpointStatus = EHCI_SetEndpointStatus;
    RegPacket.RH_GetRootHubData = EHCI_RH_GetRootHubData;
    RegPacket.RH_GetStatus = EHCI_RH_GetStatus;
    RegPacket.RH_GetPortStatus = EHCI_RH_GetPortStatus;
    RegPacket.RH_GetHubStatus = EHCI_RH_GetHubStatus;
    RegPacket.RH_SetFeaturePortReset = EHCI_RH_SetFeaturePortReset;
    RegPacket.RH_SetFeaturePortPower = EHCI_RH_SetFeaturePortPower;
    RegPacket.RH_SetFeaturePortEnable = EHCI_RH_SetFeaturePortEnable;
    RegPacket.RH_SetFeaturePortSuspend = EHCI_RH_SetFeaturePortSuspend;
    RegPacket.RH_ClearFeaturePortEnable = EHCI_RH_ClearFeaturePortEnable;
    RegPacket.RH_ClearFeaturePortPower = EHCI_RH_ClearFeaturePortPower;
    RegPacket.RH_ClearFeaturePortSuspend = EHCI_RH_ClearFeaturePortSuspend;
    RegPacket.RH_ClearFeaturePortEnableChange = EHCI_RH_ClearFeaturePortEnableChange;
    RegPacket.RH_ClearFeaturePortConnectChange = EHCI_RH_ClearFeaturePortConnectChange;
    RegPacket.RH_ClearFeaturePortResetChange = EHCI_RH_ClearFeaturePortResetChange;
    RegPacket.RH_ClearFeaturePortSuspendChange = EHCI_RH_ClearFeaturePortSuspendChange;
    RegPacket.RH_ClearFeaturePortOvercurrentChange = EHCI_RH_ClearFeaturePortOvercurrentChange;
    RegPacket.RH_DisableIrq = EHCI_RH_DisableIrq;
    RegPacket.RH_EnableIrq = EHCI_RH_EnableIrq;
    RegPacket.StartSendOnePacket = EHCI_StartSendOnePacket;
    RegPacket.EndSendOnePacket = EHCI_EndSendOnePacket;
    RegPacket.PassThru = EHCI_PassThru;
    RegPacket.RebalanceEndpoint = EHCI_RebalanceEndpoint;
    RegPacket.FlushInterrupts = EHCI_FlushInterrupts;
    RegPacket.RH_ChirpRootPort = EHCI_RH_ChirpRootPort;
    RegPacket.TakePortControl = EHCI_TakePortControl;

    DriverObject->DriverUnload = EHCI_Unload;

    return USBPORT_RegisterUSBPortDriver(DriverObject,
                                         USB20_MINIPORT_INTERFACE_VERSION,
                                         &RegPacket);
}
