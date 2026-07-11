include_directories(
    ${REACTOS_SOURCE_DIR}/submodules/fex-arm64ec/External/Vulkan-Headers/include)

spec2def(vulkan-1.dll vulkan-1.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    vulkan_loader.c
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/vulkan-1.def)

add_library(vulkan-1 MODULE ${SOURCE})
set_module_type(vulkan-1 win32dll)
add_importlibs(vulkan-1 advapi32 gdi32 msvcrt kernel32 ntdll)
add_cd_file(TARGET vulkan-1 DESTINATION reactos/system32 FOR all)
