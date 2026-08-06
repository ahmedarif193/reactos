
if(NOT CMAKE_CROSSCOMPILING)
    add_definitions(-DDBGHELP_STATIC_LIB -DNONAMELESSUNION -D_CRT_NON_CONFORMING_SWPRINTFS)

    include_directories(
        ${REACTOS_SOURCE_DIR}/tools)

    list(APPEND SOURCE
        compat.c
        dbghelp.c
        dwarf.c
        module.c
        pe_module.c
        source.c
        stabs.c
        storage.c
        symbol.c
        type.c)

    if(ARCH STREQUAL "i386")
        list(APPEND SOURCE cpu_i386.c)
    elseif(ARCH STREQUAL "amd64")
        list(APPEND SOURCE cpu_amd64_host.c)
    elseif(ARCH STREQUAL "arm64")
        list(APPEND SOURCE cpu_arm64_host.c)
    endif()

    # This helper is consumed only by rsym.  Architectures without an rsym
    # target must not build an incomplete host CPU table as part of ALL.
    add_library(dbghelphost EXCLUDE_FROM_ALL ${SOURCE})
    target_link_libraries(dbghelphost PRIVATE host_includes zlibhost)
    if(NOT MSVC)
        target_compile_options(dbghelphost PRIVATE -fshort-wchar)
    endif()
    if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "arm64")
        target_compile_definitions(dbghelphost PRIVATE _TARGET_PE64 _WIN64)
    endif()
else()
    add_definitions(
        -D_WINE
        -D__WINESRC__
        -DHAVE_ALLOCA_H
        -D_IMAGEHLP_SOURCE_)

    include_directories(
        BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine
        ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib)

    spec2def(dbghelp.dll dbghelp.spec ADD_IMPORTLIB)

    list(APPEND SOURCE
        coff.c
        cpu_arm.c
        cpu_arm64.c
        cpu_i386.c
        cpu_x86_64.c
        dbghelp.c
        dwarf.c
#       elf_module.c
        image.c
#       macho_module.c
        minidump.c
        module.c
        msc.c
        path.c
        pdb.c
        pe_module.c
        rsym.c
        source.c
        stabs.c
        stack.c
        storage.c
        symbol.c
        type.c)

    add_library(dbghelp MODULE
        ${SOURCE}
        version.rc
        ${CMAKE_CURRENT_BINARY_DIR}/dbghelp_stubs.c
        ${CMAKE_CURRENT_BINARY_DIR}/dbghelp.def)

    set_module_type(dbghelp win32dll)
    target_link_libraries(dbghelp wine ${PSEH_LIB} oldnames zlib)
    add_delay_importlibs(dbghelp version)
    add_importlibs(dbghelp psapi msvcrt kernel32_vista kernel32 ntdll)
    add_cd_file(TARGET dbghelp DESTINATION reactos/system32 FOR all)
    set_wine_module(dbghelp)

    if(MSVC)
        # Disable warning C4146: unary minus operator applied to unsigned type, result still unsigned
        # Disable warning C4477 (printf format warnings)
        target_compile_options(dbghelp PRIVATE /wd4146 /wd4477)
    endif()
endif()
