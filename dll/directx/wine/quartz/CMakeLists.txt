
add_definitions(
    -DENTRY_PREFIX=QUARTZ_
    -DWINE_REGISTER_DLL
    -DPROXY_DELEGATION)

spec2def(quartz.dll quartz.spec)
add_rpcproxy_files(quartz_strmif.idl)

list(APPEND SOURCE
    acmwrapper.c
    avidec.c
    decoder.c
    dsoundrender.c
    filesource.c
    filtergraph.c
    filtermapper.c
    main.c
    memallocator.c
    parser.c
    passthrough.c
    systemclock.c
    videorenderer.c
    vmr7.c
    vmr7_presenter.c
    vmr9.c
    window.c)

list(APPEND PCH_SKIP_SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/proxy.dlldata.c
    ${CMAKE_CURRENT_BINARY_DIR}/quartz_strmif_p.c
    ${CMAKE_CURRENT_BINARY_DIR}/quartz_stubs.c)

add_library(quartz MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/quartz.def
    quartz.rc
    version.rc)

add_typelib(control_tlb.idl)

list(APPEND quartz_rc_deps
    ${CMAKE_CURRENT_BINARY_DIR}/control_tlb.tlb
    ${CMAKE_CURRENT_SOURCE_DIR}/control_tlb.rgs
    ${CMAKE_CURRENT_SOURCE_DIR}/quartz_strmif.rgs)

set_source_files_properties(version.rc PROPERTIES OBJECT_DEPENDS "${quartz_rc_deps}")
set_source_files_properties(quartz.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/quartz.rgs")
set_module_type(quartz win32dll)
target_link_libraries(quartz strmbase strmiids dxguid uuid wine wine_dll_register wine_dll_canunload ${PSEH_LIB})
add_importlibs(quartz dsound msacm32 msvfw32 ole32 oleaut32 rpcrt4 user32 gdi32 advapi32 winmm msvcrt kernel32_vista kernel32 ntdll)
add_delay_importlibs(quartz ddraw)
add_dependencies(quartz dxsdk stdole2)
add_cd_file(TARGET quartz DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(quartz) # CORE-5743: No CONST_VTABLE
