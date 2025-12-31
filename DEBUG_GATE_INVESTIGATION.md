# Gate Assertion Failure Investigation

## Problem Summary
The ARM64 kernel is hitting the `ASSERT_GATE` assertion at `gate.c:40` despite the `DECLSPEC_ALIGN(16)` fix being applied to pushlock wait blocks.

### Critical Evidence
- **FAR Register**: `6369766544784D3C` = ASCII `"<MxDevic"` (partial WDF/device string)
- **Implication**: The Gate pointer is pointing to a device name string, not a valid KGATE structure
- **This is NOT just an alignment issue** - the pointer itself is wrong/corrupted

## Root Cause Hypothesis
The wait block structure in pushlock.c is allocated on the stack with proper alignment, but one of the following is occurring:

1. **Stack corruption**: Something is overwriting the stack after the wait block is initialized
2. **Pointer corruption**: The wait block pointer stored in the pushlock is being corrupted
3. **Use-after-free**: The stack-allocated wait block's memory is being reused while another CPU still has a reference to it
4. **Timing race**: The wait block is being published to other CPUs before KeInitializeGate completes, or after the stack frame is destroyed

## Changes Made for Debugging

### File: `/home/ahmed/WorkDir/TTE/reactos_cpu/ntoskrnl/ke/gate.c`

Added comprehensive logging in `KeWaitForGate()` **before** the ASSERT_GATE:
- Log gate pointer value
- Log gate header type and signal state
- Check if gate pointer is 16-byte aligned
- Check if gate pointer is in kernel address space
- Dump gate structure contents including WaitListHead
- Dump first 8 bytes at gate address to see what data is actually there

### File: `/home/ahmed/WorkDir/TTE/reactos_cpu/ntoskrnl/ex/pushlock.c`

Added logging at critical points:

1. **ExfAcquirePushLockExclusive()** - After KeInitializeGate, before publishing:
   - Log WaitBlock address and alignment
   - Log WakeGate address and initialized type
   - Log PushLock address where wait block is being published

2. **ExfAcquirePushLockExclusive()** - Before calling KeWaitForGate:
   - Log WaitBlock, WakeGate address and type
   - Verify gate is still valid just before waiting

3. **ExfAcquirePushLockShared()** - Same logging as exclusive path

4. **ExfWakePushLock()** - When extracting wait block from pushlock:
   - Log OldValue.Value (raw pushlock value)
   - Log extracted FirstWaitBlock pointer
   - Log WakeGate address, type, and flags
   - **This will reveal if the wait block pointer itself is corrupted**

5. **ExfWakePushLock()** - Before signaling gate:
   - Log WaitBlock being signaled
   - Log gate type to detect corruption at signal time

## What We're Looking For

When the kernel runs with these changes, the logs will reveal:

1. **Sequence of events**:
   - When wait block is initialized (should show Type = GateObject = 0x12)
   - When wait block is published to pushlock
   - When wait block is extracted from pushlock (by another CPU or same CPU)
   - When KeWaitForGate is called

2. **Point of corruption**:
   - If gate type is correct in ExfAcquire but wrong in KeWaitForGate → stack corruption
   - If gate type is correct when extracted but wrong at signal → race condition
   - If wait block pointer itself contains ASCII → pointer corruption in pushlock storage

3. **Address space issues**:
   - Stack addresses should be in kernel space (0xFFFF...)
   - If WaitBlock address changes between init and wait → stack reuse bug
   - If WaitBlock address looks like heap or device object → severe memory corruption

## Potential Issues to Check

### Stack Corruption Scenarios
- Compiler optimizations incorrectly reusing stack space
- Buffer overflow in nearby stack variables
- ARM64 stack alignment requirements not met by caller

### Pointer Corruption Scenarios
- InterlockedCompareExchangePointer incorrectly masking/modifying wait block pointer
- EX_PUSH_LOCK_PTR_BITS (0xF) calculation wrong on ARM64
- Wait block pointer arithmetic overflow

### Timing/Race Scenarios
- Wait block published before KeInitializeGate completes (memory barriers missing)
- Stack frame destroyed while another CPU holds wait block reference
- Signal occurring before wait, with wait block already freed

## Next Steps

1. Boot kernel and capture full log output
2. Identify the **exact sequence** where gate type changes from valid to invalid
3. Compare WaitBlock addresses across log entries - should be consistent
4. Check if corruption occurs on same CPU or different CPU (MP race)
5. Look for pattern in FAR values - always device names suggests specific corruption source

## Key ARM64 Considerations

- ARM64 has relaxed memory ordering - need explicit barriers
- Stack must be 16-byte aligned, wait blocks must be 16-byte aligned
- DECLSPEC_ALIGN may not work correctly with stack variables on ARM64/Clang
- InterlockedXxx functions must use proper ARM64 atomics with barriers

## File Locations

- Gate code: `/home/ahmed/WorkDir/TTE/reactos_cpu/ntoskrnl/ke/gate.c`
- Pushlock code: `/home/ahmed/WorkDir/TTE/reactos_cpu/ntoskrnl/ex/pushlock.c`
- Structure definitions: `/home/ahmed/WorkDir/TTE/reactos_cpu/sdk/include/ndk/extypes.h` (line 654)
- Build output: `/home/ahmed/WorkDir/TTE/reactos_cpu/output-Clang-arm64-Debug`
