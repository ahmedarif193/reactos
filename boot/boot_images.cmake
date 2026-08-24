
# Include ARM64 hardware boot media configuration.
include(${CMAKE_SOURCE_DIR}/media/boot/arm64_boot_media.cmake)

option(LATTEPANDAMU_SUPPORT "Enable the LattePanda Mu board profile" OFF)
if(LATTEPANDAMU_SUPPORT AND NOT ARCH STREQUAL "amd64")
    message(FATAL_ERROR "The LattePanda Mu profile requires ARCH=amd64")
endif()
if(FREELDR_HTTP_BOOT AND NOT (LATTEPANDAMU_SUPPORT OR RPI_SUPPORT))
    message(FATAL_ERROR "FREELDR_HTTP_BOOT requires a board profile that ships UEFI network drivers")
endif()

#
# Splice an HTTP boot entry into a boot menu at its ROSCONFIG_PROFILE markers.
# DEFAULT_OS names the entry to boot unattended, or is empty to keep the menu's
# own selection (the flashed image keeps whichever entry the harness chose).
#
function(freeldr_ini_add_http_boot SOURCE OUTPUT URL DEFAULT_OS)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SOURCE}")
    file(READ "${SOURCE}" _contents)

    set(_os_marker "; ROSCONFIG_PROFILE_OS_ENTRIES")
    set(_boot_marker "; ROSCONFIG_PROFILE_BOOT_ENTRIES")
    string(FIND "${_contents}" "${_os_marker}" _os_marker_offset)
    string(FIND "${_contents}" "${_boot_marker}" _boot_marker_offset)
    if(_os_marker_offset EQUAL -1 OR _boot_marker_offset EQUAL -1)
        message(FATAL_ERROR "HTTP boot menu anchors are missing from ${SOURCE}")
    endif()

    if(DEFAULT_OS)
        string(REGEX REPLACE "DefaultOS=[^\r\n]*" "DefaultOS=${DEFAULT_OS}" _contents "${_contents}")
        string(REGEX REPLACE "TimeOut=[^\r\n]*" "TimeOut=0" _contents "${_contents}")
    endif()

    string(REPLACE "${_os_marker}"
                   "HttpBoot=\"ReactOS HTTP Boot - Debug\""
                   _contents "${_contents}")
    string(REPLACE "${_boot_marker}"
                   "[HttpBoot]\nBootType=Windows2003\nSystemPath=ramdisk(0)\\reactos\nOptions=/KERNEL=ntkrnlmp.exe /DEBUG /DEBUGPORT=COM1 /BAUDRATE=115200 /SOS /FASTDETECT /MININT /LOADSYMBOLS\nHttpBootUrl=${URL}"
                   _contents "${_contents}")
    file(WRITE "${OUTPUT}" "${_contents}")
endfunction()

#
# Add one kernel command-line option to every selectable entry. This keeps
# manual boot-menu selections consistent with the configured image behavior.
# DEFAULT_OS can select a debug entry for an otherwise unattended image.
#
function(freeldr_ini_add_boot_option SOURCE OUTPUT OPTION DEFAULT_OS)
    string(FIND "${SOURCE}" "${CMAKE_BINARY_DIR}/" _binary_source_prefix)
    if(NOT _binary_source_prefix EQUAL 0)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SOURCE}")
    endif()
    file(READ "${SOURCE}" _contents)

    if(DEFAULT_OS)
        string(REGEX REPLACE "DefaultOS=[^\r\n]*" "DefaultOS=${DEFAULT_OS}" _contents "${_contents}")
    endif()

    string(REGEX REPLACE "\nOptions=([^\r\n]*)" "\nOptions=\\1 ${OPTION}" _contents "${_contents}")
    file(WRITE "${OUTPUT}" "${_contents}")
endfunction()

set(FREELDR_BOOTCD_INI "${REACTOS_SOURCE_DIR}/boot/bootdata/bootcd.ini")
set(FREELDR_PREINSTALL_INI "${REACTOS_SOURCE_DIR}/boot/bootdata/preinstall.ini")
if(FREELDR_HTTP_BOOT)
    # The downloaded image is architecture-specific, so each board profile
    # points at its own copy on the build host.
    if(ARCH STREQUAL "arm64")
        set(_freeldr_http_boot_default_url "http://10.42.0.1/livecd-arm64.iso")
    else()
        set(_freeldr_http_boot_default_url "http://10.42.0.1/livecd.iso")
    endif()
    set(FREELDR_HTTP_BOOT_URL "${_freeldr_http_boot_default_url}" CACHE STRING
        "URL FreeLdr downloads the live image from")

    set(FREELDR_BOOTCD_INI "${CMAKE_CURRENT_BINARY_DIR}/bootdata/bootcd_httpboot.ini")
    freeldr_ini_add_http_boot("${REACTOS_SOURCE_DIR}/boot/bootdata/bootcd.ini"
                              "${FREELDR_BOOTCD_INI}"
                              "${FREELDR_HTTP_BOOT_URL}"
                              "HttpBoot")

    set(FREELDR_PREINSTALL_INI "${CMAKE_CURRENT_BINARY_DIR}/bootdata/preinstall_httpboot.ini")
    freeldr_ini_add_http_boot("${REACTOS_SOURCE_DIR}/boot/bootdata/preinstall.ini"
                              "${FREELDR_PREINSTALL_INI}"
                              "${FREELDR_HTTP_BOOT_URL}"
                              "HttpBoot")
endif()

set(_boot_test_options "")
if(ENABLE_ROSAUTOTEST_BOOT_RUN)
    string(APPEND _boot_test_options " /ROSAUTOTEST")
endif()
if(ENABLE_CPUBENCH_BOOT_RUN)
    string(APPEND _boot_test_options " /CPUBENCH")
endif()
if(ENABLE_RP1GEM_BENCHMARK)
    string(APPEND _boot_test_options " /ETHBENCH")
endif()
if(ENABLE_KMTEST_BOOT_RUN)
    string(APPEND _boot_test_options " /KMTEST")
endif()
if(ENABLE_RPI5_WIFI_BOOT_RUN)
    string(APPEND _boot_test_options " /RPI5WIFITEST")
endif()
string(STRIP "${_boot_test_options}" _boot_test_options)

if(ENABLE_BOOT_TEST_RUN)
    set(_freeldr_bootcd_source "${FREELDR_BOOTCD_INI}")
    set(_freeldr_preinstall_source "${FREELDR_PREINSTALL_INI}")
    set(FREELDR_BOOTCD_INI "${CMAKE_CURRENT_BINARY_DIR}/bootdata/bootcd_boot_tests.ini")
    set(FREELDR_PREINSTALL_INI "${CMAKE_CURRENT_BINARY_DIR}/bootdata/preinstall_boot_tests.ini")

    freeldr_ini_add_boot_option("${_freeldr_bootcd_source}"
                                "${FREELDR_BOOTCD_INI}"
                                "${_boot_test_options}"
                                "")

    if(FREELDR_HTTP_BOOT)
        set(_freeldr_preinstall_default "")
    else()
        set(_freeldr_preinstall_default "ReactOS_Debug")
    endif()
    freeldr_ini_add_boot_option("${_freeldr_preinstall_source}"
                                "${FREELDR_PREINSTALL_INI}"
                                "${_boot_test_options}"
                                "${_freeldr_preinstall_default}")
endif()

# EFI platform ID - Used for naming the EFI boot image on supported platforms.
if(ARCH STREQUAL "i386")
    if(NOT (SARCH STREQUAL "pc98" OR SARCH STREQUAL "xbox"))
        set(EFI_PLATFORM_ID "ia32")
    endif()
elseif(ARCH STREQUAL "amd64")
    set(EFI_PLATFORM_ID "x64")
elseif(ARCH STREQUAL "ia64")
    set(EFI_PLATFORM_ID "ia64")
elseif(ARCH STREQUAL "arm")
    set(EFI_PLATFORM_ID "arm")
elseif(ARCH STREQUAL "arm64")
    set(EFI_PLATFORM_ID "aa64")
else()
    message(FATAL_ERROR "Unknown ARCH '" ${ARCH} "', cannot generate a valid UEFI boot image filename.")
endif()

## efisys.bin
if(DEFINED EFI_PLATFORM_ID)
    set(_efisys_boot_options)
    set(_efisys_depends native-fatten uefildr)
    set(_uefi_driver_fatten_options)
    set(_uefi_driver_files)
    if(FREELDR_HAS_BIOS_BOOT)
        set(_efisys_boot_options -boot ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/fat.bin)
        list(APPEND _efisys_depends fat)
    endif()

    if(FREELDR_HTTP_BOOT)
        set(_uefi_driver_dir "${REACTOS_SOURCE_DIR}/media/boot/uefi_drivers/${ARCH}")
        file(GLOB _uefi_driver_files CONFIGURE_DEPENDS "${_uefi_driver_dir}/*.efi")
        if(NOT _uefi_driver_files)
            message(FATAL_ERROR "UEFI network boot drivers are missing from ${_uefi_driver_dir}")
        endif()

        list(APPEND _uefi_driver_fatten_options -mkdir EFI/BOOT/drivers)
        foreach(_driver ${_uefi_driver_files})
            get_filename_component(_driver_name "${_driver}" NAME)
            list(APPEND _uefi_driver_fatten_options -add "${_driver}" "EFI/BOOT/drivers/${_driver_name}")
        endforeach()
        list(APPEND _efisys_depends ${_uefi_driver_files})
        message(STATUS "UEFI network boot drivers: ${_uefi_driver_files}")
    endif()

    add_custom_target(efisys
        COMMAND native-fatten ${CMAKE_CURRENT_BINARY_DIR}/efisys.bin -format 5760 EFIBOOT
            ${_efisys_boot_options}
            -mkdir EFI -mkdir EFI/BOOT -add $<TARGET_FILE:uefildr> EFI/BOOT/boot${EFI_PLATFORM_ID}.efi
            ${_uefi_driver_fatten_options}
        DEPENDS ${_efisys_depends}
        VERBATIM)
endif()


# Create an 'empty' directory (guaranteed to be empty) to be able to add
# arbitrary empty directories to the ISO image using mkisofs.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/empty)

# Retrieve the full paths to the generated boot files.
if(FREELDR_HAS_BIOS_BOOT)
    set(_isombr_file  ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/isombr.bin)  # get_target_property(_isombr_file  isombr  LOCATION)
    set(_isoboot_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/isoboot.bin) # get_target_property(_isoboot_file isoboot LOCATION)
    set(_isobtrt_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/isobtrt.bin) # get_target_property(_isobtrt_file isobtrt LOCATION)
endif()
if(DEFINED EFI_PLATFORM_ID)
    set(_efisys_file  ${CMAKE_CURRENT_BINARY_DIR}/efisys.bin) # get_target_property(_efisys_file  efisys  LOCATION)
endif()

# Create an mkisofs sort file that defines an explicit ordering to place the
# boot files at the beginning of the ISO image, making its analysis easier.
# See mkisofs/schilytools/mkisofs/README.sort for more details.
#
# As the default file sort weight is '0', give the boot files sort weights >= 1.
# Note that, sadly, '-sort' does not work using grafted points. As a result, we
# need in particular to use the boot catalog file "path" that mkisofs expects.
# Indeed, the boot catalog file name is appended to the first host-system path
# listed in the file list (whatever it is), and this does not work well if the
# first item is a graft point, especially if it's a file and not a directory.
#
# To fix that, the trick is to use, as the first file item, the empty directory
# created earlier. This ensures that:
# - the boot catalog file path is meaningful;
# - since its contents are included by mkisofs at the root of the ISO image,
#   using the empty directory ensures that no extra unwanted files are added.
#
set(ISO_SORT_FILE_DATA "\
${CMAKE_CURRENT_BINARY_DIR}/empty/boot.catalog 4
")
if(FREELDR_HAS_BIOS_BOOT)
    string(APPEND ISO_SORT_FILE_DATA "${_isoboot_file} 3\n${_isobtrt_file} 2\n")
endif()
if(DEFINED EFI_PLATFORM_ID)
    string(APPEND ISO_SORT_FILE_DATA "${_efisys_file} 1\n")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort ${ISO_SORT_FILE_DATA})

# ISO image identifier names
set(ISO_MANUFACTURER "ReactOS Project") # For both the publisher and the preparer
set(ISO_VOLNAME      "ReactOS")         # For both the Volume ID and the Volume set ID

# ISO image options
# Preserve Windows DLL names such as avformat-62.dll in the primary ISO directory.
set(ISO_COMMON_OPTIONS
    -iso-level 4 -relaxed-filenames -publisher ${ISO_MANUFACTURER} -preparer ${ISO_MANUFACTURER} -volid ${ISO_VOLNAME} -volset ${ISO_VOLNAME})
set(ISO_LAYOUT_OPTIONS
    -duplicates-once -no-cache-inodes -graft-points)
# Boot catalog and sorting files options
set(ISO_BOOT_FILES_OPTIONS
    -hide boot.catalog -sort ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort)

## "El Torito" ISO boot options
# ISO_BOOT_OPTIONS; ISO_BOOT_OPTIONS_REGTEST (only for BootCDRegTest)

# BIOS-based PC boot entry (x86/x64 only)
if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    set(ISO_BOOT_OPTIONS
        -eltorito-platform x86 -eltorito-boot loader/isoboot.bin -no-emul-boot -boot-load-size 4)
    set(ISO_BOOT_OPTIONS_REGTEST
        -eltorito-platform x86 -eltorito-boot loader/isobtrt.bin -no-emul-boot -boot-load-size 4)
endif()

# EFI boot entry
if(DEFINED EFI_PLATFORM_ID)
    set(ISO_BOOT_EFI_OPTIONS
        -eltorito-platform efi -eltorito-boot loader/efisys.bin -no-emul-boot)

    if(DEFINED ISO_BOOT_OPTIONS)
        list(APPEND ISO_BOOT_OPTIONS -eltorito-alt-boot)
        list(APPEND ISO_BOOT_OPTIONS_REGTEST -eltorito-alt-boot)
    endif()
    list(APPEND ISO_BOOT_OPTIONS ${ISO_BOOT_EFI_OPTIONS})
    list(APPEND ISO_BOOT_OPTIONS_REGTEST ${ISO_BOOT_EFI_OPTIONS})
endif()

set(ISOHYBRID_DEPENDS)
set(ISOHYBRID_BOOTCD_COMMAND)
set(ISOHYBRID_BOOTCDREGTEST_COMMAND)
set(ISOHYBRID_LIVECD_COMMAND)
if(FREELDR_HAS_BIOS_BOOT)
    set(ISOHYBRID_DEPENDS isombr native-isohybrid)
    set(ISOHYBRID_BOOTCD_COMMAND
        COMMAND native-isohybrid -b ${_isombr_file} -t 0x96 ${REACTOS_BINARY_DIR}/bootcd.iso)
    set(ISOHYBRID_BOOTCDREGTEST_COMMAND
        COMMAND native-isohybrid -b ${_isombr_file} -t 0x96 ${REACTOS_BINARY_DIR}/bootcdregtest.iso)
    set(ISOHYBRID_LIVECD_COMMAND
        COMMAND native-isohybrid -b ${_isombr_file} -t 0x96 ${REACTOS_BINARY_DIR}/livecd.iso)
endif()


# Create user profile directories in the LiveImage
function(add_allusers_profile_dirs _image_filelist _rootdir)
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Application Data=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Documents/My Music=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Documents/My Pictures=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Documents/My Videos=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Favorites=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/My Documents=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Start Menu/Programs/Startup=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Templates=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
endfunction()
function(add_user_profile_dirs _image_filelist _rootdir _username)
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Application Data=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Application Data/Microsoft/Internet Explorer/Quick Launch=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Cookies=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Desktop=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Favorites=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Local Settings/Application Data=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Local Settings/History=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Local Settings/Temporary Internet Files=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/My Documents=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/My Music=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/My Pictures=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/My Videos=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/NetHood=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/PrintHood=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Recent=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/SendTo=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Start Menu/Programs=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Start Menu/Programs/Administrative Tools=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Start Menu/Programs/Startup=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Templates=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
endfunction()


## BootCD
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootcd.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")

add_custom_target(bootcd
    COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/bootcd.iso
        ${ISO_COMMON_OPTIONS} ${ISO_BOOT_OPTIONS} ${ISO_BOOT_FILES_OPTIONS} ${ISO_LAYOUT_OPTIONS}
        -path-list ${CMAKE_CURRENT_BINARY_DIR}/bootcd.$<CONFIG>.lst
    ${ISOHYBRID_BOOTCD_COMMAND}
    DEPENDS ${ISOHYBRID_DEPENDS} native-mkisofs livecd
    VERBATIM)

## BootCDRegTest
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootcdregtest.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")

add_custom_target(bootcdregtest
    COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/bootcdregtest.iso
        ${ISO_COMMON_OPTIONS} ${ISO_BOOT_OPTIONS_REGTEST} ${ISO_BOOT_FILES_OPTIONS} ${ISO_LAYOUT_OPTIONS}
        -path-list ${CMAKE_CURRENT_BINARY_DIR}/bootcdregtest.$<CONFIG>.lst
    ${ISOHYBRID_BOOTCDREGTEST_COMMAND}
    DEPENDS ${ISOHYBRID_DEPENDS} native-mkisofs
    VERBATIM)

## LiveImage -- Constitutes a small RAMDISK ISO, and is also merged with the BootCD
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")
# Create TEMP directory
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "reactos/TEMP=${CMAKE_CURRENT_BINARY_DIR}/empty\n")

# Create user profile directories
add_allusers_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "Profiles")
add_user_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "Profiles" "Default User")

if(FREELDR_WIM_RAMDISK)
    set(_livecd_stage_dir  ${CMAKE_CURRENT_BINARY_DIR}/livecd_wim_stage)
    set(_livecd_boot_wim   ${CMAKE_CURRENT_BINARY_DIR}/boot.wim)
    set(_livecd_wim_lst    ${CMAKE_CURRENT_BINARY_DIR}/livecd_wim.$<CONFIG>.lst)
    set(_livecd_ini_src    ${REACTOS_SOURCE_DIR}/boot/bootdata/wim/livecd_wim.ini)
    set(_livecd_wim_ini    ${CMAKE_CURRENT_BINARY_DIR}/livecd_wim.freeldr.ini)
    set(_livecd_wim_script ${REACTOS_SOURCE_DIR}/boot/bootdata/wim/make_livecd_wim.cmake)

    add_custom_target(livecd
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_LIST=${CMAKE_CURRENT_BINARY_DIR}/livecd.$<CONFIG>.lst
            -DSTAGE_DIR=${_livecd_stage_dir}
            -DOUTPUT_WIM=${_livecd_boot_wim}
            -DOUTPUT_LIST=${_livecd_wim_lst}
            -DWIMAGE_EXE=$<TARGET_FILE:native-wimage>
            -DSOURCE_FREELDR_INI=${_livecd_ini_src}
            -DOUTPUT_FREELDR_INI=${_livecd_wim_ini}
            -P ${_livecd_wim_script}
        COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/livecd.iso
            ${ISO_COMMON_OPTIONS} ${ISO_BOOT_OPTIONS} ${ISO_BOOT_FILES_OPTIONS} ${ISO_LAYOUT_OPTIONS}
            -path-list ${_livecd_wim_lst}
        ${ISOHYBRID_LIVECD_COMMAND}
        DEPENDS ${ISOHYBRID_DEPENDS} native-mkisofs native-wimage ${_livecd_ini_src} ${_livecd_wim_script}
        VERBATIM)
else()
    add_custom_target(livecd
        COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/livecd.iso
            ${ISO_COMMON_OPTIONS} ${ISO_BOOT_OPTIONS} ${ISO_BOOT_FILES_OPTIONS} ${ISO_LAYOUT_OPTIONS}
            -path-list ${CMAKE_CURRENT_BINARY_DIR}/livecd.$<CONFIG>.lst
        ${ISOHYBRID_LIVECD_COMMAND}
        DEPENDS ${ISOHYBRID_DEPENDS} native-mkisofs
        VERBATIM)
endif()


## ReactOSImg
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/preinstall.cmake.lst "")
# The NTFS image importer requires every entry to map an image path to a host path.

set(_preinstall_boot_partition_file ${CMAKE_CURRENT_BINARY_DIR}/partition.boot.fat)
set(_preinstall_system_partition_file ${CMAKE_CURRENT_BINARY_DIR}/partition.ntfs)
set(_preinstall_image_file ${REACTOS_BINARY_DIR}/ReactOS.img)
set(_preinstall_vhd_file ${REACTOS_BINARY_DIR}/ReactOS.vhd)
# Keep ROSBOOT as an active MBR ESP. UEFI discovers it by type, while the BIOS
# MBR follows the active flag and loads its FAT32 boot sector. The Raspberry Pi
# 1-3 boot ROM only scans for FAT MBR ids and skips 0xEF, so the ARM images
# mark the same volume as FAT32 LBA instead; UEFI mounts it by content.
if(ARCH MATCHES "^arm")
    set(_preinstall_boot_partition_type 0c)
else()
    set(_preinstall_boot_partition_type ef)
endif()

# Create TEMP dir
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/preinstall.cmake.lst "reactos/TEMP=${CMAKE_CURRENT_BINARY_DIR}/empty\n")

# Create installed-system directories that second-stage setup normally creates.
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/preinstall.cmake.lst "Program Files=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/preinstall.cmake.lst "Program Files/Common Files=${CMAKE_CURRENT_BINARY_DIR}/empty\n")

# Create user profile directories
add_allusers_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/preinstall.cmake.lst "Profiles")
add_user_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/preinstall.cmake.lst "Profiles" "Default User")

# Optional build-local payload for preinstalled disk images. Each non-comment
# line uses the same "image/path=host/path" format as preinstall.cmake.lst.
# Keep external payloads outside the source tree while still making their
# contents and dependencies part of the generated image build graph.
set(PREINSTALL_EXTRA_FILE_LIST "" CACHE FILEPATH
    "Additional file manifest for preinstalled ReactOS disk images")
set(_preinstall_overlay_deps)
if(PREINSTALL_EXTRA_FILE_LIST)
    get_filename_component(_preinstall_extra_file_list
        "${PREINSTALL_EXTRA_FILE_LIST}" ABSOLUTE BASE_DIR "${REACTOS_BINARY_DIR}")
    if(NOT EXISTS "${_preinstall_extra_file_list}")
        message(FATAL_ERROR "PREINSTALL_EXTRA_FILE_LIST does not exist: ${_preinstall_extra_file_list}")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_preinstall_extra_file_list}")
    file(STRINGS "${_preinstall_extra_file_list}" _preinstall_extra_entries)
    get_filename_component(_preinstall_extra_base "${_preinstall_extra_file_list}" DIRECTORY)
    foreach(_preinstall_extra_entry IN LISTS _preinstall_extra_entries)
        if(_preinstall_extra_entry STREQUAL "" OR _preinstall_extra_entry MATCHES "^[ \t]*#")
            continue()
        endif()

        string(FIND "${_preinstall_extra_entry}" "=" _preinstall_extra_separator)
        if(_preinstall_extra_separator LESS 1)
            message(FATAL_ERROR "Invalid preinstall overlay entry: ${_preinstall_extra_entry}")
        endif()

        string(SUBSTRING "${_preinstall_extra_entry}" 0 ${_preinstall_extra_separator} _preinstall_extra_destination)
        math(EXPR _preinstall_extra_source_offset "${_preinstall_extra_separator} + 1")
        string(SUBSTRING "${_preinstall_extra_entry}" ${_preinstall_extra_source_offset} -1 _preinstall_extra_source)
        get_filename_component(_preinstall_extra_source "${_preinstall_extra_source}" ABSOLUTE BASE_DIR "${_preinstall_extra_base}")
        if(NOT EXISTS "${_preinstall_extra_source}")
            message(FATAL_ERROR "Preinstall overlay source does not exist: ${_preinstall_extra_source}")
        endif()

        set_property(GLOBAL APPEND PROPERTY PREINSTALL_OVERLAY_FILE_LIST
            "${_preinstall_extra_destination}=${_preinstall_extra_source}")
        list(APPEND _preinstall_overlay_deps "${_preinstall_extra_source}")
    endforeach()
    list(APPEND _preinstall_overlay_deps "${_preinstall_extra_file_list}")
endif()

# Disk image size configuration (in MB)
set(_preinstall_image_size_default 512)
set(PREINSTALL_IMAGE_SIZE_MB ${_preinstall_image_size_default} CACHE STRING "Boot and system area size in MB; the private crash-dump partition is additional")
set(PREINSTALL_CRASH_DUMP_SIZE_MB 64 CACHE STRING "Private raw crash-dump partition size in MB")
if(PREINSTALL_CRASH_DUMP_SIZE_MB LESS 1)
    message(FATAL_ERROR "PREINSTALL_CRASH_DUMP_SIZE_MB must be at least 1 MB")
endif()
set(_rosprofiler_package_pdbs_default OFF)
if(MSVC AND (CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug$" OR
             CMAKE_CONFIGURATION_TYPES))
    set(_rosprofiler_package_pdbs_default ON)
endif()
option(ROSPROFILER_PACKAGE_IMAGE_PDBS
       "Package budgeted PDBs in Debug preinstalled images"
       ${_rosprofiler_package_pdbs_default})
set(ROSPROFILER_IMAGE_PDB_BUDGET_MB 160 CACHE STRING
    "Maximum PDB payload in the preinstalled image")
set(ROSPROFILER_IMAGE_FREE_RESERVE_MB 32 CACHE STRING
    "Free-space reserve kept after adding profiler PDBs")
set(ROSPROFILER_IMAGE_FS_OVERHEAD_MB 4 CACHE STRING
    "Conservative filesystem and directory overhead reserved for profiler symbols")
set(_rosprofiler_image_symbol_dir
    ${CMAKE_CURRENT_BINARY_DIR}/rosprofiler-image-symbols)
set(_rosprofiler_embedded_rossym OFF)
if(NOT MSVC AND NOT NO_ROSSYM)
    set(_rosprofiler_embedded_rossym ON)
endif()
if(MSVC)
    set(_rosprofiler_pdb_dir ${REACTOS_BINARY_DIR}/msvc_pdb)
else()
    set(_rosprofiler_pdb_dir ${REACTOS_BINARY_DIR}/symbols)
endif()
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/preinstall.cmake.lst
     "reactos/symbols=${_rosprofiler_image_symbol_dir}\n")
# Keep a small FAT volume for BIOS/UEFI boot files and use NTFS for ReactOS.
# Both partitions start on 1-MB boundaries.
set(_preinstall_boot_partition_size_mb 64)
math(EXPR _preinstall_boot_partition_sectors "${_preinstall_boot_partition_size_mb} * 2048")
math(EXPR _preinstall_system_partition_start "(1 + ${_preinstall_boot_partition_size_mb}) * 2048")
# Keep the boot-tested 4-KB NTFS allocation unit instead of the formatter's
# automatic 1-KB choice for a volume of this size.
set(_preinstall_ntfs_sectors_per_cluster 8)
math(EXPR _preinstall_system_partition_size_mb "${PREINSTALL_IMAGE_SIZE_MB} - 1 - ${_preinstall_boot_partition_size_mb}")
if(_preinstall_system_partition_size_mb LESS 1)
    message(FATAL_ERROR "PREINSTALL_IMAGE_SIZE_MB must leave room for the 1-MB alignment gap, the ${_preinstall_boot_partition_size_mb}-MB boot partition, and the NTFS system partition")
endif()
math(EXPR _preinstall_system_partition_sectors "${_preinstall_system_partition_size_mb} * 2048")
math(EXPR _preinstall_crash_dump_partition_start "${PREINSTALL_IMAGE_SIZE_MB} * 2048")
math(EXPR _preinstall_crash_dump_partition_sectors "${PREINSTALL_CRASH_DUMP_SIZE_MB} * 2048")

# BIOS boot-sector binaries are available on x86/x64. UEFI platforms also put
# their removable-media loader in the FAT boot partition.
set(_preinstall_boot_partition_options)
set(_preinstall_boot_partition_fs fat)
set(_preinstall_boot_partition_files
    -add ${FREELDR_PREINSTALL_INI} freeldr.ini)
file(GLOB _preinstall_rpi_firmware ${REACTOS_SOURCE_DIR}/media/boot/rpi/*)
foreach(_rpi_firmware_file ${_preinstall_rpi_firmware})
    if(NOT IS_DIRECTORY ${_rpi_firmware_file})
        get_filename_component(_rpi_firmware_name ${_rpi_firmware_file} NAME)
        list(APPEND _preinstall_boot_partition_files
            -add ${_rpi_firmware_file} ${_rpi_firmware_name})
    endif()
endforeach()
# config.txt dtoverlay= lines resolve against overlays/ on the boot volume.
file(GLOB _preinstall_rpi_overlays ${REACTOS_SOURCE_DIR}/media/boot/rpi/overlays/*)
if(_preinstall_rpi_overlays)
    list(APPEND _preinstall_boot_partition_files -mkdir overlays)
    foreach(_rpi_overlay_file ${_preinstall_rpi_overlays})
        if(NOT IS_DIRECTORY ${_rpi_overlay_file})
            get_filename_component(_rpi_overlay_name ${_rpi_overlay_file} NAME)
            list(APPEND _preinstall_boot_partition_files
                -add ${_rpi_overlay_file} overlays/${_rpi_overlay_name})
        endif()
    endforeach()
endif()
set(_preinstall_partition_deps native-fatten native-ntfsimg ${_preinstall_overlay_deps})
set(_reactosimg_mbr_args)
set(_reactosimg_deps native-mkdiskimg)
if(FREELDR_HAS_BIOS_BOOT)
    set(_dosmbr_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/dosmbr.bin)
    set(_fat32_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/fat32.bin)
    set(_freeldr_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/freeldr/freeldr.sys)
    set(_preinstall_boot_partition_fs fat32)
    list(APPEND _preinstall_boot_partition_options -boot ${_fat32_file})
    list(APPEND _preinstall_boot_partition_files
        -add ${_freeldr_file} freeldr.sys
        -add $<TARGET_FILE:rosload> rosload.exe)
    list(APPEND _preinstall_partition_deps fat32 freeldr rosload)
    list(APPEND _reactosimg_mbr_args -mbr ${_dosmbr_file})
    list(APPEND _reactosimg_deps dosmbr)
endif()
if(DEFINED EFI_PLATFORM_ID)
    list(APPEND _preinstall_boot_partition_files
        -mkdir EFI
        -mkdir EFI/BOOT
        -add $<TARGET_FILE:uefildr> EFI/BOOT/boot${EFI_PLATFORM_ID}.efi)
    list(APPEND _preinstall_partition_deps uefildr)
    if(_uefi_driver_files)
        list(APPEND _preinstall_boot_partition_files -mkdir EFI/BOOT/drivers)
        foreach(_driver ${_uefi_driver_files})
            get_filename_component(_driver_name "${_driver}" NAME)
            list(APPEND _preinstall_boot_partition_files -add "${_driver}" "EFI/BOOT/drivers/${_driver_name}")
        endforeach()
        list(APPEND _preinstall_partition_deps ${_uefi_driver_files})
    endif()
endif()

add_custom_target(preinstall_partition
    COMMAND ${CMAKE_COMMAND} -E rm -f
        ${_preinstall_boot_partition_file}
        ${_preinstall_system_partition_file}
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_DIR=${_rosprofiler_image_symbol_dir}
        -DPREINSTALL_LIST=${CMAKE_CURRENT_BINARY_DIR}/preinstall.$<CONFIG>.lst
        -DIMAGE_SIZE_MB=${_preinstall_system_partition_size_mb}
        -DRESERVE_MB=${ROSPROFILER_IMAGE_FREE_RESERVE_MB}
        -DFS_OVERHEAD_MB=${ROSPROFILER_IMAGE_FS_OVERHEAD_MB}
        -DMAX_SYMBOL_MB=${ROSPROFILER_IMAGE_PDB_BUDGET_MB}
        -DPDB_DIR=${_rosprofiler_pdb_dir}
        -DPACKAGE_PDBS=${ROSPROFILER_PACKAGE_IMAGE_PDBS}
        -DEMBEDDED_ROSSYM=${_rosprofiler_embedded_rossym}
        -P ${REACTOS_SOURCE_DIR}/boot/pack_rosprofiler_symbols.cmake
    COMMAND native-fatten ${_preinstall_boot_partition_file}
        -format ${_preinstall_boot_partition_sectors} ${_preinstall_boot_partition_fs} ROSBOOT
        ${_preinstall_boot_partition_options}
        ${_preinstall_boot_partition_files}
    COMMAND native-ntfsimg
        --format
        ${_preinstall_system_partition_file}
        ${_preinstall_system_partition_sectors}
        ${_preinstall_system_partition_start}
        ${_preinstall_ntfs_sectors_per_cluster}
        ReactOS
    COMMAND native-ntfsimg
        --addfiles
        ${_preinstall_system_partition_file}
        ${CMAKE_CURRENT_BINARY_DIR}/preinstall.$<CONFIG>.lst
    DEPENDS ${_preinstall_partition_deps}
    VERBATIM)

add_custom_target(reactosimg
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_preinstall_image_file}
    COMMAND native-mkdiskimg
        -o ${_preinstall_image_file}
        ${_reactosimg_mbr_args}
        -partition ${_preinstall_boot_partition_file}
        -start 2048
        -type ${_preinstall_boot_partition_type}
        -partition ${_preinstall_system_partition_file}
        -start ${_preinstall_system_partition_start}
        -type 07
        -blank ${_preinstall_crash_dump_partition_sectors}
        -start ${_preinstall_crash_dump_partition_start}
        -type 7f
    DEPENDS ${_reactosimg_deps}
    VERBATIM)
add_dependencies(reactosimg preinstall_partition)

add_custom_target(reactosvhd
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_preinstall_vhd_file}
    COMMAND native-mkdiskimg
        -o ${_preinstall_vhd_file}
        ${_reactosimg_mbr_args}
        -partition ${_preinstall_boot_partition_file}
        -start 2048
        -type ${_preinstall_boot_partition_type}
        -partition ${_preinstall_system_partition_file}
        -start ${_preinstall_system_partition_start}
        -type 07
        -blank ${_preinstall_crash_dump_partition_sectors}
        -start ${_preinstall_crash_dump_partition_start}
        -type 7f
        -vhd
    DEPENDS ${_reactosimg_deps}
    VERBATIM)
add_dependencies(reactosvhd preinstall_partition)


if(DEFINED EFI_PLATFORM_ID)
    # For devices such as USB drives, add also the EFI boot image into efi/boot.
    add_cd_file(TARGET efisys FILE ${CMAKE_CURRENT_BINARY_DIR}/efisys.bin DESTINATION loader NO_CAB FOR bootcd livecd regtest)
    add_cd_file(TARGET uefildr DESTINATION efi/boot NO_CAB NAME_ON_CD boot${EFI_PLATFORM_ID}.efi FOR bootcd livecd regtest)
    if(_uefi_driver_files)
        foreach(_driver ${_uefi_driver_files})
            get_filename_component(_driver_name "${_driver}" NAME)
            add_cd_file(FILE "${_driver}" DESTINATION efi/boot/drivers NO_CAB NAME_ON_CD "${_driver_name}" FOR bootcd livecd regtest)
        endforeach()
    endif()
endif()
