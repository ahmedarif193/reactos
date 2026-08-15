list(APPEND SOURCE
    rpi5vc4.c
    rpi5vc4_hvs.c
    rpi5vc4_hvs.h
    rpi5vc4_crtc.c
    rpi5vc4_crtc.h
    rpi5vc4_v3d.c
    rpi5vc4_v3d_exec.c
    rpi5vc4_v3d.h
    rpi5vc4.h)

add_library(rpi5vc4 MODULE ${SOURCE} rpi5vc4.rc)
set_module_type(rpi5vc4 kernelmodedriver)
add_importlibs(rpi5vc4 ntoskrnl videoprt)
target_link_libraries(rpi5vc4 libcntpr)
add_cd_file(TARGET rpi5vc4 DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rpi5vc4 rpi5vc4.inf)
add_registry_inf(rpi5vc4_reg.inf)

include_directories(BEFORE
    ${REACTOS_SOURCE_DIR}/dll/opengl/mesa
    ${REACTOS_SOURCE_DIR}/dll/opengl/opengl32)

spec2def(rpi5vc4ogl.dll rpi5vc4ogl.spec)
add_library(rpi5vc4ogl MODULE
    rpi5vc4ogl.c
    ${CMAKE_CURRENT_BINARY_DIR}/rpi5vc4ogl.def)
target_compile_definitions(rpi5vc4ogl PRIVATE
    _GDI32_
    _GLAPI_NO_EXPORTS)
target_link_libraries(rpi5vc4ogl
    wine
    ${PSEH_LIB}
    mesa)
set_module_type(rpi5vc4ogl win32dll ENTRYPOINT DllMain 12)
add_importlibs(rpi5vc4ogl opengl32 gdi32 user32 msvcrt kernel32 ntdll)
add_dependencies(rpi5vc4ogl psdk)
add_cd_file(TARGET rpi5vc4ogl DESTINATION reactos/system32 FOR all)
