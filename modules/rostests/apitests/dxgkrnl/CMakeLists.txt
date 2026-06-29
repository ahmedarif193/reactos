
list(APPEND SOURCE
    D3dkmtCallbackExchange.c
    D3dkmtIoctl.c
    D3dkmtStructures.c
    D3dkmtAdapter.c
    D3dkmtDevice.c
    D3dkmtSync.c
    D3dkmtDisplay.c
    D3dkmtPresent.c
    HandleEncoding.c
    VidPnConstants.c
    VidPnTopology.c
    VidPnModeSet.c
    WddmConstants.c
    Scheduler.c
    VidMm.c
    PresentQueue.c
    TdrPreemption.c
    CddDisplay.c
    WddmPipeline.c
    GpuVirtualAddress.c
    ProcessLifecycle.c
    DmaSubmission.c
    VidSch.c
    D3dkmtChain.c
    D3dkmtDisplayAdv.c
    D3dkmtPresentPath.c
    D3dkmtTdr.c
    D3dkmtVidMm.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(dxgkrnl_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

target_link_libraries(dxgkrnl_apitest wine ${PSEH_LIB})
set_module_type(dxgkrnl_apitest win32cui)
add_importlibs(dxgkrnl_apitest msvcrt kernel32 ntdll)
add_pch(dxgkrnl_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_rostests_file(TARGET dxgkrnl_apitest)
add_cd_file(TARGET dxgkrnl_apitest DESTINATION reactos/system32 FOR all)
