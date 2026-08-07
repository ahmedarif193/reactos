
remove_definitions(-DWINVER=${REACTOS_TARGET_NT} -D_WIN32_WINNT=${REACTOS_TARGET_NT})

list(APPEND SOURCE
    broadcast.c
    class.c
    clipboard.c
    combo.c
    cursoricon.c
    dce.c
    dde.c
    dialog.c
    edit.c
    generated.c
    input.c
    listbox.c
    menu.c
    monitor.c
    msg.c
    resource.c
    scroll.c
    static.c
    sysparams.c
    text.c
    uitools.c
    win.c
    winstation.c
    wsprintf.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c
    class.c
    monitor.c
    win.c
    winstation.c)

set_property(SOURCE win.c APPEND PROPERTY COMPILE_OPTIONS -iquote ${REACTOS_SOURCE_DIR}/sdk/include/wine)

add_executable(user32_winetest
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    resource.rc)

if(MSVC)
    target_compile_options(user32_winetest PRIVATE "/wd4005")
    target_compile_options(user32_winetest PRIVATE "/wd4024")
    target_compile_options(user32_winetest PRIVATE "/wd4047")
    target_compile_options(user32_winetest PRIVATE "/wd4098")
    remove_target_compile_option(user32_winetest "/we4477")
endif()

if(NOT MSVC)
    target_compile_options(user32_winetest PRIVATE
    -UNTDDI_VERSION -DNTDDI_VERSION=0x0A000005
    -U_WIN32_WINNT -D_WIN32_WINNT=0x0A00
    -UWINVER -DWINVER=0x0A00
    -Wno-format
    -Wno-int-conversion)
endif()

target_compile_definitions(user32_winetest PRIVATE wcsicmp=_wcsicmp)
set_module_type(user32_winetest win32cui)
add_importlibs(user32_winetest user32 gdi32 advapi32 imm32 hid msvcrt kernel32 ntdll)
add_pch(user32_winetest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET user32_winetest)
