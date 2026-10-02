# PReP HAL for Windows NT PowerPC (QEMU 40p): 8259 pair, decrementer clock,
# MC146818 RTC and the Raven PCI host bridge.
spec2def(hal.dll ../hal.spec ADD_IMPORTLIB)

add_library(hal MODULE
    clock.c
    dma.c
    ../arch/common/generic/dma_coherent.c
    halinit.c
    halpnpdd.c
    pci.c
    pic.c
    port.c
    rtc.c
    smp.c
    stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/hal.def)
set_module_type(hal kerneldll ENTRYPOINT 0)
target_link_libraries(hal halcommon libcntpr)
add_importlibs(hal ntoskrnl)
target_compile_definitions(hal PRIVATE _NTHAL_ _NTHALDLL_)
target_include_directories(hal PRIVATE ${REACTOS_SOURCE_DIR}/ntoskrnl/include)
add_dependencies(hal xdk psdk bugcodes)
add_cd_file(TARGET hal DESTINATION reactos/system32 NO_CAB FOR all)
