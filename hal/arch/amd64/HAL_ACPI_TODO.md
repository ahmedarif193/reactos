# HAL ACPI (amd64) Roadmap

- [x] Support ACPI table overrides provided by the loader extension so firmware-supplied overrides are honored (`hal/arch/common/acpi/halacpi.c:760`).
- [x] Use FADT 64-bit `x_*` pointers (for example `x_dsdt`) and audit table mapping code paths so ACPI tables above 4 GB are reachable (`hal/arch/common/acpi/halacpi.c:165`, `hal/arch/common/acpi/halacpi.c:698`).
- [x] Implement the actual ACPI timer bring-up in `HalaAcpiTimerInit` to program the PM timer on amd64 hardware (`hal/arch/common/acpi/halacpi.c:773`).
- [ ] Initialize 64-bit DMA support during resource reporting to respect systems with memory above 4 GB (`hal/arch/common/acpi/halacpi.c:1482`).
- [ ] Flesh out MCA bus initialization so Micro Channel platforms are enumerated correctly (`hal/arch/common/acpi/halacpi.c:1484`).
- [ ] Handle the ACPI Boot table (BOOT) and expose its state instead of logging it as unsupported (`hal/arch/common/acpi/halacpi.c:529`).
- [ ] Process SRAT information to enable NUMA topology and memory hot-plug on amd64 (`hal/arch/common/acpi/halacpi.c:833`).
- [ ] Implement machine-specific ACPI table match handling so platform workarounds can be applied (`hal/arch/common/acpi/halacpi.c:508`).
- [ ] Surface the ACPI watchdog (WDRT) as a child device when present instead of logging a placeholder (`hal/arch/common/acpi/halpnpdd.c:137`).
- [ ] Provide the standard ACPI interfaces (`GUID_ACPI_REGS_INTERFACE_STANDARD`, `GUID_ACPI_PORT_RANGES_INTERFACE_STANDARD`, etc.) in `HalpQueryInterface` (`hal/arch/common/acpi/halpnpdd.c:152`).
- [ ] Implement WMI handling in the HAL PnP driver rather than breaking into the debugger (`hal/arch/common/acpi/halpnpdd.c:826`).
- [ ] Return real EISA configuration data (or a graceful fallback) instead of asserting in `HalGetBusData` for `EisaConfiguration` requests (`hal/arch/i386/acpi/busemul.c:204`).
- [ ] Extend MADT parsing beyond legacy mode: respect `ACPI_MADT_PCAT_COMPAT`, support additional subtable types, and configure APIC mode appropriately (`hal/arch/common/acpi/madt.c:74`).
- [ ] Apply interrupt source override data (including `IntiFlags`) when processing MADT entries so `HalpPicVectorRedirect` reflects firmware routing (`hal/arch/common/acpi/madt.c:206`).
- [x] Enhance ACPI PCI root handling: parse `_CRS/_PRT` for both `PNP0A03` and `PNP0A08`, honor segment/group numbers, and feed the PCI bus driver with 64-bit address resources so modern chipsets enumerate correctly.
- [ ] Expand PCIe `_OSC` support to claim advanced error reporting, PME, and hotplug controls once HAL programming paths are implemented.
- [ ] Consume ACPI 1.5+/3.0 fields (e.g. extended PCI resource descriptors, `PciExpress` windows) during PCI bus setup to support PCIe-based systems and virtualization platforms.
