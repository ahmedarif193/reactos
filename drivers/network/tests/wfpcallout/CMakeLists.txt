
list(APPEND SOURCE
    wfpcallout.c
    ${CMAKE_CURRENT_BINARY_DIR}/wfpcallout.def)

spec2def(wfpcallout.sys wfpcallout.spec)

add_library(wfpcallout MODULE ${SOURCE})
set_module_type(wfpcallout kernelmodedriver)
add_importlibs(wfpcallout ntoskrnl netio hal)
add_cd_file(TARGET wfpcallout DESTINATION reactos/system32/drivers FOR all)
