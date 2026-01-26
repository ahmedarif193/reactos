/*
 * Advanced kernel testing functionality
 *
 * This module provides more sophisticated implementations for:
 * - Real PCI device enumeration via HAL
 * - ACPI table parsing
 * - Registry access
 * - I/O port operations
 */

#![allow(dead_code)]

// Import types from parent module (which uses wdk-sys)
use crate::{PVOID, ULONG, PHYSICAL_ADDRESS, dprint1, dprint_hex};
use crate::{MEMORY_CACHING_TYPE_MM_NON_CACHED, MmMapIoSpace, MmUnmapIoSpace};

// ============================================================================
// PCI Access via HAL
// ============================================================================

pub const PCI_TYPE0_ADDRESSES: usize = 6;
pub const PCI_TYPE1_ADDRESSES: usize = 2;
pub const PCI_TYPE2_ADDRESSES: usize = 5;

#[repr(C)]
pub struct PCI_SLOT_NUMBER {
    pub u: PciSlotBits,
}

#[repr(C)]
pub union PciSlotBits {
    pub bits: core::mem::ManuallyDrop<PciSlotBitFields>,
    pub AsULONG: ULONG,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct PciSlotBitFields {
    pub DeviceNumber: u32,  // bits 0-4
    pub FunctionNumber: u32, // bits 5-7
    pub Reserved: u32,       // bits 8-31
}

// HAL PCI configuration space access
#[link(name = "hal")]
extern "system" {
    pub fn HalGetBusData(
        BusDataType: u32,
        BusNumber: ULONG,
        SlotNumber: ULONG,
        Buffer: PVOID,
        Length: ULONG,
    ) -> ULONG;

    pub fn HalGetBusDataByOffset(
        BusDataType: u32,
        BusNumber: ULONG,
        SlotNumber: ULONG,
        Buffer: PVOID,
        Offset: ULONG,
        Length: ULONG,
    ) -> ULONG;

    pub fn HalSetBusData(
        BusDataType: u32,
        BusNumber: ULONG,
        SlotNumber: ULONG,
        Buffer: PVOID,
        Length: ULONG,
    ) -> ULONG;
}

// BusDataType values
const PCI_CONFIGURATION: u32 = 4;

/// Read PCI configuration space for a device
pub fn read_pci_config(bus: u32, device: u32, function: u32, offset: u32, size: u32) -> Option<u32> {
    // PCI_SLOT_NUMBER: bits 0-4 = DeviceNumber, bits 5-7 = FunctionNumber
    let slot_number = (function << 5) | device;
    let mut data: u32 = 0;

    unsafe {
        let bytes_read = HalGetBusDataByOffset(
            PCI_CONFIGURATION,
            bus,
            slot_number,
            &mut data as *mut _ as PVOID,
            offset,
            size,
        );

        if bytes_read == size {
            Some(data)
        } else {
            None
        }
    }
}

// PCI Capability IDs
const PCI_CAPABILITY_ID_MSI: u8 = 0x05;
const PCI_CAPABILITY_ID_MSIX: u8 = 0x11;
const PCI_CAPABILITY_ID_PCIE: u8 = 0x10;
const PCI_CAPABILITY_ID_POWER_MANAGEMENT: u8 = 0x01;

/// Find PCI capability by ID
fn find_pci_capability(bus: u32, device: u32, function: u32, cap_id: u8) -> Option<u8> {
    // Read Status register (offset 0x06)
    if let Some(status_cmd) = read_pci_config(bus, device, function, 0x04, 4) {
        let status = (status_cmd >> 16) & 0xFFFF;

        // Check if capabilities list is supported (bit 4)
        if (status & 0x10) == 0 {
            return None;
        }

        // Read Capabilities Pointer (offset 0x34)
        if let Some(cap_ptr_data) = read_pci_config(bus, device, function, 0x34, 4) {
            let mut cap_ptr = (cap_ptr_data & 0xFF) as u8;

            // Walk the capability list
            while cap_ptr != 0 && cap_ptr != 0xFF {
                if let Some(cap_data) = read_pci_config(bus, device, function, cap_ptr as u32, 4) {
                    let current_cap_id = (cap_data & 0xFF) as u8;
                    if current_cap_id == cap_id {
                        return Some(cap_ptr);
                    }
                    // Next capability pointer is in bits 8-15
                    cap_ptr = ((cap_data >> 8) & 0xFF) as u8;
                } else {
                    break;
                }
            }
        }
    }
    None
}

/// Enumerate all PCI devices and print modern capabilities
pub fn enumerate_pci_devices() {
    dprint1("=== PCI Device Enumeration (Modern Capabilities) ===");

    let mut device_count = 0;

    // Scan first few buses (0-3) to avoid long boot times
    for bus in 0..4u32 {
        for device in 0..32u32 {
            for function in 0..8u32 {
                // Read VendorID (first 16 bits)
                if let Some(vendor_device) = read_pci_config(bus, device, function, 0, 4) {
                    let vendor_id = (vendor_device & 0xFFFF) as u16;
                    let device_id = ((vendor_device >> 16) & 0xFFFF) as u16;

                    // VendorID of 0xFFFF means no device present
                    if vendor_id == 0xFFFF {
                        continue;
                    }

                    device_count += 1;

                    // Print device information
                    dprint1("");
                    dprint_hex("PCI Device - Bus:Dev:Fn = ", (bus << 16) | (device << 8) | function);
                    dprint_hex("  VendorID:DeviceID = ", (vendor_id as u32) | ((device_id as u32) << 16));

                    // Read class code
                    if let Some(class_data) = read_pci_config(bus, device, function, 0x08, 4) {
                        let class_code = (class_data >> 8) & 0xFFFFFF;
                        dprint_hex("  Class: ", class_code);
                    }

                    // Detect modern capabilities
                    let mut caps = [0u8; 64];
                    let mut cap_str_len = 0;

                    // Check for MSI
                    if find_pci_capability(bus, device, function, PCI_CAPABILITY_ID_MSI).is_some() {
                        for &b in b"MSI " {
                            caps[cap_str_len] = b;
                            cap_str_len += 1;
                        }
                    }

                    // Check for MSI-X
                    if let Some(msix_cap_offset) = find_pci_capability(bus, device, function, PCI_CAPABILITY_ID_MSIX) {
                        for &b in b"MSI-X " {
                            caps[cap_str_len] = b;
                            cap_str_len += 1;
                        }

                        // Read MSI-X table size from Message Control register
                        if let Some(msix_ctrl) = read_pci_config(bus, device, function, msix_cap_offset as u32, 4) {
                            let table_size = ((msix_ctrl >> 16) & 0x7FF) + 1;
                            dprint_hex("  MSI-X Vectors: ", table_size);
                        }
                    }

                    // Check for PCIe
                    if find_pci_capability(bus, device, function, PCI_CAPABILITY_ID_PCIE).is_some() {
                        for &b in b"PCIe " {
                            caps[cap_str_len] = b;
                            cap_str_len += 1;
                        }
                    }

                    // Check for PM
                    if find_pci_capability(bus, device, function, PCI_CAPABILITY_ID_POWER_MANAGEMENT).is_some() {
                        for &b in b"PM" {
                            caps[cap_str_len] = b;
                            cap_str_len += 1;
                        }
                    }

                    // Print capabilities if any found
                    if cap_str_len > 0 {
                        caps[cap_str_len] = 0;
                        let mut buffer = [0u8; 128];
                        let prefix = b"  Capabilities: ";
                        let mut pos = 0;
                        for &b in prefix {
                            buffer[pos] = b;
                            pos += 1;
                        }
                        for i in 0..cap_str_len {
                            buffer[pos] = caps[i];
                            pos += 1;
                        }
                        buffer[pos] = b'\n';
                        buffer[pos + 1] = 0;
                        unsafe {
                            crate::DbgPrint(buffer.as_ptr());
                        }
                    }

                    // For non-multifunction devices, don't scan other functions
                    if function == 0 {
                        if let Some(header) = read_pci_config(bus, device, function, 0x0C, 4) {
                            let header_type = ((header >> 16) & 0xFF) as u8;
                            if (header_type & 0x80) == 0 {
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    dprint1("");
    dprint_hex("Total PCI devices: ", device_count);
}


// ============================================================================
// Memory-Mapped I/O (MMIO) Operations
// ============================================================================

/// Map physical address to virtual address for MMIO access
pub unsafe fn map_mmio(physical_address: u64, size: usize) -> Option<*mut u8> {
    let phys_addr = PHYSICAL_ADDRESS {
        QuadPart: physical_address as i64,
    };

    let virt_addr = MmMapIoSpace(phys_addr, size, MEMORY_CACHING_TYPE_MM_NON_CACHED);

    if virt_addr.is_null() {
        None
    } else {
        Some(virt_addr as *mut u8)
    }
}

/// Unmap MMIO region
pub unsafe fn unmap_mmio(virtual_address: *mut u8, size: usize) {
    if !virtual_address.is_null() {
        MmUnmapIoSpace(virtual_address as PVOID, size);
    }
}

/// Read 32-bit value from MMIO address
pub unsafe fn mmio_read32(addr: *const u8) -> u32 {
    core::ptr::read_volatile(addr as *const u32)
}

/// Write 32-bit value to MMIO address
pub unsafe fn mmio_write32(addr: *mut u8, value: u32) {
    core::ptr::write_volatile(addr as *mut u32, value);
}

/// Read 64-bit value from MMIO address
pub unsafe fn mmio_read64(addr: *const u8) -> u64 {
    core::ptr::read_volatile(addr as *const u64)
}

/// Test MMIO operations (amd64/ARM64)
#[cfg(any(target_arch = "x86_64", target_arch = "aarch64"))]
pub fn test_mmio_operations() {
    dprint1("=== Memory-Mapped I/O (MMIO) Test ===");

    unsafe {
        // Example: Try to map and read from LAPIC base (0xFEE00000 on x86/x64)
        // Note: This is safe to read on x86/x64 systems with APIC
        #[cfg(target_arch = "x86_64")]
        {
            dprint1("Testing x86_64 MMIO access...");
            dprint1("  Example: Local APIC base = 0xFEE00000");

            // LAPIC base address
            let lapic_base: u64 = 0xFEE00000;
            let map_size: usize = 4096; // One page

            if let Some(virt_addr) = map_mmio(lapic_base, map_size) {
                dprint1("  [+] Successfully mapped LAPIC MMIO region");
                dprint_hex("  Virtual address: ", virt_addr as u32);

                // Read LAPIC ID register (offset 0x20)
                let lapic_id = mmio_read32(virt_addr.add(0x20));
                dprint_hex("  LAPIC ID register: ", lapic_id);

                // Read LAPIC Version register (offset 0x30)
                let lapic_version = mmio_read32(virt_addr.add(0x30));
                dprint_hex("  LAPIC Version: ", lapic_version);

                // Cleanup
                unmap_mmio(virt_addr, map_size);
                dprint1("  [+] MMIO region unmapped");
            } else {
                dprint1("  [-] Failed to map LAPIC MMIO region");
            }
        }

        #[cfg(target_arch = "aarch64")]
        {
            dprint1("Testing ARM64 MMIO access...");
            dprint1("  ARM64 uses MMIO exclusively (no I/O ports)");
            dprint1("  Example regions: GIC, UART, timers, etc.");
            dprint1("  MMIO framework available for device access");
        }
    }

    dprint1("MMIO operations test complete");
}

// ============================================================================
// I/O Port Operations (i386 only - amd64 should prefer MMIO)
// ============================================================================

/// Read from I/O port (i386 only - use MMIO on amd64)
#[cfg(target_arch = "x86")]
pub unsafe fn inb(port: u16) -> u8 {
    let result: u8;
    core::arch::asm!("in al, dx", out("al") result, in("dx") port, options(nomem, nostack));
    result
}

/// Write to I/O port (i386 only - use MMIO on amd64)
#[cfg(target_arch = "x86")]
pub unsafe fn outb(port: u16, value: u8) {
    core::arch::asm!("out dx, al", in("dx") port, in("al") value, options(nomem, nostack));
}

/// Test I/O port operations (i386 only)
#[cfg(target_arch = "x86")]
pub fn test_io_ports() {
    dprint1("=== I/O Port Operations Test (i386) ===");

    unsafe {
        // Read from POST code port (usually safe to read)
        let post_code = inb(0x80);
        dprint_hex("POST code port (0x80): ", post_code as u32);

        // Read CMOS RTC seconds register (safe, read-only test)
        outb(0x70, 0x00); // Select register 0 (seconds)
        let seconds = inb(0x71);
        dprint_hex("RTC seconds (BCD): ", seconds as u32);
    }

    dprint1("I/O port test complete");
    dprint1("Note: On amd64, prefer MMIO over I/O ports");
}

