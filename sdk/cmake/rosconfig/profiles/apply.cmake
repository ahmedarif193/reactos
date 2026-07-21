set(ROSCONFIG_PROFILE_PACKAGES)

set(_rosconfig_profile_arch "")
if(DEFINED ARCH AND NOT ARCH STREQUAL "")
    string(TOLOWER "${ARCH}" _rosconfig_profile_arch)
endif()

if(NOT DEFINED ROSCONFIG_PROFILE OR ROSCONFIG_PROFILE STREQUAL "")
    if(_rosconfig_profile_arch MATCHES "^(amd64|i386|arm64)$")
        set(ROSCONFIG_PROFILE "generic")
    endif()
endif()

if(DEFINED ROSCONFIG_PROFILE AND NOT ROSCONFIG_PROFILE STREQUAL "")
    if(_rosconfig_profile_arch STREQUAL "")
        message(FATAL_ERROR "ROSCONFIG_PROFILE requires ARCH to be selected")
    endif()

    if(NOT _rosconfig_profile_arch MATCHES "^(amd64|i386|arm64)$")
        message(FATAL_ERROR "No rosconfig profiles are defined for ARCH=${ARCH}")
    endif()
    if(NOT ROSCONFIG_PROFILE MATCHES "^[A-Za-z0-9][A-Za-z0-9_-]*$")
        message(FATAL_ERROR "Invalid rosconfig profile name: ${ROSCONFIG_PROFILE}")
    endif()

    set(_rosconfig_profile_file
        "${CMAKE_CURRENT_LIST_DIR}/${_rosconfig_profile_arch}/${ROSCONFIG_PROFILE}.cmake")
    if(NOT EXISTS "${_rosconfig_profile_file}")
        message(FATAL_ERROR
            "Unknown rosconfig profile '${ROSCONFIG_PROFILE}' for ${_rosconfig_profile_arch}")
    endif()

    set(ROSCONFIG_PROFILE_CONFIGS)
    include("${_rosconfig_profile_file}")

    foreach(_rosconfig_profile_config IN LISTS ROSCONFIG_PROFILE_CONFIGS)
        if(NOT _rosconfig_profile_config MATCHES
           "^([A-Za-z_][A-Za-z0-9_]*):(BOOL|STRING|PATH|FILEPATH)=(.*)$")
            message(FATAL_ERROR
                "Invalid config '${_rosconfig_profile_config}' in ${_rosconfig_profile_file}")
        endif()
        set(_rosconfig_config_name "${CMAKE_MATCH_1}")
        set(_rosconfig_config_type "${CMAKE_MATCH_2}")
        set(_rosconfig_config_value "${CMAKE_MATCH_3}")
        set(${_rosconfig_config_name} "${_rosconfig_config_value}"
            CACHE ${_rosconfig_config_type}
            "Selected by the ${ROSCONFIG_PROFILE} rosconfig profile" FORCE)
    endforeach()
endif()

function(rosconfig_validate_profile_packages)
    foreach(_rosconfig_profile_package IN LISTS ROSCONFIG_PROFILE_PACKAGES)
        if(NOT _rosconfig_profile_package MATCHES "^[A-Za-z0-9_.+-]+$")
            message(FATAL_ERROR
                "Invalid package name '${_rosconfig_profile_package}' in rosconfig profile")
        endif()
        if(NOT TARGET "${_rosconfig_profile_package}")
            message(FATAL_ERROR
                "Rosconfig profile package '${_rosconfig_profile_package}' was not built")
        endif()
    endforeach()
endfunction()
