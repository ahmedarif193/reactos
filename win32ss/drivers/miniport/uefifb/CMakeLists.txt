
list(APPEND SOURCE
    uefifb.c
    uefifb.h)

add_library(uefifb MODULE ${SOURCE} uefifb.rc)
set_module_type(uefifb kernelmodedriver)
add_importlibs(uefifb ntoskrnl videoprt)
target_link_libraries(uefifb libcntpr)
add_cd_file(TARGET uefifb DESTINATION reactos/system32/drivers FOR all)
# XPDM display path RETIRED in favor of WDDM (dxgkrnl/softgpu drive the firmware
# framebuffer now). Leaving uefifb.sys on the media but unregistered: no driver
# INF (so PnP does not bind it to DETECTEDInternal\uefifb) and no service, so
# softgpu claims the firmware framebuffer device instead. Re-enable these two
# lines to fall back to the legacy XPDM framebuffer miniport.
# add_driver_inf(uefifb uefifb.inf)
# add_registry_inf(uefifb_reg.inf)
