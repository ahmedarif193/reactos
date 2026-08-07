
add_definitions(-DUSE_WINE_TODOS)

list(APPEND SOURCE
    reader.c
    writer.c
    testlist.c)

add_executable(xmllite_winetest ${SOURCE})
set_source_files_properties(writer.c PROPERTIES COMPILE_DEFINITIONS CONST_VTABLE)
set_module_type(xmllite_winetest win32cui)
add_importlibs(xmllite_winetest xmllite ole32 msvcrt kernel32)
add_rostests_file(TARGET xmllite_winetest)
