list(APPEND SOURCE
    attribmod.c
    attrlist.c
    bitmap.c
    blockdev.c
    cache_runtime.c
    clusteralloc.c
    collate.c
    compress.c
    core.c
    diskwrite.c
    dispatch.c
    ea.c
    fcb.c
    filecreate.c
    filewrite.c
    fileio.c
    fsctl.c
    indexalloc.c
    indexmod.c
    logfile.c
    metadata.c
    mftwrite.c
    mst.c
    notify.c
    ntfslx.c
    ntfslx.h
    ntfslx_layout.h
    path.c
    reparse.c
    runlist.c
    security.c
    selftest.c
    share.c
    vpbguard.c
    volinfo.c)

option(NTFSLX_ENABLE_DEBUG_TRACES "Enable verbose ntfslx debug output" ON)

add_library(ntfslx MODULE ${SOURCE})
if(NTFSLX_ENABLE_DEBUG_TRACES)
    target_compile_definitions(ntfslx PRIVATE NTFSLX_ENABLE_DEBUG_TRACES=1)
else()
    target_compile_definitions(ntfslx PRIVATE NTFSLX_ENABLE_DEBUG_TRACES=0)
endif()
set_module_type(ntfslx kernelmodedriver)
add_importlibs(ntfslx ntoskrnl hal)
add_pch(ntfslx ntfslx.h SOURCE)
add_cd_file(TARGET ntfslx DESTINATION reactos/system32/drivers FOR all)

#
# TC.2 - sync-IRP-under-APC-disabled-lock static check.
#
# Run scripts/ntfslx-lint.py at every ntfslx build. If a code change reverts
# any ntfslx .c file to call IoBuildSynchronousFsdRequest /
# IoBuildDeviceIoControlRequest / Zw*File from a function (transitively)
# under FAST_MUTEX or KGUARDED_MUTEX without a custom completion routine,
# the build fails before the regression escapes.
#
# Implemented as a custom target the ntfslx target depends on, so the lint
# step always runs before the link.
#
find_program(NTFSLX_PYTHON3 NAMES python3 python)
if(NTFSLX_PYTHON3)
    set(_ntfslx_lint_script
        ${REACTOS_SOURCE_DIR}/scripts/ntfslx-lint.py)
    set(_ntfslx_lint_stamp
        ${CMAKE_CURRENT_BINARY_DIR}/ntfslx-lint.stamp)
    add_custom_command(
        OUTPUT ${_ntfslx_lint_stamp}
        COMMAND ${NTFSLX_PYTHON3} ${_ntfslx_lint_script}
        COMMAND ${CMAKE_COMMAND} -E touch ${_ntfslx_lint_stamp}
        DEPENDS ${_ntfslx_lint_script} ${SOURCE}
        WORKING_DIRECTORY ${REACTOS_SOURCE_DIR}
        COMMENT "Running ntfslx-lint (sync-IRP-under-APC-disabled-lock check)"
        VERBATIM
    )
    add_custom_target(ntfslx_lint DEPENDS ${_ntfslx_lint_stamp})
    add_dependencies(ntfslx ntfslx_lint)
else()
    message(WARNING "python3 not found; ntfslx-lint static check disabled")
endif()
