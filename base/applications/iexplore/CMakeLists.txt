
add_rc_deps(iexplore.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/iexplore.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/iexplore.inf)
add_executable(iexplore main.c iexplore.rc version.rc)
if(MSVC)
    target_link_options(iexplore PRIVATE /LARGEADDRESSAWARE)
elseif(ARCH STREQUAL "i386")
    target_link_options(iexplore PRIVATE "-Wl,--large-address-aware")
endif()
target_link_libraries(iexplore wine)
set_module_type(iexplore win32gui UNICODE)
add_delay_importlibs(iexplore advpack version user32 ieframe)
add_importlibs(iexplore user32_vista msvcrt kernel32 ntdll)
add_cd_file(TARGET iexplore DESTINATION reactos FOR all)
