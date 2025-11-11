
spec2def(ucrtbase.dll ucrtbase.spec NO_DBG ADD_IMPORTLIB)

# Hack to replace the old CRT include directory with the UCRT include directory
get_property(INCLUDE_DIRS DIRECTORY . PROPERTY INCLUDE_DIRECTORIES)
list(REMOVE_ITEM INCLUDE_DIRS "${REACTOS_SOURCE_DIR}/sdk/include/crt")
set_property(DIRECTORY . PROPERTY INCLUDE_DIRECTORIES ${INCLUDE_DIRS})
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/ucrt)

remove_definitions(-D_CRT_NON_CONFORMING_SWPRINTFS)

add_compile_definitions(
    _UCRT
    _CORECRT_BUILD
    CRTDLL
)

set(UCRTBASE_SOURCES
    printf.c
    stubs.c
    ucrtbase_stubs.c
    ucrtbase.def
)

if(USE_NATIVE_MINGW_CRT AND NOT MINGW)
    message(FATAL_ERROR "USE_NATIVE_MINGW_CRT requires a MinGW-w64 toolchain")
endif()

if(USE_NATIVE_MINGW_CRT)
    include(MinGWCRT)
    mingwcrt_import_default_libs(USE_NATIVE_MINGW_CRT_LIBS NO_MSVCRT)
endif()

add_library(ucrtbase SHARED ${UCRTBASE_SOURCES})

if(NOT MSVC)
    target_compile_options(ucrtbase PRIVATE -Wno-builtin-declaration-mismatch)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Silence warnings in ucrtbase_stubs.c
    target_compile_options(ucrtbase PRIVATE -Wno-incompatible-library-redeclaration)
endif()

if(USE_NATIVE_MINGW_CRT)
    target_compile_definitions(ucrtbase PRIVATE USE_NATIVE_MINGW_CRT=1)
endif()

set_entrypoint(ucrtbase __acrt_DllMain 12)

if(USE_NATIVE_MINGW_CRT)
    target_link_libraries(ucrtbase ${USE_NATIVE_MINGW_CRT_LIBS})
else()
    target_link_libraries(ucrtbase
        ucrt
        ucrtsupport
        crtmath
        vcruntime
        libcntpr
        wine
    )
endif()

# Implicitly link to vcstartup
target_link_libraries(libucrtbase vcstartup)

if(MSVC)
    target_link_libraries(ucrtbase runtmchk)
elseif(USE_NATIVE_MINGW_CRT)
    # MinGW CRT already provides the guard helpers via mingw_supcxx
else()
    # For __cxa_guard_acquire / __cxa_guard_release
    target_link_libraries(ucrtbase libsupc++)
endif()

add_importlibs(ucrtbase kernel32 ntdll)

add_cd_file(TARGET ucrtbase DESTINATION reactos/system32 FOR all)
