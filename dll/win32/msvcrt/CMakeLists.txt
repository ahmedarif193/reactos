
include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/crt/include)
spec2def(msvcrt.dll msvcrt.spec ADD_IMPORTLIB)

target_compile_definitions(libmsvcrt INTERFACE _DLL __USE_CRTIMP)

if(USE_NATIVE_MINGW_CRT AND NOT MINGW)
    message(FATAL_ERROR "USE_NATIVE_MINGW_CRT requires a MinGW-w64 toolchain")
endif()

if(USE_NATIVE_MINGW_CRT)
    include(MinGWCRT)
    mingwcrt_import_default_libs(USE_NATIVE_MINGW_CRT_LIBS NO_MSVCRT)
endif()

# On native i386 builds, embed startup objects into libmsvcrt so importers keep
# functioning regardless of the CRT backend being used. When building i386 via
# amd64 multilib, avoid mixing real objects into the import library to prevent
# symbol decoration mismatches and unresolved references.
if(NOT REACTOS_MULTILIB_I386)
    target_sources(libmsvcrt PRIVATE $<TARGET_OBJECTS:msvcrtex>)
    add_custom_command(TARGET libmsvcrt POST_BUILD
        COMMAND ${CMAKE_AR} q $<TARGET_FILE:libmsvcrt> $<TARGET_OBJECTS:msvcrtex>
        COMMAND ${CMAKE_AR} s $<TARGET_FILE:libmsvcrt>
        COMMAND ${CMAKE_RANLIB} $<TARGET_FILE:libmsvcrt>
        COMMAND_EXPAND_LISTS
        COMMENT "Appending CRT startup objects to libmsvcrt")
endif()

# Embed RTC libs when available for stack protector support.
if (STACK_PROTECTOR)
    if(NOT REACTOS_MULTILIB_I386)
        target_sources(libmsvcrt PRIVATE $<TARGET_OBJECTS:gcc_ssp_msvcrt>)
        target_link_libraries(libmsvcrt INTERFACE libkernel32) # For OutputDebugStringA
    endif()
endif()

add_definitions(
    -DUSE_MSVCRT_PREFIX
    -D_MSVCRT_
    -D_MSVCRT_LIB_
    -D_MT
    -D_CTYPE_DISABLE_MACROS
    -D_NO_INLINING
    -DCRTDLL
    -D__MINGW_IMPORT="")

set(MSVCRT_SOURCES
    dllmain.c
    stubs.c
    wctob.c
    msvcrt.rc
    ${CMAKE_CURRENT_BINARY_DIR}/msvcrt_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/msvcrt.def)

if(USE_NATIVE_MINGW_CRT)
    list(APPEND MSVCRT_SOURCES native_shims.c)
endif()

add_library(msvcrt MODULE ${MSVCRT_SOURCES})
set_module_type(msvcrt win32dll ENTRYPOINT DllMain 12)
add_dependencies(msvcrt psdk)
if(USE_NATIVE_MINGW_CRT)
    add_dependencies(msvcrt ucrtbase)
endif()

if(USE_NATIVE_MINGW_CRT)
    target_link_libraries(msvcrt libmsvcrt)
    target_compile_definitions(msvcrt PRIVATE USE_NATIVE_MINGW_CRT=1)
    target_link_libraries(msvcrt ${USE_NATIVE_MINGW_CRT_LIBS})
else()
    if(REACTOS_MULTILIB_I386)
        target_link_libraries(msvcrt crt libcntpr vcruntime cpprt wine ${PSEH_LIB})
    else()
        target_link_libraries(msvcrt crt vcruntime cpprt wine ${PSEH_LIB})
    endif()
    target_link_options(msvcrt PRIVATE "-Wl,--exclude-libs,libmsvcrt.a" "-Wl,--allow-multiple-definition")
endif()

if(MSVC)
    # export of deleting destructor "name"
    target_link_options(msvcrt PRIVATE "/ignore:4102")
    set_property(TARGET libmsvcrt APPEND PROPERTY STATIC_LIBRARY_OPTIONS "/ignore:4102")
    if(ARCH STREQUAL "i386")
        target_sources(libmsvcrt PRIVATE $<TARGET_OBJECTS:ftol2_sse>)
    endif()
endif()

add_importlibs(msvcrt kernel32 ntdll)
add_cd_file(TARGET msvcrt DESTINATION reactos/system32 FOR all)
