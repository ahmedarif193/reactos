# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the Raspberry Pi VCOS kernel export DLL
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

add_library(vcos_win32_kern SHARED vcos_pthreads.c)
set_module_type(vcos_win32_kern kerneldll ENTRYPOINT DriverEntry)

target_include_directories(vcos_win32_kern PRIVATE
    ${_vchiq_userland_root}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_SOURCE_DIR}/sdk/include/wdf/kmdf/1.17)
target_compile_definitions(vcos_win32_kern PRIVATE
    UNICODE
    _UNICODE
    WIN32_KERN
    WIN32DLL_EXPORTS
    VCOS_PROVIDER
    WDF_DEVICE_NO_WDMSEC_H)

target_link_libraries(vcos_win32_kern
    vcos_win32_kern_generic
    ntoskrnl_vista
    libcntpr
    ${PSEH_LIB})
add_importlibs(vcos_win32_kern ntoskrnl hal)

add_cd_file(TARGET vcos_win32_kern DESTINATION reactos/system32/drivers FOR all)
set_target_properties(vcos_win32_kern PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
