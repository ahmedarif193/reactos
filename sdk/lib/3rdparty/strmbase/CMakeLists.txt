add_library(strmbase STATIC
    dispatch.c
    filter.c
    mediatype.c
    pin.c
    pospass.c
    renderer.c
    seeking.c)

target_compile_definitions(strmbase PRIVATE __WINESRC__)
add_dependencies(strmbase psdk dxsdk)
