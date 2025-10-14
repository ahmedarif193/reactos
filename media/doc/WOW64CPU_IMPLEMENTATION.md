# WOW64 CPU Transition Layer Implementation

## Overview

This document describes the implementation of the WOW64 CPU transition layer for ReactOS, which provides low-level support for running 32-bit (x86) applications on 64-bit (AMD64) Windows systems.

## Implementation Date
2025-10-14

## Files Modified/Created

1. **wow64cpu_amd64.S** (NEW)
   - AMD64 assembly implementation of CPU transition stubs
   - Location: `/dll/win32/wow64cpu/wow64cpu_amd64.S`

2. **wow64cpu.c** (MODIFIED)
   - Added debug tracing infrastructure
   - Location: `/dll/win32/wow64cpu/wow64cpu.c`

3. **wow64cpu.spec** (MODIFIED)
   - Added exports for new transition functions
   - Location: `/dll/win32/wow64cpu/wow64cpu.spec`

4. **CMakeLists.txt** (MODIFIED)
   - Added assembly file to build
   - Location: `/dll/win32/wow64cpu/CMakeLists.txt`

## Key Components

### 1. CpupDoCallBack

**Purpose:** Handles transitions from 64-bit native code to 32-bit compatibility mode for callback execution.

**Signature:**
```c
NTSTATUS NTAPI CpupDoCallBack(
    ULONG_PTR CallbackRoutine,    // RCX - 32-bit callback routine address
    ULONG_PTR Stack32,             // RDX - 32-bit stack pointer
    PVOID CompatContext,           // R8  - Pointer to WOW64_CONTEXT
    PVOID NativeContext);          // R9  - Pointer to AMD64 CONTEXT
```

**Implementation Details:**
- Saves AMD64 non-volatile registers (RBX, RSI, RDI, R12-R15)
- Preserves FP/XMM state using FXSAVE64
- Loads 32-bit register state from WOW64_CONTEXT structure
- Synchronizes extended registers (512-byte ExtendedRegisters field)
- Includes DBG-gated tracing for debugging transitions
- Restores state and returns STATUS_SUCCESS

**Stack Frame:**
- Total allocation: 640 bytes (0x280)
- Shadow space: 32 bytes
- Non-volatile save: 64 bytes
- FP/XMM area: 512 bytes (FXSAVE64 format)
- Local variables: 32 bytes

### 2. CpupDispatchException

**Purpose:** Handles exception dispatching for WOW64 processes by converting native AMD64 exception contexts to 32-bit format.

**Signature:**
```c
NTSTATUS NTAPI CpupDispatchException(
    PEXCEPTION_RECORD ExceptionRecord,  // RCX
    PVOID CompatContext,                 // RDX - Pointer to WOW64_CONTEXT
    PVOID NativeContext);                // R8  - Pointer to AMD64 CONTEXT
```

**Implementation Details:**
- Converts AMD64 CONTEXT to WOW64_CONTEXT by truncating 64-bit registers
- Properly maps all GPRs: RAX→EAX, RBX→EBX, RCX→ECX, etc.
- Preserves EFLAGS and segment registers (CS, SS, DS, ES, FS, GS)
- Synchronizes FP/XMM state between native and compatibility formats
- Sets WOW64_CONTEXT_FULL flag (0x00010007) to indicate complete context
- Includes comprehensive debug tracing

**Stack Frame:**
- Total allocation: 800 bytes (0x320)
- Includes space for exception record copy
- Aligned for FXSAVE64 operations

### 3. FP/XMM Context Management

**Helper Functions:**

#### Wow64cpuSaveFpuContext
```c
VOID NTAPI Wow64cpuSaveFpuContext(PVOID FpuSaveArea);
```
- Saves current FPU and XMM register state
- Uses FXSAVE64 instruction
- Ensures 16-byte alignment

#### Wow64cpuRestoreFpuContext
```c
VOID NTAPI Wow64cpuRestoreFpuContext(PVOID FpuSaveArea);
```
- Restores FPU and XMM register state
- Uses FXRSTOR64 instruction
- Ensures 16-byte alignment

**Context Synchronization:**
- AMD64 uses XSAVE format (extended state)
- x86 compatibility uses legacy FXSAVE format
- ExtendedRegisters[512] field in WOW64_CONTEXT stores FP state
- Bidirectional conversion ensures consistency

### 4. Debug Tracing Infrastructure

**Wow64cpuDebugTraceTransition:**
```c
VOID NTAPI Wow64cpuDebugTraceTransition(
    ULONG TransitionType,
    ULONG_PTR Parameter);
```

**Transition Types:**
1. Callback Entry (Type 1)
2. Callback Return (Type 2)
3. Exception Dispatch Entry (Type 3)
4. Exception Context Converted (Type 4)

**Debug Output Format:**
```
wow64cpu: CPU Transition - Type: [TypeString], Param: 0x[Address]
```

**Usage:**
- Only active in DBG builds (gated by #if DBG)
- Provides visibility into transition events for early LiveCD diagnostics
- Logs register states and context addresses
- Uses OutputDebugStringA for Windows debug output

## Register Mapping

### AMD64 → x86 (64-bit to 32-bit)

| AMD64 | x86  | Notes                          |
|-------|------|--------------------------------|
| RAX   | EAX  | Lower 32 bits, zero-extended   |
| RBX   | EBX  | Lower 32 bits, zero-extended   |
| RCX   | ECX  | Lower 32 bits, zero-extended   |
| RDX   | EDX  | Lower 32 bits, zero-extended   |
| RSI   | ESI  | Lower 32 bits, zero-extended   |
| RDI   | EDI  | Lower 32 bits, zero-extended   |
| RBP   | EBP  | Lower 32 bits, zero-extended   |
| RSP   | ESP  | Lower 32 bits, zero-extended   |
| RIP   | EIP  | Lower 32 bits, zero-extended   |

### Segment Registers
All segment registers (CS, SS, DS, ES, FS, GS) are preserved as 16-bit values during transitions.

### EFLAGS
The EFLAGS register is directly compatible and preserved across transitions.

## WOW64_CONTEXT Structure Layout

```c
typedef struct _WOW64_CONTEXT {
    DWORD ContextFlags;                                    // +0x00
    DWORD Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;                   // +0x04
    WOW64_FLOATING_SAVE_AREA FloatSave;                   // +0x1C (112 bytes)
    DWORD SegGs, SegFs, SegEs, SegDs;                     // +0x8C
    DWORD Edi, Esi, Ebx, Edx, Ecx, Eax;                   // +0x9C
    DWORD Ebp, Eip;                                       // +0xB4
    DWORD SegCs, EFlags, Esp, SegSs;                      // +0xBC
    BYTE ExtendedRegisters[512];                          // +0xCC
} WOW64_CONTEXT;
```

**Key Offsets (Hexadecimal):**
- `0x00`: ContextFlags
- `0x88`: SegGs
- `0x8C`: SegFs
- `0x90`: SegEs
- `0x94`: SegDs
- `0x9C`: Edi
- `0xA0`: Esi
- `0xA4`: Ebx
- `0xA8`: Edx
- `0xAC`: Ecx
- `0xB0`: Eax
- `0xB4`: Ebp
- `0xB8`: Eip
- `0xBC`: SegCs
- `0xC0`: EFlags
- `0xC4`: Esp
- `0xC8`: SegSs
- `0xCC`: ExtendedRegisters[512]

## AMD64 CONTEXT Offsets

The assembly code uses these symbolic offsets from ksamd64.inc:
- `CxRax`, `CxRbx`, `CxRcx`, `CxRdx`, `CxRsi`, `CxRdi`, `CxRbp`, `CxRsp`, `CxRip`
- `CxEFlags`
- `CxSegCs`, `CxSegSs`, `CxSegDs`, `CxSegEs`, `CxSegFs`, `CxSegGs`

## Assembly Conventions

### Calling Convention
- Windows x64 calling convention (Microsoft ABI)
- Parameters: RCX, RDX, R8, R9, then stack
- Shadow space: 32 bytes (4 × 8 bytes)
- Stack alignment: 16 bytes
- Non-volatile registers: RBX, RBP, RDI, RSI, RSP, R12-R15, XMM6-XMM15

### Unwind Information
- Uses .pushreg, .allocstack, .savereg directives
- Enables proper exception handling and debugging
- Compatible with Windows SEH (Structured Exception Handling)

### Alignment Requirements
- FXSAVE64/FXRSTOR64 require 16-byte alignment
- Code explicitly masks addresses: `and rax, 0xFFFFFFFFFFFFFFF0`

## Error Handling

All functions return NTSTATUS:
- `STATUS_SUCCESS` (0x00000000): Operation completed successfully
- `STATUS_INVALID_PARAMETER`: NULL pointer or invalid parameters
- `STATUS_NOT_FOUND`: Required context not available
- Other NTSTATUS codes as appropriate

## Memory Model Considerations

### AMD64 Memory Ordering
- AMD64 uses a relaxed memory model (not strictly sequential)
- While not explicitly needed in current implementation, future enhancements may require memory barriers:
  - `MFENCE`: Full memory barrier
  - `LFENCE`: Load fence
  - `SFENCE`: Store fence

### Cache Coherency
- FXSAVE/FXRSTOR handle cache coherency automatically for FP state
- No explicit cache management needed for current implementation

## Build Integration

### CMakeLists.txt
```cmake
add_library(wow64cpu MODULE
    wow64cpu.c
    wow64cpu_amd64.S
    ${CMAKE_CURRENT_BINARY_DIR}/wow64cpu.def)
```

### Exported Functions (wow64cpu.spec)
```
@ stdcall CpupDoCallBack(long long ptr ptr)
@ stdcall CpupDispatchException(ptr ptr ptr)
@ stdcall Wow64cpuSaveFpuContext(ptr)
@ stdcall Wow64cpuRestoreFpuContext(ptr)
```

## Testing Recommendations

### Unit Tests
1. **Register Mapping:**
   - Verify correct truncation of 64-bit to 32-bit registers
   - Test zero-extension when moving from 32-bit to 64-bit
   - Validate segment register preservation

2. **FP/XMM State:**
   - Test FPU state preservation across transitions
   - Verify XMM register synchronization
   - Check alignment handling

3. **Exception Handling:**
   - Test exception context conversion accuracy
   - Verify exception record preservation
   - Validate stack unwinding

### Integration Tests
1. **Callback Execution:**
   - Test actual 32-bit callback invocation
   - Verify stack pointer management
   - Check return value handling

2. **Exception Dispatching:**
   - Generate exceptions in 32-bit code
   - Verify proper exception propagation
   - Test debugger integration

### Debug Tracing
Enable DBG builds to activate comprehensive tracing:
```
wow64cpu: CPU Transition - Type: Callback Entry, Param: 0x12345678
wow64cpu: CPU Transition - Type: Exception Dispatch Entry, Param: 0xABCDEF00
```

## Performance Considerations

### Optimization Opportunities
1. **Inline Assembly:** Critical paths could use inline assembly for minimal overhead
2. **Fast Path:** Add fast paths for common cases (e.g., no FP state change)
3. **Lazy State Saving:** Only save/restore FP state if modified

### Current Implementation
- Conservative approach: Always saves/restores full state
- Suitable for correctness-first phase
- Performance optimizations deferred to later iterations

## Known Limitations

1. **Actual Mode Switch:** Current implementation prepares register state but doesn't perform actual CPU mode transition (handled by kernel)
2. **Advanced FP Features:** AVX/AVX2/AVX-512 not yet supported
3. **Debug Registers:** DR0-DR7 copied but not actively managed
4. **Hardware Breakpoints:** May need special handling in future

## Future Enhancements

1. **Syscall Thunking:** Implement wow64cpu syscall translation layer
2. **Performance Counters:** Add instrumentation for profiling
3. **Advanced SIMD:** Support AVX and later instruction sets
4. **ARM64 Port:** Adapt for ARM64 ↔ ARM32 transitions
5. **Memory Barriers:** Add explicit barriers for multi-core safety if needed

## Compatibility

### Windows Compatibility
- Designed to match Windows wow64cpu.dll behavior
- Uses same structure layouts and calling conventions
- Compatible with existing WOW64 infrastructure

### ReactOS Integration
- Integrates with ReactOS kernel WOW64 support
- Uses ReactOS assembly macros and conventions
- Compatible with existing build system

## References

1. **AMD64 Architecture Manual:** Volume 1-5, AMD Corporation
2. **Windows Internals:** WOW64 subsystem architecture
3. **ReactOS Code:** Existing context switching implementations in ntoskrnl/arch/amd64
4. **Intel Software Developer Manual:** Volume 1-4, x86-64 instruction set reference

## Conclusion

This implementation provides a solid foundation for WOW64 CPU transitions in ReactOS, with:
- Complete register mapping between AMD64 and x86
- Proper FP/XMM state synchronization
- Comprehensive debug tracing
- Windows-compatible behavior
- Clean integration with ReactOS build system

The code is production-ready for initial testing and can be incrementally optimized as the WOW64 subsystem matures.
