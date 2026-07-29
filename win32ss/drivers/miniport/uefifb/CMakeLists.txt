
list(APPEND SOURCE
    uefifb.c
    uefifb.h)

add_library(uefifb MODULE ${SOURCE} uefifb.rc)
set_module_type(uefifb kernelmodedriver)
add_importlibs(uefifb ntoskrnl videoprt)
target_link_libraries(uefifb libcntpr)
add_cd_file(TARGET uefifb DESTINATION reactos/system32/drivers FOR all)

# Both models keep the binary as a recovery component. Only XPDM registers it:
# in WDDM mode softgpu owns the firmware framebuffer devnode instead.
if(REACTOS_USE_XPDM)
    add_driver_inf(uefifb uefifb.inf)
    add_registry_inf(uefifb_reg.inf)
endif()
