add_definitions(
    -D_WINE
    -D_COMCTL32_
    -D__WINE_COMCTL32_VERSION=6)

set(COMCTL32_COMMON_DIR ${REACTOS_SOURCE_DIR}/dll/win32/comctl32)
include_directories(BEFORE ${COMCTL32_COMMON_DIR})

spec2def(comctl32.dll comctl32_v6.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    ${COMCTL32_COMMON_DIR}/animate.c
    button.c
    combo.c
    ${COMCTL32_COMMON_DIR}/comboex.c
    ${COMCTL32_COMMON_DIR}/commctrl.c
    ${COMCTL32_COMMON_DIR}/datetime.c
    ${COMCTL32_COMMON_DIR}/dpa.c
    ${COMCTL32_COMMON_DIR}/draglist.c
    ${COMCTL32_COMMON_DIR}/dsa.c
    edit.c
    ${COMCTL32_COMMON_DIR}/flatsb.c
    ${COMCTL32_COMMON_DIR}/header.c
    ${COMCTL32_COMMON_DIR}/hotkey.c
    ${COMCTL32_COMMON_DIR}/imagelist.c
    ${COMCTL32_COMMON_DIR}/ipaddress.c
    listbox.c
    ${COMCTL32_COMMON_DIR}/listview.c
    ${COMCTL32_COMMON_DIR}/monthcal.c
    ${COMCTL32_COMMON_DIR}/nativefont.c
    ${COMCTL32_COMMON_DIR}/pager.c
    ${COMCTL32_COMMON_DIR}/progress.c
    ${COMCTL32_COMMON_DIR}/propsheet.c
    ${COMCTL32_COMMON_DIR}/rebar.c
    ${COMCTL32_COMMON_DIR}/smoothscroll.c
    static.c
    ${COMCTL32_COMMON_DIR}/status.c
    ${COMCTL32_COMMON_DIR}/string.c
    syslink.c
    ${COMCTL32_COMMON_DIR}/tab.c
    taskdialog.c
    ${COMCTL32_COMMON_DIR}/toolbar.c
    ${COMCTL32_COMMON_DIR}/tooltips.c
    ${COMCTL32_COMMON_DIR}/trackbar.c
    ${COMCTL32_COMMON_DIR}/treeview.c
    ${COMCTL32_COMMON_DIR}/updown.c
    ${COMCTL32_COMMON_DIR}/reactos.c
    ${COMCTL32_COMMON_DIR}/stubs.c)

set_source_files_properties(
    ${COMCTL32_COMMON_DIR}/stubs.c
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND (ARCH STREQUAL "amd64" OR ARCH STREQUAL "arm64"))
    # These sources catch access faults with native SEH. Without asynchronous
    # exception semantics Clang can omit the corresponding unwind handlers.
    set_property(SOURCE ${COMCTL32_COMMON_DIR}/imagelist.c ${COMCTL32_COMMON_DIR}/treeview.c listbox.c PROPERTY SKIP_PRECOMPILE_HEADERS ON)
    set_property(SOURCE ${COMCTL32_COMMON_DIR}/imagelist.c ${COMCTL32_COMMON_DIR}/treeview.c listbox.c APPEND PROPERTY COMPILE_OPTIONS -Xclang -fasync-exceptions)
endif()

add_library(comctl32_v6 MODULE
    ${SOURCE}
    ${COMCTL32_COMMON_DIR}/rsrc.rc
    rsrc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/comctl32.def)

set_target_properties(comctl32_v6 PROPERTIES OUTPUT_NAME comctl32)
set_module_type(comctl32_v6 win32dll UNICODE)
target_link_libraries(comctl32_v6 uuid wine ${PSEH_LIB} oldnames)
add_delay_importlibs(comctl32_v6 oleacc winmm uxtheme)
add_importlibs(comctl32_v6 user32 gdi32 advapi32 usp10 imm32 kernelbase_ros oleaut32 msvcrt kernel32 ntdll)
add_pch(comctl32_v6 ${COMCTL32_COMMON_DIR}/precomp.h SOURCE)
add_dependencies(comctl32_v6 psdk)

add_cd_file(
    TARGET comctl32_v6
    NAME_ON_CD comctl32.dll
    DESTINATION reactos/winsxs/${WINARCH}_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.2982_none_deadbeef
    FOR all)

set_wine_module_FIXME(comctl32_v6)
