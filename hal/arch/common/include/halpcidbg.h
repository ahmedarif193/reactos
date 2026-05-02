#pragma once

/*
 * Shared PCI debug helpers.
 *
 * NOTE: These helpers are intended for early boot / HAL diagnostics and are
 * compiled into multiple translation units as static routines.
 */

#include <halpcie.h>

typedef
VOID
(NTAPI *PHALP_PCI_CONFIG_READ)(
    _In_ PVOID Context,
    _In_ ULONG Offset,
    _Out_writes_bytes_all_(Length) PVOID Buffer,
    _In_ ULONG Length);

CODE_SEG("INIT")
static
VOID
NTAPI
HalpPciDebugDumpCapabilities(
    _In_ PVOID ReadContext,
    _In_ PHALP_PCI_CONFIG_READ ReadConfig,
    _In_ UCHAR HeaderType,
    _In_ PPCI_COMMON_CONFIG PciData)
{
    PCI_CAPABILITIES_HEADER Header;
    UCHAR CapabilityPointer;
    ULONG GuardCount;
    ULONG Visited[8];
    BOOLEAN IsPcie;
    USHORT Offset;
    ULONG ExtVisited[32];

    if (!ReadConfig || !PciData)
    {
        return;
    }

    IsPcie = FALSE;
    RtlZeroMemory(Visited, sizeof(Visited));
    RtlZeroMemory(ExtVisited, sizeof(ExtVisited));

    if (!(PciData->Status & PCI_STATUS_CAPABILITIES_LIST))
    {
        return;
    }

    switch (HeaderType)
    {
        case PCI_DEVICE_TYPE:
            CapabilityPointer = PciData->u.type0.CapabilitiesPtr;
            break;

        case PCI_BRIDGE_TYPE:
            CapabilityPointer = PciData->u.type1.CapabilitiesPtr;
            break;

        case PCI_CARDBUS_BRIDGE_TYPE:
            CapabilityPointer = PciData->u.type2.CapabilitiesPtr;
            break;

        default:
            CapabilityPointer = 0;
            break;
    }

    if (!CapabilityPointer)
    {
        return;
    }

    GuardCount = 0;
    while (CapabilityPointer >= 0x40 &&
           GuardCount++ < 48)
    {
        ULONG Index;
        ULONG Mask;

        CapabilityPointer &= (UCHAR)~0x3;

        Index = CapabilityPointer / 4;
        Mask = 1u << (Index & 31);
        if (Visited[Index >> 5] & Mask)
        {
            break;
        }
        Visited[Index >> 5] |= Mask;

        RtlFillMemory(&Header, sizeof(Header), 0xFF);
        ReadConfig(ReadContext,
                   CapabilityPointer,
                   &Header,
                   sizeof(Header));

        if (Header.CapabilityID == 0 ||
            Header.CapabilityID == 0xFF ||
            Header.Next == CapabilityPointer)
        {
            break;
        }

        switch (Header.CapabilityID)
        {
            case PCI_CAPABILITY_ID_POWER_MANAGEMENT:
            {
                USHORT Pmc;

                Pmc = 0xFFFF;
                ReadConfig(ReadContext,
                           (ULONG)CapabilityPointer + 2,
                           &Pmc,
                           sizeof(Pmc));

                DbgPrint("\tCapabilities: [%02x] Power Management version %u\n",
                         CapabilityPointer,
                         (ULONG)(Pmc & 0x7));
                break;
            }

            case PCI_CAPABILITY_ID_MSI:
            {
                USHORT Control;
                UCHAR MultipleCapable;
                UCHAR MultipleEnabled;

                Control = 0xFFFF;
                ReadConfig(ReadContext,
                           (ULONG)CapabilityPointer + 2,
                           &Control,
                           sizeof(Control));

                MultipleCapable = (UCHAR)((Control >> 1) & 0x7);
                MultipleEnabled = (UCHAR)((Control >> 4) & 0x7);
                if (MultipleEnabled > MultipleCapable)
                {
                    MultipleEnabled = MultipleCapable;
                }

                DbgPrint("\tCapabilities: [%02x] MSI: Enable%c Count=%lu/%lu Maskable%c 64bit%c\n",
                         CapabilityPointer,
                         (Control & 0x0001) ? '+' : '-',
                         1ul << MultipleEnabled,
                         1ul << MultipleCapable,
                         (Control & 0x0100) ? '+' : '-',
                         (Control & 0x0080) ? '+' : '-');
                break;
            }

            case PCI_CAPABILITY_ID_PCI_EXPRESS:
            {
                USHORT PcieCap;
                UCHAR DeviceType;
                UCHAR MessageNumber;

                IsPcie = TRUE;

                PcieCap = 0xFFFF;
                ReadConfig(ReadContext,
                           (ULONG)CapabilityPointer + 2,
                           &PcieCap,
                           sizeof(PcieCap));

                DeviceType = (UCHAR)((PcieCap >> 4) & 0xF);
                MessageNumber = (UCHAR)((PcieCap >> 9) & 0x1F);

                DbgPrint("\tCapabilities: [%02x] Express %s, MSI %02u\n",
                         CapabilityPointer,
                         HalpPciExpressDeviceTypeName(DeviceType),
                         (ULONG)MessageNumber);
                break;
            }

            case PCI_CAPABILITY_ID_MSIX:
            {
                USHORT Control;
                ULONG TableSize;

                Control = 0xFFFF;
                ReadConfig(ReadContext,
                           (ULONG)CapabilityPointer + 2,
                           &Control,
                           sizeof(Control));

                TableSize = (ULONG)(Control & 0x07FF) + 1;

                DbgPrint("\tCapabilities: [%02x] MSI-X: Enable%c Count=%lu Masked%c\n",
                         CapabilityPointer,
                         (Control & 0x8000) ? '+' : '-',
                         TableSize,
                         (Control & 0x4000) ? '+' : '-');
                break;
            }

            default:
                DbgPrint("\tCapabilities: [%02x] Capability ID %02x\n",
                         CapabilityPointer,
                         (ULONG)Header.CapabilityID);
                break;
        }

        CapabilityPointer = Header.Next;
        if (!CapabilityPointer)
        {
            break;
        }
    }

    if (!IsPcie)
    {
        return;
    }

    Offset = 0x100;
    GuardCount = 0;

    while (Offset >= 0x100 &&
           Offset < 0x1000 &&
           GuardCount++ < 64)
    {
        ULONG ExtHeader;
        USHORT CapabilityId;
        USHORT NextOffset;
        ULONG Index;
        ULONG Mask;

        Index = Offset / 4;
        Mask = 1u << (Index & 31);
        if (ExtVisited[Index >> 5] & Mask)
        {
            break;
        }
        ExtVisited[Index >> 5] |= Mask;

        ExtHeader = 0xFFFFFFFF;
        ReadConfig(ReadContext,
                   Offset,
                   &ExtHeader,
                   sizeof(ExtHeader));

        if (ExtHeader == 0 || ExtHeader == 0xFFFFFFFF)
        {
            break;
        }

        CapabilityId = (USHORT)(ExtHeader & 0xFFFF);
        if (CapabilityId == 0 || CapabilityId == 0xFFFF)
        {
            break;
        }

        NextOffset = (USHORT)((ExtHeader >> 20) & 0xFFF);

        switch (CapabilityId)
        {
            case 0x0001:
                DbgPrint("\tCapabilities: [%03x] Advanced Error Reporting\n", Offset);
                break;

            case 0x0003:
            {
                ULONG SerialLow;
                ULONG SerialHigh;
                ULONGLONG Serial;
                UCHAR B0, B1, B2, B3, B4, B5, B6, B7;

                SerialLow = 0xFFFFFFFF;
                SerialHigh = 0xFFFFFFFF;
                ReadConfig(ReadContext,
                           (ULONG)Offset + 4,
                           &SerialLow,
                           sizeof(SerialLow));
                ReadConfig(ReadContext,
                           (ULONG)Offset + 8,
                           &SerialHigh,
                           sizeof(SerialHigh));

                Serial = ((ULONGLONG)SerialHigh << 32) | SerialLow;
                B0 = (UCHAR)(Serial >> 56);
                B1 = (UCHAR)(Serial >> 48);
                B2 = (UCHAR)(Serial >> 40);
                B3 = (UCHAR)(Serial >> 32);
                B4 = (UCHAR)(Serial >> 24);
                B5 = (UCHAR)(Serial >> 16);
                B6 = (UCHAR)(Serial >> 8);
                B7 = (UCHAR)(Serial);

                DbgPrint("\tCapabilities: [%03x] Device Serial Number %02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x\n",
                         Offset,
                         (ULONG)B0,
                         (ULONG)B1,
                         (ULONG)B2,
                         (ULONG)B3,
                         (ULONG)B4,
                         (ULONG)B5,
                         (ULONG)B6,
                         (ULONG)B7);
                break;
            }

            default:
                DbgPrint("\tCapabilities: [%03x] Extended Capability ID %04x\n",
                         Offset,
                         (ULONG)CapabilityId);
                break;
        }

        if (NextOffset == 0 || NextOffset == Offset || (NextOffset & 0x3))
        {
            break;
        }

        Offset = NextOffset;
    }
}
