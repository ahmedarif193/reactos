list(APPEND SOURCE
    blockdev.c
    core.c
    dispatch.c
    fsctl.c
    ntfslx.c
    ntfslx.h)

add_library(ntfslx MODULE ${SOURCE})
set_module_type(ntfslx kernelmodedriver)
add_importlibs(ntfslx ntoskrnl hal)
add_pch(ntfslx ntfslx.h SOURCE)
add_cd_file(TARGET ntfslx DESTINATION reactos/system32/drivers FOR all)
