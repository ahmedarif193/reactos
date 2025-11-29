/*
 * Provide __bss_start__/__bss_end__ markers when GNU linker scripts are
 * unavailable (e.g. when lld links the PE image). We rely on section
 * suffix sorting (.bss$*) so the markers bracket the entire BSS range.
 */

#if !defined(__GNUC__)
#error "bssmarkers.c requires a compiler that supports GNU inline assembly"
#endif

__asm__(".section .bss$A,\"bw\"\n"
        ".globl __bss_start__\n"
        "__bss_start__:\n"
        ".section .bss$Z,\"bw\"\n"
        ".globl __bss_end__\n"
        "__bss_end__:\n");
