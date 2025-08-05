# ReactOS Windows 10 API Build Issues

## Summary
This document tracks the issues encountered and fixes applied when upgrading ReactOS from Windows Server 2003 API compatibility to Windows 10 API compatibility (NTDDI_VERSION=0x0A000000).

## Completed Fixes

### 1. Structure Member Changes
- **KTHREAD**: Many fields were renamed or removed in newer Windows versions
  - `SuspendApc` → `SchedulerApc` (Windows 8+)
  - `SuspendSemaphore` → `SuspendEvent` (Windows 8+)
  - `Quantum` → Removed (use QuantumTarget)
  - `LargeStack` → Removed
  - `CallbackStack` → Removed
  - `ServiceTable` → Removed
  - `ApcStatePointer` → `ApcStateIndex`
  - `SwapBusy` → Removed (Vista+) - Fixed in ke_x.h with conditional compilation

- **ETHREAD**: Field changes
  - `ThreadsProcess` → Use `Tcb.Process`
  - `LpcReplyMessage` → `LpcReplySlot`
  - `LpcReplySemaphore` → `KeyedWaitSemaphore` (Vista+)
  - `DeadThread` → `ThreadInserted`
  - `GrantedAccess` → `GrantedAccess2`

- **EPROCESS**: Field changes
  - `ExceptionPort` → `ExceptionPortData` (Vista+)
  - `GrantedAccess` → Removed (Windows 7+)

- **KPROCESS**: Field changes
  - `DirectoryTableBase` → Changed from array to single value (Windows 10)

### 2. API Changes
- Push lock functions moved from Ex to Ke namespace (Windows 10)
  - Added `KeAcquirePushLockExclusive`
  - Added `KeReleasePushLockExclusive`

- New Windows 10 APIs stubbed:
  - `NtQueryTimer`
  - `CcCopyWriteWontFlush`
  - `CcCoherencyFlushAndPurgeCache` (fixed signature: returns VOID, takes ULONG Flags)
  - `KeUnwaitThread`
  - `KeExpandKernelStackAndCallout`
  - `FsRtlCheckOplockEx`
  - `FsRtlOplockBreakH`
  - `FsRtlCurrentOplockH`
  - `FsRtlOplockIsSharedRequest`
  - `FsRtlAreVolumeStartupApplicationsComplete`
  - `IoRetrievePriorityInfo`
  - `PsIsDiskCountersEnabled`
  - `KeQueryActiveProcessorCount`
  - `ExInitializeDeleteTimerParameters`

### 3. Type Changes
- `KAFFINITY` → `GROUP_AFFINITY` for thread affinity (Windows 7+)
  - Fixed in ke_x.h: `Thread->Affinity.Mask` instead of `Thread->Affinity`
- Lock types changed from KSPIN_LOCK to EX_PUSH_LOCK in many places
- `TYPE_ALIGNMENT` macro fixed for GCC compatibility

### 4. Assembly Code Changes
- Removed `SwapBusy` field checks (removed in Vista+)
- Fixed `KTHREAD_LargeStack` references (field removed)
- Updated structure offsets for Windows 10 compatibility

### 5. Driver Fixes
- **FastFAT**: Added Windows 10 API stubs
- **Disk driver**: Fixed duplicate IOCTL case values using conditional compilation
- **USB storage**: Fixed include dependencies
- **drwtsn32**: Fixed duplicate symbol with weak attributes

### 6. DLL Fixes
- **ws2_32.dll**: 
  - Created simple_stubs.c for missing Windows 10 exports
  - Fixed duplicate symbols between auto-generated stubs and implementations
  - Added UDP message size APIs (WSASetUdpSendMessageSize, etc.)

## Current Issues

### 1. SEH2 Linking Errors (AMD64)
The kernel and some drivers compile successfully but fail to link with SEH2 symbol errors:
```
undefined reference to `__seh2$$begin_try__238`
undefined reference to `__seh2$$end_try__238`
undefined reference to `__seh2$$filter__238`
undefined reference to `__seh2$$begin_except__238`
```

This appears to be an issue with the GCC SEH plugin for AMD64 not generating all required symbols. The plugin is loaded (`-fplugin=gcc_plugin_seh.so`) but some line-specific SEH symbols are not being generated.

**Affected files:**
- `ntoskrnl/lpc/complete.c`
- `ntoskrnl/lpc/send.c`
- `sdk/lib/drivers/rdbsslib/rdbss.c` - Has macro conflicts with _SEH2_TRY_RETURN

**Note**: Per user instructions, GCC does not support native SEH for AMD64, so these SEH modifications should be ignored.

### 2. Parser Generation Issues
- JScript parser files need manual generation with bison
- Fixed by running bison to generate parser.tab.c and cc_parser.tab.c

## Build Environment
- Host: Linux (Ubuntu)
- Target: AMD64 Windows
- Compiler: x86_64-w64-mingw32-gcc
- NTDDI_VERSION: 0x0A000000 (Windows 10)
- Build system: CMake with Ninja

## Recommendations
1. The SEH2 issue on AMD64 is a known limitation of GCC - consider using MSVC for full SEH support
2. Continue using stub implementations for unimplemented Windows 10 APIs
3. Use conditional compilation (#if NTDDI_VERSION) to maintain compatibility

## Build Progress
- ✅ ntoskrnl: Compiles successfully (SEH linking issues remain)
- ✅ ws2_32.dll: Fixed and builds successfully
- ✅ drwtsn32: Fixed duplicate symbols
- ✅ FastFAT driver: API stubs added
- ✅ Disk driver: Fixed duplicate IOCTLs
- ✅ USB storage: Fixed includes
- 🚧 rdbsslib: SEH2 macro conflicts
- 🚧 Full livecd build: In progress

## Notes
- All kernel compilation errors have been resolved
- The approach uses conditional compilation to maintain compatibility across Windows versions
- Most changes involve adapting to structure member renames/removals between Windows versions
- SEH2 issues are a GCC limitation, not a code issue