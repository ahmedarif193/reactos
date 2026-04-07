set(LIBWIM_PUBLIC_INCLUDE ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/libwim)

set(LIBWIM_CORE_SOURCE
    sha1.c
    wimcore.c
    xpress_huff.c)

set(LIBWIM_SOURCE
    wim_capture.c
    wim_io.c
    wim_read.c
    wim_write.c)

if(CMAKE_CROSSCOMPILING)
    add_library(libwimcore STATIC ${LIBWIM_CORE_SOURCE})
    target_include_directories(libwimcore PUBLIC ${LIBWIM_PUBLIC_INCLUDE})
    target_compile_definitions(libwimcore PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_property(TARGET libwimcore PROPERTY C_STANDARD 11)
    add_dependencies(libwimcore psdk)

    add_library(libwim ${LIBWIM_SOURCE})
    target_include_directories(libwim PUBLIC ${LIBWIM_PUBLIC_INCLUDE})
    target_compile_definitions(libwim PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_property(TARGET libwim PROPERTY C_STANDARD 11)
    target_link_libraries(libwim PUBLIC libwimcore)
    add_dependencies(libwim psdk)
else()
    add_library(libwimcorehost STATIC ${LIBWIM_CORE_SOURCE})
    target_include_directories(libwimcorehost PUBLIC ${LIBWIM_PUBLIC_INCLUDE})
    set_property(TARGET libwimcorehost PROPERTY C_STANDARD 11)

    add_library(libwimhost ${LIBWIM_SOURCE})
    target_include_directories(libwimhost PUBLIC ${LIBWIM_PUBLIC_INCLUDE})
    set_property(TARGET libwimhost PROPERTY C_STANDARD 11)

    if(MSVC)
        target_compile_definitions(libwimcorehost PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_definitions(libwimhost PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_definitions(libwimhost PRIVATE
            _POSIX_C_SOURCE=200809L
            _DEFAULT_SOURCE)
        find_package(Threads)
        if(Threads_FOUND)
            target_link_libraries(libwimhost PUBLIC Threads::Threads)
        endif()
    endif()

    target_link_libraries(libwimhost PUBLIC libwimcorehost)
    target_link_libraries(libwimcorehost PRIVATE host_includes)
    target_link_libraries(libwimhost PRIVATE host_includes)
endif()
