    .text
    .align 2
    .global __chkstk
__chkstk:
    ret

    .align 2
    .global __alloca_probe
__alloca_probe:
    ret

    .section .drectve,"yn"
    .ascii " -export:__chkstk"
    .ascii " -export:__alloca_probe"
