set(aero_rc_source ${CMAKE_CURRENT_SOURCE_DIR}/aero.rc)
set(aero_rc_generated ${CMAKE_CURRENT_BINARY_DIR}/aero_ros.rc)

# Wine's wrc stores strings in user-defined resources as UTF-16. windres
# requires an explicit wide-string prefix for the same resource data.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${aero_rc_source})
file(READ ${aero_rc_source} aero_rc_contents)
string(REGEX REPLACE "\n\"" "\nL\"" aero_rc_contents "${aero_rc_contents}")
file(WRITE ${aero_rc_generated} "${aero_rc_contents}")

file(GLOB aero_rc_deps ${CMAKE_CURRENT_SOURCE_DIR}/*.bmp)
add_rc_deps(${aero_rc_generated} ${aero_rc_source} ${aero_rc_deps})

add_library(aero.msstyles MODULE ${aero_rc_generated})
set_module_type(aero.msstyles module)
add_dependencies(aero.msstyles xdk)
set_target_properties(aero.msstyles PROPERTIES SUFFIX "")
add_cd_file(TARGET aero.msstyles DESTINATION reactos/Resources/Themes/Aero FOR all)
