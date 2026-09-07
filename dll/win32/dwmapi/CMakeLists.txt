
add_definitions(-D__WINESRC__)
spec2def(dwmapi.dll dwmapi.spec ADD_IMPORTLIB)

list(APPEND SOURCE
     dwmapi_main.c
     dwmapi_private.c
     dxsurface.c)

add_library(dwmapi MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dwmapi.def
    ${CMAKE_CURRENT_BINARY_DIR}/dwmapi_stubs.c)

set_module_type(dwmapi win32dll UNICODE)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_link_options(dwmapi PRIVATE
        -Wl,-Xlink=/version:10.0
        -Wl,--major-subsystem-version,10
        -Wl,--minor-subsystem-version,0)
endif()
target_link_libraries(dwmapi uuid wine)
add_importlibs(dwmapi advapi32 dcomp gdi32 win32u user32_vista user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dwmapi DESTINATION reactos/system32 FOR all)
