
spec2def(dcomp.dll dcomp.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    device.c)

add_library(dcomp MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dcomp.def
    ${CMAKE_CURRENT_BINARY_DIR}/dcomp_stubs.c)

set_module_type(dcomp win32dll)
target_link_libraries(dcomp uuid wine)
add_importlibs(dcomp gdi32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dcomp DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(dcomp)
