PROJECT(SHELL)

list(APPEND SOURCE
    explorer11.cpp
    navpane.cpp
    frame.h)

add_executable(explorer11 ${SOURCE} explorer11.rc)
target_link_libraries(explorer11 uuid wine cpprt atl_classes)
set_module_type(explorer11 win32gui UNICODE)
add_importlibs(explorer11 advapi32 gdi32 user32 comctl32 ole32 oleaut32 shell32 shlwapi uxtheme msvcrt kernel32 ntdll)
add_pch(explorer11 frame.h SOURCE)
add_cd_file(TARGET explorer11 DESTINATION reactos FOR all)
