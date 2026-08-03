
add_definitions(-DUSE_WINE_TODOS)

spec2def(testdll.dll testdll.spec)

add_library(imagehlp_testdll MODULE
    testdll.c
    ${CMAKE_CURRENT_BINARY_DIR}/testdll.def)
set_target_properties(imagehlp_testdll PROPERTIES OUTPUT_NAME "testdll")
set_module_type(imagehlp_testdll win32dll)
add_importlibs(imagehlp_testdll oleaut32 comdlg32 shlwapi msvcrt kernel32)

list(APPEND SOURCE
    image.c
    integrity.c
    testlist.c)

add_executable(imagehlp_winetest ${SOURCE} imagehlp_winetest.rc)
set_module_type(imagehlp_winetest win32cui)
add_importlibs(imagehlp_winetest imagehlp psapi msvcrt kernel32)
add_dependencies(imagehlp_winetest imagehlp_testdll)
set_source_files_properties(imagehlp_winetest.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/testdll.dll)
add_rostests_file(TARGET imagehlp_winetest)
