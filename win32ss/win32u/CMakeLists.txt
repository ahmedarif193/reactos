
include_directories(${REACTOS_SOURCE_DIR}/win32ss)

spec2def(win32u.dll win32u.spec ADD_IMPORTLIB)
add_asm_files(win32u_asm sys-stubs.S)

add_library(win32u MODULE
    main.c
    ${win32u_asm}
    ${CMAKE_CURRENT_BINARY_DIR}/win32u.def)

set_module_type(win32u module)
add_dependencies(win32u psdk)
add_cd_file(TARGET win32u DESTINATION reactos/system32 FOR all)

if(WOW64_I386_RUNTIME AND ARCH STREQUAL "i386")
    target_compile_definitions(win32u PRIVATE WOW64_I386_RUNTIME)
endif()

if (STACK_PROTECTOR)
    target_compile_options(win32u PRIVATE -fno-stack-protector)
endif()
