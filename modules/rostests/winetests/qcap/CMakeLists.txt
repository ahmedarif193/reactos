add_definitions(
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG)

list(APPEND SOURCE
    audiorecord.c
    avico.c
    avimux.c
    capturegraph.c
    filewriter.c
    qcap.c
    smartteefilter.c
    videocapture.c
    testlist.c)

add_executable(qcap_winetest ${SOURCE})
target_link_libraries(qcap_winetest strmbase strmiids uuid wine)
set_module_type(qcap_winetest win32cui)
add_importlibs(qcap_winetest advapi32 msvfw32 oleaut32 ole32 user32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET qcap_winetest)
