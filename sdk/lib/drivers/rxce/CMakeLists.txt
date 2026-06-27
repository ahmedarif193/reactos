add_definitions(-DUNICODE -D_UNICODE -DRDBSS_TRACKER)

include_directories(${REACTOS_SOURCE_DIR}/drivers/filesystems/mup)

list(APPEND SOURCE
    rxce.c)

add_library(rxce ${SOURCE})
target_link_libraries(rxce PRIVATE pseh)
add_dependencies(rxce bugcodes xdk)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(rxce_native ${SOURCE})
    set_arm64ec_native_target(rxce_native)
    target_link_libraries(rxce_native PRIVATE pseh)
    add_dependencies(rxce_native bugcodes xdk)
endif()
# Pin rxce to WS03 SP4 API level -- it implements pre-Vista RDBSS APIs.
# Use -U/-D in compile options to override the global _WIN32_WINNT after
# directory-level definitions have been emitted.
target_compile_options(rxce PRIVATE
    -UNTDDI_VERSION -DNTDDI_VERSION=${REACTOS_TARGET_NTDDI}
    -U_WIN32_WINNT -D_WIN32_WINNT=0x0502
    -UWINVER -DWINVER=0x0502)
if(TARGET rxce_native)
    target_compile_options(rxce_native PRIVATE
        -UNTDDI_VERSION -DNTDDI_VERSION=${REACTOS_TARGET_NTDDI}
        -U_WIN32_WINNT -D_WIN32_WINNT=0x0502
        -UWINVER -DWINVER=0x0502)
endif()
