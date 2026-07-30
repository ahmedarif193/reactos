set(ROSCONFIG_PROFILE_PACKAGES
    cyw43455
    rp1gem)
if(REACTOS_USE_XPDM OR
   (REACTOS_USE_WDDM AND (REACTOS_WDDM_TARGET_LEVEL GREATER_EQUAL 2000)))
    list(APPEND ROSCONFIG_PROFILE_PACKAGES rpi5vc4)
endif()
set(ROSCONFIG_PROFILE_CONFIGS
    "RPI_SUPPORT:BOOL=ON"
    # The board has no writable local disk in the test rig; the loader pulls
    # the live image over the network instead.
    "FREELDR_HTTP_BOOT:BOOL=ON")
