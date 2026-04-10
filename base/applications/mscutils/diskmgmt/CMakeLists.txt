include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/storageutils)

list(APPEND SOURCE
    actions.c
    actions.h
    contextmenu.c
    contextmenu.h
    diskview.c
    diskview.h
    diskviewwnd.c
    diskviewwnd.h
    diskmgmt.c
    labels.c
    labels.h
    layout.c
    layout.h
    mainwnd.c
    mainwndaction.c
    mainwndaction.h
    mainwndchrome.c
    mainwndchrome.h
    mainwndlayout.c
    mainwndlayout.h
    mainwndproc.c
    mainwndproc.h
    menustate.c
    menustate.h
    sort.c
    sort.h
    precomp.h
    properties.c
    properties.h
    snapshot.c
    snapshotenum.c
    snapshotenum.h
    snapshot.h
    snapshotmatch.c
    snapshotmatch.h
    ${REACTOS_SOURCE_DIR}/sdk/lib/storageutils/mountmgrutil.c
    splitter.c
    splitter.h
    volumelist.c
    volumelist.h)

add_rc_deps(diskmgmt.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/res/hdisk.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/tb_back.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/res/tb_forward.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/res/tb_rescan.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/res/tb_refresh.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/res/tb_create.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/res/tb_delete.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/res/tb_properties.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/res/tb_help.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/res/lv_volume.bmp
    ${CMAKE_CURRENT_SOURCE_DIR}/res/lv_partition.bmp)
add_executable(diskmgmt ${SOURCE} diskmgmt.rc)
set_module_type(diskmgmt win32gui UNICODE)
add_importlibs(diskmgmt fmifs user32 gdi32 comctl32 comdlg32 shell32 advapi32 uxtheme msvcrt kernel32 ntdll)
add_pch(diskmgmt precomp.h SOURCE)
add_cd_file(TARGET diskmgmt DESTINATION reactos/system32 FOR all)
