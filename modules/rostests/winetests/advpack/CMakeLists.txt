
spec2def(selfreg.dll selfreg.spec)
add_library(advpack_selfreg MODULE selfreg.c ${CMAKE_CURRENT_BINARY_DIR}/selfreg.def)
set_target_properties(advpack_selfreg PROPERTIES OUTPUT_NAME "selfreg")
set_module_type(advpack_selfreg win32dll)
add_importlibs(advpack_selfreg advapi32 msvcrt kernel32)

list(APPEND SOURCE
    advpack.c
    files.c
    install.c
    testlist.c)

set_property(SOURCE advpack_winetest.rc PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/selfreg.dll)

add_executable(advpack_winetest ${SOURCE} advpack_winetest.rc)

target_compile_definitions(advpack_winetest PRIVATE USE_WINE_TODOS)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(advpack_winetest PRIVATE -Wno-format-overflow)
endif()

set_module_type(advpack_winetest win32cui)
add_importlibs(advpack_winetest advpack cabinet user32 advapi32 msvcrt kernel32)
add_dependencies(advpack_winetest advpack_selfreg)
add_rostests_file(TARGET advpack_winetest)
