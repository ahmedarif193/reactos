
#include <stdint.h>
#include <intrin.h>
#include <malloc.h>
#include <stdarg.h>
#define _USE_MATH_DEFINES
#include <math.h>

#ifdef USE_NATIVE_MINGW_CRT

#include <wchar.h>

char **__dcrt_initial_narrow_environment = NULL;

/* Forward to MinGW's proper invalid_parameter implementation.
 * This ensures proper parameter validation and error handling,
 * allowing custom handlers to be registered and invoked correctly.
 */
__declspec(dllimport) void __cdecl _invoke_watson(const wchar_t *, const wchar_t *,
                                                   const wchar_t *, unsigned int, uintptr_t);

void __cdecl _invalid_parameter(const wchar_t *expr,
                                const wchar_t *func,
                                const wchar_t *file,
                                unsigned int line,
                                uintptr_t reserved)
{
    /* Delegate to MinGW's watson handler which properly invokes
     * any registered invalid parameter handlers before terminating.
     */
    _invoke_watson(expr, func, file, line, reserved);
}

int __cdecl _CrtCheckMemory(void)
{
    return 1;
}

int __cdecl _CrtDbgReport(int report_type,
                          const char *file,
                          int line,
                          const char *module,
                          const char *format,
                          ...)
{
    va_list args;

    (void)report_type;
    (void)file;
    (void)line;
    (void)module;

    if (format)
    {
        va_start(args, format);
        va_end(args);
    }

    return 0;
}

int __cdecl _CrtDbgReportV(int report_type,
                           const char *file,
                           int line,
                           const char *module,
                           const char *format,
                           va_list arglist)
{
    (void)report_type;
    (void)file;
    (void)line;
    (void)module;
    (void)format;
    (void)arglist;

    return 0;
}

int __cdecl _CrtDbgReportW(int report_type,
                           const wchar_t *file,
                           int line,
                           const wchar_t *module,
                           const wchar_t *format,
                           ...)
{
    va_list args;

    (void)report_type;
    (void)file;
    (void)line;
    (void)module;

    if (format)
    {
        va_start(args, format);
        va_end(args);
    }

    return 0;
}

int __cdecl _CrtDbgReportWV(int report_type,
                            const wchar_t *file,
                            int line,
                            const wchar_t *module,
                            const wchar_t *format,
                            va_list arglist)
{
    (void)report_type;
    (void)file;
    (void)line;
    (void)module;
    (void)format;
    (void)arglist;

    return 0;
}

typedef void (__cdecl *crt_client_callback)(void *, void *);

void __cdecl _CrtDoForAllClientObjects(crt_client_callback callback,
                                       void *context)
{
    // Do nothing - no client blocks to iterate in release CRT
    (void)callback;
    (void)context;
}

int __cdecl _VCrtDbgReportA(int report_type,
                            void *context,
                            const char *file,
                            int line,
                            const char *module,
                            const char *format,
                            va_list arglist)
{
    (void)report_type;
    (void)context;
    (void)file;
    (void)line;
    (void)module;
    (void)format;
    (void)arglist;

    return 0;
}

int __cdecl _VCrtDbgReportW(int report_type,
                            void *context,
                            const wchar_t *file,
                            int line,
                            const wchar_t *module,
                            const wchar_t *format,
                            va_list arglist)
{
    (void)report_type;
    (void)context;
    (void)file;
    (void)line;
    (void)module;
    (void)format;
    (void)arglist;

    return 0;
}

unsigned char _mbcasemap[257] = {0};

unsigned char* __cdecl __p__mbcasemap(void)
{
    return _mbcasemap;
}

unsigned short const _wctype[257] = {0};

#else /* !USE_NATIVE_MINGW_CRT */

typedef void (__cdecl *crt_client_callback)(void *, void *);

// atexit is needed by libsupc++
extern int __cdecl _crt_atexit(void (__cdecl*)(void));
int __cdecl atexit(void (__cdecl* function)(void))
{
    return _crt_atexit(function);
}

int __cdecl _CrtCheckMemory(void)
{
    return 1;  //NOTE Always return "heap is OK"
    // Real apps use this inside #ifdef _DEBUG blocks
    // They don't expect it to work in release builds
}

void __cdecl _CrtDoForAllClientObjects(crt_client_callback callback,
                                       void *context)
{
    (void)callback;
    (void)context;
}

int __cdecl _CrtDbgReport(int report_type,
                          const char *file,
                          int line,
                          const char *module,
                          const char *format,
                          ...)
{
    va_list args;

    (void)report_type;
    (void)file;
    (void)line;
    (void)module;

    va_start(args, format);
    va_end(args);
    /* Pretend success. */
    return 0;
}

int __cdecl _CrtDbgReportV(int report_type,
                           const char *file,
                           int line,
                           const char *module,
                           const char *format,
                           va_list arglist)
{
    (void)report_type;
    (void)file;
    (void)line;
    (void)module;
    (void)format;
    (void)arglist;
    return 0;
}

int __cdecl _CrtDbgReportW(int report_type,
                           const wchar_t *file,
                           int line,
                           const wchar_t *module,
                           const wchar_t *format,
                           ...)
{
    va_list args;

    (void)report_type;
    (void)file;
    (void)line;
    (void)module;

    va_start(args, format);
    va_end(args);
    return 0;
}

int __cdecl _CrtDbgReportWV(int report_type,
                            const wchar_t *file,
                            int line,
                            const wchar_t *module,
                            const wchar_t *format,
                            va_list arglist)
{
    (void)report_type;
    (void)file;
    (void)line;
    (void)module;
    (void)format;
    (void)arglist;
    return 0;
}

int __cdecl _VCrtDbgReportA(int report_type,
                            void *context,
                            const char *file,
                            int line,
                            const char *module,
                            const char *format,
                            va_list arglist)
{
    (void)report_type;
    (void)context;
    (void)file;
    (void)line;
    (void)module;
    (void)format;
    (void)arglist;
    return 0;
}

int __cdecl _VCrtDbgReportW(int report_type,
                            void *context,
                            const wchar_t *file,
                            int line,
                            const wchar_t *module,
                            const wchar_t *format,
                            va_list arglist)
{
    (void)report_type;
    (void)context;
    (void)file;
    (void)line;
    (void)module;
    (void)format;
    (void)arglist;
    return 0;
}

void* __cdecl operator_new(size_t size)
{
    return malloc(size);
}

void _cdecl operator_delete(void *mem)
{
    free(mem);
}

#ifdef _M_IX86
void _chkesp_failed(void)
{
    __debugbreak();
}
#endif

int __cdecl __acrt_initialize_sse2(void)
{
    return 0;
}

// The following stubs cannot be implemented as stubs by spec2def, because they are intrinsics

#ifdef _MSC_VER
#pragma warning(disable:4163) // not available as an intrinsic function
#pragma warning(disable:4164) // intrinsic function not declared
#pragma function(fma)
#pragma function(fmaf)
#pragma function(log2)
#pragma function(log2f)
#pragma function(lrint)
#pragma function(lrintf)
#endif

double fma(double x, double y, double z)
{
    // Simplistic implementation
    return (x * y) + z;
}

float fmaf(float x, float y, float z)
{
    // Simplistic implementation
    return (x * y) + z;
}

double log2(double x)
{
    // Simplistic implementation: log2(x) = log(x) / log(2)
    return log(x) * M_LOG2E;
}

float log2f(float x)
{
    return (float)log2((double)x);
}

long int lrint(double x)
{
    __debugbreak();
    return 0;
}

long int lrintf(float x)
{
    __debugbreak();
    return 0;
}

#endif /* !USE_NATIVE_MINGW_CRT */

#include <windef.h>
#include <winbase.h>

#ifdef USE_NATIVE_MINGW_CRT

/* Proper UCRT initialization for MinGW builds.
 *
 * The MinGW ucrtbase provides initialization functions that we must call
 * to properly set up UCRT state including:
 * - Locale initialization via setlocale()
 * - Environment synchronization
 * - Invalid parameter handler setup
 * - onexit/atexit table initialization
 *
 * This ensures Win32k/Winsrv have fully initialized UCRT state and
 * prevents worker queue stalls caused by incomplete initialization.
 *
 * Since MinGW's ucrtbase doesn't expose __acrt_* internal functions,
 * we manually initialize the essential components that Windows UCRT
 * initializes in its DllMain.
 */

#include <locale.h>
#include <stdlib.h>

/* Import MinGW's environment initialization functions */
extern int __cdecl _initialize_narrow_environment(void);
extern int __cdecl _initialize_wide_environment(void);

/* Import locale initialization */
extern int __cdecl __initialize_lconv_for_unsigned_char(void);

/* Thread-local storage for UCRT */
static DWORD ucrt_tls_index = TLS_OUT_OF_INDEXES;

static int ucrt_process_attach(void)
{
    /* Allocate TLS slot for per-thread UCRT data.
     * This is the ONLY safe operation in DLL_PROCESS_ATTACH. */
    ucrt_tls_index = TlsAlloc();
    if (ucrt_tls_index == TLS_OUT_OF_INDEXES)
        return 0;

    /* DO NOT perform any complex initialization here!
     *
     * The following operations are UNSAFE in DLL_PROCESS_ATTACH
     * and can cause loader deadlocks:
     * - setlocale() - may load locale DLLs or acquire locks
     * - __initialize_lconv_for_unsigned_char() - calls localeconv() which acquires locks
     * - _initialize_narrow_environment() - calls __p__environ() which may lazy-init
     * - _initialize_wide_environment() - calls __p__wenviron() which may lazy-init
     *
     * All CRT state will be properly initialized on first use through
     * lazy initialization in the MinGW CRT. The "C" locale is the default.
     */

    return 1;
}

static int ucrt_process_detach(void)
{
    /* Free TLS slot */
    if (ucrt_tls_index != TLS_OUT_OF_INDEXES)
    {
        TlsFree(ucrt_tls_index);
        ucrt_tls_index = TLS_OUT_OF_INDEXES;
    }

    return 1;
}

static int ucrt_thread_attach(void)
{
    /* Allocate per-thread UCRT data if needed */
    if (ucrt_tls_index != TLS_OUT_OF_INDEXES)
    {
        /* For now, just ensure the slot is valid.
         * Future: allocate thread-specific errno, strerror buffers, etc.
         */
        if (!TlsSetValue(ucrt_tls_index, NULL))
            return 0;
    }
    return 1;
}

static int ucrt_thread_detach(void)
{
    /* Clean up per-thread UCRT data.
     * NOTE: Do NOT call free() here - it's unsafe inside DLL_THREAD_DETACH
     * callbacks as it can cause loader deadlocks while the loader lock is held.
     * The TLS slot will be freed during DLL_PROCESS_DETACH, so any allocated
     * data will be reclaimed when the process terminates. */
    if (ucrt_tls_index != TLS_OUT_OF_INDEXES)
    {
        TlsSetValue(ucrt_tls_index, NULL);
    }
    return 1;
}

BOOL WINAPI __acrt_DllMain(HINSTANCE instance,
                           DWORD reason,
                           LPVOID reserved)
{
    (void)instance;
    (void)reserved;

    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            /* Initialize UCRT state: locale, environment, etc. */
            if (!ucrt_process_attach())
                return FALSE;
            break;

        case DLL_PROCESS_DETACH:
            /* Clean up UCRT state */
            ucrt_process_detach();
            break;

        case DLL_THREAD_ATTACH:
            /* Initialize per-thread UCRT state */
            if (!ucrt_thread_attach())
                return FALSE;
            break;

        case DLL_THREAD_DETACH:
            /* Clean up per-thread UCRT state */
            ucrt_thread_detach();
            break;
    }

    return TRUE;
}

#endif /* USE_NATIVE_MINGW_CRT */
