##
## PROJECT:     FreeLoader
## LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
## PURPOSE:     Build definitions for UEFI
## COPYRIGHT:   Copyright 2023 Justin Miller <justinmiller100@gmail.com>
##

include_directories(BEFORE
    ${REACTOS_SOURCE_DIR}/boot/environ/include/efi
    ${REACTOS_SOURCE_DIR}/boot/freeldr/freeldr
    ${REACTOS_SOURCE_DIR}/boot/freeldr/freeldr/include
    ${REACTOS_SOURCE_DIR}/boot/freeldr/freeldr/include/arch/uefi)

if(DEFINED TOOLCHAIN_PATH AND NOT MSVC)
    # Ensure MinGW target libs are visible during Clang linking.
    link_directories(
        ${TOOLCHAIN_PATH}/../lib
        ${TOOLCHAIN_PATH}/../${TOOLCHAIN_PREFIX}/lib
        ${TOOLCHAIN_PATH}/../sysroot/usr/${TOOLCHAIN_PREFIX}/lib
        ${TOOLCHAIN_PATH}/../sysroot/mingw/lib)
endif()

list(APPEND UEFILDR_ARC_SOURCE
    ${FREELDR_ARC_SOURCE}
    arch/uefi/stubs.c
    arch/uefi/efiapp.c
    arch/uefi/ueficon.c
    arch/uefi/uefidebug.c
    arch/uefi/uefidisk.c
    arch/uefi/uefihw.c
    arch/uefi/uefimem.c
    arch/uefi/uefisetup.c
    arch/uefi/uefiserial.c
    arch/uefi/uefiutil.c
    arch/uefi/uefivid.c
    arch/uefi/uefisym.c
    arch/uefi/uefibacktrace.c
    arch/vgafont.c
    arch/uefi/uefiarc.c)

if(ARCH STREQUAL "i386")
    list(APPEND UEFILDR_ARC_SOURCE
        arch/i386/i386idt.c)
    list(APPEND UEFILDR_COMMON_ASM_SOURCE
        arch/uefi/i386/uefiasm.S
        arch/i386/i386trap.S)
elseif(ARCH STREQUAL "amd64")
    list(APPEND UEFILDR_COMMON_ASM_SOURCE
        arch/uefi/amd64/uefiasm.S)
elseif(ARCH STREQUAL "arm")
    list(APPEND UEFILDR_ARC_SOURCE
        arch/arm/macharm.c
        arch/arm/debug.c)
    #TBD
elseif(ARCH STREQUAL "arm64")
    list(APPEND UEFILDR_ARC_SOURCE
        arch/uefi/arm64/uefitrap.c)
    list(APPEND UEFILDR_COMMON_ASM_SOURCE
        arch/uefi/arm64/uefiasm.S
        arch/uefi/arm64/uefitrap.S)
else()
    #TBD
endif()

list(APPEND UEFILDR_BOOTMGR_SOURCE
    ${FREELDR_BOOTMGR_SOURCE}
    custom.c
    options.c
    oslist.c
)

add_asm_files(uefifreeldr_common_asm ${FREELDR_COMMON_ASM_SOURCE} ${UEFILDR_COMMON_ASM_SOURCE})

add_library(uefifreeldr_common
    ${uefifreeldr_common_asm}
    ${UEFILDR_ARC_SOURCE}
    ${FREELDR_BOOTLIB_SOURCE}
    ${UEFILDR_BOOTMGR_SOURCE}
    ${FREELDR_NTLDR_SOURCE})

target_link_libraries(uefifreeldr_common PUBLIC apisets)
if(KDBG)
    target_link_libraries(uefifreeldr_common PUBLIC rossym)
endif()

target_compile_definitions(uefifreeldr_common PRIVATE UEFIBOOT)

if((CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    AND (ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64"))
    # Prevent using SSE (no support in freeldr)
    target_compile_options(uefifreeldr_common PUBLIC -mno-sse)
endif()

set(PCH_SOURCE
    ${UEFILDR_ARC_SOURCE}
    ${FREELDR_BOOTLIB_SOURCE}
    ${UEFILDR_BOOTMGR_SOURCE}
    ${FREELDR_NTLDR_SOURCE})

add_pch(uefifreeldr_common include/arch/uefi/uefildr.h PCH_SOURCE)
add_dependencies(uefifreeldr_common bugcodes asm xdk)

## GCC builds need this extra thing for some reason...
if(ARCH STREQUAL "i386" AND NOT MSVC)
    target_link_libraries(uefifreeldr_common PUBLIC mini_hal)
endif()


spec2def(uefildr.exe freeldr.spec)

list(APPEND UEFILDR_BASE_SOURCE
    include/arch/uefi/uefildr.h
    arch/uefi/uefildr.c
    bootmgr.c
    ntldr/setupldr.c
    ntldr/inffile.c
    ${FREELDR_BASE_SOURCE})

if(ARCH STREQUAL "i386")
    # Must be included together with disk/scsiport.c
    list(APPEND UEFILDR_BASE_SOURCE
        ${CMAKE_CURRENT_BINARY_DIR}/uefildr.def)
endif()

set(_uefi_saved_c_implicit ${CMAKE_C_IMPLICIT_LINK_LIBRARIES})
set(_uefi_saved_cxx_implicit ${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
set(CMAKE_C_IMPLICIT_LINK_LIBRARIES "")
set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES "")

add_executable(uefildr ${UEFILDR_BASE_SOURCE})
set_target_properties(uefildr PROPERTIES SUFFIX ".efi")
set_property(TARGET uefildr PROPERTY LINK_LIBRARIES "")

target_compile_definitions(uefildr PRIVATE UEFIBOOT)

# On AMD64 we only map 1GB with freeloader, tell UEFI to keep us low!
if(ARCH STREQUAL "amd64")
    set_image_base(uefildr 0x10000)
endif()

if(MSVC)
if(NOT ARCH STREQUAL "arm")
    target_link_options(uefildr PRIVATE /DYNAMICBASE:NO)
endif()
    target_link_options(uefildr PRIVATE /NXCOMPAT:NO /ignore:4078 /ignore:4254 /DRIVER)
    # We don't need hotpatching
    remove_target_compile_option(uefildr "/hotpatch")
else()
    target_link_options(uefildr PRIVATE -Wl,--file-alignment,0x200,--section-alignment,0x200)
    target_link_options(uefildr PRIVATE -Wl,--as-needed)
    # DWARF sections are preserved directly in the binary for rossym_new to parse
endif()

set_subsystem(uefildr EFI_APPLICATION)

set_entrypoint(uefildr EfiEntry)

if(ARCH STREQUAL "i386")
    target_link_libraries(uefildr mini_hal)
endif()

set(_uefildr_link_libs
    uefifreeldr_common
    cportlib
    blcmlib
    blrtl
    libcntpr)
if(ARCH STREQUAL "arm64" AND NOT MSVC)
    if(TARGET libgcc)
        list(APPEND _uefildr_link_libs libgcc)
    else()
        list(APPEND _uefildr_link_libs gcc)
    endif()
endif()
if(ARCH STREQUAL "arm64")
    if(TARGET libatomic)
        list(APPEND _uefildr_link_libs libatomic)
    endif()
    if(TARGET pthread_stubs)
        list(APPEND _uefildr_link_libs pthread_stubs)
    endif()
endif()
set_property(TARGET uefildr PROPERTY LINK_LIBRARIES "${_uefildr_link_libs}")
unset(_uefildr_link_libs)

set(CMAKE_C_IMPLICIT_LINK_LIBRARIES "${_uefi_saved_c_implicit}")
set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES "${_uefi_saved_cxx_implicit}")
unset(_uefi_saved_c_implicit)
unset(_uefi_saved_cxx_implicit)

# dynamic analysis switches
if(STACK_PROTECTOR)
    target_sources(uefildr PRIVATE $<TARGET_OBJECTS:gcc_ssp_nt>)
endif()

if(RUNTIME_CHECKS)
    target_link_libraries(uefildr runtmchk)
endif()

add_dependencies(uefildr xdk)
