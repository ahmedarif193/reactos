# PCI Interrupt Routing Plan

## Current State
- `acpi_pci_routing.c` walks every ACPI root (`_SEG` / `_BBN`) and caches `_PRT` entries.
- HAL exposes `HalpRegisterPciRouteQuery` so `HalGetInterruptVector` can query GSIs.
- `HalpSetPciRoutingMap` now seeds the HAL GSI table with `_PRT` polarity/trigger data during ACPI init.
- The HAL reads the IOAPIC version register to size the redirection table (clamped to its current 256-entry support) before programming defaults.
- `HalpAllocateSystemInterrupt` now respects that span and tags overflow `_PRT` entries for diagnostics.
- IOAPIC redirection programming reuses cached `_PRT` polarity/trigger data on every allocation/unmask and reports when callers request a conflicting interrupt mode.
- No code yet programs IOAPIC entries or translates `_PRT` GSIs into actual vectors/MSI capability usage.

## Work Items
1. **GSI → IOAPIC programming**
   - With the HAL GSI table pre-populated from `_PRT`, extend `HalpGetGsiInfo` / `HalpIntiMap` to program IOAPIC entries when vectors are allocated.
   - Use `_PRT` polarity/trigger hints when calling `HalpProgramIoApic`.

2. **Per-device interrupt binding**
   - Provide a routine (`HalpAcpiAssignPciIrq`) invoked during `BUS_INTERFACE_STANDARD.SetBusData` that selects a vector and programs IOAPIC.
   - Update `HalpAdjustResourceList` so translated resources reflect the routed GSI and vector.

3. **MSI/MSI-X hand-off**
   - Detect device capabilities during `HalpAdjustResourceList` and prefer MSI when `_OSC` grants native Express control.
   - Fallback to `_PRT` routing if MSI setup fails.

4. **Debugger / diagnostics**
   - Add `!pciroute` KDBG command dumping the routing cache and current IOAPIC programming.
   - Log `_PRT` conflicts (multiple INTA# entries for same slot pointing at different GSIs) during boot.

5. **Testing Hooks**
   - Extend Testman scenario: boot on ICH9 & Q35 capturing `pcirange`, `_PRT` dump, and IOAPIC table.
   - Add unit tests exercising `HalpPciRouteQueryCallback` with synthetic `_PRT` data.
