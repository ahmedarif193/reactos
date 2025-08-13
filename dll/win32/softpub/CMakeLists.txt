
spec2def(softpub.dll softpub.spec)

add_library(softpub MODULE
    softpub.rc
    ${CMAKE_CURRENT_BINARY_DIR}/softpub.def)

set_module_type(softpub win32dll ENTRYPOINT 0 )
target_link_libraries(softpub wine)
add_importlibs(softpub wintrust)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET softpub PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/wintrust/libwintrust.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested wintrust archive using CMake script")
add_cd_file(TARGET softpub DESTINATION reactos/system32 FOR all)
set_wine_module(softpub)
