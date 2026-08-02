
add_definitions(
    -D_SHDOCVW_)

spec2def(shdocvw.dll shdocvw.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    shdocvw_main.c
    shlinstobj.c
    precomp.h
    ${CMAKE_CURRENT_BINARY_DIR}/shdocvw_stubs.c)

list(APPEND shdocvw_rc_deps
    ${CMAKE_CURRENT_SOURCE_DIR}/res/shdocvw.rgs
    ${CMAKE_CURRENT_SOURCE_DIR}/res/favband.rgs
    ${CMAKE_CURRENT_SOURCE_DIR}/res/explorerband.rgs)
set_source_files_properties(shdocvw.rc PROPERTIES OBJECT_DEPENDS "${shdocvw_rc_deps}")

add_library(shdocvw MODULE
    ${SOURCE}
    shdocvw.rc
    $<TARGET_OBJECTS:shdocvw_sublib>
    ${CMAKE_CURRENT_BINARY_DIR}/shdocvw.def)

target_include_directories(shdocvw BEFORE PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/include/wine
)

set_module_type(shdocvw win32dll UNICODE)
add_idl_reg_scripts(shdocvw regtypelib res/shdocvw_v1.idl)
target_link_libraries(shdocvw cpprt uuid wine)
add_delay_importlibs(shdocvw version shell32 user32 gdi32 ole32 oleaut32 ieframe)
add_importlibs(shdocvw shlwapi advapi32 msvcrt comctl32 kernel32 ntdll)
add_dependencies(shdocvw stdole2)
add_pch(shdocvw precomp.h SOURCE)
add_cd_file(TARGET shdocvw DESTINATION reactos/system32 FOR all)

add_library(shdocvw_sublib OBJECT
    CExplorerBand.cpp
    CFavBand.cpp
    CNSCBand.cpp
    mrulist.cpp
    objects.cpp
    utility.cpp
    winlist.cpp)
target_link_libraries(shdocvw_sublib PRIVATE atl_classes)
target_compile_definitions(shdocvw_sublib PRIVATE $<TARGET_PROPERTY:shdocvw,COMPILE_DEFINITIONS>)
target_compile_options(shdocvw_sublib PRIVATE $<TARGET_PROPERTY:shdocvw,COMPILE_OPTIONS>)
add_dependencies(shdocvw_sublib psdk)
