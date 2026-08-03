
spec2def(inetcpl.cpl inetcpl.cpl.spec)

list(APPEND SOURCE
    connections.c
    content.c
    general.c
    inetcpl.c
    security.c)

file(GLOB inetcpl_ros_rc_deps resources/*.*)
list(APPEND inetcpl_ros_rc_deps
    ${CMAKE_CURRENT_SOURCE_DIR}/inetcpl.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/inetcpl.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/inetcpl.manifest)
add_rc_deps(inetcpl_ros.rc ${inetcpl_ros_rc_deps})

add_library(inetcpl MODULE
    ${SOURCE}
    inetcpl_ros.rc
    ${CMAKE_CURRENT_BINARY_DIR}/inetcpl_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/inetcpl.def)

set_module_type(inetcpl cpl UNICODE)
target_link_libraries(inetcpl wine)
add_delay_importlibs(inetcpl cryptui wininet ole32 urlmon shell32)
add_importlibs(inetcpl advapi32 comctl32 user32 shlwapi msvcrt kernel32 ntdll)
add_cd_file(TARGET inetcpl DESTINATION reactos/system32 FOR all)
target_compile_definitions(inetcpl PRIVATE __WINESRC__)
set_wine_module(inetcpl)
