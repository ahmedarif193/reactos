# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>

set(ROSCONFIG_PROFILE_PACKAGES
    sdbus
    usbdwc2
    smsc95xx
    rp1gem
    cyw43455sdio
    cyw43455
    rpiq
    bcm2836pwm
    pi_miniuart
    vcos_win32_kern
    vchiq_arm_kern
    vchiq
    rpi3winsync
    rpiwav
    rpi5hdmi
    rpi5vc4ogl)

if(MESA_GALLIUM_FROM_SOURCE)
    list(APPEND ROSCONFIG_PROFILE_PACKAGES mesa_gallium)
else()
    list(APPEND ROSCONFIG_PROFILE_PACKAGES rpi3vc4_mesa_icd_stage)
endif()
if(REACTOS_USE_WDDM AND (REACTOS_WDDM_TARGET_LEVEL GREATER_EQUAL 2000))
    list(APPEND ROSCONFIG_PROFILE_PACKAGES rpi3vc4 rpi5vc4)
elseif(REACTOS_USE_XPDM)
    list(APPEND ROSCONFIG_PROFILE_PACKAGES rpi5vc4)
endif()
set(ROSCONFIG_PROFILE_CONFIGS
    "RPI_SUPPORT:BOOL=ON"
    "RPI3_SUPPORT:BOOL=ON"
    "RPI5_SUPPORT:BOOL=ON")
