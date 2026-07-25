include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)

set(NTFS3G_CORE_SOURCES
    ${NTFS3G_PORTABLE_SOURCES}
    ${NTFS3G_SOURCE_DIR}/compat.c
    ${NTFS3G_SOURCE_DIR}/misc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/reactos/common/device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/reactos/common/file.c
    ${CMAKE_CURRENT_SOURCE_DIR}/reactos/common/volume.c)

if(CMAKE_CROSSCOMPILING)
    list(APPEND NTFS3G_CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/reactos/common/logging.c
        ${CMAKE_CURRENT_SOURCE_DIR}/reactos/common/runtime.c)
else()
    list(APPEND NTFS3G_CORE_SOURCES
        ${NTFS3G_SOURCE_DIR}/logging.c
        ${NTFS3G_SOURCE_DIR}/unix_io.c)
endif()

add_library(ntfs3g_core STATIC ${NTFS3G_CORE_SOURCES})

if(CMAKE_CROSSCOMPILING)
    add_dependencies(ntfs3g_core psdk)
    target_compile_definitions(ntfs3g_core PRIVATE
        HAVE_CONFIG_H
        NO_NTFS_DEVICE_DEFAULT_IO_OPS
        _CRT_DECLARE_NONSTDC_NAMES=1
        _NO_INLINING
        _errno=ntfs3g_errno_location
        calloc=ntfs3g_calloc
        free=ntfs3g_free
        malloc=ntfs3g_malloc
        mbstowcs=ntfs3g_mbstowcs
        mbtowc=ntfs3g_mbtowc
        realloc=ntfs3g_realloc
        setlocale=ntfs3g_setlocale
        strdup=ntfs3g_strdup
        strerror=ntfs3g_strerror
        strnlen=ntfs3g_strnlen
        time=ntfs3g_time
        wctomb=ntfs3g_wctomb
        _time64=ntfs3g_time)
    set(NTFS3G_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/config/core)
else()
    target_compile_definitions(ntfs3g_core PRIVATE HAVE_CONFIG_H)
    set(NTFS3G_CONFIG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/config/host)
endif()

target_include_directories(ntfs3g_core BEFORE
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${NTFS3G_CONFIG_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/reactos/common
        ${NTFS3G_INCLUDE_DIR})

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ntfs3g_core PRIVATE
        -Wno-format
        -Wno-deprecated-declarations
        -Wno-pointer-sign
        -Wno-sign-compare
        -Wno-unused-function
        -Wno-unused-parameter
        -Wno-unused-variable)
    if(CMAKE_CROSSCOMPILING)
        target_compile_options(ntfs3g_core PRIVATE -mno-sse)
    endif()
endif()

add_library(ntfs3g_um STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/reactos/um/host.c)
target_link_libraries(ntfs3g_um PUBLIC ntfs3g_core)
if(NOT CMAKE_CROSSCOMPILING)
    find_package(Threads REQUIRED)
    target_link_libraries(ntfs3g_um PUBLIC Threads::Threads)
endif()
target_include_directories(ntfs3g_um PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/reactos/common)
