# Keep the standard driver-side HAL import catalog available even though the
# bootstrap native provider exposes only its explicitly implemented boundary.
spec2def(hal.dll ../hal.spec ADD_IMPORTLIB)

# Native module ownership does not enable the unavailable platform services.
add_riscv_native_image(hal hal.dll 0 SOURCES
    clock.c
    dma.c
    halinit.c
    pci.c
    plic.c
    platform.c
    sbi.c
    stubs.c
    timer.c
    EXPORTS
        KeQueryPerformanceCounter KeStallExecutionProcessor HalSetTimeIncrement HalReportResourceUsage
        HalDisplayString HalReturnToFirmware HalStartProfileInterrupt HalStopProfileInterrupt
        HalSetProfileInterval HalProcessorIdle HalMakeBeep
        READ_PORT_UCHAR READ_PORT_USHORT READ_PORT_ULONG
        READ_PORT_BUFFER_UCHAR READ_PORT_BUFFER_USHORT READ_PORT_BUFFER_ULONG
        WRITE_PORT_UCHAR WRITE_PORT_USHORT WRITE_PORT_ULONG
        WRITE_PORT_BUFFER_UCHAR WRITE_PORT_BUFFER_USHORT WRITE_PORT_BUFFER_ULONG
        HalInitSystem HalAllProcessorsStarted HalQueryRealTimeClock HalSetRealTimeClock
        HalGetEnvironmentVariable HalSetEnvironmentVariable HalAllocateAdapterChannel
        HalAdjustResourceList HalTranslateBusAddress HalGetInterruptVector
        HalEnableSystemInterrupt HalDisableSystemInterrupt HalQueryPciRoutedInterrupt
        HalQueryPciMsiSupport
        HalGetAdapter HalGetInterruptTargetInformation HalGetMessageRoutingInfo
        HalGetBusData HalGetBusDataByOffset HalSetBusData HalSetBusDataByOffset
        HalpRiscvIsDeviceMemory HalpRiscvGetPciBusRange
        HalpRiscvClaimPlicInterrupt HalpRiscvCompletePlicInterrupt
        HalRequestSoftwareInterrupt HalClearSoftwareInterrupt KeFlushWriteBuffer
        HalpRiscvClockInterrupt
    FORWARDERS
        KdComPortInUse=ntoskrnl.KdComPortInUse
        KdHvComPortInUse=ntoskrnl.KdHvComPortInUse
    IMPORTS
        ntoskrnl.exe:InbvDisplayString
        ntoskrnl.exe:IoReportHalResourceUsage
        ntoskrnl.exe:KiRiscvUnimplemented
        ntoskrnl.exe:KiRiscvRequestSoftwareInterrupt
        ntoskrnl.exe:KiRiscvClearSoftwareInterrupt
        ntoskrnl.exe:KiRiscvSetInterruptEnabled
        ntoskrnl.exe:KiRiscvQueryFeatureFlags
        ntoskrnl.exe:MmMapIoSpace
        ntoskrnl.exe:MmAllocateContiguousMemorySpecifyCache
        ntoskrnl.exe:MmFreeContiguousMemory
        ntoskrnl.exe:MmGetPhysicalAddress
        ntoskrnl.exe:ExAllocatePoolWithTag
        ntoskrnl.exe:ExFreePoolWithTag
        ntoskrnl.exe:KeGetCurrentIrql
        ntoskrnl.exe:KeSetDmaIoCoherency
        ntoskrnl.exe:KfRaiseIrql
        ntoskrnl.exe:KfLowerIrql
        ntoskrnl.exe:KeAcquireSpinLockRaiseToDpc
        ntoskrnl.exe:KeReleaseSpinLock
        ntoskrnl.exe:KeSetTimeIncrement
        ntoskrnl.exe:KeUpdateSystemTime
        ntoskrnl.exe:KeBugCheckEx)
target_compile_definitions(hal PRIVATE _NTHAL_ _NTHALDLL_ _NTSYSTEM_)
target_include_directories(hal PRIVATE ${REACTOS_SOURCE_DIR}/ntoskrnl/include)
add_dependencies(hal xdk psdk bugcodes)
add_cd_file(TARGET hal DESTINATION reactos/system32 NO_CAB FOR all)
