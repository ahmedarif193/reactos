remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT} -DWINVER=${REACTOS_TARGET_NT})

function(add_wdfdriverentry_version _target _minor)
    add_library(${_target} STATIC
        classbind.c
        inittypes.c
        wdfdriverentry.c)
    add_dependencies(${_target} xdk)

    if(GCC)
        target_compile_options(${_target} PRIVATE -Wno-unknown-pragmas -Wno-switch)
    endif()

    target_compile_definitions(${_target}
        PUBLIC WDF_DEVICE_NO_WDMSEC_H
        PRIVATE
            WDF_STUB
            WDF_STUB_VERSION=${_minor}
            __WDF_MAJOR_VERSION=1
            __WDF_MINOR_VERSION=${_minor}
            __WDF_BUILD_NUMBER=0)
    target_compile_options(${_target} PRIVATE
        -U_WIN32_WINNT -D_WIN32_WINNT=0x603
        -UNTDDI_VERSION -DNTDDI_VERSION=0x06030000)

    target_include_directories(${_target}
        PUBLIC ${REACTOS_SOURCE_DIR}/sdk/include/wdf/kmdf/1.${_minor}
        PRIVATE
            ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf/shared/inc/private/common
            ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf/kmdf/inc/private)
endfunction()

# Keep the historical target name as the default KMDF 1.17 client stub.
add_wdfdriverentry_version(wdfdriverentry 17)
add_wdfdriverentry_version(wdfdriverentry_1_33 33)
