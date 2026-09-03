PROJECT(SHELL)

if(DBG)
    add_definitions(-D_DEBUG=1) # CORE-17505
endif()

list(APPEND SOURCE
    explorer11.cpp
    navpane.cpp
    icons.cpp
    thispc.cpp
    frame.h)

list(APPEND SHELL_SOURCE
    ../explorer/appbar.cpp
    ../explorer/desktop.cpp
    ../explorer/explorer.cpp
    ../explorer/notifyiconscust.cpp
    ../explorer/rshell.cpp
    ../explorer/settings.cpp
    ../explorer/shellservice.cpp
    ../explorer/startctxmnu.cpp
    ../explorer/startmnu.cpp
    ../explorer/startmnu2.cpp
    ../explorer/startmnucust.cpp
    ../explorer/startmnusite.cpp
    ../explorer/startup.cpp
    ../explorer/syspager.cpp
    ../explorer/taskband.cpp
    ../explorer/taskswnd.cpp
    ../explorer/tbsite.cpp
    ../explorer/trayclock.cpp
    ../explorer/traydeskbtn.cpp
    ../explorer/trayflyouts.cpp
    ../explorer/trayntfy.cpp
    ../explorer/trayprop.cpp
    ../explorer/traywnd.cpp
    ../explorer/util.cpp)

file(GLOB explorer11_rc_deps ${CMAKE_CURRENT_SOURCE_DIR}/../explorer/res/*.* ${CMAKE_CURRENT_SOURCE_DIR}/../explorer/res/ico/fluent/*.ico ${CMAKE_CURRENT_SOURCE_DIR}/../explorer/explorer.rc ${CMAKE_CURRENT_SOURCE_DIR}/../explorer/resource.h)
add_rc_deps(explorer11.rc ${explorer11_rc_deps})
add_executable(explorer11 ${SOURCE} ${SHELL_SOURCE} explorer11.rc)
target_compile_definitions(explorer11 PRIVATE EXPLORER11_BUILD)
target_include_directories(explorer11 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../explorer)
target_link_libraries(explorer11 uuid wine cpprt atl_classes)
set_module_type(explorer11 win32gui UNICODE)
add_importlibs(explorer11 msimg32 setupapi advapi32 gdi32 user32 comctl32 ole32 oleaut32 shell32 browseui shlwapi shdocvw version uxtheme winmm iphlpapi msvcrt kernel32 ntdll)
add_pch(explorer11 frame.h "${SHELL_SOURCE}")
add_cd_file(TARGET explorer11 DESTINATION reactos FOR all)
