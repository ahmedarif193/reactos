# Windows 10 Build Fixes Summary

This document tracks all fixes applied to resolve Windows 10 API compatibility issues in ReactOS.

## Completed Fixes

### 1. NtSetTimer System Call Implementation ✓
- **File**: `ntoskrnl/ex/timer.c` 
- **Issue**: Missing NtSetTimer export required by Windows 10 applications
- **Fix**: Implemented full NtSetTimer system call with proper parameter validation, timer object handling, and APC callback support
- **Status**: COMPLETED

### 2. PEB Structure Compatibility ✓
- **File**: `sdk/lib/crt/startup/crtexe.c`
- **Issue**: ReadOnlySharedMemoryHeap member missing from PEB structure
- **Fix**: Added conditional check for Windows 10 API level to handle new PEB members
- **Status**: COMPLETED  

### 3. Parser Build Issues (Bison/Yacc) ✓
- **Files**: 
  - `dll/win32/wbemprox/wql.y`
  - `dll/win32/jscript/parser.y`
  - `dll/win32/jscript/cc_parser.y`
  - `dll/win32/msxml3/xslpattern.y`
- **Issue**: Undefined references to yyparse, yylex, yyerror in generated code
- **Fix**: Added wrapper functions to map bison-generated names to expected names
- **Status**: COMPLETED

### 4. User32 Vista Extensions ✓
- **File**: `dll/win32/user32/user32_vista/user32_vista.c`
- **Issue**: POINTER_INPUT_TYPE redeclaration conflicts
- **Fix**: Added header guard to prevent duplicate definitions
- **Status**: COMPLETED

### 5. Winspool Missing Exports ✓
- **Files**: 
  - `win32ss/printing/base/winspool/printers.c`
  - `win32ss/printing/base/winspool/spoolfile.c`
- **Issue**: DocumentEvent and GetSpoolFileHandle exports missing
- **Fix**: Added wrapper functions to expose the W versions without suffix for compatibility
- **Status**: COMPLETED

### 6. __acrt_iob_func Multiple Definition ✓
- **File**: `sdk/lib/cpprt/gcc_compat.c`
- **Issue**: Symbol conflict between ucrtbase and cpprt libraries
- **Fix**: Commented out duplicate definitions since ucrtbase already provides them
- **Status**: COMPLETED

### 7. GDI32 Missing Exports ✓
- **Files**: 
  - `win32ss/gdi/gdi32/misc/win10_stubs.c` (extended existing file)
  - `win32ss/gdi/gdi32/CMakeLists.txt`
- **Issue**: Missing Direct2D/DirectWrite related functions
- **Fix**: Added stub implementations for critical exports to allow linking
- **Status**: COMPLETED (stubs only - full implementation pending)

## Implementation Details

### Parser Fixes
The bison parser fixes use a wrapper function approach:
```c
/* Forward declarations */
int yyparse(parser_ctx_t *);
static int parser_lex(void *lval, parser_ctx_t *ctx);

/* Wrapper functions */
static int yylex(void *lval, parser_ctx_t *ctx) {
    return parser_lex(lval, ctx);
}

static int parser_parse(parser_ctx_t *ctx) {
    return yyparse(ctx);
}
```

### Winspool Exports
Added non-W versions that call the W implementations:
```c
#ifdef DocumentEvent
#undef DocumentEvent
#endif

HRESULT WINAPI DocumentEvent(...) {
    return DocumentEventW(...);
}
```

### GDI32 Stubs
Added minimal stub implementations for Direct2D functions:
```c
BOOL WINAPI AddBezierToPath(HDC hdc, LPPOINT lppt) {
    WARN("AddBezierToPath stub\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
```

## Build Command

```bash
cd output-MinGW-amd64
ninja
```

## Testing

All changes should be tested with:
1. ReactOS boot test
2. Windows 10 application compatibility test
3. Regression test suite

## Notes

- Parser fixes use wrapper functions as a workaround for bison prefix issues
- GDI32 stubs are placeholder implementations - proper Direct2D support needed
- SEH-related fixes were excluded per user request
- All fixes maintain backward compatibility with existing ReactOS code