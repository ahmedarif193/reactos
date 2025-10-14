# ReactOS WOW64 Build Guide

## Overview

This guide explains how to build ReactOS with WOW64 (Windows-on-Windows 64-bit) support, enabling 32-bit application execution on 64-bit ReactOS systems.

## Architecture Overview

### Directory Structure

On a WOW64-enabled amd64 build, ReactOS uses the following directory layout:

```
C:\ReactOS\
├── system32\          # 64-bit native binaries
│   ├── ntdll.dll      (64-bit)
│   ├── kernel32.dll   (64-bit)
│   ├── user32.dll     (64-bit)
│   ├── wow64.dll      (64-bit WOW64 core)
│   ├── wow64cpu.dll   (64-bit CPU translator)
│   └── wow64win.dll   (64-bit Win32k thunker)
│
└── SysWOW64\          # 32-bit WOW64 binaries
    ├── ntdll.dll      (32-bit)
    ├── kernel32.dll   (32-bit)
    └── user32.dll     (32-bit)
```

### Registry Configuration

The system maintains two separate KnownDlls lists:

**KnownDlls** (64-bit native DLLs):
```
HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDlls
  DllDirectory = %SystemRoot%\system32
  kernel32 = kernel32.dll
  ntdll = ntdll.dll
  wow64 = wow64.dll        # WOW64 emulation layer
  wow64cpu = wow64cpu.dll  # CPU context translator
  wow64win = wow64win.dll  # Win32k syscall thunker
  ...
```

**KnownDlls32** (32-bit WOW64 DLLs):
```
HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDlls32
  DllDirectory = %SystemRoot%\SysWOW64
  kernel32 = kernel32.dll
  ntdll = ntdll.dll
  user32 = user32.dll
  ...
```

## Build Methods

### Method 1: Automated Build Script (Recommended)

The easiest way to build WOW64-enabled ReactOS:

```bash
# Build both architectures (release)
./build-wow64.sh release

# Build both architectures (debug)
./build-wow64.sh debug

# Rebuild i386 binaries
./build-wow64.sh release --rebuild-i386

# Use existing i386 binaries
./build-wow64.sh release --skip-i386
```

### Method 2: Manual Build

#### Step 1: Build i386 Binaries

```bash
# Create build directory
mkdir ../build-i386-release
cd ../build-i386-release

# Configure i386 build
cmake -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DARCH=i386 \
      ../reactos

# Build
ninja
```

#### Step 2: Build amd64 with WOW64 Support

```bash
# Create build directory
mkdir ../build-amd64-release
cd ../build-amd64-release

# Configure amd64 build with WOW64
cmake -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DARCH=amd64 \
      -DREACTOS_I386_ROOT=/path/to/i386/output-MinGW-i386-Release/reactos \
      ../reactos

# Build
ninja
```

#### Step 3: Create Bootable Images

```bash
# Create bootable CD
ninja bootcd

# Create live CD with desktop
ninja livecd

# Create bootable hard disk image
ninja liveimg
```

## Build System Details

### CMake Configuration

The build system automatically detects i386 binaries in several locations:

1. `${REACTOS_BINARY_DIR}/../output-MinGW-i386-Release`
2. `${REACTOS_BINARY_DIR}/../output-MinGW-i386-Debug`
3. `${REACTOS_BINARY_DIR}/../build-i386/output-MinGW-i386-Release`
4. `${REACTOS_BINARY_DIR}/../build-i386/output-MinGW-i386-Debug`
5. `$ENV{REACTOS_I386_ROOT}` (environment variable)
6. `-DREACTOS_I386_ROOT=/path` (CMake variable)

### File Filtering

When mirroring binaries to SysWOW64, the following file types are excluded:

**Developer Artifacts** (not included in SysWOW64):
- `.a`, `.lib` - Static libraries
- `.pdb`, `.map` - Debug symbols
- `.exp`, `.ilk`, `.idb` - Linker artifacts
- `.obj`, `.o` - Object files
- `.txt`, `.md`, `.log` - Documentation/logs
- `.h`, `.hpp`, `.c`, `.cpp` - Source code
- `.cmake`, `.py`, `.pl`, `.sh` - Build scripts
- `.inf`, `.ini` - Configuration files (handled separately)

**Runtime Binaries** (included in SysWOW64):
- `.dll` - Dynamic link libraries
- `.exe` - Executables
- `.sys` - Drivers (if applicable)
- `.cpl` - Control panel applets
- `.ocx` - ActiveX controls
- `.drv` - Legacy drivers

### WOW64 Module Components

#### 1. wow64.dll (64-bit)
- **Location**: `%SystemRoot%\system32\wow64.dll`
- **Purpose**: Core WOW64 process and thread management
- **Functions**:
  - Process creation and initialization
  - Thread context management
  - Syscall interception
  - Memory layout management

#### 2. wow64cpu.dll (64-bit)
- **Location**: `%SystemRoot%\system32\wow64cpu.dll`
- **Purpose**: CPU architecture translation
- **Functions**:
  - x86-64 to x86 register mapping
  - Stack frame conversion
  - Instruction pointer translation
  - CPU mode switching

#### 3. wow64win.dll (64-bit)
- **Location**: `%SystemRoot%\system32\wow64win.dll`
- **Purpose**: Win32k syscall thunking
- **Functions**:
  - USER32 syscall translation
  - GDI32 syscall translation
  - Window handle conversion
  - Graphics context mapping

## Testing WOW64

### Verify Build

After building, verify the WOW64 infrastructure:

```bash
# Check for 64-bit WOW64 DLLs in system32
ls build-amd64-release/output-MinGW-amd64-Release/reactos/system32/wow64*.dll

# Check for 32-bit binaries in SysWOW64
ls build-amd64-release/output-MinGW-amd64-Release/reactos/SysWOW64/*.dll

# Verify registry hives contain KnownDlls32
strings build-amd64-release/boot/bootdata/system | grep -i knowndlls32
```

### Runtime Testing

1. **Boot WOW64-enabled ReactOS**
   ```
   qemu-system-x86_64 -cdrom bootcd.iso -m 2048
   ```

2. **Verify WOW64 DLLs are loaded**
   - Open Task Manager
   - Check that `wow64.dll` is loaded by system processes

3. **Run 32-bit applications**
   - Copy 32-bit executables to the system
   - Verify they execute correctly via WOW64

4. **Check KnownDlls registry**
   ```
   HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDlls
   HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDlls32
   ```

## Troubleshooting

### i386 Binaries Not Found

**Error**: "WOW64: No i386 binaries found"

**Solution**:
```bash
# Explicitly specify i386 location
export REACTOS_I386_ROOT=/path/to/i386/output-MinGW-i386-Release/reactos
cmake -DREACTOS_I386_ROOT=/path/to/i386/output ../reactos
```

### SysWOW64 Directory Empty

**Problem**: The SysWOW64 directory exists but contains no files

**Causes**:
1. i386 build did not complete successfully
2. Path to i386 binaries is incorrect
3. All files were filtered out (unlikely)

**Verification**:
```bash
# Check i386 build output
ls $REACTOS_I386_ROOT/system32/ntdll.dll

# Review CMake configuration output
cmake .. | grep -i wow64
```

### 32-bit Applications Won't Run

**Symptoms**: 32-bit executables fail to launch or crash immediately

**Checklist**:
1. Verify `wow64.dll`, `wow64cpu.dll`, `wow64win.dll` are in system32
2. Check KnownDlls registry entries exist
3. Confirm SysWOW64 contains 32-bit ntdll.dll and kernel32.dll
4. Review WOW64 kernel support is enabled

## Development Notes

### Adding New 32-bit Binaries

When adding new DLLs that should be available for 32-bit applications:

1. **Build the DLL in i386 configuration**
2. **It will automatically be mirrored to SysWOW64** during amd64 image creation
3. **Add to KnownDlls32 if critical** (edit `boot/bootdata/hivesys.inf`)

### Cross-Compilation (Future)

The current build system requires separate i386 and amd64 builds. Future enhancements may support:

- Single-pass dual-architecture builds
- Cross-compilation using `-m32` flag
- Separate i386 toolchain invocation
- Parallel architecture builds

### Build System Files

**Modified/Created for WOW64 support**:
- `boot/bootdata/hivesys.inf` - Dual KnownDlls registry entries
- `boot/boot_images.cmake` - SysWOW64 binary mirroring logic
- `sdk/cmake/CMakeMacros.cmake` - SysWOW64 directory number (82)
- `sdk/cmake/wow64-support.cmake` - WOW64 CMake helper functions
- `build-wow64.sh` - Automated build script
- `dll/win32/wow64/CMakeLists.txt` - wow64.dll build configuration
- `dll/win32/wow64cpu/CMakeLists.txt` - wow64cpu.dll build configuration
- `dll/win32/wow64win/CMakeLists.txt` - wow64win.dll build configuration

## Additional Resources

- **WOW64 Architecture**: [ReactOS WOW64 Roadmap](wow64_roadmap.txt)
- **Kernel Support**: [ntoskrnl/ps/wow64.c](../../ntoskrnl/ps/wow64.c)
- **CPU Context Translation**: [dll/win32/wow64cpu/wow64cpu_amd64.S](../../dll/win32/wow64cpu/wow64cpu_amd64.S)
- **Process Management**: [ntoskrnl/ps/process.c](../../ntoskrnl/ps/process.c) (PspAllocateProcess)

## Summary

The ReactOS WOW64 build system provides:

1. **Dual KnownDlls** registry configuration (KnownDlls + KnownDlls32)
2. **Automatic binary mirroring** from i386 build to SysWOW64
3. **Developer artifact filtering** (.pdb, .lib, .map excluded)
4. **SxS assembly support** for 32-bit side-by-side DLLs
5. **Automated build script** for easy dual-architecture builds
6. **Flexible path detection** for i386 binary sources

This infrastructure enables full 32-bit application support on 64-bit ReactOS through the WOW64 emulation layer.
