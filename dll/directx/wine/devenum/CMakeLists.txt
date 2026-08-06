
spec2def(devenum.dll devenum.spec)

list(APPEND SOURCE
    createdevenum.c
    devenum_main.c
    mediacatenum.c
    parsedisplayname.c)

add_library(devenum MODULE
    ${SOURCE}
    devenum.rc
    ${CMAKE_CURRENT_BINARY_DIR}/devenum.def)

add_idl_reg_scripts(devenum registry devenum_classes.idl)
set_module_type(devenum win32dll UNICODE)
target_link_libraries(devenum dmoguids strmiids uuid wine wine_dll_canunload)
add_delay_importlibs(devenum msvfw32)
add_importlibs(devenum advapi32 ole32 oleaut32 winmm user32 avicap32 dsound msdmo msvcrt kernel32 ntdll)
add_dependencies(devenum wineheaders)
add_cd_file(TARGET devenum DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(devenum) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
