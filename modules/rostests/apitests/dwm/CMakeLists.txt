
list(APPEND SOURCE
    DwmSurface.c
    DwmDirtyRegion.c
    DwmBlit.c
    DwmStructures.c
    DwmWindowList.c
    DwmCompositor.c
    DwmCompositionModel.c
    DwmExtEscape.c
    DwmPublicApi.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(dwm_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

target_link_libraries(dwm_apitest wine ${PSEH_LIB})
set_module_type(dwm_apitest win32cui)
add_importlibs(dwm_apitest gdi32 user32 msvcrt kernel32 ntdll)
add_pch(dwm_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET dwm_apitest)
add_cd_file(TARGET dwm_apitest DESTINATION reactos/system32 FOR all)
