
project(taskmgr11)

# The GPU section reads the same D3DKMT interface Windows reads, so this
# module has to see the very layouts dxgkrnl was compiled against.  Leaving
# the tree-wide Vista selector in place would silently give it a different
# D3DKMT_QUERYSTATISTICS shape than the kernel fills in.
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

list(APPEND SOURCE
    theme.cpp
    render.cpp
    icons.cpp
    data.cpp
    gpu.cpp
    treelist.cpp
    menu.cpp
    controls.cpp
    dialogs.cpp
    page_processes.cpp
    page_performance.cpp
    page_apphistory.cpp
    page_startup.cpp
    page_users.cpp
    page_details.cpp
    page_services.cpp
    page_sensors.cpp
    page_settings.cpp
    main.cpp
    battery_telemetry.h
    app.h)

file(GLOB taskmgr11_rc_deps res/*.*)
add_rc_deps(taskmgr11.rc ${taskmgr11_rc_deps})
add_executable(taskmgr11 ${SOURCE} taskmgr11.rc)
set_module_type(taskmgr11 win32gui UNICODE)
target_link_libraries(taskmgr11 uuid wdmguid dx10guid cpprt)
add_dependencies(taskmgr11 d3d_idl_headers)
add_importlibs(taskmgr11 advapi32 user32 gdi32 gdiplus shell32 shlwapi setupapi ole32 oleaut32 comdlg32 wtsapi32 iphlpapi ws2_32 version psapi comctl32 dwmapi uxtheme msvcrt kernel32 ntdll)
add_pch(taskmgr11 app.h SOURCE)
add_cd_file(TARGET taskmgr11 DESTINATION reactos/system32 FOR all)
