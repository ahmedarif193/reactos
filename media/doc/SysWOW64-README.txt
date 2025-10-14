ReactOS SysWOW64 Directory
==========================

This directory contains 32-bit (x86) binaries for Windows-on-Windows 64-bit
(WOW64) emulation on amd64 systems.

Purpose
-------
On 64-bit ReactOS systems, this directory mirrors the functionality of the
System32 directory but contains 32-bit versions of system DLLs and executables.
This enables 32-bit applications to run on 64-bit ReactOS through the WOW64
emulation layer.

Directory Structure (on amd64 builds)
--------------------------------------
%SystemRoot%\system32\   - 64-bit native binaries and WOW64 infrastructure
%SystemRoot%\SysWOW64\   - 32-bit binaries for WOW64 emulation

Build System Integration
-------------------------
The build system automatically populates this directory during image creation
by mirroring binaries from a separate i386 build. See the WOW64 Build Guide
for detailed instructions:

    media/doc/WOW64_BUILD_GUIDE.md

To enable SysWOW64 population:
1. Build i386 binaries in a separate build directory
2. Configure amd64 build with: -DREACTOS_I386_ROOT=/path/to/i386/output
3. The build system will automatically copy 32-bit binaries to SysWOW64

Registry Configuration
----------------------
The system maintains separate KnownDlls registry keys:

KnownDlls (64-bit):
    HKLM\...\Session Manager\KnownDlls
    DllDirectory = %SystemRoot%\system32

KnownDlls32 (32-bit):
    HKLM\...\Session Manager\KnownDlls32
    DllDirectory = %SystemRoot%\SysWOW64

File Redirection
----------------
When a 32-bit application accesses %SystemRoot%\system32, the WOW64 layer
automatically redirects file operations to %SystemRoot%\SysWOW64 to ensure
the application loads 32-bit DLLs.

Note: This directory is empty on i386 builds and single-architecture amd64
builds without WOW64 support.
