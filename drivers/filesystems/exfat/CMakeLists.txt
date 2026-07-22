list(APPEND SOURCE
    create.c
    dirctl.c
    driver.c
    fsctl.c
    info.c
    misc.c
    rw.c
    exfat.h)

add_library(exfat MODULE ${SOURCE} exfat.rc)
set_module_type(exfat kernelmodedriver)
target_link_libraries(exfat fatfs ${PSEH_LIB})
add_importlibs(exfat ntoskrnl hal)
add_pch(exfat exfat.h SOURCE)
add_cd_file(TARGET exfat DESTINATION reactos/system32/drivers FOR all)
