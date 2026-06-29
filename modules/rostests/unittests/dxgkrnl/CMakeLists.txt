message(STATUS ">>>>>> Configuring dxgkddi_unittest <<<<<<")

include_directories(
    ${REACTOS_SOURCE_DIR}/modules/rostests/apitests/include
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl)

list(APPEND SOURCE
    dxgkddi_test.c
    testlist.c)

add_executable(dxgkddi_unittest ${SOURCE})
set_module_type(dxgkddi_unittest win32cui)
add_importlibs(dxgkddi_unittest gdi32 msvcrt kernel32 ntdll)
add_rostests_file(TARGET dxgkddi_unittest)
