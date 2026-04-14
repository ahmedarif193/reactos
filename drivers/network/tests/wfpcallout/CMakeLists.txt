
remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT} -DWINVER=${REACTOS_TARGET_NT})
add_definitions(
    -DNTDDI_VERSION=0x06000000
    -D_WIN32_WINNT=0x0600
    -DWINVER=0x0600)

list(APPEND SOURCE
    wfpcallout.c
    ${CMAKE_CURRENT_BINARY_DIR}/wfpcallout.def)

spec2def(wfpcallout.sys wfpcallout.spec)

add_library(wfpcallout MODULE ${SOURCE})
set_module_type(wfpcallout kernelmodedriver)
add_importlibs(wfpcallout ntoskrnl netio hal)
add_cd_file(TARGET wfpcallout DESTINATION reactos/system32/drivers FOR all)
