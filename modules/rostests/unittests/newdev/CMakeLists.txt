include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/dll/win32/newdev)

list(APPEND SOURCE
    BatchDriverCache.c
    ${REACTOS_SOURCE_DIR}/dll/win32/newdev/driver_cache.c
    testlist.c)

add_executable(newdev_unittest ${SOURCE})
set_module_type(newdev_unittest win32cui)
add_importlibs(newdev_unittest msvcrt kernel32 ntdll)
add_rostests_file(TARGET newdev_unittest)
