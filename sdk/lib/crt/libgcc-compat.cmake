#
# GCC Compatibility Library for i386
#
# Provides runtime helper functions that GCC emits for 64-bit arithmetic operations.
# This library wraps the existing MSVC-style implementations to provide GCC-style names.
#

if(ARCH STREQUAL "i386")
    list(APPEND LIBGCC_COMPAT_SOURCE
        except/i386/chkstk_asm.s
        except/i386/chkstk_ms.s
        math/i386/alldiv_asm.s
        math/i386/alldvrm_asm.s
        math/i386/allrem_asm.s
        math/i386/allmul_asm.s
        math/i386/allshl_asm.s
        math/i386/allshr_asm.s
        math/i386/aulldiv_asm.s
        math/i386/aulldvrm_asm.s
        math/i386/aullrem_asm.s
        math/i386/aullshr_asm.s
        math/i386/gcc_compat.s
    )

    add_asm_files(libgcc_compat_asm ${LIBGCC_COMPAT_SOURCE})
    add_library(gcc-compat STATIC ${libgcc_compat_asm})
    set_target_properties(gcc-compat PROPERTIES LINKER_LANGUAGE "C")
    add_dependencies(gcc-compat asm)
endif()
