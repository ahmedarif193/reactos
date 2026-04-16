list(APPEND SOURCE
    attribmod.c
    attrlist.c
    bitmap.c
    blockdev.c
    cache.c
    cache_runtime.c
    clusteralloc.c
    collate.c
    compress.c
    core.c
    direnum.c
    dirlookup.c
    diskwrite.c
    dispatch.c
    ea.c
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
    volinfo.c
    write.c)

add_library(ntfslx MODULE ${SOURCE})
set_module_type(ntfslx kernelmodedriver)
add_importlibs(ntfslx ntoskrnl hal)
add_pch(ntfslx ntfslx.h SOURCE)
add_cd_file(TARGET ntfslx DESTINATION reactos/system32/drivers FOR all)
