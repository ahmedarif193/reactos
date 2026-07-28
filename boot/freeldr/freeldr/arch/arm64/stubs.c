/*
 * Minimal ARM64 stubs to satisfy links in UEFI FreeLDR
 */

#include <freeldr.h>
#include <stdarg.h>
#include <arch/arm64/arm64.h>

#ifdef KeGetCurrentIrql
#undef KeGetCurrentIrql
#endif

KIRQL
KeGetCurrentIrql(VOID)
{
    return PASSIVE_LEVEL;
}

unsigned __int64
__rdtsc(void)
{
    unsigned __int64 value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
}

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

VOID
FrLdrBugCheckWithMessage(
    ULONG BugCode,
    PCHAR File,
    ULONG Line,
    PCSTR Format,
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

/*
 * ARM64 FreeLDR uses a simple KSEG0 offset mapping for kernel addresses.
 *
 * Virtual Address Layout:
 *   - Physical addresses: 0x0000000000000000 - 0xFFFF7FFFFFFFFFFF
 *   - Kernel virtual:     0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF
 *
 * The mapping is: KernelVA = PhysicalAddress + ARM64_KSEG0_BASE
 *
 * These functions are idempotent - calling VaToPa(VaToPa(x)) or PaToVa(PaToVa(x))
 * will not cause corruption, making them safe even if addresses are already in the
 * expected format.
 */
PVOID VaToPa(PVOID Va)
{
    ULONGLONG value = (ULONGLONG)(ULONG_PTR)Va;
    /* Check if this is a kernel virtual address */
    if (value >= ARM64_KSEG0_BASE)
        return (PVOID)(value - ARM64_KSEG0_BASE);
    /* Already a physical address (or identity-mapped low address) */
    return Va;
}

PVOID PaToVa(PVOID Pa)
{
    ULONGLONG value = (ULONGLONG)(ULONG_PTR)Pa;
    if (Pa == NULL)
        return NULL;
    /* Check if this is already a kernel virtual address - avoid double conversion */
    if (value >= ARM64_KSEG0_BASE)
        return Pa;
    /* Convert physical address to kernel virtual address */
    return (PVOID)(value + ARM64_KSEG0_BASE);
}

/* RtlFillMemoryUlong is provided by sdk/lib/rtl/mem.c, which blrtl now
 * compiles for arm64 as well. */
