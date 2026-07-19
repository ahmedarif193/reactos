
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x10003)

add_library(vc4kmt STATIC vc4kmt.c)
add_dependencies(vc4kmt psdk)
