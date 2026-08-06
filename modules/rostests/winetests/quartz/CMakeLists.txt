add_definitions(
    -D__WINESRC__
    -DWINETEST_USE_WINE_DEBUG
    -DWINETEST_USE_DBGSTR_LONGLONG)

list(APPEND SOURCE
    acmwrapper.c
    avidec.c
    avisplit.c
    dsoundrender.c
    filesource.c
    filtergraph.c
    filtermapper.c
    memallocator.c
    mpegaudio.c
    mpeglayer3.c
    mpegsplit.c
    mpegvideo.c
    passthrough.c
    systemclock.c
    videorenderer.c
    vmr7.c
    vmr9.c
    waveparser.c
    testlist.c)

add_executable(quartz_winetest ${SOURCE} rsrc.rc)
target_link_libraries(quartz_winetest strmbase wine uuid)
add_dependencies(quartz_winetest dxsdk)
set_module_type(quartz_winetest win32cui)
add_importlibs(quartz_winetest advapi32 d3d9 ddraw dsound msdmo msvfw32 ole32 oleaut32 user32 winmm msvcrt kernel32 ntdll)
add_rostests_file(TARGET quartz_winetest)
