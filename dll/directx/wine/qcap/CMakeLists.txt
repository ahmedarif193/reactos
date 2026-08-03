
add_definitions(-DWINE_REGISTER_DLL)

spec2def(qcap.dll qcap.spec)

list(APPEND SOURCE
    audiorecord.c
    avico.c
    avimux.c
    capturegraph.c
    filewriter.c
    qcap_main.c
    reactos.c
    smartteefilter.c
    vfwcapture.c)

add_library(qcap MODULE ${SOURCE}
           ${CMAKE_CURRENT_BINARY_DIR}/qcap_stubs.c
           ${CMAKE_CURRENT_BINARY_DIR}/qcap.def
           version.rc)

add_idl_reg_scripts(qcap registry qcap_classes.idl)
target_compile_definitions(qcap PRIVATE __WINESRC__)
set_module_type(qcap win32dll)
target_link_libraries(qcap strmbase strmiids uuid wine wine_dll_canunload)
add_importlibs(qcap ole32 oleaut32 user32 winmm msvcrt kernel32 ntdll)
add_delay_importlibs(qcap msvfw32)
add_cd_file(TARGET qcap DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(qcap) # CORE-5743: No CONST_VTABLE
