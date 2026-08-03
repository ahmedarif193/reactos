
remove_definitions(-DWINVER=${REACTOS_TARGET_NT} -D_WIN32_WINNT=${REACTOS_TARGET_NT})
include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)

list(APPEND SOURCE
    cred.c
    crypt.c
    crypt_lmhash.c
    crypt_md4.c
    crypt_md5.c
    crypt_sha.c
    eventlog.c
    lsa.c
    perf.c
    registry.c
    security.c
    service.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(advapi32_winetest
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    rsrc.rc)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(advapi32_winetest PRIVATE
    -UNTDDI_VERSION -DNTDDI_VERSION=0x0A000000
    -U_WIN32_WINNT -D_WIN32_WINNT=0x0A00
    -UWINVER -DWINVER=0x0A00
    -Wno-format
    -Wno-unused-function
    -Wno-old-style-declaration)
endif()

set_module_type(advapi32_winetest win32cui)
add_importlibs(advapi32_winetest advapi32 ole32 user32 msvcrt kernel32 ntdll)
add_pch(advapi32_winetest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET advapi32_winetest)
target_compile_definitions(advapi32_winetest PRIVATE
    WINETEST_USE_DBG_SPRINTF
    strcmpi=_strcmpi
    wcsnicmp=_wcsnicmp
    wcsicmp=_wcsicmp)
