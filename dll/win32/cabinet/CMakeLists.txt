
add_definitions(
    -DHAVE_ZLIB)

include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib)

spec2def(cabinet.dll cabinet.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    cabinet_main.c
    compressapi.c
    fci.c
    fdi.c)

add_library(cabinet MODULE
    ${SOURCE}
    cabinet.rc
    ${CMAKE_CURRENT_BINARY_DIR}/cabinet.def
    ${CMAKE_CURRENT_BINARY_DIR}/cabinet_stubs.c)

set_module_type(cabinet win32dll)
target_link_libraries(cabinet wine wine_dll_version zlib)
target_compile_definitions(cabinet PRIVATE NO_SHLWAPI_REG)
add_importlibs(cabinet msvcrt kernel32 ntdll)
add_pch(cabinet precomp.h SOURCE)
add_cd_file(TARGET cabinet DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(cabinet)
