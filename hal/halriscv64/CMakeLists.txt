# Use the shared export catalog and the normal NT PE/COFF link path.
spec2def(hal.dll ../hal.spec ADD_IMPORTLIB)

add_library(hal MODULE
    clock.c
    dma.c
    halinit.c
    halpnpdd.c
    pci.c
    plic.c
    platform.c
    rtc.c
    sbi.c
    smp.c
    smp_trampoline.S
    stubs.c
    timer.c
    ${CMAKE_CURRENT_BINARY_DIR}/hal.def)
set_module_type(hal kerneldll ENTRYPOINT 0)
target_link_libraries(hal libcntpr)
add_importlibs(hal ntoskrnl)
target_compile_definitions(hal PRIVATE _NTHAL_ _NTHALDLL_ _NTSYSTEM_)
target_include_directories(hal PRIVATE ${REACTOS_SOURCE_DIR}/ntoskrnl/include)
add_dependencies(hal xdk psdk bugcodes)
add_cd_file(TARGET hal DESTINATION reactos/system32 NO_CAB FOR all)
