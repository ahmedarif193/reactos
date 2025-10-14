# WOW64 Dual KnownDLL Lists and Binary Mirroring - Implementation Summary

## Overview

This document summarizes the implementation of dual KnownDLL lists and 32-bit binary mirroring infrastructure for ReactOS WOW64 support on amd64 systems.

## Implementation Date

October 2025

## Objectives Completed

1. **Finalized dual KnownDLL lists in boot hives**
   - Separated System32 vs SysWOW64 in registry
   - Added WOW64 modules (wow64.dll, wow64cpu.dll, wow64win.dll) to 64-bit KnownDlls
   - Enhanced KnownDlls32 with additional critical 32-bit DLLs
   - Updated hivesys.inf with proper comments and organization

2. **Implemented 32-bit binary mirroring to SysWOW64**
   - Enhanced build system to mirror binaries from i386 builds
   - Added intelligent file filtering to exclude developer artifacts
   - Implemented SxS assembly mirroring for 32-bit side-by-side DLLs
   - Created flexible path detection for i386 binary sources

3. **Established developer-friendly build infrastructure**
   - Created automated build script (build-wow64.sh)
   - Added CMake support module (wow64-support.cmake)
   - Provided comprehensive documentation
   - Ensured proper WOW64 DLL installation paths

## Files Modified

### Registry Configuration

**File**: `/boot/bootdata/hivesys.inf`

**Changes**:
- Added WOW64 DLLs to 64-bit KnownDlls section (lines 1594-1596):
  ```
  HKLM,"...KnownDlls","wow64",0x00000000,"wow64.dll"
  HKLM,"...KnownDlls","wow64cpu",0x00000000,"wow64cpu.dll"
  HKLM,"...KnownDlls","wow64win",0x00000000,"wow64win.dll"
  ```

- Enhanced KnownDlls32 with additional critical DLLs (lines 1599-1623):
  - Added `comctl32.dll` - Common controls
  - Added `ntdll.dll` - Native API layer
  - Added `shlwapi.dll` - Shell lightweight API

- Removed WOW64 DLLs from KnownDlls32 (they belong in 64-bit list)

- Added descriptive comments explaining purpose of each section

**Rationale**: WOW64 DLLs (wow64.dll, wow64cpu.dll, wow64win.dll) are 64-bit binaries that manage 32-bit process emulation. They must be in the native 64-bit KnownDlls, not KnownDlls32.

### Build System - Image Creation

**File**: `/boot/boot_images.cmake`

**Changes** (lines 166-260):
- Replaced basic mirroring logic with comprehensive implementation
- Added multiple search paths for i386 binaries:
  ```cmake
  "${REACTOS_BINARY_DIR}/../output-MinGW-i386-Release"
  "${REACTOS_BINARY_DIR}/../output-MinGW-i386-Debug"
  "${REACTOS_BINARY_DIR}/../build-i386/output-MinGW-i386-Release"
  "${REACTOS_BINARY_DIR}/../build-i386/output-MinGW-i386-Debug"
  "$ENV{REACTOS_I386_ROOT}"
  ```

- Enhanced file filtering to exclude developer artifacts:
  ```cmake
  .a .lib .pdb .exp .map .obj .ilk .idb .log
  .txt .cmake .py .pl .bat .sh .c .cpp .h .hpp
  .idl .inf .ini .md
  ```

- Added SxS assembly mirroring for 32-bit side-by-side DLLs

- Improved status messages for better developer feedback

- Added files to all image targets:
  - bootcd.cmake.lst
  - bootcdregtest.cmake.lst
  - livecd.cmake.lst
  - liveimg.cmake.lst
  - hybridcd.cmake.lst

**Rationale**: The original implementation only supported a single hardcoded path and had incomplete file filtering. The new implementation is flexible, robust, and developer-friendly.

### WOW64 Module Build Configuration

#### wow64.dll

**File**: `/dll/win32/wow64/CMakeLists.txt`

**Changes**:
- Added comprehensive header comment explaining module purpose
- Documented architecture requirement (amd64 only)
- Clarified installation location (system32, not SysWOW64)
- Explained role in WOW64 infrastructure

#### wow64cpu.dll

**File**: `/dll/win32/wow64cpu/CMakeLists.txt`

**Changes**:
- Added header comment explaining CPU translation functionality
- Documented low-level context switching purpose
- Clarified 64-bit nature and system32 installation

#### wow64win.dll

**File**: `/dll/win32/wow64win/CMakeLists.txt`

**Changes**:
- Added architecture guard (amd64 only)
- Documented Win32k syscall thunking purpose
- Clarified USER32/GDI32 translation role
- Ensured proper system32 installation

**Rationale**: Clear documentation prevents confusion about why 64-bit WOW64 DLLs are not in SysWOW64 (they manage 32-bit processes, but are themselves 64-bit).

## Files Created

### 1. WOW64 CMake Support Module

**File**: `/sdk/cmake/wow64-support.cmake`

**Purpose**: Provides reusable CMake functions for WOW64 build infrastructure

**Functions**:
- `WOW64_CONFIGURE_PATHS()` - Locates i386 binaries
- `WOW64_ADD_32BIT_MODULE()` - Placeholder for future cross-compilation
- `WOW64_MIRROR_BINARIES()` - Mirrors binaries to SysWOW64
- `WOW64_GET_SKIP_EXTENSIONS()` - Returns filtered extensions

**Benefits**:
- Centralizes WOW64 build logic
- Enables future enhancements (cross-compilation)
- Provides consistent error handling
- Simplifies maintenance

### 2. Automated Build Script

**File**: `/build-wow64.sh`

**Purpose**: Automates dual-architecture ReactOS builds

**Features**:
- Builds both i386 and amd64 in correct order
- Supports release and debug configurations
- Provides `--rebuild-i386` and `--skip-i386` options
- Validates build outputs
- Displays helpful status messages
- Creates bootable images

**Usage**:
```bash
./build-wow64.sh release          # Build both architectures
./build-wow64.sh debug            # Debug build
./build-wow64.sh --rebuild-i386   # Force i386 rebuild
./build-wow64.sh --skip-i386      # Use existing i386 binaries
```

**Benefits**:
- Eliminates manual build coordination
- Reduces developer errors
- Documents standard build workflow
- Speeds up development iteration

### 3. Comprehensive Build Guide

**File**: `/media/doc/WOW64_BUILD_GUIDE.md`

**Contents**:
- Architecture overview with directory structure
- Registry configuration details
- Two build methods (automated script + manual)
- Build system implementation details
- File filtering explanation
- WOW64 module component descriptions
- Testing procedures
- Troubleshooting guide
- Development notes for contributors

**Benefits**:
- Onboards new WOW64 developers quickly
- Documents design decisions
- Provides reference for troubleshooting
- Explains non-obvious behaviors

### 4. SysWOW64 Directory README

**File**: `/media/SysWOW64-README.txt`

**Purpose**: Explains the SysWOW64 directory for developers encountering it

**Contents**:
- Purpose and functionality explanation
- Directory structure overview
- Build system integration details
- Registry configuration
- File redirection behavior
- Note about when directory is empty

**Benefits**:
- Prevents confusion about empty directory
- Explains WOW64 file redirection
- Points to comprehensive build guide

### 5. Implementation Summary

**File**: `/media/doc/WOW64_IMPLEMENTATION_SUMMARY.md` (this document)

**Purpose**: Documents all changes made for WOW64 binary infrastructure

## Directory Mapping in CMakeMacros.cmake

**File**: `/sdk/cmake/CMakeMacros.cmake` (line 97)

**Existing Configuration**:
```cmake
elseif(${dir} STREQUAL reactos/SysWOW64)
    set(${var} 82)
```

**Status**: Already present, no changes needed

**Purpose**: Maps SysWOW64 directory to cabinet file number 82 for reactos.dff packaging

## Package Definition File

**File**: `/boot/bootdata/packages/reactos.dff.in` (line 104)

**Existing Configuration**:
```
82 = SysWOW64
```

**Status**: Already present, no changes needed

**Purpose**: Defines SysWOW64 directory for cabinet packaging system

## Registry Hive Configuration

### KnownDlls (64-bit System DLLs)

```ini
[HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDlls]
DllDirectory = %SystemRoot%\system32
advapi32 = advapi32.dll
comdlg32 = comdlg32.dll
gdi32 = gdi32.dll
imagehlp = imagehlp.dll
kernel32 = kernel32.dll
lz32 = lz32.dll
msvcrt = msvcrt.dll
ole32 = ole32.dll
oleaut32 = oleaut32.dll
olecli32 = olecli32.dll
olesvr32 = olesvr32.dll
olethk32 = olethk32.dll
rpcrt4 = rpcrt4.dll
setupapi = setupapi.dll
shell32 = shell32.dll
url = url.dll
urlmon = urlmon.dll
user32 = user32.dll
version = version.dll
wininet = wininet.dll
wldap32 = wldap32.dll
wow64 = wow64.dll       # WOW64 core emulation layer
wow64cpu = wow64cpu.dll # CPU context translator
wow64win = wow64win.dll # Win32k syscall thunker
```

### KnownDlls32 (32-bit WOW64 DLLs)

```ini
[HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDlls32]
DllDirectory = %SystemRoot%\SysWOW64
advapi32 = advapi32.dll
comdlg32 = comdlg32.dll
comctl32 = comctl32.dll  # Added
gdi32 = gdi32.dll
imagehlp = imagehlp.dll
kernel32 = kernel32.dll
lz32 = lz32.dll
msvcrt = msvcrt.dll
ntdll = ntdll.dll        # Added
ole32 = ole32.dll
oleaut32 = oleaut32.dll
olecli32 = olecli32.dll
olesvr32 = olesvr32.dll
olethk32 = olethk32.dll
rpcrt4 = rpcrt4.dll
setupapi = setupapi.dll
shell32 = shell32.dll
shlwapi = shlwapi.dll    # Added
url = url.dll
urlmon = urlmon.dll
user32 = user32.dll
version = version.dll
wininet = wininet.dll
wldap32 = wldap32.dll
```

## Build Workflow

### Current Workflow (Manual)

1. Build i386 binaries separately
2. Note path to i386 output directory
3. Configure amd64 build with REACTOS_I386_ROOT
4. Build amd64
5. Create images (bootcd/livecd/liveimg)

### New Automated Workflow

1. Run `./build-wow64.sh release`
2. Script handles all steps automatically
3. Produces WOW64-enabled images

## Technical Details

### File Filtering Logic

**Included in SysWOW64**:
- `.dll` - Dynamic link libraries
- `.exe` - Executables
- `.sys` - System drivers
- `.cpl` - Control panel applets
- `.ocx` - ActiveX controls
- `.drv` - Legacy drivers

**Excluded from SysWOW64**:
- Static libraries (`.a`, `.lib`)
- Debug symbols (`.pdb`, `.map`)
- Linker artifacts (`.exp`, `.ilk`, `.idb`)
- Object files (`.obj`, `.o`)
- Documentation (`.txt`, `.md`, `.log`)
- Source code (`.c`, `.cpp`, `.h`, `.hpp`)
- Build scripts (`.cmake`, `.py`, `.pl`, `.sh`)
- Configuration (`.inf`, `.ini` - handled separately)

### Path Detection Order

The build system searches for i386 binaries in this order:

1. CMake variable: `-DREACTOS_I386_ROOT=/path`
2. Environment variable: `$REACTOS_I386_ROOT`
3. Sibling directory: `../output-MinGW-i386-Release`
4. Sibling directory: `../output-MinGW-i386-Debug`
5. Separate build: `../build-i386/output-MinGW-i386-Release`
6. Separate build: `../build-i386/output-MinGW-i386-Debug`

First valid path found is used.

### WOW64 Module Architecture

```
32-bit Application (notepad.exe)
         |
         | LoadLibrary("user32.dll")
         v
32-bit ntdll.dll (SysWOW64)
         |
         | NtCreateFile (32-bit syscall)
         v
wow64cpu.dll (CPU context switch)
         |
         | Convert x86 → x86-64
         v
wow64.dll (Core emulation)
         |
         | Route syscall
         v
wow64win.dll (Win32k thunking) [for USER/GDI calls]
         |
         | Thunk to 64-bit
         v
64-bit ntoskrnl.exe (Kernel)
```

## Testing and Validation

### Build Verification

After implementing these changes, verify:

```bash
# 1. Configuration detects i386 binaries
cmake .. -DARCH=amd64 | grep -i "WOW64: Found"

# 2. SysWOW64 is populated
ls output-MinGW-amd64-Release/reactos/SysWOW64/*.dll

# 3. WOW64 DLLs are in system32
ls output-MinGW-amd64-Release/reactos/system32/wow64*.dll

# 4. Registry hives contain both KnownDlls
strings boot/bootdata/system | grep -i knowndlls
```

### Runtime Verification

After booting WOW64-enabled ReactOS:

```
1. Open Registry Editor
2. Navigate to HKLM\SYSTEM\CurrentControlSet\Control\Session Manager
3. Verify both KnownDlls and KnownDlls32 keys exist
4. Check DllDirectory values point to correct locations
5. Run 32-bit application to test WOW64 functionality
```

## Future Enhancements

### Potential Improvements

1. **Cross-Compilation Support**
   - Build both architectures in single pass
   - Use GCC multilib (`-m32` flag)
   - Eliminate need for separate i386 build

2. **Parallel Builds**
   - Build i386 and amd64 simultaneously
   - Reduce total build time
   - Coordinate through CMake ExternalProject

3. **Incremental Updates**
   - Detect changed i386 binaries
   - Only mirror updated files
   - Speed up iterative development

4. **ARM64 WOW64 Support**
   - Extend infrastructure for ARM64
   - Support ARM32 emulation on ARM64
   - Mirror architecture-specific patterns

## Dependencies

### Build Requirements

- CMake 3.10+
- Ninja or Make build system
- GCC/MinGW toolchain for i386
- GCC/MinGW toolchain for amd64
- bash (for build-wow64.sh)

### Runtime Requirements

- WOW64 kernel support (ntoskrnl/ps/wow64.c)
- CPU context translation (wow64cpu_amd64.S)
- Process management hooks (PspAllocateProcess)
- File system redirection (future)
- Registry redirection (future)

## Known Limitations

1. **Separate Builds Required**
   - Currently requires two separate build passes
   - Cannot cross-compile i386 from amd64 toolchain
   - Future: Implement cross-compilation support

2. **Static Binary Mirror**
   - i386 binaries are copied, not built during amd64 build
   - Changes to i386 code require rebuilding i386 separately
   - Future: Trigger i386 rebuild automatically

3. **No File System Redirection Yet**
   - WOW64 file system redirection not implemented
   - 32-bit apps must use SysWOW64 paths explicitly
   - Future: Implement FS redirection in object manager

## Conclusion

This implementation provides a complete build infrastructure for WOW64 dual-architecture support in ReactOS:

- ✅ Dual KnownDLL lists (system32 vs SysWOW64)
- ✅ Automatic binary mirroring from i386 builds
- ✅ Developer artifact filtering (.pdb, .lib, .map excluded)
- ✅ WOW64 module installation (wow64.dll, wow64cpu.dll, wow64win.dll)
- ✅ SxS assembly support
- ✅ Comprehensive documentation
- ✅ Automated build script
- ✅ CMake support infrastructure

The infrastructure is ready for WOW64 kernel support development and testing.

## References

- ReactOS WOW64 Roadmap: `media/doc/wow64_roadmap.txt`
- WOW64 Build Guide: `media/doc/WOW64_BUILD_GUIDE.md`
- Kernel WOW64 Support: `ntoskrnl/ps/wow64.c`
- CPU Context Translation: `dll/win32/wow64cpu/wow64cpu_amd64.S`
- SysWOW64 README: `media/SysWOW64-README.txt`

---

**Implementation Status**: Complete ✅

**Ready for**: WOW64 kernel development, 32-bit application testing, runtime validation

**Next Steps**:
1. Test dual-architecture build workflow
2. Implement file system redirection
3. Implement registry redirection
4. Add WOW64 process lifecycle support
5. Test 32-bit applications on WOW64-enabled ReactOS
