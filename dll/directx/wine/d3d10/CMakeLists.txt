add_definitions(
    -D__WINESRC__
    -D__ROS_LONG64__
    -DD3D_COMPILER_VERSION=0)

spec2def(d3d10.dll d3d10.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    blob.c
    d3d10_main.c
    effect.c
    reflection.c
    shader.c
    stateblock.c
    utils.c)

add_library(d3d10 MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d3d10.def
    ${CMAKE_CURRENT_BINARY_DIR}/d3d10_stubs.c)

set_module_type(d3d10 win32dll)
target_link_libraries(d3d10 vkd3d_shader dx10guid uuid wine)
add_importlibs(d3d10 d3d10core d3dcompiler_43 dxgi d3dwine msvcrt kernel32 ntdll)
add_dependencies(d3d10 d3d_idl_headers)
add_cd_file(TARGET d3d10 DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(d3d10)
