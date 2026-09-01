# PROJECT:     ReactOS Raspberry Pi 3 VC4 OpenGL ICD packaging
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

set(RPI3VC4_MESA_VC4_ICD "$ENV{REACTOS_MESA_VC4_ICD}" CACHE FILEPATH
    "Mesa VC4 WGL ICD to deploy for the Raspberry Pi 3")

if(NOT RPI3VC4_MESA_VC4_ICD)
    message(FATAL_ERROR
        "RPI3VC4_MESA_VC4_ICD must name the Mesa VC4 WGL ICD for Raspberry Pi 3 builds")
endif()

if(NOT EXISTS "${RPI3VC4_MESA_VC4_ICD}")
    message(FATAL_ERROR
        "RPI3VC4_MESA_VC4_ICD does not exist: ${RPI3VC4_MESA_VC4_ICD}")
endif()

# reactos.cab stores the source basename and ignores NAME_ON_CD.  Stage the
# external Mesa artifact under the installed ICD name before CAB insertion.
set(RPI3VC4_MESA_ICD_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/mesa-icd")
set(RPI3VC4_MESA_ICD_STAGE
    "${RPI3VC4_MESA_ICD_STAGE_DIR}/rpi3vc4ogl.dll")

add_custom_command(
    OUTPUT "${RPI3VC4_MESA_ICD_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${RPI3VC4_MESA_ICD_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RPI3VC4_MESA_VC4_ICD}"
            "${RPI3VC4_MESA_ICD_STAGE}"
    COMMAND ${CMAKE_STRIP} --strip-debug "${RPI3VC4_MESA_ICD_STAGE}"
    DEPENDS "${RPI3VC4_MESA_VC4_ICD}"
    VERBATIM)

add_custom_target(rpi3vc4_mesa_icd_stage
    DEPENDS "${RPI3VC4_MESA_ICD_STAGE}")

add_cd_file(
    FILE "${RPI3VC4_MESA_ICD_STAGE}"
    TARGET rpi3vc4_mesa_icd_stage
    DESTINATION reactos/system32
    FOR all)
