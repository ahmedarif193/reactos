# PROJECT:     ReactOS Raspberry Pi 3 VC4 OpenGL ICD packaging
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

# The Raspberry Pi 3 gets hardware OpenGL 2.1 from Mesa's gallium "vc4"
# driver built as a Windows WGL ICD.  Mesa does not build inside this tree,
# so the ICD ships as a prebuilt artifact (see README.md for provenance) and
# is unpacked at build time.  Shipping it here rather than requiring an
# externally configured path is deliberate: without the ICD present,
# opengl32 silently falls back to its GL 1.1 software rasteriser and the
# board loses hardware acceleration, so a default build must carry it.
#
# Set RPI3VC4_MESA_VC4_ICD to override with a locally built Mesa when
# iterating on the driver itself.

set(RPI3VC4_MESA_VC4_ICD "$ENV{REACTOS_MESA_VC4_ICD}" CACHE FILEPATH
    "Override: external Mesa VC4 WGL ICD to deploy instead of the in-tree one")

# reactos.cab stores the source basename and ignores NAME_ON_CD, so the file
# handed to add_cd_file must already be named rpi3vc4ogl.dll.
set(RPI3VC4_MESA_ICD_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/mesa-icd")
set(RPI3VC4_MESA_ICD_STAGE "${RPI3VC4_MESA_ICD_STAGE_DIR}/rpi3vc4ogl.dll")

if(RPI3VC4_MESA_VC4_ICD)
    if(NOT EXISTS "${RPI3VC4_MESA_VC4_ICD}")
        message(FATAL_ERROR
            "RPI3VC4_MESA_VC4_ICD does not exist: ${RPI3VC4_MESA_VC4_ICD}")
    endif()
    message(STATUS "RPi3 VC4 OpenGL ICD: ${RPI3VC4_MESA_VC4_ICD} (override)")

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
else()
    set(RPI3VC4_MESA_ICD_ARCHIVE
        "${CMAKE_CURRENT_SOURCE_DIR}/rpi3vc4ogl.dll.tar.xz")
    if(NOT EXISTS "${RPI3VC4_MESA_ICD_ARCHIVE}")
        message(FATAL_ERROR
            "In-tree RPi3 VC4 OpenGL ICD is missing: ${RPI3VC4_MESA_ICD_ARCHIVE}")
    endif()
    message(STATUS "RPi3 VC4 OpenGL ICD: in-tree prebuilt Mesa vc4")

    # The archive holds a single already-stripped rpi3vc4ogl.dll.  CMake's
    # own tar reads .tar.xz, so unpacking needs no external tool.
    add_custom_command(
        OUTPUT "${RPI3VC4_MESA_ICD_STAGE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${RPI3VC4_MESA_ICD_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E chdir "${RPI3VC4_MESA_ICD_STAGE_DIR}"
                ${CMAKE_COMMAND} -E tar xf "${RPI3VC4_MESA_ICD_ARCHIVE}"
        COMMAND ${CMAKE_COMMAND} -E touch "${RPI3VC4_MESA_ICD_STAGE}"
        DEPENDS "${RPI3VC4_MESA_ICD_ARCHIVE}"
        COMMENT "Unpacking the Raspberry Pi 3 Mesa VC4 OpenGL ICD"
        VERBATIM)
endif()

add_custom_target(rpi3vc4_mesa_icd_stage
    DEPENDS "${RPI3VC4_MESA_ICD_STAGE}")

add_cd_file(
    FILE "${RPI3VC4_MESA_ICD_STAGE}"
    TARGET rpi3vc4_mesa_icd_stage
    DESTINATION reactos/system32
    FOR all)
