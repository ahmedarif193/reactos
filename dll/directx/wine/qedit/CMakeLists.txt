
add_definitions(-DWINE_REGISTER_DLL)

spec2def(qedit.dll qedit.spec)

list(APPEND SOURCE
    main.c
    mediadet.c
    nullrenderer.c
    samplegrabber.c
    timeline.c)

add_library(qedit MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/qedit.def)

add_idl_reg_scripts(qedit registry qedit_classes.idl)
target_compile_definitions(qedit PRIVATE USE_NEW_WINE_REGISTER_RESOURCES __WINESRC__)
target_include_directories(qedit BEFORE PRIVATE
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/strmbase11/include)
set_module_type(qedit win32dll)
target_link_libraries(qedit strmbase11 strmiids uuid wine wine_dll_canunload)
add_importlibs(qedit advapi32 oleaut32 ole32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET qedit DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(qedit) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
