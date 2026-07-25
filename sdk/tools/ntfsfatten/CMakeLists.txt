set(NTFS3G_DIR ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/ntfs3g)
set(NTFSPROGS_DIR ${NTFS3G_DIR}/upstream/ntfsprogs)

set_source_files_properties(
    ${NTFSPROGS_DIR}/mkntfs.c
    PROPERTIES COMPILE_DEFINITIONS "main=ntfs3g_mkntfs_main")

add_host_tool(ntfsfatten
    ntfsfatten.c
    ${NTFSPROGS_DIR}/attrdef.c
    ${NTFSPROGS_DIR}/boot.c
    ${NTFSPROGS_DIR}/mkntfs.c
    ${NTFSPROGS_DIR}/sd.c
    ${NTFSPROGS_DIR}/utils.c)

target_compile_definitions(ntfsfatten PRIVATE HAVE_CONFIG_H)
target_include_directories(ntfsfatten PRIVATE
    ${NTFS3G_DIR}/config/host
    ${NTFS3G_DIR}/include
    ${NTFS3G_DIR}/upstream/include/ntfs-3g
    ${NTFSPROGS_DIR})
target_link_libraries(ntfsfatten PRIVATE ntfs3g_um host_includes)

if(NOT CMAKE_HOST_WIN32)
    target_compile_definitions(ntfsfatten PRIVATE _FILE_OFFSET_BITS=64)
endif()
