
# small trick to get the real source directory at this stage
STRING(REPLACE "/PreLoad.cmake" "" REACTOS_HOME_DIR ${CMAKE_CURRENT_LIST_FILE})

#message("/PreLoad.cmake ... ${REACTOS_HOME_DIR}")

SET(CMAKE_MODULE_PATH "${REACTOS_HOME_DIR}/sdk/cmake" CACHE INTERNAL "")

#message("CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")

# rosconfig (menuconfig): apply the build options selected through
# menuconfig.sh / "configure menuconfig", generated from the untracked
# .rosconfig/config.cache. The regular cache sets in that file are not
# FORCEd, so explicit -D arguments on the CMake command line win; the
# selected profile separately enforces the settings it owns.
if(EXISTS "${REACTOS_HOME_DIR}/.rosconfig/overrides.cmake")
    include("${REACTOS_HOME_DIR}/.rosconfig/overrides.cmake")
endif()

include("${REACTOS_HOME_DIR}/sdk/cmake/rosconfig/profiles/apply.cmake")
