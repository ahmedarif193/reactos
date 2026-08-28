# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the Raspberry Pi VCOS kernel support library
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

set(VCOS_WIN32_KERN_GENERIC_SOURCE
    vcos_abort.c
    vcos_generic_blockpool.c
    vcos_generic_named_sem.c
    vcos_init.c
    vcos_logcat.c
    vcos_mem_from_malloc.c
    vcos_msgqueue.c)

add_library(vcos_win32_kern_generic STATIC ${VCOS_WIN32_KERN_GENERIC_SOURCE})

target_include_directories(vcos_win32_kern_generic PRIVATE
    ${_vchiq_userland_root}
    ${_vchiq_userland_root}/interface/vcos
    ${_vchiq_userland_root}/interface/vcos/win32
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${REACTOS_SOURCE_DIR}/sdk/include/wdf/kmdf/1.17)
target_compile_definitions(vcos_win32_kern_generic PRIVATE
    UNICODE
    _UNICODE
    WIN32_KERN
    WIN32DLL_EXPORTS
    VCOS_PROVIDER
    WDF_DEVICE_NO_WDMSEC_H)

set_target_properties(vcos_win32_kern_generic PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
