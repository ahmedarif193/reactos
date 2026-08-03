
spec2def(dmusic.dll dmusic.spec)

list(APPEND SOURCE
    band.c
    buffer.c
    clock.c
    collection.c
    dmobject.c
    dmusic.c
    dmusic_main.c
    download.c
    instrument.c
    port.c
    wave.c)

add_library(dmusic MODULE
    ${SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dmusic.def)

set_module_type(dmusic win32dll)
target_link_libraries(dmusic dxguid uuid wine wine_dll_register wine_dll_canunload)
add_importlibs(dmusic ole32 advapi32 dsound user32 winmm msvcrt kernel32 ntdll)
add_cd_file(TARGET dmusic DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(dmusic) # CORE-5743: No CONST_VTABLE
