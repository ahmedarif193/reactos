remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT})
remove_definitions(-DWINVER=${REACTOS_TARGET_NT})
remove_definitions(-DDXGKDDI_INTERFACE_VERSION=0x1052)
add_definitions(-D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DNTDDI_VERSION=0x06010000)
add_definitions(-DDXGKDDI_INTERFACE_VERSION=0x11007)

add_library(displib STATIC displib.c)
add_dependencies(displib bugcodes xdk)
