# Windows 10 Build Compatibility Issues - ReactOS

## Overview
This document summarizes the compatibility issues encountered when building ReactOS with Windows 10 target version (NTDDI_VERSION=0x0A000000) and the fixes applied.

## Key Issues and Resolutions

### 1. Structure Field Evolution
Many kernel structures changed between Windows versions. Fields present in older versions were removed in Windows 10.

#### Affected Files and Fields:
- **sdk/include/asm/ksx.template.h**
  - `KUSER_SHARED_DATA.Reserved2` - Not present in Win10
  - `KUSER_SHARED_DATA.SystemCall` - Not present in Win10
  - `KUSER_SHARED_DATA.SystemCallReturn` - Not present in Win10
  - `KUSER_SHARED_DATA.Wow64SharedInformation` - Not present in Win10
  - `KWAIT_BLOCK.NextWaitBlock` - Not present in Win10
  
- **sdk/include/asm/ksamd64.template.h**
  - `KPROCESS.LdtSystemDescriptor` - Not present in Win10
  - `KPROCESS.LdtBaseAddress` - Not present in Win10
  - `KTHREAD.LargeStack` - Not present in Win10
  - Typo: `EnabledSupervisorFeaturestures` → `EnabledSupervisorFeatures`

### 2. TEB Field Name Changes
- **dll/ntdll/rtl/libsupp.c**
  - Changed `InDbgPrint` to `DbgInDebugPrint` (Windows 10 naming)

### 3. Header Conflicts (NDK vs XDK/WDM)
Systemic conflicts between Native Development Kit headers and Windows Driver headers when building for Windows 10.

#### Resolution Strategies:
- Added include guards for duplicate definitions
- Added forward declarations for missing types
- Renamed duplicate members with suffix "2"
- Changed non-standard `VOLATILE` to standard `volatile`

#### Specific Fixes:
- **sdk/include/ndk/iotypes.h**
  - Added `DISPATCHER_HEADER` with include guards
  - Added forward declarations for various types
  - Added `RTL_AVL_TREE` structure definition

- **sdk/include/ndk/pstypes.h**  
  - Fixed `KWAIT_REASON` enum with include guards
  - Changed `VOLATILE` to `volatile` throughout
  - Fixed duplicate `Unused` → `Unused2`
  - Fixed types: `PTR_ADDEND` → `ULONG_PTR`, `PTRAP_FRAME` → `PVOID`

- **sdk/include/ndk/peb_teb.h**
  - Fixed duplicate `Reserved` → `Reserved2`
  - Fixed duplicate `SystemAssemblyStorageMap` → `SystemAssemblyStorageMap2`

### 4. Build System Adjustments
- **dll/appcompat/shims/CMakeLists.txt**
  - Temporarily disabled shimlib subdirectories due to unresolvable header conflicts

## Root Cause
The primary issue is that ReactOS was originally designed to be compatible with Windows Server 2003, and the kernel structures have evolved significantly in Windows 10. The NDK headers contain definitions that conflict with newer Windows SDK headers when targeting Windows 10.

## Recommendations
1. Consider creating separate header sets for different Windows versions
2. Implement conditional compilation based on NTDDI_VERSION throughout the codebase
3. Review and update all kernel structure definitions for Windows 10 compatibility
4. Consider isolating NDK and SDK headers to prevent conflicts

## Build Status
After applying all fixes documented above, the ReactOS build completed successfully with `ninja bootcd` generating a bootable CD image for Windows 10 compatibility.