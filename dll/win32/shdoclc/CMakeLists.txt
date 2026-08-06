
spec2def(shdoclc.dll shdoclc.spec)

set_source_files_properties(rsrc.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ERROR.HTM")

add_library(shdoclc MODULE
    rsrc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/shdoclc.def)

set_module_type(shdoclc win32dll ENTRYPOINT 0 )
target_link_libraries(shdoclc wine)
add_importlibs(shdoclc kernel32 ntdll)
add_dependencies(shdoclc psdk)
add_cd_file(TARGET shdoclc DESTINATION reactos/system32 FOR all)
