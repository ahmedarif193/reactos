/*
 * GCC/MinGW Compatibility Library for ReactOS
 * 
 * Provides stub implementations of GCC runtime functions that are missing
 * when building C++ applications with MinGW-w64 on ReactOS.
 * 
 * This addresses issues with DWARF exception handling, thread-local storage,
 * and other GCC-specific runtime requirements.
 */

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

/* Forward declarations for exception handling structures */
struct _Unwind_Exception;
struct _Unwind_Context;

/* Thread-Local Storage emulation stubs */
void* __emutls_get_address(void* control_object)
{
    /* Stub implementation - should not be called in practice */
    return NULL;
}

/* MinGW thread key destructor stub */
void __mingwthr_key_dtor(void* key)
{
    /* Stub implementation for MinGW thread-local storage cleanup */
    return;
}

/* CRT threading support symbol */
int _CRT_MT = 1;

/* DWARF Exception Handling Stubs */
/* These functions are part of the DWARF unwinding runtime that GCC expects.
 * On Windows, we typically use SEH instead, so these are stubs. */

void _Unwind_Resume(struct _Unwind_Exception* exception)
{
    /* In a real implementation, this would resume exception unwinding.
     * For now, we terminate to avoid undefined behavior. */
    abort();
}

unsigned long _Unwind_GetTextRelBase(struct _Unwind_Context* context)
{
    return 0;
}

unsigned long _Unwind_GetDataRelBase(struct _Unwind_Context* context)
{
    return 0;
}

unsigned long _Unwind_GetRegionStart(struct _Unwind_Context* context)
{
    return 0;
}

void _Unwind_SetGR(struct _Unwind_Context* context, int reg, unsigned long value)
{
    /* Stub - would set general register value */
}

void _Unwind_SetIP(struct _Unwind_Context* context, unsigned long value)
{
    /* Stub - would set instruction pointer */
}

const void* _Unwind_GetLanguageSpecificData(struct _Unwind_Context* context)
{
    return NULL;
}

unsigned long _Unwind_GetIPInfo(struct _Unwind_Context* context, int* ip_before_insn)
{
    if (ip_before_insn) *ip_before_insn = 0;
    return 0;
}

void _Unwind_DeleteException(struct _Unwind_Exception* exception)
{
    /* Stub - would free exception object */
}

int _Unwind_RaiseException(struct _Unwind_Exception* exception)
{
    /* In a real implementation, would start exception unwinding */
    return 5; /* _URC_END_OF_STACK */
}

int _Unwind_Resume_or_Rethrow(struct _Unwind_Exception* exception)
{
    /* Stub - would resume or rethrow exception */
    return 5; /* _URC_END_OF_STACK */
}

/* GCC-specific handler for SEH integration */
int _GCC_specific_handler(void* exception_record, void* establisher_frame,
                         void* context_record, void* dispatcher_context)
{
    /* Stub implementation - would integrate GCC exceptions with Windows SEH */
    return 1; /* ExceptionContinueSearch */
}

/* C Runtime I/O stream access compatibility for libsupc++ */
/* Note: __acrt_iob_func is provided by ucrtbase.dll in ReactOS.
 * The acrt_iob_func.c file handles the import symbol requirements.
 */