
add_definitions(
    -D_WINE)

spec2def(mciwave.dll mciwave.spec)

list(APPEND SOURCE
    mciwave.c
    ${CMAKE_CURRENT_BINARY_DIR}/mciwave.def)

add_library(mciwave MODULE ${SOURCE})
set_module_type(mciwave win32dll)
target_link_libraries(mciwave wine)
add_importlibs(mciwave user32 winmm msvcrt kernel32 ntdll)
add_cd_file(TARGET mciwave DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(mciwave) # CORE-5743: No ARRAY_SIZE macro
