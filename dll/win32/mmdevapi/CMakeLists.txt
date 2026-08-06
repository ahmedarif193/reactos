
spec2def(mmdevapi.dll mmdevapi.spec ADD_IMPORTLIB)

include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/sound
    ${REACTOS_SOURCE_DIR}/drivers/wdm/audio/legacy/wdmaud)

list(APPEND SOURCE
    audiosessionmanager.c
    audiovolume.c
    client.c
    devenum.c
    main.c
    session.c
    spatialaudio.c
    mmdevapi_private.h
    unixlib.h
    precomp.h
    ${CMAKE_CURRENT_BINARY_DIR}/mmdevapi_stubs.c)

if(ARCH STREQUAL "arm64" OR ARCH STREQUAL "amd64" OR ARCH STREQUAL "i386")
    list(APPEND SOURCE reactos_wdmaud.c)
endif()

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_library(mmdevapi MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    mmdevapi.rc
    ${CMAKE_CURRENT_BINARY_DIR}/mmdevapi.def)

set_module_type(mmdevapi win32dll)
target_compile_definitions(mmdevapi PRIVATE _FORCENAMELESSUNION _FORCENAMELESSSTRUCT CONST_VTABLE CP_UNIXCP=CP_ACP)
target_link_libraries(mmdevapi uuid wine)
add_importlibs(mmdevapi ole32 oleaut32 user32 advapi32 version setupapi msvcrt kernel32_vista kernel32 ntdll)
add_delay_importlibs(mmdevapi winmm)
add_dependencies(mmdevapi dxsdk)
add_cd_file(TARGET mmdevapi DESTINATION reactos/system32 FOR all)
set_wine_module(mmdevapi)
