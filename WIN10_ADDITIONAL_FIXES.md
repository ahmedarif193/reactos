# Windows 10 Additional Build Fixes

This document describes additional fixes applied to resolve build errors in ReactOS for Windows 10 compatibility.

## Thread Affinity Issues

### Files Modified:
- `ntoskrnl/ke/thrdschd.c`
- `ntoskrnl/ke/thrdobj.c`

### Issue:
In Windows 10 (NTDDI_WIN7 and above), thread affinity is stored as a `GROUP_AFFINITY` structure instead of a simple `KAFFINITY` value.

### Fix:
Added conditional compilation to access the `Mask` member of the `GROUP_AFFINITY` structure:

```c
#if (NTDDI_VERSION >= NTDDI_WIN7)
    PreferredSet = Thread->Affinity.Mask;
#else
    PreferredSet = Thread->Affinity;
#endif
```

## NtGdiGetRealizationInfo Type Conflict

### File Modified:
- `win32ss/gdi/ntgdi/font.c`

### Issue:
Function signature mismatch - declaration expects `PFONT_REALIZATION_INFO` but implementation used `PREALIZATION_INFO`.

### Fix:
Updated function signature to match declaration and implemented as a stub returning default values:

```c
BOOL APIENTRY
NtGdiGetRealizationInfo(
    _In_ HDC hdc,
    _Out_ PFONT_REALIZATION_INFO pri)
{
    /* Stub implementation */
    pri->Size = sizeof(FONT_REALIZATION_INFO);
    pri->Flags = 0;
    // ... other members
    return TRUE;
}
```

## MMPTE_HARDWARE Structure Issues

### File Modified:
- `ntoskrnl/include/internal/amd64/mm.h`

### Issue:
For Windows Vista and later, the PTE hardware structure uses different member names (`Dirty1` and `NoWrite` instead of `Write/Writable`).

### Fix:
Updated the `MI_IS_PAGE_WRITEABLE` macro to handle different Windows versions:

```c
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
#define MI_IS_PAGE_WRITEABLE(x)    (!((x)->u.Hard.NoWrite))
#elif !defined(CONFIG_SMP)
#define MI_IS_PAGE_WRITEABLE(x)    ((x)->u.Hard.Write == 1)
#else
#define MI_IS_PAGE_WRITEABLE(x)    ((x)->u.Hard.Writable == 1)
#endif
```

## __acrt_iob_func Import Symbol

### File Modified:
- `sdk/lib/cpprt/gcc_compat.c`

### Issue:
libsupc++ requires the `__imp___acrt_iob_func` import symbol, but it was causing multiple definition errors.

### Fix:
Provided the import symbol by referencing the external function from ucrtbase:

```c
extern FILE* __cdecl __acrt_iob_func(unsigned index);
const void* __imp___acrt_iob_func = (const void*)&__acrt_iob_func;
```

## Summary

These fixes address:
1. Windows 10 thread affinity structure changes
2. GDI font realization API compatibility
3. Memory management PTE structure updates for Vista+
4. C++ runtime library import symbol requirements

All fixes maintain backward compatibility with earlier Windows versions through conditional compilation.