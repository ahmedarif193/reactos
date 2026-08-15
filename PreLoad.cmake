
# small trick to get the real source directory at this stage
STRING(REPLACE "/PreLoad.cmake" "" REACTOS_HOME_DIR ${CMAKE_CURRENT_LIST_FILE})

#message("/PreLoad.cmake ... ${REACTOS_HOME_DIR}")

SET(CMAKE_MODULE_PATH "${REACTOS_HOME_DIR}/sdk/cmake" CACHE INTERNAL "")

#message("CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")

# rosconfig (menuconfig): apply the options owned by this output tree. Keeping
# the generated fragment below CMAKE_BINARY_DIR prevents one target's menu
# selections from leaking into another target or a managed nested build. The
# regular cache sets are not FORCEd, so explicit -D arguments still win.
set(_rosconfig_overrides "${CMAKE_BINARY_DIR}/.rosconfig/overrides.cmake")
if(NOT ROSCONFIG_SKIP_OVERRIDES AND EXISTS "${_rosconfig_overrides}")
    include("${_rosconfig_overrides}")
endif()
unset(_rosconfig_overrides)

include("${REACTOS_HOME_DIR}/sdk/cmake/rosconfig/profiles/apply.cmake")
