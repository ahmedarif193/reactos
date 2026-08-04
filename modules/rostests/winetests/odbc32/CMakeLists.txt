spec2def(driver.dll driver.spec)

add_library(odbc32_driver MODULE
    driver.c
    ${CMAKE_CURRENT_BINARY_DIR}/driver.def)

set_target_properties(odbc32_driver PROPERTIES OUTPUT_NAME "driver")
set_module_type(odbc32_driver win32dll)
add_importlibs(odbc32_driver msvcrt kernel32)

add_executable(odbc32_winetest
    odbc32.c
    testlist.c
    odbc32_winetest.rc)

target_compile_definitions(odbc32_winetest PRIVATE __WINESRC__ USE_WINE_TODOS)
set_module_type(odbc32_winetest win32cui)
add_importlibs(odbc32_winetest advapi32 odbc32 odbccp32 msvcrt kernel32)
add_dependencies(odbc32_winetest odbc32_driver)
set_source_files_properties(odbc32_winetest.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/driver.dll)
add_rostests_file(TARGET odbc32_winetest)
