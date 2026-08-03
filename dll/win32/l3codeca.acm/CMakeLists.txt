add_definitions(-DWIN32)

include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/libmpg123)

spec2def(l3codeca.acm l3codeca.acm.spec)

add_library(l3codeca.acm MODULE
    mpegl3.c
    ${CMAKE_CURRENT_BINARY_DIR}/l3codeca.def)

set_module_type(l3codeca.acm win32dll)
set_target_properties(l3codeca.acm PROPERTIES SUFFIX "")
target_compile_definitions(l3codeca.acm PRIVATE MPG123_NO_LARGENAME)
target_link_libraries(l3codeca.acm wine libmpg123 oldnames)
add_importlibs(l3codeca.acm winmm user32 kernelbase msvcrt kernel32 ntdll)
add_cd_file(TARGET l3codeca.acm DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(l3codeca.acm) # CORE-5743: No ARRAY_SIZE macro
