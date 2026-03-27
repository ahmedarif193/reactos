add_executable(rosl rosl_common.c rosl_service.c rosl_client.c rosl_main.c terminal.c log_style.c)
target_include_directories(rosl PRIVATE ${CMAKE_SOURCE_DIR}/modules/rosv/include)
set_module_type(rosl win32cui)
add_importlibs(rosl advapi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET rosl DESTINATION reactos/system32 FOR all)

add_executable(rosl_netd rosl_netd.c log_style.c)
target_include_directories(rosl_netd PRIVATE ${CMAKE_SOURCE_DIR}/modules/rosv/include)
set_module_type(rosl_netd win32cui)
add_importlibs(rosl_netd ws2_32 iphlpapi winmm msvcrt kernel32 ntdll)
add_cd_file(TARGET rosl_netd DESTINATION reactos/system32 FOR all)

add_executable(vhdxtool vhdxtool.c)
target_include_directories(vhdxtool PRIVATE ${CMAKE_SOURCE_DIR}/modules/rosv/include)
set_module_type(vhdxtool win32cui)
add_importlibs(vhdxtool msvcrt kernel32 ntdll)
add_cd_file(TARGET vhdxtool DESTINATION reactos/system32 FOR all)

