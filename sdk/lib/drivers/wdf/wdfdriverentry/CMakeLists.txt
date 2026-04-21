remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT} -DWINVER=${REACTOS_TARGET_NT})

add_library(wdfdriverentry STATIC wdfdriverentry.c)
add_dependencies(wdfdriverentry xdk)

if(GCC)
    target_compile_options(wdfdriverentry PRIVATE -Wno-unknown-pragmas -Wno-switch)
endif()

target_compile_definitions(wdfdriverentry
    PUBLIC WDF_DEVICE_NO_WDMSEC_H
    PRIVATE __WDF_MAJOR_VERSION=1 __WDF_MINOR_VERSION=17 __WDF_BUILD_NUMBER=7600
)
target_compile_options(wdfdriverentry PUBLIC
    -U_WIN32_WINNT -D_WIN32_WINNT=0x603
    -UNTDDI_VERSION -DNTDDI_VERSION=0x06030000)

target_include_directories(wdfdriverentry
    PUBLIC ${REACTOS_SOURCE_DIR}/sdk/include/wdf/kmdf/1.17
    PRIVATE ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf/shared/inc/private/common
    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf/kmdf/inc/private
)
