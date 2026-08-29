/*++ NDK Version: 0095

Copyright (c) Alex Ionescu.  All rights reserved.

Header Name:

    asm.h

Abstract:

    Portability header to choose the correct Architecture-specific header.

Author:

    Alex Ionescu (alex.ionescu@reactos.com)   06-Oct-2004

--*/

#if defined(_M_IX86)
#include <i386/asm.h>
#elif defined(_M_AMD64) || defined(_M_ARM) || defined(_M_PPC) || defined(_M_ARM64)
//
// AMD64, ARM and PPC ports don't use asm.h
//
#if defined(_M_ARM64) && defined(__GNUC__) && !defined(__clang__) && defined(__ASM__) && !defined(__REACTOS_GNU_ARM64_SEH_MACROS)
#define __REACTOS_GNU_ARM64_SEH_MACROS
/* GNU as emits absolute IMAGE_REL_ARM64_ADDR32 relocations for ARM64 SEH
 * procedure records. They overflow for normal 64-bit image bases, so accept
 * all Windows unwind annotations as no-ops until the toolchain can emit RVAs. */
#define seh_proc reactos_seh_proc
#define seh_endproc reactos_seh_endproc
#define seh_endprologue reactos_seh_endprologue
#define seh_stackalloc reactos_seh_stackalloc
#define seh_pushframe reactos_seh_pushframe
.macro .reactos_seh_proc name
.endm
.macro .reactos_seh_endproc
.endm
.macro .reactos_seh_endprologue
.endm
.macro .reactos_seh_stackalloc size
.endm
.macro .reactos_seh_pushframe code=0
.endm
.macro .seh_add_fp offset
.endm
.macro .seh_context
.endm
.macro .seh_nop
.endm
.macro .seh_save_any_reg reg, offset
.endm
.macro .seh_save_fplr offset
.endm
.macro .seh_save_fplr_x offset
.endm
.macro .seh_save_fregp reg, offset
.endm
.macro .seh_save_reg reg, offset
.endm
.macro .seh_save_regp reg, offset
.endm
.macro .seh_save_regp_x reg, offset
.endm
.macro .seh_set_fp
.endm
#endif
#else
#error Unsupported Architecture
#endif
