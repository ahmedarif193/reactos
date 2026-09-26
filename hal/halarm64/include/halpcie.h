#pragma once

/*
 * PCI Express Capability: Port/Device Type decode.
 *
 * PCIe Capability Register at offset +0x02:
 *   Bits 7:4 = Device/Port Type
 *
 * Keep this shared between the ACPI and legacy PCI debug paths.
 */

FORCEINLINE
PCSTR
NTAPI
HalpPciExpressDeviceTypeName(
    _In_ UCHAR DeviceType)
{
    switch (DeviceType & 0xF)
    {
        case 0x0: return "Endpoint";
        case 0x1: return "Legacy Endpoint";
        case 0x2: return "Reserved (0x2)";
        case 0x3: return "Reserved (0x3)";
        case 0x4: return "Root Port";
        case 0x5: return "Upstream Port";
        case 0x6: return "Downstream Port";
        case 0x7: return "PCI Express to PCI/PCI-X Bridge";
        case 0x8: return "PCI/PCI-X to PCI Express Bridge";
        case 0x9: return "Root Complex Integrated Endpoint";
        case 0xA: return "Root Complex Event Collector";
        case 0xB: return "Reserved (0xB)";
        case 0xC: return "Reserved (0xC)";
        case 0xD: return "Reserved (0xD)";
        case 0xE: return "Reserved (0xE)";
        case 0xF: return "Reserved (0xF)";
        default:  return "Reserved";
    }
}
