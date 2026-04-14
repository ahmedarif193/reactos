
list(APPEND SOURCE
    uefifb.c
    uefifb.h)

add_library(uefifb MODULE ${SOURCE} uefifb.rc)
set_module_type(uefifb kernelmodedriver)
add_importlibs(uefifb ntoskrnl videoprt)
target_link_libraries(uefifb libcntpr)
add_cd_file(TARGET uefifb DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(uefifb_reg.inf)
