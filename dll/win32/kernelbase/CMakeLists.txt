
remove_definitions(-D_WIN32_WINNT=${REACTOS_TARGET_NT} -DWINVER=${REACTOS_TARGET_NT})

add_definitions(
    -D__WINESRC__
    -D_WINE
    -D_KERNELBASE_)

include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)

add_subdirectory(wine)

spec2def(kernelbase.dll kernelbase.spec ADD_IMPORTLIB)

add_library(kernelbase MODULE
    AppContainer.c
    CancelSynchronousIo.c
    CpuSets.c
    DllMain.c
    AppModelToken.c
    PackageFamily.c
    PseudoConsole.c
    PssSnapshot.c
    Security.c
    SyncObjects.c
    VirtualAlloc2.c
    VolumeInformation.c
    WerRuntime.c
    XState.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/crt/string/wcsnlen.c
    ${CMAKE_CURRENT_BINARY_DIR}/kernelbase_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/kernelbase.def)

set_module_type(kernelbase win32dll ENTRYPOINT DllMain 12)
target_link_libraries(kernelbase psapi_static kernelbase_static kernelbase_console kernel32_shared win32_registry_hkcr wine chkstk uuid)
add_importlibs(kernelbase kernel32 kernel32_vista ntdll ntdll_vista)
add_dependencies(kernelbase psdk errcodes)
add_cd_file(TARGET kernelbase DESTINATION reactos/system32 FOR all)

spec2def(kernelbase_ros.dll kernelbase_ros.spec ADD_IMPORTLIB)
add_library(kernelbase_ros MODULE
    ${CMAKE_CURRENT_BINARY_DIR}/kernelbase_ros_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/kernelbase_ros.def)
set_module_type(kernelbase_ros win32dll ENTRYPOINT 0)
add_dependencies(kernelbase_ros kernelbase)
add_cd_file(TARGET kernelbase_ros DESTINATION reactos/system32 FOR all)
