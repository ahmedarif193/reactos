
list(APPEND SOURCE
    wskclient.c
    ${CMAKE_CURRENT_BINARY_DIR}/wskclient.def)

spec2def(wskclient.sys wskclient.spec)

add_library(wskclient MODULE ${SOURCE})
set_module_type(wskclient kernelmodedriver)
add_importlibs(wskclient ntoskrnl netio hal)
add_cd_file(TARGET wskclient DESTINATION reactos/system32/drivers FOR all)
