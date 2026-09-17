# Package a native core only after all image producers have completed.
if(LIVECD_MISSING_TARGETS)
    list(JOIN LIVECD_MISSING_TARGETS ", " _missing)
    message(FATAL_ERROR "RISC-V livecd requires native build targets: ${_missing}")
endif()

set(_fatten_files)
set(_iso_files)
set(_payload_bytes 0)
foreach(_entry IN LISTS LIVECD_IMAGES)
    string(FIND "${_entry}" "=" _separator)
    string(SUBSTRING "${_entry}" 0 ${_separator} _destination)
    math(EXPR _source_offset "${_separator} + 1")
    string(SUBSTRING "${_entry}" ${_source_offset} -1 _source)
    execute_process(COMMAND "${READOBJ}" --file-headers "${_source}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _headers ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0 OR
       NOT _headers MATCHES "Machine: IMAGE_FILE_MACHINE_RISCV64 \\(0x5064\\)" OR
       NOT _headers MATCHES "Magic: 0x20B" OR
       NOT _headers MATCHES "Subsystem: IMAGE_SUBSYSTEM_NATIVE \\(0x1\\)")
        message(FATAL_ERROR "${_source} is not a native RISC-V PE32+ image; ELF and UEFI images cannot be installed as ${_destination}. ${_error}")
    endif()
    file(SIZE "${_source}" _size)
    math(EXPR _payload_bytes "${_payload_bytes} + ${_size}")
    list(APPEND _fatten_files -add "${_source}" "${_destination}")
    list(APPEND _iso_files "${_destination}=${_source}")
endforeach()

foreach(_hive system software default sam security)
    set(_source "${HIVE_DIR}/${_hive}")
    if(NOT EXISTS "${_source}")
        message(FATAL_ERROR "RISC-V livecd is missing the generated registry hive ${_source}")
    endif()
    file(SIZE "${_source}" _size)
    math(EXPR _payload_bytes "${_payload_bytes} + ${_size}")
    list(APPEND _fatten_files -add "${_source}" "reactos/system32/config/${_hive}")
    list(APPEND _iso_files "reactos/system32/config/${_hive}=${_source}")
endforeach()

# FreeLdr currently opens the loaded image's UEFI volume. Keep its menu and
# core payload in that FAT volume, not only in an inaccessible ISO directory.
file(SIZE "${UEFI_IMAGE}" _uefi_size)
file(SIZE "${FREELDR_INI}" _ini_size)
math(EXPR _fat_sectors "((${_payload_bytes} + ${_uefi_size} + ${_ini_size}) * 2 + 33554432 + 511) / 512")
set(_efisys "${OUTPUT_DIR}/efisys.bin")
execute_process(COMMAND "${FATTEN}" "${_efisys}" -format ${_fat_sectors} EFIBOOT
    -mkdir EFI -mkdir EFI/BOOT -add "${UEFI_IMAGE}" EFI/BOOT/bootriscv64.efi
    -add "${FREELDR_INI}" freeldr.ini
    -mkdir reactos -mkdir reactos/system32 -mkdir reactos/system32/config
    ${_fatten_files} RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Could not create the RISC-V EFI system image: ${_result}")
endif()

execute_process(COMMAND "${XORRISO}" -as mkisofs -quiet
    -o "${OUTPUT_ISO}.tmp" -iso-level 3 -J -R -V ReactOS
    -eltorito-platform efi -e loader/efisys.bin -no-emul-boot -graft-points
    "loader/efisys.bin=${_efisys}" "efi/boot/bootriscv64.efi=${UEFI_IMAGE}"
    "freeldr.ini=${FREELDR_INI}" ${_iso_files}
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Could not create the RISC-V ISO image: ${_result}")
endif()
file(RENAME "${OUTPUT_ISO}.tmp" "${OUTPUT_ISO}")
