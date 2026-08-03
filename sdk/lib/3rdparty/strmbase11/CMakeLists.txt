add_library(strmbase11 STATIC
    dispatch.c
    filter.c
    mediatype.c
    pin.c
    pospass.c
    renderer.c
    seeking.c)

target_compile_definitions(strmbase11 PRIVATE __WINESRC__)
target_include_directories(strmbase11 BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_dependencies(strmbase11 psdk dxsdk)
