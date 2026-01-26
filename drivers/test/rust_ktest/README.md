# Rust Kernel Test Driver

A boot-loaded kernel driver written in Rust for testing ReactOS kernel functionality.

## Overview

This driver demonstrates Rust kernel-mode programming on ReactOS using type definitions compatible with Microsoft's WDK. It performs basic kernel tests at boot time including:

- System information queries
- Memory allocation tests
- PCI device enumeration (framework)
- ACPI information queries (framework)
- Optional advanced hardware tests (MMIO, PCI, ACPI, Registry)

## Architecture

### Type Definitions

The driver uses manually defined types in `src/wdk_types.rs` that are binary-compatible with:
- Microsoft's wdk-sys crate
- Windows NT kernel structures
- ReactOS kernel structures

This approach avoids cross-compilation issues with Microsoft's wdk-sys crate (which requires Windows host) while maintaining full compatibility.

### Structure

```
drivers/test/rust_ktest/
├── Cargo.toml          # Rust package configuration
├── CMakeLists.txt      # ReactOS build integration
├── src/
│   ├── lib.rs          # Driver entry point and basic tests
│   ├── wdk_types.rs    # WDK-compatible type definitions
│   └── advanced.rs     # Advanced hardware tests (optional)
└── rust_ktest.inf      # Driver installation file
```

## Building

The driver is built automatically as part of the ReactOS build when Rust is enabled:

```bash
cmake -DENABLE_RUST=ON ...
ninja rust_ktest
```

### Build Configuration

- **Target**: `x86_64-pc-windows-gnu` (amd64), `i686-pc-windows-gnu` (i386), or `aarch64-pc-windows-gnu` (ARM64)
- **Linking**: Links against ReactOS `ntoskrnl.a` and `hal.a`
- **Profile**: Debug builds use `opt-level=2` for better debugging while maintaining performance

### Features

- `advanced_tests`: Enable real hardware access tests (PCI, ACPI, I/O ports, Registry)
  ```bash
  # In CMakeLists.txt, set:
  set(RUST_KTEST_FEATURES "advanced_tests")
  ```

## Design Principles

### No Build Options

The driver has been simplified to remove all conditional compilation related to binding sources:
- ✅ Uses WDK-compatible types directly
- ❌ No `use_ms_wdk` feature
- ❌ No `use_windows_ddk` feature
- ❌ No bindgen dependency
- ❌ No build.rs script

### Cross-Platform Compatibility

- Compiles on Linux, macOS, and Windows hosts
- Targets Windows/ReactOS kernel for all architectures
- No WDK installation required
- No Windows-specific build tools needed

### Safety

- Uses `#![no_std]` for kernel-mode compatibility
- Minimal unsafe code (only FFI boundaries)
- Clear separation between safe and unsafe operations
- Proper panic handler for kernel mode

### Hardware Access Methods

The driver uses architecture-appropriate methods for hardware access:

- **amd64 (x86_64)**: Uses **MMIO** (Memory-Mapped I/O) via `MmMapIoSpace()`
  - Modern approach, portable, supports 64-bit addressing
  - Examples: LAPIC, PCI BARs, device registers
  - Preferred over legacy I/O ports

- **i386 (x86)**: Uses **I/O ports** via `IN/OUT` instructions
  - Legacy method, limited to 64KB address space
  - Examples: 0x80 (POST), 0x70/0x71 (CMOS/RTC)

- **ARM64 (aarch64)**: Uses **MMIO exclusively**
  - No I/O port concept on ARM
  - All device access via memory-mapped registers

## Testing

The driver loads at boot time and runs tests automatically. Output is visible in:
- ReactOS debug log (qemu -debugcon file:debug.log)
- Serial console
- Kernel debugger

Expected output:
```
==============================================
Rust Kernel Test Driver v0.1.0
Boot-time kernel testing framework
Using Microsoft wdk-sys compatible types
==============================================
=== System Information ===
Kernel test driver loaded successfully
Testing kernel subsystems:
  [+] Memory Manager: Available
  [+] Object Manager: Available
  [+] I/O Manager: Available
  [+] Executive Support: Available
  Architecture: amd64 (x86_64)
System information test complete

=== Memory Allocation Tests ===
  [+] Small allocation (256 bytes): SUCCESS
  [+] Small allocation freed: SUCCESS
  [+] Large allocation (4KB): SUCCESS
  [+] Large allocation freed: SUCCESS
Memory allocation tests complete
...
```

## Future Enhancements

When Microsoft's wdk-sys crate supports cross-compilation from non-Windows hosts, we can:
1. Add wdk-sys as a dependency in Cargo.toml
2. Replace manual type definitions with `pub use wdk_sys::*`
3. Keep everything else the same (binary compatibility)

Current blocker: wdk-sys build scripts require Windows filesystem APIs (symlink_file).

## References

- [Microsoft windows-drivers-rs](https://github.com/microsoft/windows-drivers-rs)
- [Windows Driver Kit Documentation](https://docs.microsoft.com/windows-hardware/drivers/)
- [ReactOS Driver Development](https://reactos.org/wiki/Driver_Development)
