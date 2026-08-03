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
    libs/vkd3d-common/blob.c
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
    libs/vkd3d-shader/spirv.c
    libs/vkd3d-shader/tpf.c
    libs/vkd3d-shader/vkd3d_shader_main.c
    libs/vkd3d-utils/vkd3d_utils_main.c
    libs/vkd3d/cache.c
    libs/vkd3d/command.c
    libs/vkd3d/device.c
    libs/vkd3d/resource.c
    libs/vkd3d/state.c
    libs/vkd3d/utils.c
    libs/vkd3d/vkd3d_main.c
    ${BISON_vkd3d_hlsl_parser_OUTPUTS}
    ${FLEX_vkd3d_hlsl_scanner_OUTPUTS}
    ${BISON_vkd3d_preproc_parser_OUTPUTS}
    ${FLEX_vkd3d_preproc_scanner_OUTPUTS})

add_library(vkd3d STATIC ${SOURCE})
add_dependencies(vkd3d psdk)
target_compile_definitions(vkd3d PRIVATE
    LIBVKD3D_SOURCE
    LIBVKD3D_UTILS_SOURCE)

target_include_directories(vkd3d
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include/private
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/vkd3d-common
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/vkd3d-shader
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/vkd3d-utils
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/vkd3d
        ${CMAKE_CURRENT_BINARY_DIR})

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(vkd3d PRIVATE -Wno-format-overflow)
endif()

add_library(vkd3d_shader INTERFACE)
target_link_libraries(vkd3d_shader INTERFACE vkd3d)
