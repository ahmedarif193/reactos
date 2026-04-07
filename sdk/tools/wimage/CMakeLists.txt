add_host_tool(wimage wimage.c)
set_property(TARGET wimage PROPERTY C_STANDARD 11)

target_compile_definitions(wimage PRIVATE
    WIMAGE_VERSION_STRING="1.0-reactos")

if(NOT MSVC)
    target_compile_definitions(wimage PRIVATE
        _POSIX_C_SOURCE=200809L
        _DEFAULT_SOURCE)
else()
    target_compile_definitions(wimage PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(wimage PRIVATE libwimhost host_includes)
