# AMD64 Service Initialization Failure - Root Cause Analysis

## Executive Summary
AMD64 builds fail to initialize services while i386 works correctly. The failure is caused by a combination of namespace handling contradictions, memory pressure, and RPC implementation complexity.

## Critical Findings

### 1. Global Namespace Contradiction (PRIMARY CAUSE)
**Location**: `/sdk/include/reactos/services/services.h` vs `/base/system/services/services.c`

The header file defines AMD64 to use Global\\ prefix:
```c
#ifdef _M_AMD64
    #define SCM_START_EVENT_FULL SCM_GLOBAL_PREFIX SCM_START_EVENT  // "Global\\SvcctrlStartEvent_A3752DX"
#else
    #define SCM_START_EVENT_FULL SCM_START_EVENT  // "SvcctrlStartEvent_A3752DX"
#endif
```

But the implementation explicitly avoids it:
```c
#ifdef _M_AMD64
    /* On AMD64, try without Global namespace since it seems not to work */
    hScmStartEvent = CreateEventW(NULL, TRUE, FALSE, SCM_START_EVENT);  // No Global\\ prefix!
#else
    hScmStartEvent = CreateEventW(NULL, TRUE, FALSE, SCM_START_EVENT_FULL);
#endif
```

**Impact**: Services cannot find the SCM start event, causing cascade failures.

### 2. Memory Management Issues
**Location**: `/ntoskrnl/mm/ARM3/pagfault.c:89`

AMD64 logs show:
```
[77.427] Close to our death...
[81.530] Close to our death...
```

This indicates stack overflow conditions during service initialization. AMD64 has:
- Larger stack requirements (24KB vs 12KB)
- 4-level page tables causing more overhead
- More aggressive memory management

### 3. RPC Implementation Complexity
**Location**: `/dll/win32/rpcrt4/`

AMD64 RPC errors:
```
err: receive failed with error 1726 (RPC_S_CALL_FAILED)
err: failed to open service manager
```

AMD64 RPC has:
- Complex 64-bit calling conventions
- More register save/restore operations
- Different stubless proxy implementation
- Higher failure probability under memory pressure

### 4. Thread Death During Service Startup
**Location**: `/win32ss/user/ntuser/msgqueue.c`

AMD64 shows thread failures:
```
err: NB Receiving Thread woken up dead!
err: Thread Cleanup Sent Messages
```

This indicates threads are being terminated during critical service initialization.

## Log Comparison

### i386 (Working)
```
[39.433] SERVICES: [i386] SCM initialization starting...
[39.435] SERVICES: [i386] Creating SCM start event 'SvcctrlStartEvent_A3752DX'...
[39.436] SERVICES: [i386] CreateEventW SUCCESS! Handle=000000BC
[40.249] RPCSERVER: [i386] RPC server is now listening
[40.252] SERVICES: [i386] SetEvent SUCCESS! Event is now signaled
Boot took 36574595495 cycles!
```

### AMD64 (Failing)
```
[68.326] SERVICES: [AMD64] SCM initialization starting...
[68.327] SERVICES: [AMD64] Creating SCM start event 'SvcctrlStartEvent_A3752DX'...
[68.328] SERVICES: [AMD64] CreateEventW SUCCESS! Handle=00000000000000BC
[69.083] RPCSERVER: [AMD64] RPC server is now listening
[77.427] Close to our death...
err: receive failed with error 1726
err: failed to open service manager
[239.176] Unable to open the service control manager. Last Error 0
Boot took 84332850980 cycles! (2.3x slower than i386!)
```

## Root Cause Chain
1. **Namespace mismatch** → SCM event not accessible across processes
2. **Memory pressure** → Stack overflow conditions
3. **RPC failures** → Service communication breaks down
4. **Thread deaths** → Critical service threads terminate
5. **Cascade failure** → Entire service subsystem fails

## Immediate Fixes Required

### Fix 1: Resolve Namespace Contradiction
```c
// In services.h - Make consistent:
#ifdef _M_AMD64
    #define SCM_START_EVENT_FULL SCM_START_EVENT  // Remove Global\\ for now
#else
    #define SCM_START_EVENT_FULL SCM_START_EVENT
#endif
```

### Fix 2: Increase Stack Sizes
```c
// In ke/amd64/kiinit.c
#define KERNEL_STACK_SIZE 0x8000  // Increase from 0x6000 to 32KB
```

### Fix 3: Fix BaseNamedObjects Creation
Ensure BaseNamedObjects directory is properly created for AMD64 to enable Global\\ namespace.

### Fix 4: Add RPC Error Recovery
Add retry logic and better error handling in RPC initialization.

## Performance Impact
- AMD64 boot: 84.3 billion cycles
- i386 boot: 36.6 billion cycles
- **AMD64 is 2.3x slower**

## Conclusion
The primary cause is the Global namespace contradiction causing event synchronization failures. Combined with memory pressure and RPC complexity, this creates a cascade failure preventing all services from starting on AMD64.

## Recommended Action
1. **Immediate**: Fix namespace contradiction in services.h/services.c
2. **High Priority**: Increase kernel stack size for AMD64
3. **Medium Priority**: Debug BaseNamedObjects directory creation
4. **Long Term**: Optimize AMD64 RPC implementation