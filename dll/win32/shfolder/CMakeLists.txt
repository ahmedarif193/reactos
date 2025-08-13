
spec2def(shfolder.dll shfolder.spec)

list(APPEND SOURCE
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/shfolder.def)

add_library(shfolder MODULE ${SOURCE})
set_module_type(shfolder win32dll ENTRYPOINT 0 )
target_link_libraries(shfolder wine)
add_importlibs(shfolder shell32 kernel32 ntdll)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET shfolder PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/shell32/libshell32.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested shell32 archive using CMake script")
add_dependencies(shfolder psdk)
add_cd_file(TARGET shfolder DESTINATION reactos/system32 FOR all)
set_wine_module(shfolder)
