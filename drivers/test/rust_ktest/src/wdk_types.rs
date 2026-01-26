/*
 * Windows Driver Kit Type Definitions
 *
 * This module provides type definitions compatible with Microsoft's wdk-sys crate.
 * We define them manually to avoid cross-compilation issues with wdk-sys on non-Windows hosts.
 *
 * These types are binary-compatible with Windows NT kernel structures
 * and work with both Windows and ReactOS kernels.
 *
 * Reference: https://github.com/microsoft/windows-drivers-rs
 */

#![allow(non_snake_case)]
#![allow(non_camel_case_types)]
#![allow(dead_code)]

use core::ffi::c_void;

// ============================================================================
// Basic types
// ============================================================================

pub type NTSTATUS = i32;
pub type PVOID = *mut c_void;
pub type ULONG = u32;
pub type USHORT = u16;
pub type UCHAR = u8;
pub type HANDLE = PVOID;
pub type LONG = i32;
pub type BOOLEAN = u8;
pub type ULONGLONG = u64;

// Physical address type (64-bit on all platforms)
#[repr(C)]
#[derive(Copy, Clone)]
pub struct PHYSICAL_ADDRESS {
    pub QuadPart: i64,
}

// Memory caching types
pub const MEMORY_CACHING_TYPE_MM_NON_CACHED: u32 = 0;
pub const MEMORY_CACHING_TYPE_MM_CACHED: u32 = 1;
pub const MEMORY_CACHING_TYPE_MM_WRITE_COMBINED: u32 = 2;

// OS Version structure
#[repr(C)]
pub struct RTL_OSVERSIONINFOW {
    pub dwOSVersionInfoSize: ULONG,
    pub dwMajorVersion: ULONG,
    pub dwMinorVersion: ULONG,
    pub dwBuildNumber: ULONG,
    pub dwPlatformId: ULONG,
    pub szCSDVersion: [u16; 128],
}

// ============================================================================
// Status codes
// ============================================================================

pub const STATUS_SUCCESS: NTSTATUS = 0;
pub const STATUS_UNSUCCESSFUL: NTSTATUS = -1073741823i32; // 0xC0000001

// ============================================================================
// Kernel structures
// ============================================================================

#[repr(C)]
pub struct UNICODE_STRING {
    pub Length: USHORT,
    pub MaximumLength: USHORT,
    pub Buffer: *mut u16,
}

#[repr(C)]
pub struct DRIVER_OBJECT {
    pub Type: i16,
    pub Size: i16,
    pub DeviceObject: *mut DEVICE_OBJECT,
    pub Flags: ULONG,
    pub DriverStart: PVOID,
    pub DriverSize: ULONG,
    pub DriverSection: PVOID,
    pub DriverExtension: *mut DRIVER_EXTENSION,
    pub DriverName: UNICODE_STRING,
    pub HardwareDatabase: *mut UNICODE_STRING,
    pub FastIoDispatch: PVOID,
    pub DriverInit: PVOID,
    pub DriverStartIo: PVOID,
    pub DriverUnload: Option<extern "system" fn(*mut DRIVER_OBJECT)>,
    pub MajorFunction: [PVOID; 28],
}

#[repr(C)]
pub struct DRIVER_EXTENSION {
    pub DriverObject: *mut DRIVER_OBJECT,
    pub AddDevice: PVOID,
    pub Count: ULONG,
    pub ServiceKeyName: UNICODE_STRING,
}

#[repr(C)]
pub struct DEVICE_OBJECT {
    pub Type: i16,
    pub Size: USHORT,
    pub ReferenceCount: i32,
    pub DriverObject: *mut DRIVER_OBJECT,
    pub NextDevice: *mut DEVICE_OBJECT,
    pub AttachedDevice: *mut DEVICE_OBJECT,
    // Simplified - many more fields exist in full structure
}

#[repr(C)]
pub struct PCI_COMMON_CONFIG {
    pub VendorID: USHORT,
    pub DeviceID: USHORT,
    pub Command: USHORT,
    pub Status: USHORT,
    pub RevisionID: UCHAR,
    pub ProgIf: UCHAR,
    pub SubClass: UCHAR,
    pub BaseClass: UCHAR,
    pub CacheLineSize: UCHAR,
    pub LatencyTimer: UCHAR,
    pub HeaderType: UCHAR,
    pub BIST: UCHAR,
    pub BaseAddresses: [ULONG; 6],
    pub CardbusCISPtr: ULONG,
    pub SubVendorID: USHORT,
    pub SubSystemID: USHORT,
    pub ROMBaseAddress: ULONG,
    pub CapabilitiesPtr: UCHAR,
    pub Reserved1: [UCHAR; 3],
    pub Reserved2: ULONG,
    pub InterruptLine: UCHAR,
    pub InterruptPin: UCHAR,
    pub MinimumGrant: UCHAR,
    pub MaximumLatency: UCHAR,
}
