set(ROSCONFIG_PROFILE_PACKAGES)
set(ROSCONFIG_PROFILE_CONFIGS
    "LATTEPANDAMU_SUPPORT:BOOL=ON"
    # The board is brought up over HTTP boot; a plain cmake reconfigure once
    # silently reset this to OFF and shipped a loader without the network path.
    "FREELDR_HTTP_BOOT:BOOL=ON")
