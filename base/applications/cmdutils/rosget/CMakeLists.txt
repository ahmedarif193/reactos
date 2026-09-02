include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib
    ${REACTOS_SOURCE_DIR}/sdk/lib/3rdparty/zlib/contrib)

list(APPEND SOURCE
    category.cpp
    details.cpp
    gui.cpp
    hash.cpp
    http.cpp
    installed.cpp
    installer.cpp
    main.cpp
    manifest.cpp
    source.cpp
    source_trust.cpp
    sqlite_index.cpp
    util.cpp)

add_rc_deps(rosget.rc
    ${CMAKE_CURRENT_SOURCE_DIR}/res/rosget.ico
    ${CMAKE_CURRENT_SOURCE_DIR}/res/ICON-SOURCE.txt)
add_executable(rosget ${SOURCE} rosget.rc)
set_property(TARGET rosget PROPERTY CXX_STANDARD 17)
set_property(TARGET rosget PROPERTY CXX_STANDARD_REQUIRED ON)
set_module_type(rosget win32cui UNICODE)
target_link_libraries(rosget cpprt minizip zlib)
add_importlibs(rosget advapi32 bcrypt comctl32 crypt32 gdi32 shell32 user32 wininet msvcrt kernel32 ntdll)
add_cd_file(TARGET rosget DESTINATION reactos/system32 FOR all)
