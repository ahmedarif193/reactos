# wow64win.dll - WOW64 Win32k Thunking Layer

## Overview

`wow64win.dll` is part of the ReactOS WOW64 (Windows 32-bit on Windows 64-bit) subsystem. It provides thunking services for Win32k system calls (NtUser*/NtGdi*) to enable 32-bit GUI applications to run on 64-bit ReactOS.

## Architecture

### WOW64 Layer Components

1. **wow64.dll** - Core WOW64 layer, handles NT* syscalls (kernel32/ntdll)
2. **wow64cpu.dll** - CPU context management and 32<->64-bit transitions
3. **wow64win.dll** - Win32k syscall thunking (USER32/GDI32) **(this module)**

### Syscall Flow

```
32-bit App → 32-bit USER32 → int 0x2E/syscall → wow64cpu
              ↓
         wow64win.dll (this module)
              ↓
         Structure conversion (32→64)
              ↓
         64-bit win32k.sys (NtUser*/NtGdi*)
              ↓
         Result conversion (64→32)
              ↓
         32-bit App
```

## Implementation Status

### Current Implementation (Milestone 1)

This is the **initial stub implementation** that provides:

- ✅ Basic DLL infrastructure (DllMain, debug tracing)
- ✅ Structure definitions for 32-bit MSG, CREATESTRUCT, WINDOWPOS
- ✅ Structure conversion helpers (32<->64 bit)
- ✅ Stub thunks for essential win32k syscalls:
  - Window station operations (Open/Create)
  - Desktop operations (Create/Open/Close/SetThread)
  - Message loop operations (Get/Peek/Dispatch/Wait)
  - Window creation (CreateWindowEx, DestroyWindow, ShowWindow)
- ✅ Export table (Wow64Ldrp* and Wow64NtUser* functions)
- ✅ Build system integration (CMakeLists.txt, spec file)
- ✅ Boot media packaging (KnownDlls32, SysWOW64 staging)

### Status: GUI Blocking

**IMPORTANT**: The current implementation returns `STATUS_NOT_IMPLEMENTED` for all thunks.

This is **intentional** to:
1. Block GUI processes from crashing until the layer is complete
2. Allow console applications to run (via wow64.dll)
3. Provide a foundation for incremental implementation

When `Wow64LdrpLoadWow64Win()` returns `STATUS_NOT_IMPLEMENTED`, the loader will gracefully block GUI thread initialization and display an error message.

## File Structure

```
dll/win32/wow64win/
├── wow64win.c          # Main thunk implementation
├── wow64win.spec       # Export definitions
├── CMakeLists.txt      # Build configuration
└── README.md           # This file
```

## Thunk Categories

### 1. Window Station and Desktop Thunks

Window stations and desktops are fundamental win32k objects that contain windows and user interface objects.

**Implemented Stubs:**
- `Wow64NtUserOpenWindowStation` - Opens an existing window station
- `Wow64NtUserCreateDesktop` - Creates a new desktop
- `Wow64NtUserOpenDesktop` - Opens an existing desktop
- `Wow64NtUserCloseDesktop` - Closes a desktop handle
- `Wow64NtUserSetThreadDesktop` - Associates a desktop with the calling thread

**Implementation Notes:**
- Window stations are session-scoped (terminal services support)
- Desktops contain windows, hooks, and message queues
- Handles are index-based, so no conversion needed (unlike pointers)

### 2. Message Loop Thunks

The message loop is the heart of Windows GUI programming. These thunks handle message retrieval and dispatch.

**Implemented Stubs:**
- `Wow64NtUserGetMessage` - Retrieves a message (blocking)
- `Wow64NtUserPeekMessage` - Retrieves a message (non-blocking)
- `Wow64NtUserDispatchMessage` - Dispatches a message to a window procedure
- `Wow64NtUserWaitMessage` - Waits for a message to arrive
- `Wow64NtUserMsgWaitForMultipleObjectsEx` - Waits for messages or kernel objects

**Critical Structure:**
- `MSG32` structure must match Windows WOW64 layout exactly
- Message parameters (wParam/lParam) may contain embedded pointers

### 3. Window Creation and Management Thunks

These thunks handle window creation, destruction, and visibility.

**Implemented Stubs:**
- `Wow64NtUserCreateWindowEx` - Creates a window with extended styles
- `Wow64NtUserDestroyWindow` - Destroys a window
- `Wow64NtUserShowWindow` - Controls window visibility

**Structure Conversions:**
- `CREATESTRUCT32` → `CREATESTRUCT` (contains instance/menu handles)
- `WINDOWPOS32` → `WINDOWPOS` (contains window handles)

### 4. Structure Conversion Helpers

**Implemented Functions:**
- `Wow64WinConvertMsg32To64` - Converts MSG32 → MSG
- `Wow64WinConvertMsg64To32` - Converts MSG → MSG32
- `Wow64WinConvertCreateStruct32To64` - Converts CREATESTRUCT
- `Wow64WinConvertWindowPos32To64` - Converts WINDOWPOS

**Conversion Rules:**
- Handles: Zero-extend 32-bit → 64-bit
- Pointers: Sign-extend or cast to ULONG_PTR
- Integers: Copy directly (sizes are the same)
- Structures: Convert field-by-field

## Integration Points

### 1. Loader Integration

`Wow64LdrpLoadWow64Win()` is called by `wow64.dll` during GUI thread initialization (PsConvertToGuiThread).

**Current Behavior:**
- Returns `STATUS_NOT_IMPLEMENTED` to block GUI processes
- Will return `STATUS_SUCCESS` when the layer is functional

### 2. Syscall Dispatcher (Future)

`Wow64SystemServiceEx_Win32k()` will be called by `wow64cpu.dll` for win32k syscalls:

```c
// wow64cpu.dll detects win32k syscall
if (ServiceTable == KeServiceDescriptorTableShadow)
{
    // Call wow64win.dll dispatcher
    Status = Wow64SystemServiceEx_Win32k(ServiceNumber, Arguments32);
}
```

### 3. KnownDlls32 Registry

wow64win.dll is registered in the 32-bit KnownDlls:

```ini
HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDlls32
    "wow64win" = "wow64win.dll"
```

This ensures the loader can find it in the SysWOW64 directory.

## Testing Strategy

### Phase 1: Build Verification (Current)

1. Verify the DLL builds without errors
2. Ensure exports are correctly defined
3. Confirm packaging includes wow64win.dll

**Commands:**
```bash
cd /path/to/reactos_build
ninja wow64win
ls -la dll/win32/wow64win/wow64win.dll
```

### Phase 2: GUI Blocking Test

1. Attempt to run a 32-bit GUI application
2. Verify it fails gracefully with "GUI not implemented" message
3. Ensure console applications still work

### Phase 3: Incremental Implementation

1. Implement window station/desktop thunks first
2. Test with minimal GUI (no message loop)
3. Implement message loop thunks
4. Test with full GUI application

### Phase 4: Full Validation

1. Run existing ReactOS GUI applications (notepad, calc)
2. Run Windows 32-bit GUI applications
3. Verify message handling and window operations
4. Test GDI drawing operations

## Next Steps

### Milestone 2: Window Station and Desktop Support

**Goals:**
- Implement `NtUserOpenWindowStation` thunk
- Implement `NtUserCreateDesktop` thunk
- Implement `NtUserSetThreadDesktop` thunk
- Test with GUI thread initialization

**Files to Modify:**
- `wow64win.c` - Implement thunk bodies
- Add win32k syscall table mapping

### Milestone 3: Message Loop Support

**Goals:**
- Implement `NtUserGetMessage` thunk
- Implement `NtUserDispatchMessage` thunk
- Handle MSG structure conversions
- Test with simple message loop

### Milestone 4: Window Creation Support

**Goals:**
- Implement `NtUserCreateWindowEx` thunk
- Handle CREATESTRUCT conversions
- Test with window creation

### Milestone 5: Complete Win32k Coverage

**Goals:**
- Add remaining NtUser* thunks (SetWindowPos, etc.)
- Add NtGdi* thunks (GDI drawing operations)
- Handle shared memory sections (handle tables)
- Full GUI application support

## References

### ReactOS Sources

- `ntoskrnl/ps/wow64.c` - Kernel WOW64 support
- `dll/win32/wow64/wow64.c` - Core WOW64 layer
- `dll/win32/wow64cpu/wow64cpu.c` - CPU context management
- `win32ss/include/ntuser.h` - Win32k internal structures
- `win32ss/win32k.spec` - Win32k syscall exports

### Windows Internals

- Windows Internals, Part 1 (7th Edition) - Chapter on WOW64
- ReactoOS wiki: https://reactos.org/wiki/Techwiki:Win32k
- Windows syscall table: https://j00ru.vexillium.org/syscalls/nt/64/

### WOW64 Architecture

- Microsoft Docs: Windows on Windows 64-bit (WOW64)
- https://docs.microsoft.com/en-us/windows/win32/winprog64/running-32-bit-applications

## Debugging Tips

### Enable Debug Tracing

Rebuild with DBG=1 to enable verbose debug output:

```cmake
if(DBG)
    target_compile_definitions(wow64win PRIVATE DBG=1)
endif()
```

### Debug Output

All stubs emit debug messages:

```
wow64win: Wow64LdrpLoadWow64Win called - GUI support is not yet implemented
wow64win.dll stub: Wow64NtUserGetMessage
```

### Debugging with GDB

```bash
gdb /path/to/reactos
break Wow64LdrpLoadWow64Win
break Wow64NtUserGetMessage
run
```

## Known Limitations

1. **No GDI Support**: NtGdi* thunks are not yet implemented
2. **No Shared Memory**: USER32 handle tables are not yet translated
3. **No Callback Thunks**: Window procedures are not yet thunked
4. **No Hook Support**: SetWindowsHook* operations are not supported

These will be addressed in future milestones.

## License

GPL - See COPYING in the top level directory

## Authors

- Ahmed ARIF (arif.ing@outlook.com) - Initial implementation (2025)

## Contributing

When implementing thunks, follow these guidelines:

1. **Structure Conversion**: Always convert structures field-by-field
2. **Handle Translation**: Handles are index-based, no conversion needed
3. **Pointer Validation**: Validate 32-bit pointers before dereferencing
4. **Error Handling**: Return proper NTSTATUS codes
5. **Debug Tracing**: Add debug output for all thunks
6. **Testing**: Test with both ReactOS and Windows GUI applications

## Change Log

### 2025-01-XX - Initial Implementation

- Created wow64win.dll stub implementation
- Implemented structure definitions (MSG32, CREATESTRUCT32, WINDOWPOS32)
- Added conversion helpers (32<->64 bit)
- Created thunk stubs for essential win32k syscalls
- Integrated with build system and boot media
- Added KnownDlls32 registry entries
- Documentation (this README)

**Status**: GUI processes are intentionally blocked until implementation is complete.
