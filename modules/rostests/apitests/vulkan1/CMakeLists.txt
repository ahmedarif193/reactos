
include_directories(
    ${REACTOS_SOURCE_DIR}/submodules/fex-arm64ec/External/Vulkan-Headers/include)

add_executable(vulkan1_apitest
    vulkan1.c
    testlist.c)

set_module_type(vulkan1_apitest win32cui)
add_importlibs(vulkan1_apitest advapi32 msvcrt kernel32)
add_rostests_file(TARGET vulkan1_apitest)
