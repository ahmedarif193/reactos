
include_directories(include)

#
# subdirectories containing special-purpose drivers
#
add_subdirectory(example)
add_subdirectory(fltmgr)
add_subdirectory(hidparse)
add_subdirectory(kernel32)
add_subdirectory(ntos_cc)
add_subdirectory(ntos_io)
add_subdirectory(ntos_mm)
add_subdirectory(ntos_po)
add_subdirectory(tcpip)

list(APPEND COMMON_SOURCE
    example/GuardedMemory.c
    rtl/RtlAvlTree.c
    rtl/RtlException.c
    rtl/RtlGetVersion.c
    ../apitests/rtl/RtlIntSafe.c
    rtl/RtlMemory.c
    rtl/RtlRegistry.c
    rtl/RtlSplayTree.c
    rtl/RtlStack.c
    rtl/RtlStrSafe.c
    rtl/RtlUnicodeString.c)

# These tests depend on compiler behavior rather than a particular target ISA.
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Plain faulting loads and stores must remain covered by native SEH scopes.
    if(NOT USE_DUMMY_PSEH)
        set_property(SOURCE rtl/RtlException.c APPEND PROPERTY COMPILE_OPTIONS -Xclang -fasync-exceptions)
    endif()

    # Preserve every noinline helper in the stack-walk chain.
    set_property(SOURCE rtl/RtlStack.c APPEND PROPERTY COMPILE_OPTIONS -fno-optimize-sibling-calls)

    # This test snapshots nonvolatile registers around compiler intrinsics.
    # Optimization may use those registers for the surrounding test itself.
    set_property(SOURCE ntos_ex/ExInterlocked.c APPEND PROPERTY COMPILE_OPTIONS -O0)
endif()

if(ARCH STREQUAL "i386")

elseif(ARCH STREQUAL "amd64")
    add_asm_files(kmtest_common_asm ../apitests/rtl/amd64/RtlCaptureContext-asm.s)
    list(APPEND COMMON_SOURCE
        ../apitests/rtl/amd64/RtlCaptureContext.c
        ${kmtest_common_asm}
    )
endif()

#
# kmtest_drv.sys driver
#
list(APPEND KMTEST_DRV_SOURCE
    kmtest_drv/guid.c
    kmtest_drv/kmtest_drv.c
    kmtest_drv/structs.c
    kmtest_drv/testlist.c
    kmtest_drv/vm_detect.c

    directx/DxgkDeviceWork.c
    directx/DxgkHotPlugWork.c
    directx/DxgkGpuVaAddress.c
    directx/DxgkGpuVaPageTable.c
    directx/DxgkGpuVaPte.c
    directx/DxgkFenceOrdering.c
    directx/DxgkMonitoredFence.c
    directx/DxgkPeriodicFence.c
    directx/DxgkPagingOperation.c
    directx/DxgkPagingMultipass.c
    directx/DxgkResidencyBudget.c
    directx/DxgkResidencyReference.c
    directx/DxgkOfferReclaim.c
    directx/DxgkAllocationList.c
    directx/DxgkPatchLocation.c
    directx/DxgkDmaRing.c
    directx/DxgkVidPnTopology.c
    directx/DxgkVidPnModeSet.c
    directx/DxgkFlipQueue.c
    directx/DxgkHandleTable.c
    directx/DxgkAdapterCaps.c
    directx/DxgkNodeAffinity.c
    directx/DxgkTdrPolicy.c
    directx/DxgkFenceConcurrency.c
    directx/DxgkCoreConcurrency.c
    directx/DxgkContextSync.c
    directx/DxgkPresentQueue.c
    directx/DxgkSyncWait.c
    directx/DxgkTrackedWork.c
    directx/DxgkVidMmWorkerDrain.c
    directx/Dxgmms2ContextStream.c
    directx/Dxgmms2Scheduler.c
    directx/Dxgmms2Timeline.c
    directx/Dxgmms2VidMm.c
    example/Example.c
    example/KernelType.c
    hal/HalSystemInfo.c
    npfs/NpfsConnect.c
    npfs/NpfsCreate.c
    npfs/NpfsFileInfo.c
    npfs/NpfsHelpers.c
    npfs/NpfsReadWrite.c
    npfs/NpfsVolumeInfo.c
    novp_fsrtl/FsRtlRemoveDotsFromPath.c
    ntos_cm/CmKeyKM.c
    ntos_cm/CmSecurity.c
    ntos_etw/EtwRegisterKM.c
    ntos_ex/ExCallback.c
    ntos_ex/ExCallbackExtra.c
    ntos_ex/ExDoubleList.c
    ntos_ex/ExFastMutex.c
    ntos_ex/ExFastMutexExtra.c
    ntos_ex/ExHardError.c
    ntos_ex/ExHandleTable.c
    ntos_ex/ExInterlocked.c
    ntos_ex/ExLookaside.c
    ntos_ex/ExPoolExtra.c
    ntos_ex/ExPools.c
    ntos_ex/ExPushLock.c
    ntos_ex/ExResource.c
    ntos_ex/ExResourceExtra.c
    ntos_ex/ExRundown.c
    ntos_ex/ExSequencedList.c
    ntos_ex/ExTimeZone.c
    ntos_ex/ExSingleList.c
    ntos_ex/ExTimer.c
    ntos_ex/ExTimerExtra.c
    ntos_ex/ExUuid.c
    ntos_ex/ExWorkItem.c
    ntos_fsrtl/FsRtlDissect.c
    ntos_fsrtl/FsRtlExpression.c
    ntos_fsrtl/FsRtlLegal.c
    ntos_fsrtl/FsRtlMcb.c
    ntos_fsrtl/FsRtlTunnel.c
    ntos_io/IoBuildIoctlKM.c
    ntos_io/IoCancelKM.c
    ntos_io/IoCreateFile.c
    ntos_io/IoCsqKM.c
    ntos_io/IoDeviceInterface.c
    ntos_io/IoEvent.c
    ntos_io/IoFilesystem.c
    ntos_io/IoInterrupt.c
    ntos_io/IoIrp.c
    ntos_io/IoStackKM.c
    ntos_io/IoMdl.c
    ntos_io/IoNullDeviceKM.c
    ntos_io/IoVolume.c
    ntos_kd/KdSystemDebugControl.c
    ntos_ke/KeAffinityKM.c
    ntos_ke/KeApc.c
    ntos_ke/KeApcInject.c
    ntos_ke/KeBugCheckCbKM.c
    ntos_ke/KeDevQueue.c
    ntos_ke/KeCriticalRegionKM.c
    ntos_ke/KeDpc.c
    ntos_ke/KeEvent.c
    ntos_ke/KeFloatPointState.c
    ntos_ke/KeGuardedMutex.c
    ntos_ke/KeIpiKM.c
    ntos_ke/KeIrql.c
    ntos_ke/KeMutex.c
    ntos_ke/KePcr.c
    ntos_ke/KeQueue.c
    ntos_ke/KeProcessor.c
    ntos_ke/KeSemaphore.c
    ntos_ke/KeSpinLock.c
    ntos_ke/KeThreadedDpc.c
    ntos_ke/KeTime.c
    ntos_ke/KeTimer.c
    ntos_ke/KeTimer2KM.c
    ntos_ke/KeWaitMultiple.c
    ntos_mm/MmAllocateContiguousNode.c
    ntos_mm/MmMdl.c
    ntos_mm/MmMapReserve.c
    ntos_mm/MmPhysical.c
    ntos_mm/MmPrefetchPages.c
    ntos_mm/MmReservedMapping.c
    ntos_mm/MmSelfMap.c
    ntos_mm/MmSecureKM.c
    ntos_mm/MmSection.c
    ntos_mm/ZwAllocateVirtualMemory.c
    ntos_zw/ZwDuplicateKM.c
    ntos_zw/ZwFileKM.c
    ntos_zw/ZwSystemInfoKM.c
    ntos_mm/ZwCreateSection.c
    ntos_mm/ZwMapViewOfSection.c
    ntos_ob/ObHandle.c
    ntos_ob/ObOpenByPointer.c
    ntos_ob/ObQuery.c
    ntos_ob/ObReference.c
    ntos_ob/ObSecurity.c
    ntos_ob/ObSecurityDescKM.c
    ntos_ob/ObSymbolicLink.c
    ntos_ob/ObType.cpp
    ntos_ob/ObTypes.cpp
    ntos_ob/ObWait.c
    ntos_po/PoPowerSettings.c
    ntos_ps/PsNotify.c
    ntos_ps/PsNotifyKM.c
    ntos_ps/PsProcessInfo.c
    ntos_ps/PsQuota.c
    ntos_ps/PsSystemThread.c
    ntos_se/SeAccessCheck.c
    ntos_se/SeHelpers.c
    ntos_se/SeInheritance.c
    ntos_se/SeLogonSession.c
    ntos_se/SeQueryInfoToken.c
    ntos_se/SeTokenFiltering.c
    rtl/RtlBitmapKM.c
    rtl/RtlGenericTableKM.c
    rtl/RtlGuidKM.c
    rtl/RtlImageKM.c
    rtl/RtlHashTableKM.c
    rtl/RtlTimeKM.c
    rtl/RtlIsValidOemCharacter.c
    rtl/RtlRandomKM.c
    rtl/RtlRangeList.c
    rtl/RtlStringSupportKM.c
    win32ss/CddDisplay.c
    ${COMMON_SOURCE}

    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/gpuva_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/fence_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/dma_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/residency_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/paging_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/display_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/object_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/device_work_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/present_queue_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/submit_reservation_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/context_sync_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/sync_wait_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/tracked_work_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl/vidmm_worker_drain_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgmms2/context_stream_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgmms2/scheduler_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgmms2/timeline_core.c
    ${REACTOS_SOURCE_DIR}/drivers/directx/dxgmms2/vidmm_core.c
    kmtest_drv/kmtest_drv.rc)

if(ARCH STREQUAL "i386" OR ARCH STREQUAL "amd64")
    add_asm_files(KMTEST_DRV_ASM
        kmtest_drv/vm_detect.S)
    list(APPEND KMTEST_DRV_SOURCE
        ${KMTEST_DRV_ASM}
        hal/HalPortIo.c
        ntos_io/IoTimerKM.c)
endif()

if(ARCH STREQUAL "arm64")
    list(APPEND KMTEST_DRV_SOURCE
        kmArm64/HalArm64Ipi.c
        kmArm64/HalArm64Layout.c
        kmArm64/KdArm64Layout.c
        kmArm64/KeArm64.c
        kmArm64/KeArm64AffinityEx.c
        kmArm64/KeArm64Dispatcher.c
        kmArm64/KeArm64DpcIpi.c
        kmArm64/KeArm64Frames.c
        kmArm64/KeArm64Intrinsics.c
        kmArm64/KeArm64IpiBroadcast.c
        kmArm64/KeArm64Irql.c
        kmArm64/KeArm64LoaderCache.c
        kmArm64/KeArm64PcrPrcb.c
        kmArm64/KeArm64ProcessorChange.c
        kmArm64/KeArm64Smp.c
        kmArm64/KeArm64SmpChurn.c
        kmArm64/KeArm64SpinLock.c
        kmArm64/KeArm64ThreadProcess.c
        kmArm64/RtlArm64UnwindLayout.c
        kmArm64/KeArm64SubNodeSched.c
        kmArm64/KeArm64Smt.c
        kmArm64/KeArm64Numa.c
        rtl/Win11NewKM.c
        ntos_mm/MmWin11KM.c
        ntos_mm/MmMdlWin11KM.c
        ntos_mm/MmPoolWin11KM.c
        ntos_mm/MmSectionWin11KM.c
        ntos_mm/MmPteWin11KM.c)
endif()

add_library(kmtest_drv MODULE ${KMTEST_DRV_SOURCE})
target_include_directories(kmtest_drv PRIVATE ${REACTOS_SOURCE_DIR}/drivers/directx/dxgkrnl ${REACTOS_SOURCE_DIR}/drivers/directx/dxgmms2)
set_module_type(kmtest_drv kernelmodedriver)
target_link_libraries(kmtest_drv kmtest_printf chkstk memcmp ntoskrnl_vista poguid ${PSEH_LIB})
add_importlibs(kmtest_drv ntoskrnl hal)
add_dependencies(kmtest_drv bugcodes xdk)
target_compile_definitions(kmtest_drv PRIVATE KMT_KERNEL_MODE NTDDI_VERSION=${REACTOS_TARGET_NTDDI})
#add_pch(kmtest_drv include/kmt_test.h)
add_rostests_file(TARGET kmtest_drv)

add_library(kmtest_printf
    kmtest_drv/printf_stubs.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/crt/string/strnlen.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/crt/string/wcsnlen.c
    ${REACTOS_SOURCE_DIR}/sdk/lib/crt/printf/streamout.c)
target_compile_definitions(kmtest_printf PRIVATE _LIBCNT_ _USER32_WSPRINTF wctomb=KmtWcToMb)
target_include_directories(kmtest_printf PRIVATE ${REACTOS_SOURCE_DIR}/sdk/lib/crt/include)

#
# kmtest.exe loader application
#

list(APPEND KMTEST_SOURCE
    kmtest/filter.c
    kmtest/fltsupport.c
    kmtest/kmtest.c
    kmtest/service.c
    kmtest/support.c
    kmtest/testlist.c

    example/Example_user.c

    fltmgr/fltmgr_load/fltmgr_user.c
    fltmgr/fltmgr_register/fltmgr_reg_user.c

    hidparse/HidP_user.c
    kernel32/FileAttributes_user.c
    kernel32/FindFile_user.c
    ntos_cc/CcCopyRead_user.c
    ntos_cc/CcCopyWrite_user.c
    ntos_cc/CcMapData_user.c
    ntos_cc/CcPinMappedData_user.c
    ntos_cc/CcPinRead_user.c
    ntos_cc/CcSetFileSizes_user.c
    ntos_io/IoCreateFile_user.c
    ntos_io/IoDeviceObject_user.c
    ntos_io/IoReadWrite_user.c
    ntos_kd/NtSystemDebugControl.c
    ntos_mm/MmMapLockedPagesSpecifyCache_user.c
    ntos_mm/NtCreateSection_user.c
    ntos_mm/NtUserPhysicalPages.c
    ntos_po/PoIrp_user.c
    tcpip/TcpIp_user.c
    ${COMMON_SOURCE}

    kmtest/kmtest.rc)

add_executable(kmtest ${KMTEST_SOURCE})
set_module_type(kmtest win32cui)
target_link_libraries(kmtest ${PSEH_LIB})
# kmtest/filter.c resolves fltlib.dll at runtime so kmtest still starts on
# NT 5.x systems without Filter Manager.
add_importlibs(kmtest advapi32 ws2_32 msvcrt kernel32 ntdll)
target_compile_definitions(kmtest PRIVATE KMT_USER_MODE NTDDI_VERSION=${REACTOS_TARGET_NTDDI})
#add_pch(kmtest include/kmt_test.h)
add_rostests_file(TARGET kmtest)

#
# Group targets
#
add_custom_target(kmtest_drivers)
add_dependencies(kmtest_drivers
    kmtest_drv
    example_drv
    FltMgrLoad_drv
    fltmgr_create
    fltmgrreg_drv
    hidp_drv
    iocreatefile_drv
    iodeviceobject_drv
    iohelper_drv
    ioreadwrite_drv
    kernel32_drv
    mmmaplockedpagesspecifycache_drv
    ntcreatesection_drv
    poirp_drv
    tcpip_drv
    cccopyread_drv
    cccopywrite_drv
    ccmapdata_drv
    ccpinmappeddata_drv
    ccpinread_drv
    ccsetfilesizes_drv)

add_custom_target(kmtest_all)
add_dependencies(kmtest_all kmtest_drivers kmtest)
