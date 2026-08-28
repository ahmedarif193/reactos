# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the Raspberry Pi VCHIQ kernel client DLL
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

add_library(vchiq_arm_kern SHARED vchiq_lib.c)
set_module_type(vchiq_arm_kern kerneldll ENTRYPOINT DriverEntry)

target_include_directories(vchiq_arm_kern PRIVATE
    ${_vchiq_userland_root}
    ${_vchiq_userland_root}/interface/vcos/win32
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_SOURCE_DIR}/sdk/include/wdf/kmdf/1.17)
target_compile_definitions(vchiq_arm_kern PRIVATE
    UNICODE
    _UNICODE
    WIN32
    WIN32_KERN
    WIN32DLL_EXPORTS
    USE_VCHIQ_ARM
    WDF_DEVICE_NO_WDMSEC_H)

target_link_libraries(vchiq_arm_kern
    vcos_win32_kern
    ntoskrnl_vista
    libcntpr
    ${PSEH_LIB})
add_importlibs(vchiq_arm_kern ntoskrnl hal)

add_cd_file(TARGET vchiq_arm_kern DESTINATION reactos/system32/drivers FOR all)
set_target_properties(vchiq_arm_kern PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
