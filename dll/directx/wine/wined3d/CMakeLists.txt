add_definitions(
    -D_USE_MATH_DEFINES
    -DUSE_WIN32_OPENGL
    -DI64_MAX=_I64_MAX
    -Dcopysignf=_copysignf)

# We name this d3dwine.dll, because the VirtualBox additions ship with a custom wined3d.dll.
spec2def(d3dwine.dll wined3d.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    adapter_gl.c
    adapter_vk.c
    buffer.c
    context.c
    context_gl.c
    context_vk.c
    cs.c
    decoder.c
    device.c
    directx.c
    ffp_gl.c
    ffp_hlsl.c
    gl_compat.c
    glsl_shader.c
    palette.c
    query.c
    resource.c
    sampler.c
    shader.c
    shader_sm1.c
    shader_sm4.c
    shader_spirv.c
    stateblock.c
    surface.c
    swapchain.c
    texture.c
    texture_gl.c
    texture_vk.c
    utils.c
    vertexdeclaration.c
    view.c
    wined3d_main.c
    precomp.h)

add_library(d3dwine MODULE
    ${SOURCE}
    resource.rc
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d3dwine.def)

if(REACTOS_USE_WDDM)
    target_compile_definitions(d3dwine PRIVATE REACTOS_WINE_WDDM)
endif()

set_module_type(d3dwine win32dll)
target_link_libraries(d3dwine wine vkd3d dxguid dx10guid uuid)
add_importlibs(d3dwine user32 opengl32 gdi32 gdi32_vista advapi32 msvcrt kernel32_vista kernel32 ntdll)
add_dependencies(d3dwine d3d_idl_headers)
add_pch(d3dwine precomp.h SOURCE)
add_cd_file(TARGET d3dwine DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(d3dwine) # CORE-5743: No ARRAY_SIZE

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(d3dwine PRIVATE -Wno-format-overflow)
endif()
