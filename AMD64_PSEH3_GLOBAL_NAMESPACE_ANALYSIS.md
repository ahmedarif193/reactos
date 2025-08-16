# AMD64 Boot Issue: PSEH3, Global Namespace, and Cross-CPU Synchronization

## Executive Summary

The AMD64 boot performance issues are interconnected through a complex chain of problems involving exception handling (PSEH2/3), global namespace resolution, and cross-CPU synchronization. The issue you remember from two years ago appears to be a cascade failure where:

1. **PSEH2 limitations on AMD64** cause exception handling problems in critical DLLs
2. **Global namespace failures** prevent inter-process communication
3. **Excessive cross-CPU freezing** during exception handling degrades performance

## The PSEH3 Connection

### Background
PSEH (Pseudo-Structured Exception Handling) is ReactOS's implementation of Windows SEH for GCC. The issue relates to:

1. **PSEH2 vs PSEH3 on AMD64**:
   - PSEH2 uses frame-based exception handling (32-bit style)
   - PSEH3 was designed for better AMD64 support but has limitations
   - AMD64 Windows uses table-based exception handling (not frame-based)

2. **The Critical DLLs**:
   - `advapi32.dll` - Uses _SEH2 extensively in service control code
   - `kernel32.dll` - Uses _SEH2 in synchronization and thread creation
   - `rpcrt4.dll` - Critical for RPC communication

### The Problem Chain

```
PSEH2 Exception → Cross-CPU Freeze → Global Namespace Access → Event Creation Failure
     ↓                    ↓                     ↓                        ↓
advapi32.dll      KxFreezeExecution()   BaseNamedObjects      SCM_START_EVENT
exception in       (expensive on          not accessible      creation fails
service handler     multi-core)           cross-process
```

## Deep Analysis: The Missing Link

### 1. Exception Handling During Global Object Creation

When `advapi32.dll` tries to create/open the SCM event:
```c
// In WaitForSCManager():
hEvent = OpenEventW(SYNCHRONIZE, FALSE, SCM_START_EVENT);
// SCM_START_EVENT should be "Global\\SCM_START_EVENT" but fails on AMD64
```

If an exception occurs here (likely due to namespace resolution), PSEH2 triggers:
1. Exception handler invoked
2. **KxFreezeExecution()** called to freeze other CPUs
3. Busy-wait loop on all CPUs
4. Timeout/delay in exception processing

### 2. Why Global Namespace Fails on AMD64

The global namespace issue is exacerbated by:

**Cross-CPU IPI (Inter-Processor Interrupt) Storm**:
- Every exception potentially triggers CPU freeze via NMI
- Global object creation involves kernel transitions
- Each transition may trigger debugging/exception paths
- Result: Cascade of CPU freezes

**Evidence from `/ntoskrnl/ke/amd64/freeze.c`**:
```c
// Complex state machine for CPU freezing
// Uses expensive InterlockedExchange operations
// Nested loops waiting for all CPUs to synchronize
```

### 3. The PSEH2 Limitation on AMD64

PSEH2 wasn't designed for AMD64's exception model:
- **AMD64 expects**: Table-based unwinding (.pdata/.xdata sections)
- **PSEH2 provides**: Frame-based unwinding (fs:[0] chain)
- **Result**: Exception dispatch is slow and may fail

When combined with frequent CPU freezing:
- Each PSEH2 exception handler invocation is expensive
- CPU freeze amplifies the cost
- Global namespace operations timeout

## The Workaround That Was Discovered

The suggestion to replace the DLLs with MSVC versions makes sense because:

1. **MSVC DLLs use native SEH**: Proper table-based exception handling
2. **No PSEH2 overhead**: Direct Windows-compatible exception dispatch
3. **Reduced CPU freezing**: Fewer exception-triggered freezes

## Root Cause Summary

The issue is a **perfect storm** of three problems:

1. **PSEH2 incompatibility with AMD64** causing slow exception handling
2. **Overly aggressive CPU freezing** (KxFreezeExecution) during exceptions
3. **Global namespace resolution failures** preventing IPC

These three issues create a feedback loop:
- Exceptions during global object access → CPU freeze → Timeout
- Timeout → Exception → More CPU freezes
- Services can't communicate → More exceptions → System paralysis

## Recommended Fix Strategy

### Immediate (for testing):
1. **Test with MSVC DLLs**: Replace advapi32.dll, kernel32.dll, rpcrt4.dll
2. **Disable KxFreezeExecution**: Comment out freeze calls during boot
3. **Force local namespace**: Remove Global\\ prefix temporarily

### Proper Fix:
1. **Implement PSEH3 properly for AMD64** or use MSVC for critical DLLs
2. **Optimize KxFreezeExecution**: Don't freeze on every exception
3. **Fix BaseNamedObjects creation**: Ensure global namespace works
4. **Add exception bypass**: Fast path for known-safe operations

## Community Questions to Ask

1. Has anyone successfully used PSEH3 with these critical DLLs on AMD64?
2. Is there a patch that implements table-based exception handling for GCC builds?
3. Can we conditionally disable CPU freezing during early boot?
4. What's the status of BaseNamedObjects initialization on AMD64?

## Test Plan

1. Build with MSVC for the three critical DLLs only
2. Add timing logs around exception handlers
3. Monitor CPU freeze frequency during boot
4. Check if Global\\ namespace objects are created successfully
5. Compare exception dispatch times between PSEH2 and native SEH

This analysis shows the PSEH3 issue is central to the AMD64 boot problem, creating a cascade failure through exception handling, CPU synchronization, and namespace resolution.