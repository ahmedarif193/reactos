add_definitions(
    -D__WINESRC__
    -DD3D_COMPILER_VERSION=39)

spec2def(d3dcompiler_39.dll d3dcompiler_39.spec ADD_IMPORTLIB)

set(D3DCOMPILER_SOURCE_DIR ${REACTOS_SOURCE_DIR}/dll/directx/wine/d3dcompiler_43)

list(APPEND SOURCE
    ${D3DCOMPILER_SOURCE_DIR}/asmparser.c
    ${D3DCOMPILER_SOURCE_DIR}/blob.c
    ${D3DCOMPILER_SOURCE_DIR}/bytecodewriter.c
    ${D3DCOMPILER_SOURCE_DIR}/compiler.c
    ${D3DCOMPILER_SOURCE_DIR}/reflection.c
    ${D3DCOMPILER_SOURCE_DIR}/utils.c)

FLEX_TARGET(d3dcompiler_39_asmshader_scanner
    ${D3DCOMPILER_SOURCE_DIR}/asmshader.l
    ${CMAKE_CURRENT_BINARY_DIR}/asmshader.yy.c)
BISON_TARGET(d3dcompiler_39_asmshader_parser
    ${D3DCOMPILER_SOURCE_DIR}/asmshader.y
    ${CMAKE_CURRENT_BINARY_DIR}/asmshader.tab.c)
ADD_FLEX_BISON_DEPENDENCY(d3dcompiler_39_asmshader_scanner d3dcompiler_39_asmshader_parser)

list(APPEND GENERATED_SOURCE
    ${FLEX_d3dcompiler_39_asmshader_scanner_OUTPUTS}
    ${BISON_d3dcompiler_39_asmshader_parser_OUTPUTS})

add_library(d3dcompiler_39 MODULE
    ${SOURCE}
    ${GENERATED_SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d3dcompiler_39_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3dcompiler_39.def)

set_module_type(d3dcompiler_39 win32dll)
target_include_directories(d3dcompiler_39 PRIVATE
    ${D3DCOMPILER_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(d3dcompiler_39 vkd3d_compiler dx10guid uuid wine)
add_importlibs(d3dcompiler_39 msvcrt kernel32 ntdll)
add_dependencies(d3dcompiler_39 d3d_idl_headers)
add_cd_file(TARGET d3dcompiler_39 DESTINATION reactos/system32 FOR all)
