# ARM64 Hardware Boot Media Configuration
#
# This file defines ARM64 platform boot file paths for build-time embedding.
# The ARM64 rpi5 target profile enables Raspberry Pi support.

# Save the directory where this file is located (media/boot) at include time
set(_ARM64_BOOT_MEDIA_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Initialize ARM64 boot file variables (empty by default)
set(ARM64_BOOT_GRAFT_POINTS "")
set(ARM64_BOOT_FILE_DEPS "")

if(ARCH STREQUAL "arm64")
    # Raspberry Pi support (all models: Pi 2/3/4/5, CM3/4/5, Zero 2)
    # Controlled by RPI_SUPPORT; the ARM64 rpi5 profile enables it.
    option(RPI_SUPPORT "Include Raspberry Pi boot files in ARM64 images" OFF)

    if(RPI_SUPPORT)
        set(RPI_BOOT_DIR "${_ARM64_BOOT_MEDIA_DIR}/rpi")

        if(NOT EXISTS "${RPI_BOOT_DIR}")
            message(FATAL_ERROR "ARM64: RPI_SUPPORT is ON but boot directory not found: ${RPI_BOOT_DIR}")
        endif()

        message(STATUS "ARM64: Raspberry Pi boot files will be embedded at build time")

        # Verify config.txt exists (central boot configuration for all Pi models)
        if(NOT EXISTS "${RPI_BOOT_DIR}/config.txt")
            message(FATAL_ERROR "ARM64: Required RPi boot file missing: ${RPI_BOOT_DIR}/config.txt")
        endif()

        # Check overlays directory exists
        if(NOT EXISTS "${RPI_BOOT_DIR}/overlays")
            message(FATAL_ERROR "ARM64: Required RPi overlays directory missing: ${RPI_BOOT_DIR}/overlays")
        endif()

        # Collect all boot files: UEFI firmware, device trees, GPU firmware, config
        file(GLOB _RPI_BOOT_FILES
            "${RPI_BOOT_DIR}/*.fd"
            "${RPI_BOOT_DIR}/*.dtb"
            "${RPI_BOOT_DIR}/*.elf"
            "${RPI_BOOT_DIR}/*.dat"
            "${RPI_BOOT_DIR}/*.bin"
            "${RPI_BOOT_DIR}/*.txt"
        )

        file(GLOB _RPI_OVERLAY_FILES
            "${RPI_BOOT_DIR}/overlays/*")

        # Add each file as a graft point (filename=fullpath)
        set(_rpi_file_count 0)
        foreach(_full_path ${_RPI_BOOT_FILES})
            get_filename_component(_file "${_full_path}" NAME)
            list(APPEND ARM64_BOOT_GRAFT_POINTS "${_file}=${_full_path}")
            list(APPEND ARM64_BOOT_FILE_DEPS "${_full_path}")
            add_cd_file(FILE "${_full_path}" DESTINATION root NO_CAB FOR preinstall)
            math(EXPR _rpi_file_count "${_rpi_file_count} + 1")
        endforeach()

        foreach(_full_path ${_RPI_OVERLAY_FILES})
            if(IS_DIRECTORY "${_full_path}")
                continue()
            endif()

            get_filename_component(_file "${_full_path}" NAME)
            list(APPEND ARM64_BOOT_GRAFT_POINTS "overlays/${_file}=${_full_path}")
            list(APPEND ARM64_BOOT_FILE_DEPS "${_full_path}")
            add_cd_file(FILE "${_full_path}" DESTINATION overlays NO_CAB FOR preinstall)
            math(EXPR _rpi_file_count "${_rpi_file_count} + 1")
        endforeach()

        # Set individual file paths for scripts that need them
        set(ARM64_RPI_BOOT_DIR "${RPI_BOOT_DIR}")
        set(ARM64_RPI_CONFIG "${RPI_BOOT_DIR}/config.txt")
        set(ARM64_RPI_OVERLAYS_DIR "${RPI_BOOT_DIR}/overlays")

        message(STATUS "ARM64: RPi boot files validated (${_rpi_file_count} files)")
    endif()
endif()
