
include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl
    ${CMAKE_CURRENT_BINARY_DIR})

add_rpc_files(server ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl/winreg.idl)

list(APPEND SOURCE
    environment.c
    notify.c
    rpcserver.c
    sas.c
    screensaver.c
    security.c
    setup.c
    shutdown.c
    winlogon.c
    wlx.c
    winlogon.h
    ${CMAKE_CURRENT_BINARY_DIR}/winreg_s.c)

if(ENABLE_EXPERIMENTAL_EARLY_SPLASH)
    list(APPEND SOURCE splash.c)
    include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/libpng)
endif()

add_rc_deps(winlogon.rc ${CMAKE_CURRENT_SOURCE_DIR}/res/winlogon.ico)
add_executable(winlogon ${SOURCE} winlogon.rc)
target_link_libraries(winlogon wine ${PSEH_LIB})
set_module_type(winlogon win32gui)
add_importlibs(winlogon user32 advapi32 userenv secur32 rpcrt4 mpr msvcrt kernel32 ntdll)
if(ENABLE_EXPERIMENTAL_EARLY_SPLASH)
    target_compile_definitions(winlogon PRIVATE ENABLE_EXPERIMENTAL_EARLY_SPLASH)
    add_importlibs(winlogon gdi32 libpng)
    add_cd_file(FILE ${REACTOS_SOURCE_DIR}/modules/wallpapers/BlueHorizon.png DESTINATION reactos/Web/Wallpaper FOR all)
endif()
add_pch(winlogon winlogon.h SOURCE)
add_cd_file(TARGET winlogon DESTINATION reactos/system32 FOR all)
