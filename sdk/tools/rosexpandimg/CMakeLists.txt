
add_host_tool(rosexpandimg
    rosexpandimg.c
    ${REACTOS_SOURCE_DIR}/base/applications/cmdutils/lntexpandstorage/expandcore.c)
target_include_directories(rosexpandimg PRIVATE
    ${REACTOS_SOURCE_DIR}/base/applications/cmdutils/lntexpandstorage)
