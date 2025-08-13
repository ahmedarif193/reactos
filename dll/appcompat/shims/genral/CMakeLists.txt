
include_directories(${SHIMLIB_DIR})

spec2def(acgenral.dll genral.spec)

list(APPEND SOURCE
    ignoredbgout.c
    ignorefreelib.c
    main.c
    shimtest.c
    themes.c
    genral.spec)

add_library(acgenral MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/acgenral.def)

set_module_type(acgenral win32dll ENTRYPOINT DllMain 12)
target_link_libraries(acgenral shimlib)
add_importlibs(acgenral uxtheme kernel32 ntdll)
# Target-specific fix: Use CMake script to fix nested archives  
add_custom_command(TARGET acgenral PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DARCHIVE_PATH=${CMAKE_BINARY_DIR}/dll/win32/uxtheme/libuxtheme.a -DCMAKE_AR=${CMAKE_AR} -DCMAKE_RANLIB=${CMAKE_RANLIB} -P ${CMAKE_SOURCE_DIR}/fix_archive.cmake
    COMMENT "Fixing nested uxtheme archive using CMake script")
add_cd_file(TARGET acgenral DESTINATION reactos/AppPatch FOR all)
