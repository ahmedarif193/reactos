add_definitions(-DUNICODE -D_UNICODE -DRDBSS_TRACKER)

include_directories(${REACTOS_SOURCE_DIR}/drivers/filesystems/mup
                    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/copysup)

list(APPEND SOURCE
    rdbss.c)

add_library(rdbsslib ${SOURCE})
target_link_libraries(rdbsslib PRIVATE pseh)
add_dependencies(rdbsslib bugcodes xdk)
# Pin rdbsslib to WS03 SP4 API level -- it implements pre-Vista RDBSS APIs.
# Use -U/-D in compile options to override the global _WIN32_WINNT after
# directory-level definitions have been emitted.
target_compile_options(rdbsslib PRIVATE
    -UNTDDI_VERSION -DNTDDI_VERSION=${REACTOS_TARGET_NTDDI}
    -U_WIN32_WINNT -D_WIN32_WINNT=0x0502
    -UWINVER -DWINVER=0x0502)
