add_definitions(-D__WINESRC__)

spec2def(winegstreamer.dll winegstreamer.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    reactos/main.c
    reactos/legacy/avisplit.c
    reactos/legacy/enummedia.c
    reactos/legacy/memallocator.c
    reactos/legacy/mpegsplit.c
    reactos/legacy/parser.c
    reactos/legacy/pin.c
    reactos/legacy/waveparser.c
    reactos/legacy/strmbase/enumpins.c
    reactos/legacy/strmbase/filter.c
    reactos/legacy/strmbase/mediatype.c
    reactos/legacy/strmbase/pin.c
    reactos/legacy/strmbase/seeking.c)

add_library(winegstreamer MODULE
    ${SOURCE}
    reactos/rsrc.rc
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/winegstreamer.def)

target_include_directories(winegstreamer BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/reactos/legacy)
set_source_files_properties(reactos/rsrc.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/winegstreamer.rgs;${CMAKE_CURRENT_SOURCE_DIR}/reactos/winegstreamer.rgs")
set_module_type(winegstreamer win32dll)
target_link_libraries(winegstreamer strmiids dxguid uuid wine wine_dll_register ${PSEH_LIB})
add_importlibs(winegstreamer advapi32 msvfw32 ole32 oleaut32 user32 winmm msvcrt kernel32 ntdll)
add_dependencies(winegstreamer dxsdk)
add_cd_file(TARGET winegstreamer DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(winegstreamer)
