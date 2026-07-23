set(NTFS3G_REACTOS_DIR ${CMAKE_CURRENT_LIST_DIR})
set(NTFS3G_UPSTREAM_DIR ${NTFS3G_REACTOS_DIR}/upstream)
set(NTFS3G_SOURCE_DIR ${NTFS3G_UPSTREAM_DIR}/libntfs-3g)
set(NTFS3G_INCLUDE_DIR ${NTFS3G_UPSTREAM_DIR}/include/ntfs-3g)

# Keep this list limited to the pristine filesystem core shared by every
# ReactOS host. Platform callbacks and shared glue live outside upstream.
set(NTFS3G_PORTABLE_SOURCES
    ${NTFS3G_SOURCE_DIR}/attrib.c
    ${NTFS3G_SOURCE_DIR}/attrlist.c
    ${NTFS3G_SOURCE_DIR}/bitmap.c
    ${NTFS3G_SOURCE_DIR}/bootsect.c
    ${NTFS3G_SOURCE_DIR}/cache.c
    ${NTFS3G_SOURCE_DIR}/collate.c
    ${NTFS3G_SOURCE_DIR}/compress.c
    ${NTFS3G_SOURCE_DIR}/debug.c
    ${NTFS3G_SOURCE_DIR}/device.c
    ${NTFS3G_SOURCE_DIR}/dir.c
    ${NTFS3G_SOURCE_DIR}/ea.c
    ${NTFS3G_SOURCE_DIR}/efs.c
    ${NTFS3G_SOURCE_DIR}/index.c
    ${NTFS3G_SOURCE_DIR}/inode.c
    ${NTFS3G_SOURCE_DIR}/lcnalloc.c
    ${NTFS3G_SOURCE_DIR}/logfile.c
    ${NTFS3G_SOURCE_DIR}/mft.c
    ${NTFS3G_SOURCE_DIR}/mst.c
    ${NTFS3G_SOURCE_DIR}/object_id.c
    ${NTFS3G_SOURCE_DIR}/reparse.c
    ${NTFS3G_SOURCE_DIR}/runlist.c
    ${NTFS3G_SOURCE_DIR}/unistr.c
    ${NTFS3G_SOURCE_DIR}/volume.c
    ${NTFS3G_REACTOS_DIR}/reactos/common/security_mount.c)
