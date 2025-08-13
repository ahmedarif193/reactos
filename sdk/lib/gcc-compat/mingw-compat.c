/*
 * MinGW Compatibility Library for ReactOS
 * Provides symbols that external MinGW libraries expect
 */

#include <windef.h>
#include <wchar.h>
#include <setjmp.h>
#include <winnt.h>
#include <excpt.h>

/* Forward declarations */
extern int _setjmpex(jmp_buf _Buf, void *_Ctx);

#if defined(_X86_) && !defined(__x86_64)
/* i386 uses void* for DispatcherContext */
extern EXCEPTION_DISPOSITION _except_handler(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    struct _CONTEXT *ContextRecord,
    void *DispatcherContext);
#else
/* x64 and other platforms use struct _DISPATCHER_CONTEXT* */
extern EXCEPTION_DISPOSITION __C_specific_handler(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    struct _CONTEXT *ContextRecord,
    struct _DISPATCHER_CONTEXT *DispatcherContext);
#endif

/* Provide __intrinsic_setjmpex as an alias to _setjmpex */
int __intrinsic_setjmpex(jmp_buf _Buf, void *_Ctx)
{
    return _setjmpex(_Buf, _Ctx);
}

/* Provide mbsrtowcs stub that calls the proper implementation */
size_t mbsrtowcs(wchar_t *dst, const char **src, size_t maxCount, mbstate_t *pst)
{
    /* For now, return error - this should be implemented properly later */
    return (size_t)-1;
}

/* Provide _GCC_specific_handler for C++ exception handling */
#if defined(_X86_) && !defined(__x86_64)
EXCEPTION_DISPOSITION _GCC_specific_handler(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    struct _CONTEXT *ContextRecord,
    void *DispatcherContext)
{
    /* Delegate to the platform-specific handler for now */
    return _except_handler(ExceptionRecord, EstablisherFrame, ContextRecord, DispatcherContext);
}
#else
EXCEPTION_DISPOSITION _GCC_specific_handler(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    struct _CONTEXT *ContextRecord,
    struct _DISPATCHER_CONTEXT *DispatcherContext)
{
    /* Delegate to the C-specific handler for now */
    return __C_specific_handler(ExceptionRecord, EstablisherFrame, ContextRecord, DispatcherContext);
}
#endif

/* Provide TLS functions for emutls.c - forward to kernel32 */
extern DWORD WINAPI TlsAlloc(void);
extern BOOL WINAPI TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue);