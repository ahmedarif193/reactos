## efisys.bin

# EFI platform ID, used in environ/CMakelists.txt for bootmgfw filename naming also.
# UEFI bootloader is not built for i386
if(ARCH STREQUAL "amd64")
    set(EFI_PLATFORM_ID "x64")
elseif(ARCH STREQUAL "ia64")
    set(EFI_PLATFORM_ID "ia64")
elseif(ARCH STREQUAL "arm")
    set(EFI_PLATFORM_ID "arm")
elseif(ARCH STREQUAL "arm64")
    set(EFI_PLATFORM_ID "aa64")
elseif(NOT ARCH STREQUAL "i386")
    message(FATAL_ERROR "Unknown ARCH '" ${ARCH} "', cannot generate a valid UEFI boot filename.")
endif()

if(DEFINED EFI_PLATFORM_ID)
    add_custom_target(efisys
        COMMAND native-fatten ${CMAKE_CURRENT_BINARY_DIR}/efisys.bin -format 2880 EFIBOOT
            -boot ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/fat.bin
            -mkdir EFI -mkdir EFI/BOOT -add $<TARGET_FILE:uefildr> EFI/BOOT/boot${EFI_PLATFORM_ID}.efi
        DEPENDS native-fatten fat uefildr
        VERBATIM)
endif()

# ISO image EFI boot parameters
set(ISO_EFI_BOOT_PARAMS)

# Create an 'empty' directory (guaranteed to be empty) to be able to add
# arbitrary empty directories to the ISO image using mkisofs.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/empty)

# Retrieve the full paths to the generated files of the 'isombr', 'isoboot', 'isobtrt' and 'efisys' targets
set(_isombr_file  ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/isombr.bin)  # get_target_property(_isombr_file  isombr  LOCATION)
set(_isoboot_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/isoboot.bin) # get_target_property(_isoboot_file isoboot LOCATION)
set(_isobtrt_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/isobtrt.bin) # get_target_property(_isobtrt_file isobtrt LOCATION)
if(DEFINED EFI_PLATFORM_ID)
    set(_efisys_file  ${CMAKE_CURRENT_BINARY_DIR}/efisys.bin) # get_target_property(_efisys_file  efisys  LOCATION)
    list(APPEND ISO_EFI_BOOT_PARAMS -eltorito-alt-boot -eltorito-platform efi -eltorito-boot loader/efisys.bin -no-emul-boot)
endif()

# Create a mkisofs sort file to specify an explicit ordering for the boot files
# to place them at the beginning of the image (makes ISO image analysis easier).
# See mkisofs/schilytools/mkisofs/README.sort for more details.
# As the default file sort weight is '0', give the boot files sort weights >= 1.
# Note that it is sad that '-sort' does not work using grafted points, and as a
# result we need in particular to use the boot catalog file "path" mkisofs that
# mkisofs expects, that is, the boot catalog file name is appended to the first
# host-system path listed in the file list, whatever it is, and that does not
# work well if the first item is a graft point (and especially if it's a file
# and not a directory). To fix that, the trick is to use as the first file item
# the empty directory created earlier. This ensures that:
# - the boot catalog file path is meaningful;
# - since its contents are included by mkisofs in the root of the ISO image,
#   using the empty directory ensures that no extra unwanted files are added.
#
set(ISO_SORT_FILE_DATA "\
${CMAKE_CURRENT_BINARY_DIR}/empty/boot.catalog 4
${_isoboot_file} 3
${_isobtrt_file} 2
")
if(DEFINED EFI_PLATFORM_ID)
    string(APPEND ISO_SORT_FILE_DATA "${_efisys_file} 1\n")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort ${ISO_SORT_FILE_DATA})

# ISO image identifier names
set(ISO_MANUFACTURER "ReactOS Project") # For both the publisher and the preparer
set(ISO_VOLNAME      "ReactOS")         # For both the Volume ID and the Volume set ID


# Create user profile directories in the LiveImage
function(add_allusers_profile_dirs _image_filelist _rootdir)
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Application Data=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Documents/My Music=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Documents/My Pictures=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Documents/My Videos=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Favorites=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/My Documents=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/All Users/Start Menu/Programs/StartUp=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
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
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/My Music=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/My Pictures=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/My Videos=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/NetHood=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/PrintHood=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Recent=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/SendTo=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Start Menu/Programs=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Start Menu/Programs/Administrative Tools=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Start Menu/Programs/StartUp=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
    file(APPEND ${_image_filelist} "${_rootdir}/${_username}/Templates=${CMAKE_CURRENT_BINARY_DIR}/empty\n")
endfunction()


## BootCD
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootcd.cmake.lst "")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootcd.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")

add_custom_target(bootcd
    COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/bootcd.iso -iso-level 4
        -publisher ${ISO_MANUFACTURER} -preparer ${ISO_MANUFACTURER} -volid ${ISO_VOLNAME} -volset ${ISO_VOLNAME}
        -eltorito-boot loader/isoboot.bin -no-emul-boot -boot-load-size 4 ${ISO_EFI_BOOT_PARAMS} -hide boot.catalog
        -sort ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort
        -no-cache-inodes -graft-points -path-list ${CMAKE_CURRENT_BINARY_DIR}/bootcd.$<CONFIG>.lst
    COMMAND native-isohybrid -b ${_isombr_file} -t 0x96 ${REACTOS_BINARY_DIR}/bootcd.iso
    DEPENDS isombr native-isohybrid native-mkisofs
    VERBATIM)

## BootCDRegTest
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootcdregtest.cmake.lst "")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bootcdregtest.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")

add_custom_target(bootcdregtest
    COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/bootcdregtest.iso -iso-level 4
        -publisher ${ISO_MANUFACTURER} -preparer ${ISO_MANUFACTURER} -volid ${ISO_VOLNAME} -volset ${ISO_VOLNAME}
        -eltorito-boot loader/isobtrt.bin -no-emul-boot -boot-load-size 4 ${ISO_EFI_BOOT_PARAMS} -hide boot.catalog
        -sort ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort
        -no-cache-inodes -graft-points -path-list ${CMAKE_CURRENT_BINARY_DIR}/bootcdregtest.$<CONFIG>.lst
    COMMAND native-isohybrid -b ${_isombr_file} -t 0x96 ${REACTOS_BINARY_DIR}/bootcdregtest.iso
    DEPENDS isombr native-isohybrid native-mkisofs
    VERBATIM)

## LiveCD
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")

# Create TEMP dir
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "reactos/TEMP=${CMAKE_CURRENT_BINARY_DIR}/empty\n")

# Create user profile directories
add_allusers_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "Profiles")
add_user_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "Profiles" "Default User")

add_custom_target(livecd
    COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/livecd.iso -iso-level 4
        -publisher ${ISO_MANUFACTURER} -preparer ${ISO_MANUFACTURER} -volid ${ISO_VOLNAME} -volset ${ISO_VOLNAME}
        -eltorito-boot loader/isoboot.bin -no-emul-boot -boot-load-size 4 ${ISO_EFI_BOOT_PARAMS} -hide boot.catalog
        -sort ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort
        -no-cache-inodes -graft-points -path-list ${CMAKE_CURRENT_BINARY_DIR}/livecd.$<CONFIG>.lst
    COMMAND native-isohybrid -b ${_isombr_file} -t 0x96 ${REACTOS_BINARY_DIR}/livecd.iso
    DEPENDS isombr native-isohybrid native-mkisofs
    VERBATIM)

# Optional LiveUSB helper bundle: copy the ISO alongside a pre-formatted
# writable FAT image that can be appended on removable media.
set(LIVEUSB_RW_SIZE_MB 64)
math(EXPR LIVEUSB_RW_SECTORS "${LIVEUSB_RW_SIZE_MB} * 1024 * 1024 / 512")
set(LIVEUSB_RW_LABEL "REACTOS_RW")
set(LIVEUSB_OUTPUT_DIR ${REACTOS_BINARY_DIR})
set(LIVEUSB_ISO_COPY ${LIVEUSB_OUTPUT_DIR}/liveusb.iso)
set(LIVEUSB_RW_IMAGE ${LIVEUSB_OUTPUT_DIR}/liveusb_rw.fat)

add_custom_command(
    OUTPUT ${LIVEUSB_ISO_COPY}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${REACTOS_BINARY_DIR}/livecd.iso ${LIVEUSB_ISO_COPY}
    DEPENDS livecd ${REACTOS_BINARY_DIR}/livecd.iso
    COMMENT "Copying livecd.iso to ${LIVEUSB_ISO_COPY}"
    VERBATIM)

add_custom_command(
    OUTPUT ${LIVEUSB_RW_IMAGE}
    COMMAND ${CMAKE_COMMAND} -E remove -f ${LIVEUSB_RW_IMAGE}
    COMMAND native-fatten ${LIVEUSB_RW_IMAGE} -format ${LIVEUSB_RW_SECTORS} ${LIVEUSB_RW_LABEL}
    DEPENDS native-fatten
    COMMENT "Generating ${LIVEUSB_RW_SIZE_MB} MiB writable FAT image"
    VERBATIM)

add_custom_target(liveusb
    DEPENDS ${LIVEUSB_ISO_COPY} ${LIVEUSB_RW_IMAGE})

## HybridCD
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/hybridcd.cmake.lst "")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/hybridcd.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")

# SysWOW64 Binary Mirroring for WOW64 support on amd64
# This section populates reactos/SysWOW64 with 32-bit binaries for WOW64 emulation
if(ARCH STREQUAL "amd64")
    # Search for i386 build output in common locations
    set(_I386_SEARCH_PATHS
        "${REACTOS_BINARY_DIR}/../output-MinGW-i386-Release"
        "${REACTOS_BINARY_DIR}/../output-MinGW-i386-Debug"
        "${REACTOS_BINARY_DIR}/../build-i386/output-MinGW-i386-Release"
        "${REACTOS_BINARY_DIR}/../build-i386/output-MinGW-i386-Debug"
        "$ENV{REACTOS_I386_ROOT}"
    )

    set(_I386_SYSROOT "")
    foreach(_search_path IN LISTS _I386_SEARCH_PATHS)
        if(EXISTS "${_search_path}/reactos/system32")
            set(_I386_SYSROOT "${_search_path}/reactos")
            message(STATUS "WOW64: Found i386 binaries at ${_I386_SYSROOT}")
            break()
        endif()
    endforeach()

    if(_I386_SYSROOT)
        # Define file extensions to exclude (developer artifacts)
        set(_SYSWOW64_SKIP_EXT
            ".a" ".lib" ".pdb" ".exp" ".map" ".obj" ".ilk" ".idb" ".log"
            ".txt" ".cmake" ".py" ".pl" ".bat" ".sh" ".c" ".cpp" ".h"
            ".hpp" ".idl" ".inf" ".ini" ".md")

        # Collect 32-bit binaries from system32 directory
        file(GLOB_RECURSE _SYSWOW64_SOURCE LIST_DIRECTORIES FALSE
            RELATIVE "${_I386_SYSROOT}/system32" "${_I386_SYSROOT}/system32/*")

        foreach(_rel_path IN LISTS _SYSWOW64_SOURCE)
            set(_src "${_I386_SYSROOT}/system32/${_rel_path}")
            string(REGEX MATCH "\\.[^.]*$" _rel_ext "${_rel_path}")
            string(TOLOWER "${_rel_ext}" _rel_ext)

            # Skip developer artifacts
            set(_skip_file FALSE)
            foreach(_bad_ext IN LISTS _SYSWOW64_SKIP_EXT)
                if(_rel_ext STREQUAL _bad_ext)
                    set(_skip_file TRUE)
                    break()
                endif()
            endforeach()

            if(_skip_file)
                continue()
            endif()

            # Mirror to SysWOW64 directory
            set(_dest "reactos/SysWOW64/${_rel_path}")
            file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/bootcd.cmake.lst "${_dest}=${_src}\n")
            file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/bootcdregtest.cmake.lst "${_dest}=${_src}\n")
            file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "${_dest}=${_src}\n")
            file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/liveimg.cmake.lst "${_dest}=${_src}\n")
            file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/hybridcd.cmake.lst "${_dest}=${_src}\n")
            set_property(GLOBAL APPEND PROPERTY HYBRIDCD_FILE_LIST "${_dest}=${_src}")
        endforeach()

        # Also collect SxS assemblies for 32-bit (if they exist)
        if(EXISTS "${_I386_SYSROOT}/winsxs")
            file(GLOB_RECURSE _SYSWOW64_SXS_SOURCE LIST_DIRECTORIES FALSE
                RELATIVE "${_I386_SYSROOT}/winsxs" "${_I386_SYSROOT}/winsxs/*")

            foreach(_rel_path IN LISTS _SYSWOW64_SXS_SOURCE)
                set(_src "${_I386_SYSROOT}/winsxs/${_rel_path}")
                string(REGEX MATCH "\\.[^.]*$" _rel_ext "${_rel_path}")
                string(TOLOWER "${_rel_ext}" _rel_ext)

                # Skip developer artifacts
                set(_skip_file FALSE)
                foreach(_bad_ext IN LISTS _SYSWOW64_SKIP_EXT)
                    if(_rel_ext STREQUAL _bad_ext)
                        set(_skip_file TRUE)
                        break()
                    endif()
                endforeach()

                if(_skip_file)
                    continue()
                endif()

                # Mirror to winsxs directory (SxS assemblies stay in winsxs)
                set(_dest "reactos/winsxs/${_rel_path}")
                file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/livecd.cmake.lst "${_dest}=${_src}\n")
                file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/liveimg.cmake.lst "${_dest}=${_src}\n")
                file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/hybridcd.cmake.lst "${_dest}=${_src}\n")
            endforeach()
        endif()
    else()
        message(STATUS "WOW64: No i386 binaries found. Set REACTOS_I386_ROOT to specify location.")
        message(STATUS "        SysWOW64 directory will be empty. 32-bit executables will not run.")
    endif()
endif()

# Create user profile directories
add_allusers_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/hybridcd.cmake.lst "livecd/Profiles")
add_user_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/hybridcd.cmake.lst "livecd/Profiles" "Default User")

add_custom_target(hybridcd
    COMMAND native-mkisofs -quiet -o ${REACTOS_BINARY_DIR}/hybridcd.iso -iso-level 4
        -publisher ${ISO_MANUFACTURER} -preparer ${ISO_MANUFACTURER} -volid ${ISO_VOLNAME} -volset ${ISO_VOLNAME}
        -eltorito-boot loader/isoboot.bin -no-emul-boot -boot-load-size 4 ${ISO_EFI_BOOT_PARAMS} -hide boot.catalog
        -sort ${CMAKE_CURRENT_BINARY_DIR}/bootfiles.sort
        -duplicates-once -no-cache-inodes -graft-points -path-list ${CMAKE_CURRENT_BINARY_DIR}/hybridcd.$<CONFIG>.lst
    COMMAND native-isohybrid -b ${_isombr_file} -t 0x96 ${REACTOS_BINARY_DIR}/hybridcd.iso
    DEPENDS bootcd livecd
    VERBATIM)

if(DEFINED EFI_PLATFORM_ID)
    # For things like flashing USB drives, we also add the efi file into efi/boot.
    add_cd_file(TARGET efisys FILE ${CMAKE_CURRENT_BINARY_DIR}/efisys.bin DESTINATION loader NO_CAB NOT_IN_HYBRIDCD FOR bootcd regtest livecd hybridcd)

    add_cd_file(
        TARGET uefildr
        DESTINATION efi/boot
        NO_CAB
        NAME_ON_CD boot${EFI_PLATFORM_ID}.efi
        FOR livecd hybridcd)
endif()

## LiveIMG
# Create the file list
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/liveimg.cmake.lst "")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/liveimg.cmake.lst "${CMAKE_CURRENT_BINARY_DIR}/empty\n")

# Create TEMP dir
file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/liveimg.cmake.lst "reactos/TEMP=${CMAKE_CURRENT_BINARY_DIR}/empty\n")

# Create user profile directories
add_allusers_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/liveimg.cmake.lst "Profiles")
add_user_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/liveimg.cmake.lst "Profiles" "Default User")

# LiveIMG: BIOS/UEFI bootable, RW FAT32 disk image with all bootcd and livecd files.
# - Combines bootcd and livecd file sets to create a complete RW system.
# - Adds boot code (dosmbr/fat32) and ensures UEFI loader is present.
# - Incremental: depends on the aggregated file list and collected deps.

# Aggregator for building everything needed by liveimg without invoking ISO tools.
add_custom_target(liveimg_deps)

add_custom_target(liveimg
    COMMAND /usr/bin/env bash ${CMAKE_SOURCE_DIR}/boot/tools/make_reactos_img.sh
            --mode fat32
            --output ${REACTOS_BINARY_DIR}/liveimg.img
            --size $<IF:$<CONFIG:Release>,400,600>
            --build-root ${REACTOS_BINARY_DIR}
            --list ${CMAKE_CURRENT_BINARY_DIR}/liveimg.$<CONFIG>.lst
    DEPENDS liveimg_deps dosmbr fat fat32 ${CMAKE_CURRENT_BINARY_DIR}/liveimg.$<CONFIG>.lst
    VERBATIM)
