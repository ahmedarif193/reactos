spec2def(rtworkq.dll rtworkq.spec ADD_IMPORTLIB)

add_library(rtworkq MODULE queue.c ${CMAKE_CURRENT_BINARY_DIR}/rtworkq.def ${CMAKE_CURRENT_BINARY_DIR}/rtworkq_stubs.c)
set_module_type(rtworkq win32dll)
target_compile_definitions(rtworkq PRIVATE __WINESRC__)
target_link_libraries(rtworkq wine)
add_importlibs(rtworkq ole32 msvcrt kernel32 ntdll)
add_cd_file(TARGET rtworkq DESTINATION reactos/system32 FOR all)
set_wine_module(rtworkq)
