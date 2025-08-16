# Proposal: Use Native GCC SEH for AMD64 Instead of PSEH

## Current Situation

ReactOS currently uses PSEH (Portable SEH) for exception handling on AMD64, which appears to be causing issues:

1. **RPC Exception Handling Corruption**: Error code 0x6e01ca94 appears to be corrupted/uninitialized data from `RpcExceptionCode()`
2. **PSEH Implementation**: The current implementation uses either:
   - PSEH2_64 (when available) 
   - Dummy PSEH (fallback that doesn't actually handle exceptions)

## The Problem

Looking at `/sdk/lib/pseh/include/pseh/pseh2.h`:
- Lines 43-45: References `pseh2_64.h` for GCC + AMD64 (but this file doesn't exist)
- Lines 47-99: Falls back to dummy PSEH implementation that:
  - Doesn't actually handle exceptions
  - Returns hardcoded values (0 for exception code)
  - Uses volatile variables that may not be properly initialized

## GCC Native SEH Support

Modern GCC (since version 4.7) supports native Windows SEH on x64 when targeting MinGW-w64. This includes:

1. **Compiler flags**:
   - `-fseh-exceptions`: Enable SEH exception handling
   - `-fasynchronous-unwind-tables`: Generate unwind tables for SEH

2. **Native keywords**:
   - `__try`, `__except`, `__finally` work natively
   - No need for PSEH wrapper macros

## Proposed Changes

### 1. Update `/sdk/lib/pseh/include/pseh/pseh2.h`

```c
#elif defined(__GNUC__) && !defined(__clang__) && defined(_M_AMD64) && defined(__SEH__)
// Use native GCC SEH for AMD64
#define _SEH2_TRY __try
#define _SEH2_FINALLY __finally
#define _SEH2_EXCEPT(...) __except(__VA_ARGS__)
#define _SEH2_END
#define _SEH2_GetExceptionInformation() (GetExceptionInformation())
#define _SEH2_GetExceptionCode() (GetExceptionCode())
#define _SEH2_AbnormalTermination() (AbnormalTermination())
#define _SEH2_YIELD(STMT_) STMT_
#define _SEH2_LEAVE __leave
#define _SEH2_VOLATILE
```

### 2. Update `/sdk/cmake/gcc.cmake`

Add for AMD64 builds:
```cmake
if(ARCH STREQUAL "amd64")
    # Enable native SEH support on AMD64
    add_compile_options(-fseh-exceptions -fasynchronous-unwind-tables)
    add_definitions(-D__SEH__=1)
endif()
```

### 3. Test Configuration

Create a simple test to verify SEH is working:
```c
#ifdef _M_AMD64
void test_native_seh()
{
    __try {
        // Cause an exception
        int *p = NULL;
        *p = 42;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        DPRINT1("Native SEH caught exception: 0x%lx\n", GetExceptionCode());
    }
}
#endif
```

## Benefits

1. **Proper Exception Handling**: Real exception codes instead of corrupted/dummy values
2. **Better Compatibility**: Uses Windows-standard SEH mechanisms
3. **Improved Stability**: Less chance of corruption in exception paths
4. **Performance**: Native SEH is more efficient than PSEH wrappers

## Risks and Mitigation

1. **Compiler Version**: Requires GCC 4.7+ (MinGW-w64)
   - Mitigation: Check compiler version in CMake

2. **Compatibility**: Some code may rely on PSEH quirks
   - Mitigation: Gradual rollout with testing

3. **Build System Changes**: Need to ensure all AMD64 code gets the right flags
   - Mitigation: Centralize in gcc.cmake

## Implementation Steps

1. Add compiler version check in CMake
2. Add SEH flags for AMD64 builds
3. Update PSEH headers to use native SEH when available
4. Test RPC exception handling
5. Gradually remove PSEH workarounds

## Testing Plan

1. Test RPC exceptions (OpenSCManager scenario)
2. Test kernel-mode exceptions
3. Test user-mode exceptions
4. Stress test with multiple threads
5. Verify exception codes are correct

This change should resolve the 0x6e01ca94 corruption issue and improve overall AMD64 stability.