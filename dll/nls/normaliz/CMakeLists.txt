
spec2def(normaliz.dll normaliz.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    normaliz.rc
    ${CMAKE_CURRENT_BINARY_DIR}/normaliz.def)

add_library(normaliz MODULE ${SOURCE})
set_module_type(normaliz win32dll ENTRYPOINT 0)
add_importlibs(normaliz kernel32)
add_dependencies(normaliz psdk)
add_cd_file(TARGET normaliz DESTINATION reactos/system32 FOR all)
