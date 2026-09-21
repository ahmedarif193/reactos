# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 Ahmed ARIF

# Kernel-owned catalog of the system DLLs produced by this build. No runtime
# pathname is a trust decision, and changes to a DLL regenerate the catalog.
find_program(CI_PYTHON_EXECUTABLE NAMES python3 python REQUIRED NO_CMAKE_FIND_ROOT_PATH)
get_property(_ci_targets GLOBAL PROPERTY CI_SYSTEM_TARGETS)
list(REMOVE_DUPLICATES _ci_targets)
list(SORT _ci_targets)
set(_ci_files "")
set(_ci_inputs "")
foreach(_ci_target IN LISTS _ci_targets)
    list(APPEND _ci_files "$<TARGET_FILE:${_ci_target}>")
    string(APPEND _ci_inputs "$<TARGET_FILE:${_ci_target}>\n")
endforeach()
file(GENERATE OUTPUT "${REACTOS_BINARY_DIR}/sdk/lib/ci/catalog-$<CONFIG>.txt" CONTENT "${_ci_inputs}")
add_custom_command(
    OUTPUT "${REACTOS_BINARY_DIR}/sdk/lib/ci/catalog.h"
    COMMAND "${CI_PYTHON_EXECUTABLE}" "${REACTOS_SOURCE_DIR}/sdk/lib/ci/catalog.py"
        "${REACTOS_BINARY_DIR}/sdk/lib/ci/catalog-$<CONFIG>.txt"
        "${REACTOS_BINARY_DIR}/sdk/lib/ci/catalog.h"
    DEPENDS ${_ci_files} "${REACTOS_BINARY_DIR}/sdk/lib/ci/catalog-$<CONFIG>.txt" "${REACTOS_SOURCE_DIR}/sdk/lib/ci/catalog.py"
    COMMENT "Cataloging system images for code integrity"
    VERBATIM)
add_custom_target(ci_catalog DEPENDS "${REACTOS_BINARY_DIR}/sdk/lib/ci/catalog.h")
foreach(_ci_kernel ntoskrnl ntkrnlmp nvs_neutral)
    if(TARGET ${_ci_kernel})
        add_dependencies(${_ci_kernel} ci_catalog)
        target_include_directories(${_ci_kernel} PRIVATE "${REACTOS_BINARY_DIR}/sdk/lib/ci")
    endif()
endforeach()
