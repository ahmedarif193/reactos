
spec2def(wpprecorderum.dll wpprecorderum.spec)

list(APPEND SOURCE
    wpprecorderum.c
    wpprecorderum.rc
    ${CMAKE_CURRENT_BINARY_DIR}/wpprecorderum.def)

add_library(wpprecorderum MODULE ${SOURCE})
set_module_type(wpprecorderum win32dll ENTRYPOINT 0)
add_importlibs(wpprecorderum kernel32)
add_dependencies(wpprecorderum psdk)
add_cd_file(TARGET wpprecorderum DESTINATION reactos/system32 NAME_ON_CD WppRecorderUM.dll FOR all)
