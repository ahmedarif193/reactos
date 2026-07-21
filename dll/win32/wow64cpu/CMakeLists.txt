spec2def(wow64cpu.dll wow64cpu.spec ADD_IMPORTLIB)

add_library(wow64cpu MODULE
    cpu.c
    ${CMAKE_CURRENT_BINARY_DIR}/wow64cpu.def)

set_module_type(wow64cpu win32dll ENTRYPOINT DllMain 12 IMAGEBASE 0x7a400000)
add_importlibs(wow64cpu wow64 ntdll)
add_dependencies(wow64cpu wow64)
add_cd_file(TARGET wow64cpu DESTINATION reactos/system32 FOR all)
set_wine_module(wow64cpu)
