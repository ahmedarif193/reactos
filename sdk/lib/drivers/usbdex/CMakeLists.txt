
add_definitions(-D_NTSYSTEM_)

list(APPEND SOURCE
    usbdex.c
    usbguid.c)

add_library(usbdex ${SOURCE})
add_dependencies(usbdex xdk)
