
add_definitions(-D_NTSYSTEM_)

add_library(sdbuslib sdbus.c)
add_dependencies(sdbuslib xdk)
