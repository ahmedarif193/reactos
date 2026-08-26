# The Raspberry Pi 5 VC4 OpenGL ICD. Sources live beside the XPDM miniport
# but the DLL itself is display-model agnostic user mode: it talks to
# whichever kernel driver answers the RPI5VC4_ESCAPE_* DC escapes (the XPDM
# rpi5vc4disp display driver, or the framebuf bridge over the WDDM stack),
# so it must build and ship under both display models. That is why the build
# rules live here rather than in the XPDM-gated miniport directory.

set(VC4OGL_DIR ${REACTOS_SOURCE_DIR}/win32ss/drivers/miniport/rpi5vc4)

include_directories(BEFORE
    ${REACTOS_SOURCE_DIR}/dll/opengl/mesa
    ${REACTOS_SOURCE_DIR}/dll/opengl/opengl32)

spec2def(rpi5vc4ogl.dll ../../../win32ss/drivers/miniport/rpi5vc4/rpi5vc4ogl.spec)
add_library(rpi5vc4ogl MODULE
    ${VC4OGL_DIR}/rpi5vc4ogl.c
    ${VC4OGL_DIR}/rpi5vc4ogl_buffer.c
    ${VC4OGL_DIR}/rpi5vc4ogl_buffer.h
    ${VC4OGL_DIR}/rpi5vc4ogl_fbo.c
    ${VC4OGL_DIR}/rpi5vc4ogl_fbo.h
    ${VC4OGL_DIR}/rpi5vc4ogl_gl2.c
    ${VC4OGL_DIR}/rpi5vc4ogl_gl2.h
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
