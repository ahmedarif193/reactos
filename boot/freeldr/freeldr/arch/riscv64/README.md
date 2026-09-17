# RISC-V FreeLdr firmware stage

RISC-V uses the shared FreeLdr UEFI target in `uefi.cmake`, including GOP initialization and the framebuffer menu. The NT kernel handoff lives in `ntldr/arch/riscv64/winldr.c`: it builds the Sv39 bootstrap tables, resolves the firmware console from the device tree, and fills version 3 of the `LOADER_PARAMETER_BLOCK.u.Riscv64` payload before `ExitBootServices`.

The port's build instructions, ABI reference, implementation status, and decision history are maintained in [docs/riscv64](../../../../../docs/riscv64/README.md) for later wiki migration.

## Kernel handoff

- **Page tables.** The temporary identity map may use 1 GiB and 2 MiB leaves. The direct map at `RISCV64_LOADER_DIRECT_MAP_BASE + PA` and every early device window are built from 4 KiB leaves only (`RISCV64_LOADER_FLAG_DIRECT_MAP_4K`), so the kernel page-table window can address a final-level PTE for every mapped page. See [Page-table window design](../../../../../docs/riscv64/page-table-window.md#physical-map-policy).
- **Page-table pool.** One contiguous `LoaderMemoryData` block sized from the memory map: level-0 tables for a 4 KiB direct map of all RAM, level-1 tables for the same span plus one, an identity-map estimate bounded by the number of type runs in the page lookup table, and a fixed margin. The kernel reclaims it as a single descriptor.
- **Early console.** `/chosen/stdout-path` (alias or path, optional `:baud` suffix) selects the console node. A `compatible` entry of `ns16550a`, `ns16550`, `ns8250`, `ns16450`, or `snps,dw-apb-uart` is accepted; `reg`, `reg-shift`, `reg-io-width`, `clock-frequency`, and `current-speed` fill the `EarlyConsole*` fields and the page-rounded register window becomes `EarlyDeviceRanges[0]`, mapped read/write, global, non-executable in the direct map. The loader never touches the UART. Without a usable node the payload reports `RISCV64_EARLY_CONSOLE_NONE` and the kernel uses the SBI debug console. See [Boot console](../../../../../docs/riscv64/boot-console.md).

## Build and run

Use [Build and validate the port](../../../../../docs/riscv64/build-and-validate.md). Build `uefildr` for the EFI loader or `livecd` for bootable media. The obsolete standalone text-console prototype and its tests have been removed.

The current local build uses `$HOME/.local/opt/rosbe/llvm-mingw-riscv24`; see [Toolchain architecture](../../../../../docs/riscv64/toolchain.md).

## ABI and validation

FreeLdr uses generic RISC-V ELF compilation, explicit NT typedef adaptations, and EDK2 `GenFw` conversion to UEFI PE. This is not general RISC-V Windows COFF linking or evidence of a complete NT ABI.

- [ABI reference](../../../../../docs/riscv64/abi-reference.md)
- [FreeLdr and kernel handoff](../../../../../docs/riscv64/boot-handoff.md)
- [Validation results and known limitations](../../../../../docs/riscv64/validation.md)
- [SEH direction and minimal bring-up policy](../../../../../docs/riscv64/exceptions.md)
