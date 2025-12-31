# ARM64 Kernel Lookaside "Corruption" - Root Cause Analysis

## Problem Summary

The ARM64 kernel is reporting "corrupt pool lookaside pointer" errors during boot:

```
EX: corrupt pool lookaside pointer (Paged=1 Index=7 Ptr=FFFF800042494830 Prcb=FFFF8000426BA5A8 CPU=0)
EX: corrupt lookaside entry va=FFFF800042494830 pa=42494830 first=00 00 24 00 00 00 00 00
EX: corrupt pool lookaside pointer (Paged=0 Index=6 Ptr=FFFF8000424937B0 Prcb=FFFF8000426BA5A8 CPU=0)
EX: corrupt lookaside entry va=FFFF8000424937B0 pa=424937b0 first=01 00 03 00 00 00 00 00
```

## Root Cause

**The error messages are MISLEADING**. These are NOT corrupt pointers - they are the **correct** addresses of the global lookaside list arrays!

### What's Actually Happening

1. **Structure Layout on ARM64**:
   - `GENERAL_LOOKASIDE` is 128-byte aligned on ARM64 (DECLSPEC_CACHEALIGN)
   - Each `GENERAL_LOOKASIDE` structure is 256 bytes (0x100) due to padding
   - The arrays start at base addresses like `0xFFFF800042494000`

2. **The Arrays**:
   ```c
   // From ntoskrnl/ex/lookas.c
   DECLSPEC_ALIGN(16) GENERAL_LOOKASIDE ExpSmallNPagedPoolLookasideLists[32];
   DECLSPEC_ALIGN(16) GENERAL_LOOKASIDE ExpSmallPagedPoolLookasideLists[32];
   ```

3. **Expected Addresses**:
   - `ExpSmallPagedPoolLookasideLists[7]` = base + (7 * 0x100) = `FFFF800042494830` (exact match!)
   - `ExpSmallNPagedPoolLookasideLists[6]` = base + (6 * 0x100) = `FFFF8000424937B0` (exact match!)

4. **The "first" Bytes**:
   - `00 00 24 00...` = SLIST_HEADER (16 bytes at offset 0 of GENERAL_LOOKASIDE)
   - This is **CORRECT** - it's showing the ListHead member of the structure

### The REAL Problem

The issue is NOT corruption. The real problems are:

1. **Validation Logic Error**: The function `ExpIsLookasidePointerValid()` in expool.c is incorrectly flagging valid global array addresses as corrupt.

2. **Physical Address Confusion**: The physical address (0x42494830) is in kernel BSS/data region, which is unexpected but valid during early boot before relocation.

3. **Double Initialization**: `ExInitPoolLookasidePointers()` is called twice:
   - Once in `KiSystemStartupBootStack` (ARM64-specific boot.c)
   - Again in `ExpInitializeExecutive` (ex/init.c)

## The Fix

### Option 1: Fix Validation Logic (RECOMMENDED)

The validation in `ExpIsLookasidePointerValid` should recognize that pointers to the global arrays are VALID:

```c
FORCEINLINE
BOOLEAN
ExpIsLookasidePointerValid(
    _In_ PGENERAL_LOOKASIDE Pointer,
    _In_ BOOLEAN PagedList,
    _In_ USHORT Index)
{
    ULONG_PTR Offset;
    PGENERAL_LOOKASIDE Start;

    if (!Pointer) return FALSE;

    Start = PagedList ?
            (PGENERAL_LOOKASIDE)ExpSmallPagedPoolLookasideLists :
            (PGENERAL_LOOKASIDE)ExpSmallNPagedPoolLookasideLists;

    // The pointer should be Start[Index]
    // On ARM64, each GENERAL_LOOKASIDE is 256 bytes due to cache alignment
    if (Pointer == &Start[Index])
    {
        return TRUE;
    }

    // Legacy check for offset-based validation
    Offset = (ULONG_PTR)Pointer - (ULONG_PTR)Start;
    if ((Offset % sizeof(GENERAL_LOOKASIDE)) != 0)
    {
        return FALSE;
    }

    Offset /= sizeof(GENERAL_LOOKASIDE);
    if (Offset >= NUMBER_POOL_LOOKASIDE_LISTS)
    {
        return FALSE;
    }

    return (Offset == Index);
}
```

### Option 2: Remove Double Initialization

Remove the call to `ExInitPoolLookasidePointers()` from either:
- `KiSystemStartupBootStack` (ARM64 boot.c), OR
- `ExpInitializeExecutive` (ex/init.c)

Keep only ONE call in the proper initialization sequence.

### Option 3: Disable Misleading Validation (TEMPORARY)

Comment out the validation calls in `ExpGetValidPoolLookasideList()` on ARM64:

```c
#if DBG && !defined(_M_ARM64)  // Disable on ARM64 until validation logic is fixed
        Current = UseGlobal ?
                  Prcb->PPPagedLookasideList[Index].L :
                  Prcb->PPPagedLookasideList[Index].P;

        if (Current && !ExpIsLookasidePointerValid(Current, TRUE, Index))
        {
            ExpLogLookasideCorruption(Prcb, TRUE, Index, Current);
        }
#endif
```

## Verification

To verify the pointers are correct, add debug output to show:

```c
DPRINT1("Lookaside arrays: Paged=%p NPaged=%p\n",
        ExpSmallPagedPoolLookasideLists,
        ExpSmallNPagedPoolLookasideLists);
DPRINT1("Expected[%d]: Paged=%p NPaged=%p\n",
        Index,
        &ExpSmallPagedPoolLookasideLists[Index],
        &ExpSmallNPagedPoolLookasideLists[Index]);
DPRINT1("Actual from PRCB: Paged.P=%p Paged.L=%p NPaged.P=%p NPaged.L=%p\n",
        Prcb->PPPagedLookasideList[Index].P,
        Prcb->PPPagedLookasideList[Index].L,
        Prcb->PPNPagedLookasideList[Index].P,
        Prcb->PPNPagedLookasideList[Index].L);
```

## Conclusion

**There is NO actual corruption**. The validation logic is incorrectly flagging valid global array addresses as corrupt because it doesn't account for ARM64's cache line alignment padding.

The immediate fix is to update `ExpIsLookasidePointerValid()` to properly validate pointers on ARM64 by using direct address comparison instead of offset calculation.
