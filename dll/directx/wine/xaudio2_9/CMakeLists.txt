
add_definitions(-DXAUDIO2_VER=9)

include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/faudio/include
    ${CMAKE_CURRENT_BINARY_DIR})

spec2def(xaudio2_9.dll xaudio2_9.spec)

list(APPEND SOURCE
    x3daudio.c
    xapo.c
    xapofx.c
    xaudio_allocator.c
    xaudio_dll.c)

add_idl_headers(xaudio2_9_idlheader xaudio_classes.idl)

add_library(xaudio2_9 MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/xaudio2_9_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/xaudio2_9.def)

set_module_type(xaudio2_9 win32dll)
target_compile_definitions(xaudio2_9 PRIVATE __WINESRC__)
target_link_libraries(xaudio2_9 faudio mfuuid uuid wine)
add_importlibs(xaudio2_9 mfplat mfreadwrite propsys advapi32 ole32 user32 msvcrt kernel32 ntdll)
add_dependencies(xaudio2_9 xaudio2_9_idlheader)
add_cd_file(TARGET xaudio2_9 DESTINATION reactos/system32 FOR all)
set_wine_module(xaudio2_9)
