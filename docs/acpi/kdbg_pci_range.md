# KDBG `pcirange` Command

`pcirange` prints the PCI bus span that the HAL currently exposes via `HalQueryPciBusRange`. Use it inside KDBG after the kernel boots to confirm the firmware-supplied clamp that now protects configuration space accesses.

```text
kdb:> pcirange
HAL PCI bus span : [0 - 63]
```

If the HAL could not determine a range (legacy firmware path) you will see:

```text
kdb:> pcirange
HAL has not published a PCI bus range (legacy firmware path).
```

### Usage Notes
- Run the command before issuing manual `!pci*` reads to ensure you stay inside the firmware-advertised window.
- The guard applies to kd64 `rdmsr`/`wrmsr` style PCI helpers as well, so this output reflects the limits enforced by the debugger.
- On multi-root systems this command reports the global minimum and maximum currently published by the HAL; inspect ACPI logs for per-root windows if needed.

### Verification Checklist
- Run `pcirange` in KDBG and record the `[min - max]` span.
- Compare against HAL logs (`HalInitSystem` traces) to validate the same bounds were detected during boot.
- When testing multi-root systems, correlate each ACPI `_CRS` bus window with the global span to confirm no root escaped the clamp.
