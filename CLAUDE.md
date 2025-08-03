# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## ReactOS Overview

ReactOS is an open-source operating system compatible with Windows NT applications and drivers, focused on Windows Server 2003 compatibility. This is alpha quality software that requires careful testing.

## Essential Build Commands

### Environment Setup
Use ReactOS Build Environment (RosBE) or MSVC 2019+

### Configuration
```bash
# Unix/Linux
./configure.sh -DENABLE_ROSTESTS=1 -DENABLE_ROSAPPS=1

# Windows
configure.cmd -DENABLE_ROSTESTS=1 -DENABLE_ROSAPPS=1
```

### Building
```bash
ninja                    # Build all modules
ninja bootcd            # Create bootable CD image
ninja livecd            # Create live CD image
ninja <module_name>     # Build specific module (e.g., ninja kernel32)
```

### Testing
```bash
# Enable tests during configuration with -DENABLE_ROSTESTS=1
# Run specific test module after building
# Test results: https://reactos.org/testman/
```

### Code Quality
```bash
# Format code
sdk/tools/do_code_format.sh

# Check code format
sdk/tools/check_code_format.sh

# Check struct packing
sdk/tools/check_packing.py
```

## High-Level Architecture

ReactOS implements the Windows NT architecture with these key components:

### Core Components
- **NT Kernel** (`ntoskrnl/`): Core kernel functionality, memory management, process/thread management
- **HAL** (`hal/`): Hardware Abstraction Layer for platform independence
- **Win32 Subsystem** (`win32ss/`): User-mode graphics, windowing, and UI services
- **Device Drivers** (`drivers/`): Hardware drivers following Windows driver model

### System Libraries
- **System DLLs** (`dll/`): Windows API implementations (kernel32, user32, gdi32, etc.)
- **NT DLLs** (`dll/ntdll/`): Native NT API implementation
- **Win32 DLLs** (`dll/win32/`): Win32 API implementations

### Applications & Services
- **Base Applications** (`base/applications/`): Core system utilities
- **System Services** (`base/services/`): Windows services implementation
- **Shell** (`base/shell/`): Explorer shell and related components

### Build System
- **SDK** (`sdk/`): Headers, import libraries, tools, and build infrastructure
- **CMake-based**: Uses CMake with Ninja/Make generators
- **Module System**: Each component is a CMake module with dependencies

## Development Guidelines

### Coding Style
- **Indentation**: 4 spaces (no tabs)
- **Line Width**: Maximum 100 characters
- **Braces**: Always on their own lines (Allman style)
- **Naming**: PascalCase for functions/variables
- **Comments**: Prefer single-line, avoid wasting lines
- **File Header**: Include PROJECT, LICENSE (SPDX), PURPOSE, COPYRIGHT

### Commit Style
```
[MODULE] Short descriptive summary (#pr-num)

Detailed explanation of WHY this change was made.
Keep lines under 72 characters.

CORE-XXXX
```

### Important Rules
- **No Microsoft Source**: Contributors must not have seen Windows source code
- **Wine Sync**: Code from Wine should not be modified (see media/doc/README.WINE)
- **3rd Party Code**: Don't modify 3rd party code directly (see media/doc/3rd_Party_Files.txt)
- **Real Names Required**: Use real name and email in commits (no anonymous contributions)

### Testing Requirements
- Enable tests with `-DENABLE_ROSTESTS=1` during configuration
- Check test results at https://reactos.org/testman/
- Write tests for new functionality when possible
- Fix failing tests before adding new features

### Common Development Tasks
1. **Finding code**: Use ripgrep (`rg`) for searching, not grep
2. **Debugging**: Use `DbgPrint`/`DPRINT` macros, KDBG debugger, or WinDbg
3. **Creating PRs**: Follow template, wait 1 week for reviews
4. **Code formatting**: Run `sdk/tools/check_code_format.sh` before committing

## Key Resources
- **Documentation**: media/doc/ directory
- **Wiki**: https://reactos.org/wiki/
- **JIRA**: https://jira.reactos.org/
- **Chat**: https://chat.reactos.org/
- **Forum**: https://reactos.org/forum/