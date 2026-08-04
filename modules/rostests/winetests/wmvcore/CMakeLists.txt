
add_definitions(
    -DUSE_WINE_TODOS
    -D__WINESRC__)

add_executable(wmvcore_winetest
    wmvcore.c
    testlist.c
    rsrc.rc)

set_source_files_properties(rsrc.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test.wmv;${CMAKE_CURRENT_SOURCE_DIR}/test_rawvideo.wmv")

set_module_type(wmvcore_winetest win32cui)
add_importlibs(wmvcore_winetest ole32 shcore user32 wmvcore msvcrt kernel32)
add_rostests_file(TARGET wmvcore_winetest)
