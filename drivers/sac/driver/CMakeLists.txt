
include_directories(${REACTOS_SOURCE_DIR})

list(APPEND SOURCE
    chanmgr.c
    channel.c
    cmdchan.c
    concmd.c
    conmgr.c
    data.c
    dispatch.c
    init.c
    memory.c
    rawchan.c
    util.c
    vtutf8chan.c
    sacdrv.h)

add_library(sacdrv MODULE ${SOURCE} sacdrv.rc)
set_module_type(sacdrv kernelmodedriver)
add_importlibs(sacdrv ntoskrnl hal)
# Link kernel C runtime primitives (e.g., memcmp/memcpy) for Clang builds
target_link_libraries(sacdrv libcntpr)
add_dependencies(sacdrv sacmsg)
add_pch(sacdrv sacdrv.h SOURCE)
add_cd_file(TARGET sacdrv DESTINATION reactos/system32/drivers NO_CAB FOR all)
