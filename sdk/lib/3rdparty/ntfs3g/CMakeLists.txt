set(NTFS3G_UPSTREAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/upstream)
set(NTFS3G_SOURCE_DIR ${NTFS3G_UPSTREAM_DIR}/libntfs-3g)
set(NTFS3G_INCLUDE_DIR ${NTFS3G_UPSTREAM_DIR}/include/ntfs-3g)

set(NTFS3G_CORE_SOURCES
    ${NTFS3G_SOURCE_DIR}/acls.c
    ${NTFS3G_SOURCE_DIR}/attrib.c
    ${NTFS3G_SOURCE_DIR}/attrlist.c
    ${NTFS3G_SOURCE_DIR}/bitmap.c
    ${NTFS3G_SOURCE_DIR}/bootsect.c
    ${NTFS3G_SOURCE_DIR}/cache.c
    ${NTFS3G_SOURCE_DIR}/collate.c
    ${NTFS3G_SOURCE_DIR}/compat.c
    ${NTFS3G_SOURCE_DIR}/compress.c
    ${NTFS3G_SOURCE_DIR}/debug.c
    ${NTFS3G_SOURCE_DIR}/device.c
    ${NTFS3G_SOURCE_DIR}/dir.c
    ${NTFS3G_SOURCE_DIR}/ea.c
    ${NTFS3G_SOURCE_DIR}/efs.c
    ${NTFS3G_SOURCE_DIR}/index.c
    ${NTFS3G_SOURCE_DIR}/inode.c
    ${NTFS3G_SOURCE_DIR}/ioctl.c
    ${NTFS3G_SOURCE_DIR}/lcnalloc.c
    ${NTFS3G_SOURCE_DIR}/logfile.c
    ${NTFS3G_SOURCE_DIR}/logging.c
    ${NTFS3G_SOURCE_DIR}/mft.c
    ${NTFS3G_SOURCE_DIR}/misc.c
    ${NTFS3G_SOURCE_DIR}/mst.c
    ${NTFS3G_SOURCE_DIR}/object_id.c
    ${NTFS3G_SOURCE_DIR}/realpath.c
    ${NTFS3G_SOURCE_DIR}/reparse.c
    ${NTFS3G_SOURCE_DIR}/runlist.c
    ${NTFS3G_SOURCE_DIR}/security.c
    ${NTFS3G_SOURCE_DIR}/unistr.c
    ${NTFS3G_SOURCE_DIR}/volume.c
    ${NTFS3G_SOURCE_DIR}/xattrs.c)

add_library(ntfs3g_um STATIC
    ${NTFS3G_CORE_SOURCES}
    ${NTFS3G_SOURCE_DIR}/win32_io.c)

target_compile_definitions(ntfs3g_um PRIVATE HAVE_CONFIG_H)
target_include_directories(ntfs3g_um
    PUBLIC ${NTFS3G_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/config/um)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ntfs3g_um PRIVATE
        -Wno-format
        -Wno-pointer-sign
        -Wno-sign-compare
        -Wno-unused-function
        -Wno-unused-parameter
        -Wno-unused-variable)
endif()
