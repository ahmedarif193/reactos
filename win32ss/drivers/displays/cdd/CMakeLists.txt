
list(APPEND SOURCE
    cdd_enable.c
    cdd_screen.c
    cdd_surface.c
    cdd_drawing.c
    cdd_wddm.c
    cdd.h)

add_library(cdd MODULE
    ${SOURCE}
    cdd.rc)

set_module_type(cdd kerneldll ENTRYPOINT DrvEnableDriver 12)
target_include_directories(cdd PRIVATE
    ${REACTOS_SOURCE_DIR}/win32ss/include)
add_pch(cdd cdd.h SOURCE)
target_link_libraries(cdd libcntpr)
add_importlibs(cdd win32k ntoskrnl)
add_dependencies(cdd psdk)
add_cd_file(TARGET cdd DESTINATION reactos/system32 FOR all)
