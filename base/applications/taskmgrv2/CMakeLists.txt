
list(APPEND SOURCE
    taskmgrv2.c
    shell.c
    theme.c
    registry.c
    perfdata.c
    cpu_topology.c
    graphctl.c
    page_performance.c
    page_processes.c
    page_details.c
    page_services.c
    page_users.c
    page_startup.c
    page_apphistory.c
    page_settings.c
    precomp.h)

file(GLOB taskmgrv2_rc_deps res/*.* res/modern/*.*)
add_rc_deps(taskmgrv2.rc ${taskmgrv2_rc_deps})

add_executable(taskmgrv2 ${SOURCE} taskmgrv2.rc)
set_module_type(taskmgrv2 win32gui UNICODE)
add_importlibs(taskmgrv2
    advapi32
    user32
    gdi32
    msimg32
    shell32
    shlwapi
    comctl32
    msvcrt
    kernel32
    ntdll
    uxtheme
    wtsapi32
    powrprof
    gdiplus
    version
    psapi)
add_pch(taskmgrv2 precomp.h SOURCE)
add_cd_file(TARGET taskmgrv2 DESTINATION reactos/system32 FOR all)
