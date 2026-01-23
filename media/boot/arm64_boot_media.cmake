# ARM64 Hardware Boot Media Configuration
#
# This file defines ARM64 platform boot file paths for build-time embedding.
# Enable specific platforms via CMake options (e.g., -DRPI5_SUPPORT=ON)

# Save the directory where this file is located (media/boot) at include time
set(_ARM64_BOOT_MEDIA_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Initialize ARM64 boot file variables (empty by default)
set(ARM64_BOOT_GRAFT_POINTS "")

if(ARCH STREQUAL "arm64")
    # Raspberry Pi 5 support - controlled by RPI5_SUPPORT option
    # Default: ON for arm64 builds
    option(RPI5_SUPPORT "Include Raspberry Pi 5 boot files in ARM64 images" ON)

    if(RPI5_SUPPORT)
        set(RPI5_BOOT_DIR "${_ARM64_BOOT_MEDIA_DIR}/rpi5")

        if(NOT EXISTS "${RPI5_BOOT_DIR}")
            message(FATAL_ERROR "ARM64: RPI5_SUPPORT is ON but boot directory not found: ${RPI5_BOOT_DIR}")
        endif()

        message(STATUS "ARM64: RPI5 boot files will be embedded at build time")

        # Required files list - build fails if any are missing
        set(_RPI5_REQUIRED_FILES
            "RPI_EFI.fd"
            "config.txt"
            "bcm2712-rpi-5-b.dtb"
            "bcm2712-d-rpi-5-b.dtb"
            "bcm2712d0-rpi-5-b.dtb"
            "bcm2712-rpi-500.dtb"
            "bcm2712-rpi-cm5-cm4io.dtb"
            "bcm2712-rpi-cm5-cm5io.dtb"
            "bcm2712-rpi-cm5l-cm4io.dtb"
            "bcm2712-rpi-cm5l-cm5io.dtb"
        )

        # Check all required files exist and add to graft points
        foreach(_file ${_RPI5_REQUIRED_FILES})
            set(_full_path "${RPI5_BOOT_DIR}/${_file}")
            if(NOT EXISTS "${_full_path}")
                message(FATAL_ERROR "ARM64: Required RPI5 boot file missing: ${_full_path}")
            endif()
            list(APPEND ARM64_BOOT_GRAFT_POINTS "${_file}=${_full_path}")
        endforeach()

        # Check overlays directory exists
        if(NOT EXISTS "${RPI5_BOOT_DIR}/overlays")
            message(FATAL_ERROR "ARM64: Required RPI5 overlays directory missing: ${RPI5_BOOT_DIR}/overlays")
        endif()

        # Set individual file paths for scripts that need them
        set(ARM64_RPI5_UEFI_FW "${RPI5_BOOT_DIR}/RPI_EFI.fd")
        set(ARM64_RPI5_DTB "${RPI5_BOOT_DIR}/bcm2712-rpi-5-b.dtb")
        set(ARM64_RPI5_CONFIG "${RPI5_BOOT_DIR}/config.txt")
        set(ARM64_RPI5_OVERLAYS_DIR "${RPI5_BOOT_DIR}/overlays")

        list(LENGTH _RPI5_REQUIRED_FILES _rpi5_file_count)
        message(STATUS "ARM64: RPI5 boot files validated (${_rpi5_file_count} files)")
    endif()

    # Future platforms can be added here:
    # if(RPI4_SUPPORT)
    #     set(RPI4_BOOT_DIR "${_ARM64_BOOT_MEDIA_DIR}/rpi4")
    #     ...
    # endif()
endif()
