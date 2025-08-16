/*
 * ARM64 Exception Handler
 */

#include <rtl.h>

/* __C_specific_handler - C-specific exception handler */
EXCEPTION_DISPOSITION
__cdecl
__C_specific_handler(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    struct _CONTEXT *ContextRecord,
    struct _DISPATCHER_CONTEXT *DispatcherContext)
{
    /* TODO: Implement proper C-specific handler for ARM64 */
    return ExceptionContinueSearch;
}