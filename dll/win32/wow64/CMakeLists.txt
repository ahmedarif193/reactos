spec2def(wow64.dll wow64.spec ADD_IMPORTLIB)

add_library(wow64 MODULE
    file.c
    process.c
    registry.c
    rossyscall.c
    security.c
    sync.c
    syscall.c
    system.c
    virtual.c
    ${CMAKE_CURRENT_BINARY_DIR}/wow64_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/wow64.def)

set_module_type(wow64 win32dll ENTRYPOINT DllMain 12)
target_compile_definitions(wow64 PRIVATE __WINESRC__)
target_include_directories(wow64 PRIVATE ${REACTOS_SOURCE_DIR}/ntoskrnl/include)
add_importlibs(wow64 ntdll)
add_cd_file(TARGET wow64 DESTINATION reactos/system32 FOR all)
set_wine_module(wow64)
