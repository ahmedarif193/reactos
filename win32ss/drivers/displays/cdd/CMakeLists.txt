
list(APPEND SOURCE
    enable.c
    escape.c
    palette.c
    pointer.c
    present.c
    screen.c
    surface.c
    cdd.h)

add_library(cdd MODULE
    ${SOURCE}
    cdd.rc)

set_module_type(cdd kerneldll ENTRYPOINT RcddEnableDriver 12)
add_pch(cdd cdd.h SOURCE)
target_link_libraries(cdd libcntpr)
add_importlibs(cdd win32k)
add_dependencies(cdd psdk)
add_cd_file(TARGET cdd DESTINATION reactos/system32 FOR all)
