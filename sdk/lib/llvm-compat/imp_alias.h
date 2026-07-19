/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Helpers to define dllimport slots bound to static functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/*
 * __imp_<sym> carries <sym>'s platform decoration: on i386 cdecl foo gets __imp__foo, one-argument stdcall
 * bar gets __imp__bar@4. Spell the slot names through asm labels, a plain C variable would be decorated again.
 * Do not include CRT/SDK headers in the defining TU: they would declare the aliased names dllimport.
 */

#if defined(__i386__)
#define IMP_SYMBOL_CDECL(name)         "__imp__" #name
#define IMP_SYMBOL_STDCALL(name, size) "__imp__" #name "@" #size
#else
#define IMP_SYMBOL_CDECL(name)         "__imp_" #name
#define IMP_SYMBOL_STDCALL(name, size) "__imp_" #name
#endif

/* Slot for a cdecl function, bound to the function of the same name */
#define IMP_ALIAS(name) \
    extern void name(void); \
    const void *__imp_alias_##name __asm__(IMP_SYMBOL_CDECL(name)) = \
        (const void *)&name

/* Slot for a stdcall function with `size` argument bytes, bound to an ABI-compatible target */
#define IMP_ALIAS_STDCALL(name, size, target) \
    const void *__imp_alias_##name __asm__(IMP_SYMBOL_STDCALL(name, size)) = \
        (const void *)&target
