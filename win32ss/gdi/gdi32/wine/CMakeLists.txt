
include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/wine
    ${REACTOS_SOURCE_DIR}/win32ss/include
    ../include
    .)

list(APPEND SOURCE
    emfdc.c
    emfdrv.c
    enhmetafile.c
    metadc.c
    metafile.c
    rosglue.c
    uniscribe/bidi.c
    uniscribe/bracket.c
    uniscribe/breaking.c
    uniscribe/direction.c
    uniscribe/indic.c
    uniscribe/indicsyllable.c
    uniscribe/linebreak.c
    uniscribe/mirror.c
    uniscribe/opentype.c
    uniscribe/shape.c
    uniscribe/shaping.c
    uniscribe/usp10.c)

add_library(winegdi ${SOURCE})
target_compile_definitions(winegdi PRIVATE _GDI32_)
if(MSVC)
    target_compile_options(winegdi PRIVATE /UDXGKDDI_INTERFACE_VERSION /DDXGKDDI_INTERFACE_VERSION=0x11007)
else()
    target_compile_options(winegdi PRIVATE -UDXGKDDI_INTERFACE_VERSION -DDXGKDDI_INTERFACE_VERSION=0x11007)
endif()
target_link_libraries(winegdi PRIVATE pseh)
add_dependencies(winegdi psdk)
