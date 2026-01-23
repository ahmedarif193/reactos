
PROJECT(NTOS)

if(NT_ARCH STREQUAL "arm64")
    add_definitions(-D_M_ARM64)
endif()

if (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Enable this again. CORE-17637
    add_compile_options(-Wunused-result)
endif()

include(ntos.cmake)

set(NTOSKRNL_SOURCE ${SOURCE})
set(NTOSKRNL_ASM_SOURCE ${ASM_SOURCE})

set(NTKRNLMP_SOURCE ${SOURCE})
set(NTKRNLMP_ASM_SOURCE ${ASM_SOURCE})

spec2def(ntoskrnl.exe ntoskrnl.spec ADD_IMPORTLIB)

# Embed RTC libs
if (STACK_PROTECTOR)
    target_sources(libntoskrnl PRIVATE $<TARGET_OBJECTS:gcc_ssp_nt>)
endif()

add_asm_files(ntoskrnl_asm ${NTOSKRNL_ASM_SOURCE})

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Clang optimises strcmp calls to memcmp.
    target_sources(libntoskrnl PRIVATE $<TARGET_OBJECTS:memcmp>)
endif()

if((CMAKE_C_COMPILER_ID STREQUAL "Clang") OR (CMAKE_C_COMPILER_ID STREQUAL "GNU"))
    if(NT_ARCH STREQUAL "i386" OR NT_ARCH STREQUAL "amd64")
        # Enable xsave feature on x86/x64
        add_compile_options(-mxsave)
    endif()
    if(NT_ARCH STREQUAL "arm64")
        #
        # ARM64: Prevent compiler from inlining memset with SIMD instructions.
        #
        # Clang's builtin memset uses 128-bit SIMD stores (str q0) which require
        # 16-byte alignment. The kernel memory allocator doesn't guarantee this
        # for all structures (e.g., TEB fields may only be 8-byte aligned),
        # causing alignment faults.
        #
        # With -fno-builtin-memset, the compiler calls the external memset symbol
        # which is provided by memset_asm.S. This implementation uses 64-bit
        # stores (str x0) that only require 8-byte alignment.
        #
        # We also add -mstrict-align to prevent unaligned memory accesses in
        # general. ARM64 does support unaligned accesses but they can be slower
        # and may cause issues with certain memory types.
        #
        add_compile_options(-fno-builtin-memset)
        add_compile_options(-mstrict-align)
    endif()
endif()

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_executable(ntoskrnl
    ${ntoskrnl_asm}
    ${NTOSKRNL_SOURCE}
    ${PCH_SKIP_SOURCE}
    ntoskrnl.rc
    ${CMAKE_CURRENT_BINARY_DIR}/ntoskrnl.def)
target_sources(ntoskrnl PRIVATE rtl/intrin_llshift.c)
if((NT_ARCH STREQUAL "i386" OR NT_ARCH STREQUAL "amd64") AND
   (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang"))
    target_sources(ntoskrnl PRIVATE rtl/intrin_msr.c)
endif()
set_module_type(ntoskrnl kernel)

add_dependencies(ntoskrnl ntos_syscalls)

source_group(TREE ${REACTOS_SOURCE_DIR}/ntoskrnl PREFIX "Source Files" FILES ${NTOSKRNL_SOURCE})

target_link_libraries(ntoskrnl cportlib csq ${PSEH_LIB} arbiter cmlib ntlsalib rtl ${ROSSYM_LIB} libcntpr setjmp wdmguid poguid ioevent)

if(STACK_PROTECTOR)
    target_sources(ntoskrnl PRIVATE $<TARGET_OBJECTS:gcc_ssp_nt>)
endif()

add_importlibs(ntoskrnl hal kdcom bootvid)
add_pch(ntoskrnl ${REACTOS_SOURCE_DIR}/ntoskrnl/include/ntoskrnl.h "${PCH_SKIP_SOURCE}")
add_dependencies(ntoskrnl psdk asm)
add_cd_file(TARGET ntoskrnl DESTINATION reactos/system32 NO_CAB FOR all)

add_subdirectory(ntkrnlmp)

add_asm_files(ntdllsys_asm ntdll.S)
add_library(ntdllsys ${ntdllsys_asm})
set_target_properties(ntdllsys PROPERTIES LINKER_LANGUAGE "C")
add_dependencies(ntdllsys asm ntos_syscalls)

# DWARF sections are preserved directly in the binary for rossym_new to parse
