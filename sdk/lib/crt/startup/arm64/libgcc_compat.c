/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compatibility shim for libgcc on ARM64 Native Subsystems
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winnt.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <ndk/rtlfuncs.h>

/* Defines standard NT types if they are missing */
#ifndef _NTDEF_
typedef LONG NTSTATUS;
#endif

/*
 * libgcc on ARM64 (for 128-bit math) pulls in emutls, which requires
 * malloc, free, calloc, realloc, and abort.
 * In native binaries (ntdll, winsrv), these are not available from CRT.
 * We map them to NT Heap APIs.
 *
 * We mark these as weak so that if the module actually links a CRT (like msvcrt),
 * the CRT's native implementation overrides these shims.
 */

/* Manually declare heap functions to avoid dllimport issues from NDK headers. 
 * RtlGetProcessHeap is a macro in NDK, so we don't extern it.
 */
extern PVOID NTAPI RtlAllocateHeap(HANDLE HeapHandle, ULONG Flags, SIZE_T Size);
extern BOOLEAN NTAPI RtlFreeHeap(HANDLE HeapHandle, ULONG Flags, PVOID BaseAddress);
extern PVOID NTAPI RtlReAllocateHeap(HANDLE HeapHandle, ULONG Flags, PVOID BaseAddress, SIZE_T Size);
extern VOID NTAPI RtlRaiseStatus(NTSTATUS Status);

/* 
 * RtlGetProcessHeap is a macro defined in ndk/rtlfuncs.h as (NtCurrentPeb()->ProcessHeap).
 * However, we want to avoid including too many NDK headers if they conflict.
 * If ndk/rtlfuncs.h is included, it should work.
 * Let's ensure NtCurrentPeb is available. It is usually in ndk/peb_teb.h or similar?
 * Wait, RtlGetProcessHeap macro usage requires definitions of PEB structure.
 * 
 * If we can't easily include full NDK, we can just use the manual PEB access for ARM64 or import it?
 * No, simpler: Include <ndk/rtlfuncs.h> fully and ONLY manually redeclare the functions we want to override dllimport for.
 * But RtlGetProcessHeap is NOT a function, so redeclaring it was the error.
 */

__attribute__((weak))
void *malloc(size_t size)
{
    return RtlAllocateHeap(RtlGetProcessHeap(), 0, size);
}

__attribute__((weak))
void free(void *ptr)
{
    if (ptr)
        RtlFreeHeap(RtlGetProcessHeap(), 0, ptr);
}

__attribute__((weak))
void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    return RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, total);
}

__attribute__((weak))
void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return RtlAllocateHeap(RtlGetProcessHeap(), 0, size);
    
    return RtlReAllocateHeap(RtlGetProcessHeap(), 0, ptr, size);
}

__attribute__((weak))
void abort(void)
{
    RtlRaiseStatus(STATUS_UNSUCCESSFUL);
    while (TRUE) ; /* logic dead end */
}

/*
 * Additional emutls dependencies from libclang_rt.builtins
 */

/* _aligned_malloc/_aligned_free - used by TLS emulation for aligned allocations */
__attribute__((weak))
void *_aligned_malloc(size_t size, size_t alignment)
{
    /* NT heap allocations are already 16-byte aligned on ARM64, which is sufficient for most TLS */
    (void)alignment;
    return RtlAllocateHeap(RtlGetProcessHeap(), 0, size);
}

__attribute__((weak))
void _aligned_free(void *ptr)
{
    if (ptr)
        RtlFreeHeap(RtlGetProcessHeap(), 0, ptr);
}

/* atexit - TLS emulation tries to register cleanup handlers */
__attribute__((weak))
int atexit(void (*func)(void))
{
    /* Native subsystem modules don't support atexit, so we ignore it */
    (void)func;
    return 0; /* Success */
}

/* fprintf/_iob_func - used for TLS emulation error reporting (rarely executed) */
typedef struct _iobuf {
    char *_ptr;
    int _cnt;
    char *_base;
    int _flag;
    int _file;
    int _charbuf;
    int _bufsiz;
    char *_tmpfname;
} FILE;

__attribute__((weak))
FILE *__acrt_iob_func(unsigned idx)
{
    /* Return a dummy FILE structure - TLS errors should never occur in normal operation */
    static FILE dummy_file = {0};
    (void)idx;
    return &dummy_file;
}

__attribute__((weak))
int fprintf(FILE *stream, const char *format, ...)
{
    /* Native subsystem modules don't have stdio - silently ignore error messages */
    (void)stream;
    (void)format;
    return 0;
}

/* Thread-safe initialization dummies for libgcc */
__attribute__((weak))
int _CRT_MT = 1;

__attribute__((weak))
void __mingwthr_key_dtor(void *key, void (*dtor)(void *))
{
    (void)key;
    (void)dtor;
}
