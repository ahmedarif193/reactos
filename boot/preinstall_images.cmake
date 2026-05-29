file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/preinstall.cmake.lst "")

option(REACTOS_PREINSTALL_USE_NTFS "Build the preinstalled OS partition as NTFS instead of FAT32" OFF)
option(REACTOS_PREINSTALL_USE_GPT "Use GPT partitioning (hybrid MBR for BIOS) instead of legacy MBR" ON)

if(REACTOS_PREINSTALL_USE_NTFS)
    set(_preinstall_partition_file ${CMAKE_CURRENT_BINARY_DIR}/partition.ntfs)
    set(_preinstall_partition_type 07)
else()
    set(_preinstall_partition_file ${CMAKE_CURRENT_BINARY_DIR}/partition.fat32)
    set(_preinstall_partition_type 0C)
endif()
set(_preinstall_esp_file ${CMAKE_CURRENT_BINARY_DIR}/preinstall_esp.img)
set(_preinstall_image_file ${REACTOS_BINARY_DIR}/ReactOS.img)
set(_preinstall_vhd_file ${REACTOS_BINARY_DIR}/ReactOS.vhd)

file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/preinstall.cmake.lst "reactos/TEMP=${CMAKE_CURRENT_BINARY_DIR}/empty\n")

add_allusers_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/preinstall.cmake.lst "Profiles")
add_user_profile_dirs(${CMAKE_CURRENT_BINARY_DIR}/preinstall.cmake.lst "Profiles" "Default User")

set(PREINSTALL_IMAGE_SIZE_MB 1024 CACHE STRING "Pre-installed disk image size in MB")
set(PREINSTALL_ESP_SIZE_MB 32 CACHE STRING "Pre-installed EFI System Partition size in MB (0 disables ESP)")

if(DEFINED EFI_PLATFORM_ID AND PREINSTALL_ESP_SIZE_MB GREATER 0)
    set(_preinstall_with_esp TRUE)
    set(_esp_start_sector 2048)
    math(EXPR _esp_sectors "${PREINSTALL_ESP_SIZE_MB} * 2048")
    math(EXPR _os_start_sector "${_esp_start_sector} + ${_esp_sectors}")
    math(EXPR _preinstall_partition_sectors
        "(${PREINSTALL_IMAGE_SIZE_MB} - 1 - ${PREINSTALL_ESP_SIZE_MB}) * 2048")
    set(PREINSTALL_OS_PARTITION 2)
else()
    set(_preinstall_with_esp FALSE)
    set(_os_start_sector 2048)
    set(_esp_start_sector 0)
    set(_esp_sectors 0)
    math(EXPR _preinstall_partition_sectors "(${PREINSTALL_IMAGE_SIZE_MB} - 1) * 2048")
    set(PREINSTALL_OS_PARTITION 1)
endif()

configure_file(${REACTOS_SOURCE_DIR}/boot/bootdata/preinstall.ini.in
               ${REACTOS_BINARY_DIR}/boot/bootdata/preinstall.ini @ONLY)

set(_dosmbr_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/dosmbr.bin)
set(_ntfs_vbr_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/ntfs.bin)
set(_fat32_vbr_file ${CMAKE_CURRENT_BINARY_DIR}/freeldr/bootsect/fat32.bin)
set(_preinstall_ntfs_script ${REACTOS_SOURCE_DIR}/boot/bootdata/make_preinstall_ntfs.cmake)

# Hive files copied into the partition by preinstall.<CONFIG>.lst. These are
# not visible to ninja through the .lst (it only carries paths), so list them
# explicitly as file-level deps; otherwise edits to hivesys.inf / hivesft.inf
# / hivecls.inf etc. regenerate registry.inf and preinstall/* but never
# retrigger the partition rebuild.
set(_preinstall_hive_files
    ${REACTOS_BINARY_DIR}/boot/bootdata/preinstall/system
    ${REACTOS_BINARY_DIR}/boot/bootdata/preinstall/software
    ${REACTOS_BINARY_DIR}/boot/bootdata/preinstall/default
    ${REACTOS_BINARY_DIR}/boot/bootdata/preinstall/sam
    ${REACTOS_BINARY_DIR}/boot/bootdata/preinstall/security)

option(REACTOS_NATIVE_NTFSIMG "Use native-mkntfsimg (no FUSE) to populate the preinstalled NTFS partition" OFF)

if(_preinstall_with_esp)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/preinstall_startup.nsh
        "@echo -on\nfs0:\nls\nls EFI\\BOOT\n.\\EFI\\BOOT\\boot${EFI_PLATFORM_ID}.efi\n")
endif()

if(REACTOS_PREINSTALL_USE_NTFS)
    set(_preinstall_os_vbr_arg -vbr ${_ntfs_vbr_file})
else()
    set(_preinstall_os_vbr_arg "")
endif()

if(_preinstall_with_esp)
    set(_reactosimg_part_args
        -partition ${_preinstall_esp_file} -start ${_esp_start_sector} -type EF
        -partition ${_preinstall_partition_file} -start ${_os_start_sector}
            -type ${_preinstall_partition_type} -active ${_preinstall_os_vbr_arg})
    set(_reactosimg_extra_deps ${_preinstall_esp_file})
else()
    set(_reactosimg_part_args
        -partition ${_preinstall_partition_file} -start ${_os_start_sector}
            -type ${_preinstall_partition_type} ${_preinstall_os_vbr_arg})
    set(_reactosimg_extra_deps "")
endif()

if(REACTOS_PREINSTALL_USE_GPT)
    set(_reactosimg_gpt_arg -gpt)
else()
    set(_reactosimg_gpt_arg "")
endif()

# === Deferred rule creation ===
#
# The actual add_custom_command(OUTPUT) rules for partition.{ntfs,fat32},
# preinstall_esp.img, ReactOS.img and ReactOS.vhd are NOT created here. They
# are created in create_preinstall_image_rules() below, which is invoked from
# create_iso_lists() (sdk/cmake/CMakeMacros.cmake) after every add_cd_file
# call has populated PREINSTALL_FILE_LIST. That lets us pass every binary
# packed into the partition as a file-level DEPENDS, so ninja rebuilds the
# partition (and the disk image) whenever any input driver/dll/exe changes.
#
# The phony preinstall_partition / preinstall_esp / reactosimg / reactosvhd
# custom_targets must exist EARLY though, because add_cd_file(... FOR
# preinstall ...) calls add_dependencies(preinstall_partition ...) at every
# invocation throughout the build (see CMakeMacros.cmake:401). Those calls
# need a target to attach to.

add_custom_target(preinstall_partition)
if(_preinstall_with_esp)
    add_custom_target(preinstall_esp)
endif()
add_custom_target(reactosimg)
add_custom_target(reactosvhd)

# Stash everything the deferred function will need. CMake function scope does
# not capture file-scope variables, so they have to travel through global
# properties.
set_property(GLOBAL PROPERTY _RPI_BINARY_DIR              "${CMAKE_CURRENT_BINARY_DIR}")
set_property(GLOBAL PROPERTY _RPI_USE_NTFS                "${REACTOS_PREINSTALL_USE_NTFS}")
set_property(GLOBAL PROPERTY _RPI_NATIVE_NTFSIMG          "${REACTOS_NATIVE_NTFSIMG}")
set_property(GLOBAL PROPERTY _RPI_PARTITION_FILE          "${_preinstall_partition_file}")
set_property(GLOBAL PROPERTY _RPI_PARTITION_SECTORS       "${_preinstall_partition_sectors}")
set_property(GLOBAL PROPERTY _RPI_OS_START_SECTOR         "${_os_start_sector}")
set_property(GLOBAL PROPERTY _RPI_ESP_SECTORS             "${_esp_sectors}")
set_property(GLOBAL PROPERTY _RPI_ESP_FILE                "${_preinstall_esp_file}")
set_property(GLOBAL PROPERTY _RPI_IMAGE_FILE              "${_preinstall_image_file}")
set_property(GLOBAL PROPERTY _RPI_VHD_FILE                "${_preinstall_vhd_file}")
set_property(GLOBAL PROPERTY _RPI_WITH_ESP                "${_preinstall_with_esp}")
set_property(GLOBAL PROPERTY _RPI_DOSMBR_FILE             "${_dosmbr_file}")
set_property(GLOBAL PROPERTY _RPI_FAT32_VBR_FILE          "${_fat32_vbr_file}")
set_property(GLOBAL PROPERTY _RPI_NTFS_SCRIPT             "${_preinstall_ntfs_script}")
set_property(GLOBAL PROPERTY _RPI_HIVE_FILES              "${_preinstall_hive_files}")
set_property(GLOBAL PROPERTY _RPI_EFI_PLATFORM_ID         "${EFI_PLATFORM_ID}")
set_property(GLOBAL PROPERTY _RPI_REACTOSIMG_PART_ARGS    "${_reactosimg_part_args}")
set_property(GLOBAL PROPERTY _RPI_REACTOSIMG_GPT_ARG      "${_reactosimg_gpt_arg}")
set_property(GLOBAL PROPERTY _RPI_REACTOSIMG_EXTRA_DEPS   "${_reactosimg_extra_deps}")

# Called from create_iso_lists() after PREINSTALL_FILE_LIST is finalized.
function(create_preinstall_image_rules)
    get_property(_binary_dir         GLOBAL PROPERTY _RPI_BINARY_DIR)
    get_property(_use_ntfs           GLOBAL PROPERTY _RPI_USE_NTFS)
    get_property(_native_ntfsimg     GLOBAL PROPERTY _RPI_NATIVE_NTFSIMG)
    get_property(_partition_file     GLOBAL PROPERTY _RPI_PARTITION_FILE)
    get_property(_partition_sectors  GLOBAL PROPERTY _RPI_PARTITION_SECTORS)
    get_property(_os_start_sector    GLOBAL PROPERTY _RPI_OS_START_SECTOR)
    get_property(_esp_sectors        GLOBAL PROPERTY _RPI_ESP_SECTORS)
    get_property(_esp_file           GLOBAL PROPERTY _RPI_ESP_FILE)
    get_property(_image_file         GLOBAL PROPERTY _RPI_IMAGE_FILE)
    get_property(_vhd_file           GLOBAL PROPERTY _RPI_VHD_FILE)
    get_property(_with_esp           GLOBAL PROPERTY _RPI_WITH_ESP)
    get_property(_dosmbr_file        GLOBAL PROPERTY _RPI_DOSMBR_FILE)
    get_property(_fat32_vbr_file     GLOBAL PROPERTY _RPI_FAT32_VBR_FILE)
    get_property(_ntfs_script        GLOBAL PROPERTY _RPI_NTFS_SCRIPT)
    get_property(_hive_files         GLOBAL PROPERTY _RPI_HIVE_FILES)
    get_property(_efi_platform_id    GLOBAL PROPERTY _RPI_EFI_PLATFORM_ID)
    get_property(_reactosimg_part_args  GLOBAL PROPERTY _RPI_REACTOSIMG_PART_ARGS)
    get_property(_reactosimg_gpt_arg    GLOBAL PROPERTY _RPI_REACTOSIMG_GPT_ARG)
    get_property(_reactosimg_extra_deps GLOBAL PROPERTY _RPI_REACTOSIMG_EXTRA_DEPS)

    # Pull every "dest=src" line out of PREINSTALL_FILE_LIST and keep just
    # the src side. Those are the actual files mkntfsimg / fatten will read
    # at build time, so they must be file-level DEPENDS of the partition
    # rule for ninja to invalidate it correctly.
    get_property(_preinstall_filelist GLOBAL PROPERTY PREINSTALL_FILE_LIST)
    set(_preinstall_input_files "")
    foreach(_entry IN LISTS _preinstall_filelist)
        string(REGEX REPLACE "^[^=]+=" "" _src "${_entry}")
        if(_src)
            list(APPEND _preinstall_input_files "${_src}")
        endif()
    endforeach()
    if(_preinstall_input_files)
        list(REMOVE_DUPLICATES _preinstall_input_files)
    endif()

    set(_lst_file ${_binary_dir}/preinstall.$<CONFIG>.lst)

    # --- Partition rule (NTFS native, NTFS via 3g, or FAT32) ---
    if(_use_ntfs)
        if(_native_ntfsimg)
            add_custom_command(
                OUTPUT ${_partition_file}
                COMMAND ${CMAKE_COMMAND}
                    -DPARTITION_FILE=${_partition_file}
                    -DINPUT_LIST=${_lst_file}
                    -DPARTITION_SECTORS=${_partition_sectors}
                    -DPARTITION_START_SECTOR=${_os_start_sector}
                    -DVOLUME_LABEL=ReactOS
                    -DMKNTFSIMG_EXE=$<TARGET_FILE:native-mkntfsimg>
                    -P ${_ntfs_script}
                DEPENDS freeldr native-mkntfsimg ${_ntfs_script}
                        ${_lst_file}
                        ${_hive_files}
                        ${_preinstall_input_files}
                VERBATIM)
        else()
            add_custom_command(
                OUTPUT ${_partition_file}
                COMMAND ${CMAKE_COMMAND}
                    -DPARTITION_FILE=${_partition_file}
                    -DINPUT_LIST=${_lst_file}
                    -DPARTITION_SECTORS=${_partition_sectors}
                    -DPARTITION_START_SECTOR=${_os_start_sector}
                    -DVOLUME_LABEL=ReactOS
                    -P ${_ntfs_script}
                DEPENDS freeldr ${_ntfs_script}
                        ${_lst_file}
                        ${_hive_files}
                        ${_preinstall_input_files}
                VERBATIM)
        endif()
    else()
        add_custom_command(
            OUTPUT ${_partition_file}
            COMMAND native-fatten ${_partition_file}
                -format32 ${_partition_sectors} ReactOS
                -boot ${_fat32_vbr_file}
                -addfiles ${_lst_file}
            DEPENDS native-fatten fat32 freeldr
                    ${_lst_file}
                    ${_hive_files}
                    ${_preinstall_input_files}
            VERBATIM)
    endif()

    # Inner target wrapping the partition file output. The phony
    # preinstall_partition created at file scope picks it up via
    # add_dependencies, and any add_dependencies(preinstall_partition X)
    # call from add_cd_file remains attached to the phony aggregator.
    add_custom_target(_preinstall_partition_inner DEPENDS ${_partition_file})
    add_dependencies(preinstall_partition _preinstall_partition_inner)

    # --- ESP rule (only if EFI platform with non-zero ESP size) ---
    if(_with_esp)
        add_custom_command(
            OUTPUT ${_esp_file}
            COMMAND native-fatten ${_esp_file} -format ${_esp_sectors} EFIBOOT
                -boot ${_binary_dir}/freeldr/bootsect/fat.bin
                -mkdir EFI -mkdir EFI/BOOT
                -add $<TARGET_FILE:uefildr> EFI/BOOT/boot${_efi_platform_id}.efi
                -add ${REACTOS_BINARY_DIR}/boot/bootdata/preinstall.ini freeldr.ini
                -add ${_binary_dir}/preinstall_startup.nsh startup.nsh
            DEPENDS native-fatten fat uefildr
                    ${REACTOS_BINARY_DIR}/boot/bootdata/preinstall.ini
                    ${_binary_dir}/preinstall_startup.nsh
            VERBATIM)
        add_custom_target(_preinstall_esp_inner DEPENDS ${_esp_file})
        add_dependencies(preinstall_esp _preinstall_esp_inner)
    endif()

    # --- ReactOS.img ---
    add_custom_command(
        OUTPUT ${_image_file}
        COMMAND native-mkdiskimg
            -o ${_image_file}
            -mbr ${_dosmbr_file}
            ${_reactosimg_part_args}
            ${_reactosimg_gpt_arg}
        DEPENDS native-mkdiskimg dosmbr
                ${_partition_file} ${_reactosimg_extra_deps}
        VERBATIM)
    add_custom_target(_reactosimg_inner DEPENDS ${_image_file})
    add_dependencies(reactosimg _reactosimg_inner)

    # --- ReactOS.vhd ---
    add_custom_command(
        OUTPUT ${_vhd_file}
        COMMAND native-mkdiskimg
            -o ${_vhd_file}
            -mbr ${_dosmbr_file}
            ${_reactosimg_part_args}
            ${_reactosimg_gpt_arg}
            -vhd
        DEPENDS native-mkdiskimg dosmbr
                ${_partition_file} ${_reactosimg_extra_deps}
        VERBATIM)
    add_custom_target(_reactosvhd_inner DEPENDS ${_vhd_file})
    add_dependencies(reactosvhd _reactosvhd_inner)
endfunction()
