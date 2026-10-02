
include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers/fltmgr
    includes)

add_definitions(-DEX_LEGACY_PUSH_LOCKS)

spec2def(fltmgr.sys fltmgr.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    context.c
    fastio.c
    fileio.c
    filter.c
    init.c
    instance.c
    io.c
    iomisc.c
    misc.c
    name.c
    object.c
    port.c
    transact.c
    volume.c
    work.c
    fltmgr.h)

add_library(fltmgr MODULE
    ${SOURCE}
    fltmgr.rc
    ${CMAKE_CURRENT_BINARY_DIR}/fltmgr_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/fltmgr.def)

set_module_type(fltmgr kernelmodedriver)
target_link_libraries(fltmgr ${PSEH_LIB})
add_importlibs(fltmgr ntoskrnl hal)
add_pch(fltmgr fltmgr.h SOURCE)
add_cd_file(TARGET fltmgr DESTINATION reactos/system32/drivers NO_CAB FOR all)
