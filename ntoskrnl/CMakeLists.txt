
PROJECT(NTOS)

if(KDBG AND (ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64") AND NOT TARGET kdb_zydis)
    # KDB must decode the instruction set emitted by current compilers. Build
    # the pinned Zydis copy already carried by the ARM64EC submodule in its
    # allocation-free kernel configuration. Define native ReactOS targets here
    # because the upstream CMake aliases conflict with our clean-target wrapper.
    set(KDB_ZYDIS_ROOT ${REACTOS_SOURCE_DIR}/submodules/fex-arm64ec/External/zydis)
    set(KDB_ZYCORE_ROOT ${KDB_ZYDIS_ROOT}/dependencies/zycore)

    add_library(kdb_zycore STATIC ${KDB_ZYCORE_ROOT}/src/API/Memory.c ${KDB_ZYCORE_ROOT}/src/API/Process.c ${KDB_ZYCORE_ROOT}/src/API/Synchronization.c ${KDB_ZYCORE_ROOT}/src/API/Terminal.c ${KDB_ZYCORE_ROOT}/src/API/Thread.c ${KDB_ZYCORE_ROOT}/src/Allocator.c ${KDB_ZYCORE_ROOT}/src/ArgParse.c ${KDB_ZYCORE_ROOT}/src/Bitset.c ${KDB_ZYCORE_ROOT}/src/Format.c ${KDB_ZYCORE_ROOT}/src/List.c ${KDB_ZYCORE_ROOT}/src/String.c ${KDB_ZYCORE_ROOT}/src/Vector.c ${KDB_ZYCORE_ROOT}/src/Zycore.c)
    target_compile_definitions(kdb_zycore PUBLIC ZYAN_NO_LIBC ZYCORE_STATIC_BUILD)
    target_include_directories(kdb_zycore PUBLIC ${KDB_ZYCORE_ROOT}/include)

    add_library(kdb_zydis STATIC ${KDB_ZYDIS_ROOT}/src/MetaInfo.c ${KDB_ZYDIS_ROOT}/src/Mnemonic.c ${KDB_ZYDIS_ROOT}/src/Register.c ${KDB_ZYDIS_ROOT}/src/SharedData.c ${KDB_ZYDIS_ROOT}/src/String.c ${KDB_ZYDIS_ROOT}/src/Utils.c ${KDB_ZYDIS_ROOT}/src/Zydis.c ${KDB_ZYDIS_ROOT}/src/Decoder.c ${KDB_ZYDIS_ROOT}/src/DecoderData.c ${KDB_ZYDIS_ROOT}/src/Disassembler.c ${KDB_ZYDIS_ROOT}/src/Formatter.c ${KDB_ZYDIS_ROOT}/src/FormatterBuffer.c ${KDB_ZYDIS_ROOT}/src/FormatterATT.c ${KDB_ZYDIS_ROOT}/src/FormatterBase.c ${KDB_ZYDIS_ROOT}/src/FormatterIntel.c)
    target_compile_definitions(kdb_zydis PUBLIC ZYAN_NO_LIBC ZYCORE_STATIC_BUILD ZYDIS_STATIC_BUILD ZYDIS_DISABLE_ENCODER ZYDIS_DISABLE_SEGMENT)
    target_include_directories(kdb_zydis PUBLIC ${KDB_ZYDIS_ROOT}/include ${KDB_ZYCORE_ROOT}/include PRIVATE ${KDB_ZYDIS_ROOT}/src)
    target_link_libraries(kdb_zydis kdb_zycore)
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

# Link GCC stack protector lib
if (STACK_PROTECTOR)
    target_link_libraries(libntoskrnl INTERFACE gcc_ssp_nt)
endif()

add_asm_files(ntoskrnl_asm ${NTOSKRNL_ASM_SOURCE})

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Clang optimises strcmp calls to memcmp.
    target_link_libraries(libntoskrnl INTERFACE memcmp)
endif()

if((CMAKE_C_COMPILER_ID STREQUAL "Clang") OR (CMAKE_C_COMPILER_ID STREQUAL "GNU"))
    if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
        add_compile_options(-mxsave -mxsaves -mxsaveopt)
    endif()
endif()

list(APPEND PCH_SKIP_SOURCE
    guid.c)

# ARM64 Win11 ABI export parity: spec2def-generated stubs for the unimplemented
# Win11 exports, plus hand-written placeholder data exports. Both are kept out of
# the PCH (ntoskrnl_stubs.c already gets SKIP_PRECOMPILE_HEADERS from spec2def();
# ke/arm64/exports.c is header-independent on purpose).
if(ARCH STREQUAL "arm64")
    list(APPEND PCH_SKIP_SOURCE
        ke/arm64/exports.c
        ${CMAKE_CURRENT_BINARY_DIR}/ntoskrnl_stubs.c)
endif()

add_executable(ntoskrnl
    ${ntoskrnl_asm}
    ${NTOSKRNL_SOURCE}
    ${PCH_SKIP_SOURCE}
    ntoskrnl.rc
    ${CMAKE_CURRENT_BINARY_DIR}/ntoskrnl.def)
set_module_type(ntoskrnl kernel)

source_group(TREE ${REACTOS_SOURCE_DIR}/ntoskrnl PREFIX "Source Files" FILES ${NTOSKRNL_SOURCE})

target_link_libraries(ntoskrnl ntoskrnl_vista cportlib csq ${PSEH_LIB} arbiter cmlib ntlsalib rtl ${ROSSYM_LIB} libcntpr setjmp wdmguid poguid ioevent)

if(KDBG AND (ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64"))
    target_link_libraries(ntoskrnl kdb_zydis)
endif()

if(ARCH STREQUAL "amd64")
    target_link_libraries(ntoskrnl rtl_vista)
endif()

if(STACK_PROTECTOR)
    target_link_libraries(ntoskrnl gcc_ssp_nt)
endif()

add_importlibs(ntoskrnl hal kdcom bootvid)
if(NOT (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND ARCH STREQUAL "i386"))
    add_pch(ntoskrnl ${REACTOS_SOURCE_DIR}/ntoskrnl/include/ntoskrnl.h "${PCH_SKIP_SOURCE}")
endif()
add_dependencies(ntoskrnl psdk asm)
add_cd_file(TARGET ntoskrnl DESTINATION reactos/system32 NO_CAB FOR all)

add_subdirectory(ntkrnlmp)

add_asm_files(ntdllsys_asm ntdll.S)
add_library(ntdllsys ${ntdllsys_asm})
set_target_properties(ntdllsys PROPERTIES LINKER_LANGUAGE "C")
add_dependencies(ntdllsys asm)

# NT6 syscalls for ntdll_vista
add_asm_files(ntdllsys_nt6_asm ntdll_nt6.S)
add_library(ntdllsys_nt6 ${ntdllsys_nt6_asm})
set_target_properties(ntdllsys_nt6 PROPERTIES LINKER_LANGUAGE "C")
add_dependencies(ntdllsys_nt6 asm)

if(WOW64_I386_RUNTIME AND ARCH STREQUAL "i386")
    target_compile_definitions(ntdllsys PRIVATE WOW64_I386_RUNTIME)
    target_compile_definitions(ntdllsys_nt6 PRIVATE WOW64_I386_RUNTIME)
endif()
