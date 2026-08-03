add_definitions(
    -DUSE_WINE_TODOS
    -D_USE_MATH_DEFINES
    -DD3D_COMPILER_VERSION=47
    -D__WINESRC__)

set(D3DCOMPILER_TEST_SOURCE_DIR ${REACTOS_SOURCE_DIR}/modules/rostests/winetests/d3dcompiler_43)

add_executable(d3dcompiler_47_winetest
    ${D3DCOMPILER_TEST_SOURCE_DIR}/asm.c
    ${D3DCOMPILER_TEST_SOURCE_DIR}/blob.c
    ${D3DCOMPILER_TEST_SOURCE_DIR}/hlsl_d3d11.c
    ${D3DCOMPILER_TEST_SOURCE_DIR}/hlsl_d3d9.c
    ${D3DCOMPILER_TEST_SOURCE_DIR}/reflection.c
    testlist.c)

target_link_libraries(d3dcompiler_47_winetest dx10guid uuid)
set_module_type(d3dcompiler_47_winetest win32cui)
add_dependencies(d3dcompiler_47_winetest d3d_idl_headers)
add_importlibs(d3dcompiler_47_winetest d3d9 d3dcompiler_47 user32 msvcrt kernel32)
add_rostests_file(TARGET d3dcompiler_47_winetest)
