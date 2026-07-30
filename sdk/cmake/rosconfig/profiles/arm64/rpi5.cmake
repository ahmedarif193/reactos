set(ROSCONFIG_PROFILE_PACKAGES
    cyw43455
    rp1gem
    rpi5vc4)
set(ROSCONFIG_PROFILE_CONFIGS
    "RPI_SUPPORT:BOOL=ON"
    # The board has no writable local disk in the test rig; the loader pulls
    # the live image over the network instead.
    "FREELDR_HTTP_BOOT:BOOL=ON")
