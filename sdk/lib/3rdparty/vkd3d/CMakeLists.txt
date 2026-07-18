add_definitions(
    -D_CRT_DECLARE_NONSTDC_NAMES=1
    -D_USE_MATH_DEFINES
    -DLIBVKD3D_SHADER_SOURCE
    -DWINE_NO_NAMELESS_EXTENSION)

BISON_TARGET(vkd3d_hlsl_parser
    libs/vkd3d-shader/hlsl.y
    ${CMAKE_CURRENT_BINARY_DIR}/hlsl.tab.c)
FLEX_TARGET(vkd3d_hlsl_scanner
    libs/vkd3d-shader/hlsl.l
    ${CMAKE_CURRENT_BINARY_DIR}/hlsl.yy.c)
ADD_FLEX_BISON_DEPENDENCY(vkd3d_hlsl_scanner vkd3d_hlsl_parser)

BISON_TARGET(vkd3d_preproc_parser
    libs/vkd3d-shader/preproc.y
    ${CMAKE_CURRENT_BINARY_DIR}/preproc.tab.c)
FLEX_TARGET(vkd3d_preproc_scanner
    libs/vkd3d-shader/preproc.l
    ${CMAKE_CURRENT_BINARY_DIR}/preproc.yy.c)
ADD_FLEX_BISON_DEPENDENCY(vkd3d_preproc_scanner vkd3d_preproc_parser)

list(APPEND SOURCE
    libs/vkd3d-common/debug.c
    libs/vkd3d-common/error.c
    libs/vkd3d-common/memory.c
    libs/vkd3d-common/utf8.c
    libs/vkd3d-shader/checksum.c
    libs/vkd3d-shader/d3d_asm.c
    libs/vkd3d-shader/d3dbc.c
    libs/vkd3d-shader/dxbc.c
    libs/vkd3d-shader/dxil.c
    libs/vkd3d-shader/fx.c
    libs/vkd3d-shader/glsl.c
    libs/vkd3d-shader/hlsl.c
    libs/vkd3d-shader/hlsl_codegen.c
    libs/vkd3d-shader/hlsl_constant_ops.c
    libs/vkd3d-shader/ir.c
    libs/vkd3d-shader/msl.c
    libs/vkd3d-shader/reactos_runtime.c
    libs/vkd3d-shader/spirv.c
    libs/vkd3d-shader/tpf.c
    libs/vkd3d-shader/vkd3d_shader_main.c
    ${BISON_vkd3d_hlsl_parser_OUTPUTS}
    ${FLEX_vkd3d_hlsl_scanner_OUTPUTS}
    ${BISON_vkd3d_preproc_parser_OUTPUTS}
    ${FLEX_vkd3d_preproc_scanner_OUTPUTS})

add_library(vkd3d_shader STATIC ${SOURCE})
add_dependencies(vkd3d_shader psdk)

target_include_directories(vkd3d_shader
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include/private
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/vkd3d-common
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/vkd3d-shader
        ${CMAKE_CURRENT_BINARY_DIR})

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(vkd3d_shader PRIVATE -Wno-format-overflow)
endif()
