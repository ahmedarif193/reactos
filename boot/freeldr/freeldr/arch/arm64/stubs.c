/*
 * Minimal ARM64 stubs to satisfy links in UEFI FreeLDR
 */

#include <freeldr.h>
#include <stdarg.h>
#include <arch/arm64/arm64.h>

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

static void __reactos_debugbreak_impl(void)
{
    __asm__ volatile("brk #0");
}

void __debugbreak(void) __attribute__((alias("__reactos_debugbreak_impl")));

static void __reactos_fastfail_impl(unsigned int code)
{
    (void)code;
    for (;;) { __asm__ volatile("wfi"); }
}

void __fastfail(unsigned int code) __attribute__((alias("__reactos_fastfail_impl")));

void DbgBreakPoint(void)
{
    __asm__ volatile("brk #0");
}

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return PASSIVE_LEVEL;
}

VOID
FrLdrBugCheckWithMessage(
    ULONG BugCode,
    PCHAR File,
    ULONG Line,
    PSTR Format,
    ...)
{
    CHAR buf[256];
    va_list ap;
    va_start(ap, Format);
    RtlStringCbVPrintfA(buf, sizeof(buf), Format, ap);
    va_end(ap);
    UiMessageBoxCritical(buf);
    for (;;) { __asm__ volatile("wfi"); }
}

/* ARM64 FreeLDR uses a simple KSEG0 offset mapping for kernel addresses */
PVOID VaToPa(PVOID Va)
{
    ULONGLONG value = (ULONGLONG)(ULONG_PTR)Va;
    if (value >= ARM64_KSEG0_BASE)
        return (PVOID)(value - ARM64_KSEG0_BASE);
    return Va;
}

PVOID PaToVa(PVOID Pa)
{
    if (Pa == NULL)
        return NULL;
    return (PVOID)((ULONGLONG)(ULONG_PTR)Pa + ARM64_KSEG0_BASE);
}

VOID
RtlFillMemoryUlong(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_ SIZE_T Length,
    _In_ ULONG Fill)
{
    ULONG *p = (ULONG*)Destination;
    SIZE_T n = Length / sizeof(ULONG);
    for (SIZE_T i = 0; i < n; ++i) p[i] = Fill;
}
