
remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT} -DWINVER=${REACTOS_TARGET_NT})
add_definitions(
    -DNTDDI_VERSION=0x06000000
    -D_WIN32_WINNT=0x0600
    -DWINVER=0x0600)

list(APPEND SOURCE
    wskclient.c
    ${CMAKE_CURRENT_BINARY_DIR}/wskclient.def)

spec2def(wskclient.sys wskclient.spec)

add_library(wskclient MODULE ${SOURCE})
set_module_type(wskclient kernelmodedriver)
add_importlibs(wskclient ntoskrnl netio hal)
add_cd_file(TARGET wskclient DESTINATION reactos/system32/drivers FOR all)
