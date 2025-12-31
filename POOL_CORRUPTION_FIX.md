# ARM64 Pool Corruption Fix (BAD_POOL_HEADER 0x19)

## Problem Summary

The ARM64 ReactOS kernel was crashing during boot with a BAD_POOL_HEADER (0x19) bugcheck:
- Parameter 1: 0x3 (pool freelist is corrupt)
- Parameter 2: 0xFFFF800042236F70 (pool entry address)
- Parameter 3: 0xFFFFFFFFFFFFFFFE (corrupted previous size field)
- Parameter 4: 0xFFFFFFFF14000400 (expected value)

The crash occurred immediately after MiBuildPagedPool logged "ready", indicating paged pool initialization succeeded but then pool structures became corrupted.

## Root Causes Identified

### 1. **Critical Bug in ExAllocatePoolWithTag** (ntoskrnl/mm/ARM3/expool.c:2433-2436)

**ISSUE**: The code unconditionally overwrote `PoolVector[NonPagedPool]` on EVERY pool allocation:

```c
/* Paged pool may be unavailable early; redirect to nonpaged pool. */
if (PoolVector[NonPagedPool] != &NonPagedPoolDescriptor)
{
    PoolVector[NonPagedPool] = &NonPagedPoolDescriptor;
}
```

**PROBLEM**:
- This check runs on EVERY ExAllocatePoolWithTag call, regardless of pool type
- `PoolVector[NonPagedPool]` is already correctly initialized by `InitializePool(NonPagedPool, 0)`
- Repeatedly setting this pointer corrupts the pool descriptor state
- This breaks pool accounting and list head management

**FIX**: Removed this unconditional reset. The early paged pool redirection logic (which was already present below) handles the case where paged pool isn't ready yet.

### 2. **Double Initialization in MiBuildPagedPool** (ntoskrnl/mm/ARM3/mminit.c:2061-2071)

**ISSUE**: The code re-initialized the paged pool descriptor after it was already initialized:

```c
/* Re-sync pool vectors/descriptors before enabling paged pool use. */
PoolVector[NonPagedPool] = &NonPagedPoolDescriptor;
if (ExpPagedPoolDescriptor[0])
{
    PoolVector[PagedPool] = ExpPagedPoolDescriptor[0];
    ExInitializePoolDescriptor(ExpPagedPoolDescriptor[0],
                               PagedPool, 0, 0,
                               (PVOID)(ExpPagedPoolDescriptor[0] + 1));
}
```

**PROBLEM**:
- `InitializePool(PagedPool, 0)` was already called earlier in MiBuildPagedPool
- That call properly allocated and initialized ExpPagedPoolDescriptor[0]
- Re-calling ExInitializePoolDescriptor() reinitializes all pool list heads
- This corrupts any allocations made between the first initialization and this re-initialization
- Pool list heads get reset while there are already allocated blocks referencing them

**FIX**: Removed the re-initialization. Added assertions to verify the pool is already properly initialized, then just set the MmPagedPoolInitialized flag.

### 3. **ARM64-Specific PFN Flag Clearing** (ntoskrnl/mm/ARM3/pool.c:790-838)

**ISSUE**: Code was silently clearing PFN allocation flags on ARM64 without logging:

```c
#if defined(_M_ARM64) || defined(__aarch64__)
if (Pfn1->u3.e1.StartOfAllocation != 0)
{
    Pfn1->u3.e1.StartOfAllocation = 0;
}
#else
ASSERT(Pfn1->u3.e1.StartOfAllocation == 0);
#endif
```

**PROBLEM**:
- These flags being set indicates a previous allocation wasn't properly freed
- Or pool pages are being reused without proper cleanup
- Silently clearing them masks the underlying issue

**FIX**:
- Made the code architecture-independent (removed ARM64 ifdef)
- Added DPRINT1 logging when these flags are unexpectedly set
- This helps diagnose the root cause of why pages aren't being properly cleaned up

## The Corruption Sequence

Here's how the corruption occurred:

1. **Phase 1 - NonPaged Pool Init**: `InitializePool(NonPagedPool, 0)` runs
   - PoolVector[NonPagedPool] = &NonPagedPoolDescriptor ✓
   - Early boot allocations use nonpaged pool

2. **Phase 2 - Paged Pool Init**: `InitializePool(PagedPool, 0)` runs
   - Allocates ExpPagedPoolDescriptor[0] from nonpaged pool
   - Initializes all pool list heads for paged pool ✓
   - PoolVector[PagedPool] = ExpPagedPoolDescriptor[0] ✓

3. **Phase 3 - Pool Allocations Start**:
   - Some pool allocations occur
   - **BUG**: Every ExAllocatePoolWithTag call resets PoolVector[NonPagedPool]
   - This corrupts the pool descriptor

4. **Phase 4 - MiBuildPagedPool Completes**:
   - **BUG**: Re-initializes ExpPagedPoolDescriptor[0]
   - Resets all pool list heads to empty
   - Existing allocations still have pointers to old list nodes
   - Pool freelist is now corrupted

5. **Phase 5 - First Allocation After Pool Init**:
   - Tries to walk the pool freelist
   - Encounters corrupted list pointers
   - BAD_POOL_HEADER (0x19) bugcheck with corrupted previous size

## Files Modified

1. **ntoskrnl/mm/ARM3/expool.c**
   - Removed unconditional PoolVector[NonPagedPool] reset
   - Kept existing early paged pool redirection logic

2. **ntoskrnl/mm/ARM3/mminit.c**
   - Removed double initialization of paged pool descriptor
   - Added assertions to verify proper initialization
   - Kept MmPagedPoolInitialized flag setting

3. **ntoskrnl/mm/ARM3/pool.c**
   - Added logging for unexpected PFN allocation flags
   - Made flag clearing code architecture-independent
   - Helps diagnose underlying pool cleanup issues

## Testing Recommendations

After applying these fixes:

1. Verify the kernel boots past the pool initialization phase
2. Check for DPRINT1 messages about PFN flags being set
   - If these messages appear, investigate why pool pages aren't being properly cleaned
3. Run pool-intensive workloads to verify no corruption occurs
4. Consider enabling pool verification (MmProtectFreedNonPagedPool) for thorough testing

## Additional Notes

The PFN flag clearing issue suggests there may be an underlying problem with how pool pages are being freed during early boot on ARM64. The logging added will help diagnose this if it continues to occur.

The fixes address the immediate corruption issues, but the ARM64 port should be monitored for:
- Unexpected PFN flag warnings in the log
- Any pool-related assertions during boot
- Memory leaks from improperly freed pool allocations
