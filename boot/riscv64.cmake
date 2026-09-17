# Native core media uses the same FAT-image tool as the other architectures.
# Do not package the intermediate kernel ELF as an NT executable.
find_program(RISCV_XORRISO NAMES xorriso REQUIRED)
find_program(RISCV_READOBJ NAMES llvm-readobj
    HINTS "${REACTOS_CLANG_LLVM_MINGW_ROOT}/bin" REQUIRED)
set(_fatfs_dir "${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/fatfs")
set(_fatten_dir "${REACTOS_SOURCE_DIR}/sdk/tools/fatten")
add_custom_command(OUTPUT "${RISCV_HOST_BIN}/fatten"
    COMMAND ${RISCV_HOST_CC} -O2 -D__REACTOS__ -D_FILE_OFFSET_BITS=64 -D_POSIX_C_SOURCE=200809L
        -I "${_fatfs_dir}" "${_fatten_dir}/fatten.c" "${_fatten_dir}/diskio.c"
        "${_fatfs_dir}/ff.c" "${_fatfs_dir}/ffunicode.c" -o "${RISCV_HOST_BIN}/fatten"
    DEPENDS "${_fatten_dir}/fatten.c" "${_fatten_dir}/diskio.c" "${_fatten_dir}/fatten_diskio.h"
        "${_fatfs_dir}/ff.c" "${_fatfs_dir}/ffunicode.c" "${_fatfs_dir}/ff.h"
        "${_fatfs_dir}/ffconf.h" "${_fatfs_dir}/diskio.h"
    VERBATIM)
add_custom_target(riscv_host_fatten DEPENDS "${RISCV_HOST_BIN}/fatten")
include("${CMAKE_CURRENT_SOURCE_DIR}/bootdata/riscv64/hives.cmake")

set(_livecd_ini "${CMAKE_CURRENT_SOURCE_DIR}/bootdata/riscv64/livecd.ini")
set(_livecd_script "${CMAKE_CURRENT_SOURCE_DIR}/make_riscv64_livecd.cmake")
set(_uefi_image "$<TARGET_FILE:uefildr>")
set(_livecd_dependencies freeldr_efi riscv_host_fatten uefildr "${RISCV_HOST_BIN}/fatten")
set(_livecd_images)
set(_livecd_missing_targets)

# Keep dependencies on their real producers. A missing native module or hive
# producer is an explicit packaging failure, not an empty placeholder target.
foreach(_module ntoskrnl hal bootvid kdcom)
    if(TARGET ${_module})
        list(APPEND _livecd_dependencies ${_module})
        get_target_property(_native_image ${_module} RISCV_NATIVE_IMAGE)
        list(APPEND _livecd_dependencies "${_native_image}")
        if(_module STREQUAL "ntoskrnl")
            set(_module_name ntoskrnl.exe)
        else()
            set(_module_name "${_module}.dll")
        endif()
        list(APPEND _livecd_images "reactos/system32/${_module_name}=${_native_image}")
    else()
        list(APPEND _livecd_missing_targets ${_module})
    endif()
endforeach()
if(TARGET livecd_hives)
    list(APPEND _livecd_dependencies livecd_hives)
    foreach(_hive system software default sam security)
        list(APPEND _livecd_dependencies "${REACTOS_BINARY_DIR}/boot/bootdata/${_hive}")
    endforeach()
else()
    list(APPEND _livecd_missing_targets livecd_hives)
endif()

add_custom_command(OUTPUT "${REACTOS_BINARY_DIR}/livecd.iso" "${CMAKE_CURRENT_BINARY_DIR}/efisys.bin"
    COMMAND ${CMAKE_COMMAND}
        "-DLIVECD_IMAGES=${_livecd_images}" "-DLIVECD_MISSING_TARGETS=${_livecd_missing_targets}"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "-DOUTPUT_ISO=${REACTOS_BINARY_DIR}/livecd.iso"
        "-DHIVE_DIR=${REACTOS_BINARY_DIR}/boot/bootdata"
        "-DUEFI_IMAGE=${_uefi_image}" "-DFREELDR_INI=${_livecd_ini}"
        "-DFATTEN=${RISCV_HOST_BIN}/fatten" "-DREADOBJ=${RISCV_READOBJ}"
        "-DXORRISO=${RISCV_XORRISO}"
        -P "${_livecd_script}"
    DEPENDS ${_livecd_dependencies} "${_livecd_ini}" "${_livecd_script}"
    VERBATIM)
add_custom_target(livecd DEPENDS "${REACTOS_BINARY_DIR}/livecd.iso" "${CMAKE_CURRENT_BINARY_DIR}/efisys.bin")
