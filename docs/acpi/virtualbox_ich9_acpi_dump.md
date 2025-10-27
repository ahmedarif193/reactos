# VirtualBox ICH9 ACPI Tables

Captured with `acpidump` from VirtualBox 7.0.10 (chipset: ICH9, BIOS mode) while booting ReactOS build 20251026-cd90113 on 2025-10-26.

Only the portions relevant to the MMCONFIG issue are reproduced below.

```
ACPI: RSDP 0x000F0010 000024 (v02 VBOX  )
ACPI: RSDT 0xDFEE1B4C 000048 (v01 VBOX   VBOXXSDT 00000002 VBOX 00000002)
ACPI: XSDT 0xDFEE1BE8 00004C (v01 VBOX   VBOXXSDT 00000002 VBOX 00000002)
ACPI: FACP 0xDFEE3000 0000F4 (v04 VBOX   VBOXFACP 00000001 VBOX 00000001)
ACPI: MCFG 0xDFEE6D60 00003C (v01 VBOX   VBOXMCFG 00000001 VBOX 00000001)
```

The `MCFG` table advertises a single segment that maps the familiar VirtualBox MMCONFIG aperture:

```
Base Address : 0x00000000DC000000
PCI Segment  : 0x0000
Start Bus    : 0x00
End Bus      : 0x3F
```

On the ICH9 profile this region is not decoded by the firmware: the PCIEXBAR register remains disabled, so every ECAM read returns all 1s. Modern ReactOS builds detect this condition, disable MMCONFIG for the session, and fall back to legacy CF8/CFC configuration cycles while keeping the host bridge accessible.

The HAL recognises this broken configuration via these identifiers (`OEMID` = `"VBOX  "`, `OEM Table ID` = `"VBOXMCFG"`) and records the failure in the ECAM coverage log so diagnostics stay visible.
```
