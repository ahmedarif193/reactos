
add_definitions(-D_WINE)
include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)

spec2def(dwrite.dll dwrite.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    analyzer.c
    bidi.c
    bracket.c
    direction.c
    font.c
    format.c
    freetype.c
    gdiinterop.c
    layout.c
    linebreak.c
    main.c
    mirror.c
    opentype.c
    scripts.c
    shape.c
    shapers/arabic.c
    shapers/arabic_table.c
    ${CMAKE_CURRENT_BINARY_DIR}/dwrite_stubs.c)

add_library(dwrite MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/dwrite.def)

target_compile_definitions(dwrite PRIVATE __WINESRC__)
set_module_type(dwrite win32dll)
target_link_libraries(dwrite wine freetype)
add_importlibs(dwrite user32 gdi32 advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET dwrite DESTINATION reactos/system32 FOR all)
set_wine_module(dwrite)
