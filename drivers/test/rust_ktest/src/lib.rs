#![no_std]
#![allow(non_snake_case)]
#![allow(non_camel_case_types)]
#![allow(dead_code)]

use core::panic::PanicInfo;

// ============================================================================
// Windows Driver Kit Type Definitions
// ============================================================================
// We use manually defined types compatible with Microsoft's wdk-sys crate.
// This avoids cross-compilation issues with wdk-sys on non-Windows hosts.
// Types are binary-compatible with Windows NT kernel structures.

mod wdk_types;

// Re-export commonly used types
pub use wdk_types::{
    DRIVER_OBJECT,
    DEVICE_OBJECT,
    DRIVER_EXTENSION,
    UNICODE_STRING,
    NTSTATUS,
    PVOID,
    ULONG,
    USHORT,
    UCHAR,
    HANDLE,
    STATUS_SUCCESS,
    STATUS_UNSUCCESSFUL,
    PCI_COMMON_CONFIG,
    PHYSICAL_ADDRESS,
    MEMORY_CACHING_TYPE_MM_NON_CACHED,
    MEMORY_CACHING_TYPE_MM_CACHED,
    MEMORY_CACHING_TYPE_MM_WRITE_COMBINED,
    RTL_OSVERSIONINFOW,
};

// Import external kernel functions
// Note: wdk-sys provides the types, but we link against ReactOS ntoskrnl.a and hal.a
#[link(name = "ntoskrnl")]
extern "system" {
    pub fn DbgPrint(Format: *const u8, ...) -> NTSTATUS;
    pub fn ExAllocatePoolWithTag(PoolType: u32, NumberOfBytes: usize, Tag: u32) -> PVOID;
    pub fn ExFreePoolWithTag(P: PVOID, Tag: u32);
    pub fn RtlQueryRegistryValues(
        RelativeTo: ULONG,
        Path: *const u16,
        QueryTable: PVOID,
        Context: PVOID,
        Environment: PVOID,
    ) -> NTSTATUS;
    pub fn RtlInitUnicodeString(DestinationString: *mut UNICODE_STRING, SourceString: *const u16);
    pub fn IoGetDeviceProperty(
        DeviceObject: *mut DEVICE_OBJECT,
        DeviceProperty: u32,
        BufferLength: ULONG,
        PropertyBuffer: PVOID,
        ResultLength: *mut ULONG,
    ) -> NTSTATUS;
    pub fn MmMapIoSpace(
        PhysicalAddress: PHYSICAL_ADDRESS,
        NumberOfBytes: usize,
        CacheType: u32,
    ) -> PVOID;
    pub fn MmUnmapIoSpace(
        BaseAddress: PVOID,
        NumberOfBytes: usize,
    );
    pub fn RtlGetVersion(VersionInformation: *mut RTL_OSVERSIONINFOW) -> NTSTATUS;
}

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
}

// Advanced testing features (optional)
pub mod advanced;

// ============================================================================
// Additional constants
// ============================================================================

const DPFLTR_IHVDRIVER_ID: u32 = 77;
const DPFLTR_ERROR_LEVEL: u32 = 0;
const DPFLTR_WARNING_LEVEL: u32 = 1;
const DPFLTR_INFO_LEVEL: u32 = 3;

// PCI Configuration Space offsets
const PCI_VENDOR_ID: u32 = 0x00;
const PCI_DEVICE_ID: u32 = 0x02;
const PCI_COMMAND: u32 = 0x04;
const PCI_CLASS_CODE: u32 = 0x0B;
const PCI_HEADER_TYPE: u32 = 0x0E;

// ============================================================================
// Debug print wrappers
// ============================================================================

/// Print a message at INFO level
pub fn dprint_info(msg: &str) {
    let mut buffer = [0u8; 512];
    let bytes = msg.as_bytes();
    let len = bytes.len().min(510);
    buffer[..len].copy_from_slice(&bytes[..len]);
    buffer[len] = b'\n';
    buffer[len + 1] = 0;

    unsafe {
        DbgPrint(buffer.as_ptr());
    }
}

/// Print a formatted message (simplified - only supports string substitution)
pub fn dprint1(format: &str) {
    dprint_info(format);
}

/// Print a hexadecimal value
pub fn dprint_hex(prefix: &str, value: u32) {
    let mut buffer = [0u8; 256];
    let mut pos = 0;

    // Copy prefix
    for &b in prefix.as_bytes() {
        if pos >= 240 { break; }
        buffer[pos] = b;
        pos += 1;
    }

    // Add "0x"
    buffer[pos] = b'0';
    buffer[pos + 1] = b'x';
    pos += 2;

    // Convert to hex
    let hex_chars = b"0123456789ABCDEF";
    for i in (0..8).rev() {
        let nibble = ((value >> (i * 4)) & 0xF) as usize;
        buffer[pos] = hex_chars[nibble];
        pos += 1;
    }

    buffer[pos] = b'\n';
    buffer[pos + 1] = 0;

    unsafe {
        DbgPrint(buffer.as_ptr());
    }
}

// ============================================================================
// System information tests
// ============================================================================

pub fn test_system_info() {
    // Architecture detection (compile time)
    #[cfg(target_arch = "x86")]
    dprint1("Architecture: i386 (x86)");

    #[cfg(target_arch = "x86_64")]
    dprint1("Architecture: amd64 (x86_64)");

    #[cfg(target_arch = "aarch64")]
    dprint1("Architecture: ARM64 (AArch64)");

    // Query Windows version to determine kernel alignment
    unsafe {
        let mut version_info: RTL_OSVERSIONINFOW = core::mem::zeroed();
        version_info.dwOSVersionInfoSize = core::mem::size_of::<RTL_OSVERSIONINFOW>() as u32;

        let status = RtlGetVersion(&mut version_info);
        if status == STATUS_SUCCESS {
            let major = version_info.dwMajorVersion;
            let minor = version_info.dwMinorVersion;
            let build = version_info.dwBuildNumber;

            // Determine Windows version alignment
            let win_version = match (major, minor) {
                (5, 0) => "Windows 2000",
                (5, 1) => "Windows XP",
                (5, 2) => "Windows Server 2003",
                (6, 0) => "Windows Vista / Server 2008",
                (6, 1) => "Windows 7 / Server 2008 R2",
                (6, 2) => "Windows 8",
                (6, 3) => "Windows 8.1",
                (10, 0) if build >= 22000 => "Windows 11",
                (10, 0) => "Windows 10",
                _ => "Unknown",
            };

            dprint1("Kernel Version:");
            dprint_hex("  NT Version: ", (major << 16) | minor);
            dprint_hex("  Build: ", build);

            // Print alignment info
            let mut buffer = [0u8; 128];
            let prefix = "  Aligned with: ";
            let mut pos = 0;
            for &b in prefix.as_bytes() {
                buffer[pos] = b;
                pos += 1;
            }
            for &b in win_version.as_bytes() {
                if pos >= 120 { break; }
                buffer[pos] = b;
                pos += 1;
            }
            buffer[pos] = b'\n';
            buffer[pos + 1] = 0;
            DbgPrint(buffer.as_ptr());
        }
    }
}

// ============================================================================
// Memory tests
// ============================================================================

pub fn test_memory_allocation() {
    const POOL_TAG: u32 = 0x4B545352; // 'RSTK'
    const NON_PAGED_POOL: u32 = 0;

    unsafe {
        // Test allocations
        let ptr1 = ExAllocatePoolWithTag(NON_PAGED_POOL, 256, POOL_TAG);
        let ptr2 = ExAllocatePoolWithTag(NON_PAGED_POOL, 4096, POOL_TAG);

        if !ptr1.is_null() && !ptr2.is_null() {
            dprint1("Memory: Pool allocation tests passed (256B, 4KB)");
            ExFreePoolWithTag(ptr1, POOL_TAG);
            ExFreePoolWithTag(ptr2, POOL_TAG);
        } else {
            dprint1("Memory: Pool allocation test FAILED");
        }
    }
}

// ============================================================================
// Driver unload handler
// ============================================================================

extern "system" fn driver_unload(driver_object: *mut DRIVER_OBJECT) {
    dprint1("Rust Kernel Test Driver Unloading...");

    // Perform cleanup here if needed
    let _ = driver_object; // Suppress unused warning
}

// ============================================================================
// Driver entry point
// ============================================================================

#[no_mangle]
pub extern "system" fn DriverEntry(
    driver_object: *mut DRIVER_OBJECT,
    _registry_path: *mut UNICODE_STRING,
) -> NTSTATUS {
    // Entry log to verify driver initialization
    dprint1("rust_ktest entered");
    dprint1("Rust Kernel Test Driver v0.1.0 - Boot-time testing framework");

    // Set unload routine
    if !driver_object.is_null() {
        unsafe {
            (*driver_object).DriverUnload = Some(driver_unload);
        }
    }

    // Run tests that gather REAL kernel information
    test_system_info();           // Queries actual NT version from kernel
    test_memory_allocation();      // Actually allocates and frees pool memory

    // Advanced tests - REAL hardware enumeration
    #[cfg(feature = "advanced_tests")]
    {
        dprint1("Running advanced hardware tests...");

        // Real PCI device enumeration with MSI-X detection
        advanced::enumerate_pci_devices();

        // MMIO operations (amd64 and ARM64)
        #[cfg(any(target_arch = "x86_64", target_arch = "aarch64"))]
        {
            advanced::test_mmio_operations();
        }

        // I/O ports (i386 only)
        #[cfg(target_arch = "x86")]
        {
            advanced::test_io_ports();
        }
    }

    STATUS_SUCCESS
}

// ============================================================================
// Panic handler (required for no_std)
// ============================================================================

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    // Try to print panic information
    dprint1("KERNEL PANIC in Rust driver!");

    if let Some(location) = info.location() {
        // Would need more sophisticated formatting here
        dprint1("Panic location available (formatting limited)");
        let _ = location; // Use it to avoid warning
    }

    // In newer Rust, message() returns PanicMessage directly, not Option
    let _msg = info.message();
    dprint1("Panic message available (formatting limited)");

    // In kernel mode, we cannot return from panic
    // Loop forever (similar to Linux kernel panic behavior)
    loop {
        #[cfg(target_arch = "x86")]
        unsafe { core::arch::asm!("hlt") };

        #[cfg(target_arch = "x86_64")]
        unsafe { core::arch::asm!("hlt") };

        #[cfg(target_arch = "aarch64")]
        unsafe { core::arch::asm!("wfi") };
    }
}

// ============================================================================
// Required for no_std
// ============================================================================

#[no_mangle]
pub extern "C" fn __CxxFrameHandler3() -> i32 {
    0
}

#[no_mangle]
pub extern "C" fn _fltused() -> i32 {
    0
}

// Stub for rust_eh_personality (needed even with panic=abort in some cases)
#[no_mangle]
pub extern "C" fn rust_eh_personality() {}

// Additional stubs that may be needed
#[no_mangle]
pub extern "C" fn _Unwind_Resume() {
    loop {}
}
