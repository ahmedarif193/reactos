@ fastcall -arch=arm ExAcquireFastMutex(ptr)
@ fastcall -arch=arm ExReleaseFastMutex(ptr)
@ fastcall -arch=arm ExTryToAcquireFastMutex(ptr)
@ fastcall -arch=i386 ExAcquireFastMutex(ptr) ntoskrnl.ExiAcquireFastMutex
@ fastcall -arch=i386 ExReleaseFastMutex(ptr) ntoskrnl.ExiReleaseFastMutex
@ fastcall -arch=i386 ExTryToAcquireFastMutex(ptr) ntoskrnl.ExiTryToAcquireFastMutex
@ stdcall -arch=x86_64,arm64 ExAcquireFastMutex(ptr) ntoskrnl.ExAcquireFastMutex
@ stdcall -arch=x86_64,arm64 ExReleaseFastMutex(ptr) ntoskrnl.ExReleaseFastMutex
@ stdcall -arch=x86_64,arm64 ExTryToAcquireFastMutex(ptr) ntoskrnl.ExTryToAcquireFastMutex
@ stdcall HalAcquireDisplayOwnership(ptr)
@ stdcall HalAdjustResourceList(ptr)
@ stdcall HalAllProcessorsStarted()
@ stdcall HalAllocateAdapterChannel(ptr ptr long ptr)
@ stdcall HalAllocateCommonBuffer(ptr long ptr long)
@ stdcall HalAllocateCrashDumpRegisters(ptr ptr)
@ stdcall -version=0x0601+ HalAllocateHardwareCounters(ptr long ptr ptr)
@ stdcall -version=0x0601+ HalFreeHardwareCounters(ptr)
@ stdcall -version=0x0601+ HalEnumerateEnvironmentVariablesEx(long ptr ptr)
@ stdcall -version=0x0601+ HalSetEnvironmentVariableEx(ptr ptr ptr long long)
@ stdcall -version=0x0601+ HalQueryEnvironmentVariableInfoEx(long ptr ptr ptr)
@ stdcall -version=0x0601+ HalBugCheckSystem(ptr ptr)
@ stdcall HalAssignSlotResources(ptr ptr ptr ptr long long long ptr)
@ stdcall -arch=i386,arm,arm64,x86_64 HalBeginSystemInterrupt(long long ptr)
@ stdcall HalCalibratePerformanceCounter(ptr long long)
;@ stdcall -arch=x86_64 HalCallBios()
@ fastcall HalClearSoftwareInterrupt(long)
@ stdcall -version=0x0601+ HalDisableInterrupt(ptr)
@ stdcall HalDisableSystemInterrupt(long long)
@ stdcall HalDisplayString(str)
@ stdcall -version=0x0601+ HalEnableInterrupt(ptr)
@ stdcall HalEnableSystemInterrupt(long long long)
@ stdcall -arch=i386,arm,arm64,x86_64 HalEndSystemInterrupt(long long)
@ stdcall HalFlushCommonBuffer(long long long long long)
@ stdcall HalFreeCommonBuffer(ptr long long long ptr long)
@ stdcall HalGetAdapter(ptr ptr)
@ stdcall HalGetBusData(long long long ptr long)
@ stdcall HalGetBusDataByOffset(long long long ptr long long)
@ stdcall -version=0x0601+ HalGetEnvironmentVariableEx(ptr ptr ptr ptr ptr)
@ stdcall -version=0x0601+ HalGetInterruptTargetInformation(ptr)
@ stdcall HalGetEnvironmentVariable(str long str)
@ stdcall -arch=arm64 HalGetMsiMessageAddressEx(long int64 int64 ptr ptr ptr)
@ fastcall -arch=arm HalGetInterruptSource()
@ stdcall HalGetInterruptVector(long long long long ptr ptr)
@ stdcall -version=0x0601+ HalGetVectorInput(long ptr ptr ptr ptr)
@ stdcall -version=0x0601+ HalGetMemoryCachingRequirements(int64 long ptr)
@ stdcall -version=0x0601+ HalGetMessageRoutingInfo(ptr)
@ stdcall -version=0x0601+ HalGetProcessorIdByNtNumber(long ptr)
;@ stdcall -arch=x86_64 HalHandleMcheck()
@ stdcall -arch=i386,x86_64 HalHandleNMI(ptr)
@ stdcall HalInitSystem(long ptr)
@ stdcall -arch=x86_64 HalInitializeBios(long ptr)
@ stdcall HalInitializeProcessor(long ptr)
;@ stdcall -arch=x86_64 HalIsHyperThreadingEnabled()
@ stdcall HalMakeBeep(long)
@ stdcall HalProcessorIdle()
@ stdcall HalQueryDisplayParameters(ptr ptr ptr ptr)
@ stdcall -arch=arm64 HalQueryPciMsiSupport(long long ptr ptr ptr ptr ptr)
@ stdcall HalQueryRealTimeClock(ptr)
@ stdcall HalReadDmaCounter(ptr)
@ stdcall HalReportResourceUsage()
@ stdcall -arch=arm64 HalRequestDebugWakeIpi(long)
@ stdcall -arch=arm64 HalArm64DbgGicState(long ptr ptr ptr ptr)
@ stdcall -arch=arm64 HalArm64ProfileSample(long)
@ stdcall HalRequestIpi(long)
@ fastcall HalRequestSoftwareInterrupt(long)
@ stdcall HalReturnToFirmware(long)
@ stdcall -arch=x86_64 HalSendNMI(int64)
@ stdcall -arch=x86_64 HalSendSoftwareInterrupt(int64 long)
@ stdcall HalSetBusData(long long long ptr long)
@ stdcall HalSetBusDataByOffset(long long long ptr long long)
@ stdcall HalSetDisplayParameters(long long)
@ stdcall HalSetEnvironmentVariable(str str)
@ stdcall HalSetProfileInterval(long)
@ stdcall HalSetRealTimeClock(ptr)
@ stdcall HalSetTimeIncrement(long)
@ stdcall HalStartNextProcessor(ptr ptr)
@ stdcall HalStartProfileInterrupt(long)
@ stdcall HalStopProfileInterrupt(long)
@ fastcall -arch=arm HalSweepIcache()
@ fastcall -arch=arm HalSweepDcache()
@ fastcall HalSystemVectorDispatchEntry(long long long)
@ stdcall HalTranslateBusAddress(long long long long ptr ptr)
@ stdcall -version=0x0601+ HalConvertDeviceIdtToIrql(long)
@ stdcall HalpConfigurePciRootBridge(ptr)
@ stdcall HalpRegisterPciRouteQuery(ptr)
@ stdcall HalpSetPciRoutingMap(ptr long)
@ stdcall HalpRecordPciMaxGsi(ptr)
@ stdcall -arch=x86_64,arm64 HalpGetInterruptTargetInformation(ptr)
@ stdcall -arch=x86_64,arm64 HalpGetMessageRoutingInfo(ptr)
@ stdcall -arch=i386,x86_64 IoAssignDriveLetters(ptr str ptr ptr) HalpAssignDriveLetters
@ stdcall IoFlushAdapterBuffers(ptr ptr ptr ptr long long)
@ stdcall IoFreeAdapterChannel(ptr)
@ stdcall IoFreeMapRegisters(ptr ptr long)
@ stdcall IoMapTransfer(ptr ptr ptr ptr ptr long)
@ stdcall -arch=i386,x86_64 IoReadPartitionTable(ptr long long ptr) HalpReadPartitionTable
@ stdcall -arch=i386,x86_64 IoSetPartitionInformation(ptr long long long) HalpSetPartitionInformation
@ stdcall -arch=i386,x86_64 IoWritePartitionTable(ptr long long long ptr) HalpWritePartitionTable
@ extern KdComPortInUse
@ fastcall -arch=i386,arm KeAcquireInStackQueuedSpinLock(ptr ptr)
@ fastcall -arch=i386,arm KeAcquireInStackQueuedSpinLockRaiseToSynch(ptr ptr)
@ fastcall -arch=i386,arm KeAcquireQueuedSpinLock(ptr)
@ fastcall -arch=i386,arm KeAcquireQueuedSpinLockRaiseToSynch(ptr)
@ stdcall -arch=i386,arm KeAcquireSpinLock(ptr ptr)
@ fastcall -arch=i386,arm KeAcquireSpinLockRaiseToSynch(ptr)
@ stdcall -arch=x86_64,arm64 KeAcquireInStackQueuedSpinLock(ptr ptr) ntoskrnl.KeAcquireInStackQueuedSpinLock
@ stdcall -arch=x86_64,arm64 KeAcquireInStackQueuedSpinLockRaiseToSynch(ptr ptr) ntoskrnl.KeAcquireInStackQueuedSpinLockRaiseToSynch
@ stdcall -arch=x86_64,arm64 KeAcquireQueuedSpinLock(ptr) ntoskrnl.KeAcquireQueuedSpinLock
@ stdcall -arch=x86_64,arm64 KeAcquireQueuedSpinLockRaiseToSynch(ptr) ntoskrnl.KeAcquireQueuedSpinLockRaiseToSynch
@ stdcall -arch=x86_64 KeAcquireSpinLock(ptr ptr)
@ stdcall -arch=arm64 KeAcquireSpinLock(ptr ptr) ntoskrnl.KeAcquireSpinLock
@ stdcall -arch=x86_64,arm64 KeAcquireSpinLockRaiseToSynch(ptr) ntoskrnl.KeAcquireSpinLockRaiseToSynch
@ stdcall KeFlushWriteBuffer()
@ stdcall -arch=i386,arm KeGetCurrentIrql()
@ stdcall -arch=x86_64 KeGetCurrentIrql()
@ stdcall -arch=arm64 KeGetCurrentIrql() ntoskrnl.KeGetCurrentIrql
@ stdcall -arch=i386,arm KeLowerIrql(long)
@ stdcall -arch=x86_64,arm64 KeLowerIrql(long) ntoskrnl.KeLowerIrql
@ stdcall KeQueryPerformanceCounter(ptr)
@ stdcall -arch=i386,arm KeRaiseIrql(long ptr)
@ stdcall -arch=x86_64 KeRaiseIrql(long ptr)
@ stdcall -arch=arm64 KeRaiseIrql(long ptr) ntoskrnl.KeRaiseIrql
@ stdcall -arch=i386,arm KeRaiseIrqlToDpcLevel()
@ stdcall -arch=x86_64,arm64 KeRaiseIrqlToDpcLevel() ntoskrnl.KeRaiseIrqlToDpcLevel
@ stdcall -arch=i386,arm KeRaiseIrqlToSynchLevel()
@ stdcall -arch=x86_64 KeRaiseIrqlToSynchLevel()
@ stdcall -arch=arm64 KeRaiseIrqlToSynchLevel() ntoskrnl.KeRaiseIrqlToSynchLevel
@ fastcall -arch=i386,arm KeReleaseInStackQueuedSpinLock(ptr)
@ fastcall -arch=i386,arm KeReleaseQueuedSpinLock(ptr long)
@ stdcall -arch=i386,arm KeReleaseSpinLock(ptr long)
@ stdcall -arch=x86_64,arm64 KeReleaseInStackQueuedSpinLock(ptr) ntoskrnl.KeReleaseInStackQueuedSpinLock
@ stdcall -arch=x86_64,arm64 KeReleaseQueuedSpinLock(ptr long) ntoskrnl.KeReleaseQueuedSpinLock
@ stdcall -arch=x86_64,arm64 KeReleaseSpinLock(ptr long) ntoskrnl.KeReleaseSpinLock
@ stdcall KeStallExecutionProcessor(long)
@ fastcall -arch=i386,arm KeTryToAcquireQueuedSpinLock(long ptr)
@ fastcall -arch=i386,arm KeTryToAcquireQueuedSpinLockRaiseToSynch(long ptr)
@ stdcall -arch=x86_64,arm64 KeTryToAcquireQueuedSpinLock(long ptr) ntoskrnl.KeTryToAcquireQueuedSpinLock
@ stdcall -arch=x86_64,arm64 KeTryToAcquireQueuedSpinLockRaiseToSynch(long ptr) ntoskrnl.KeTryToAcquireQueuedSpinLockRaiseToSynch
@ fastcall -arch=i386,arm KfAcquireSpinLock(ptr)
@ fastcall -arch=i386,arm KfLowerIrql(long)
@ fastcall -arch=i386,arm KfRaiseIrql(long)
@ fastcall -arch=i386,arm KfReleaseSpinLock(ptr long)
@ stdcall -arch=x86_64 KfAcquireSpinLock(ptr)
@ stdcall -arch=arm64 KfAcquireSpinLock(ptr) ntoskrnl.KfAcquireSpinLock
@ stdcall -arch=x86_64 KfLowerIrql(long)
@ stdcall -arch=arm64 KfLowerIrql(long) ntoskrnl.KfLowerIrql
@ stdcall -arch=x86_64,arm64 KfRaiseIrql(long) ntoskrnl.KfRaiseIrql
@ stdcall -arch=x86_64 KfReleaseSpinLock(ptr long)
@ stdcall -arch=arm64 KfReleaseSpinLock(ptr long) ntoskrnl.KfReleaseSpinLock
@ stdcall -arch=i386,arm,arm64,x86_64 READ_PORT_BUFFER_UCHAR(ptr ptr long)
@ stdcall -arch=i386,arm,arm64,x86_64 READ_PORT_BUFFER_ULONG(ptr ptr long)
@ stdcall -arch=i386,arm,arm64,x86_64 READ_PORT_BUFFER_USHORT(ptr ptr long)
@ stdcall -arch=i386,arm,arm64,x86_64 READ_PORT_UCHAR(ptr)
@ stdcall -arch=i386,arm,arm64,x86_64 READ_PORT_ULONG(ptr)
@ stdcall -arch=i386,arm,arm64,x86_64 READ_PORT_USHORT(ptr)
@ stdcall -arch=i386,arm,arm64,x86_64 WRITE_PORT_BUFFER_UCHAR(ptr ptr long)
@ stdcall -arch=i386,arm,arm64,x86_64 WRITE_PORT_BUFFER_ULONG(ptr ptr long)
@ stdcall -arch=i386,arm,arm64,x86_64 WRITE_PORT_BUFFER_USHORT(ptr ptr long)
@ stdcall -arch=i386,arm,arm64,x86_64 WRITE_PORT_UCHAR(ptr long)
@ stdcall -arch=i386,arm,arm64,x86_64 WRITE_PORT_ULONG(ptr long)
@ stdcall -arch=i386,arm,arm64,x86_64 WRITE_PORT_USHORT(ptr long)
@ stdcall -version=0x0502 -arch=x86_64 x86BiosAllocateBuffer(ptr ptr ptr)
@ stdcall -version=0x0600+ -arch=i386,x86_64 x86BiosAllocateBuffer(ptr ptr ptr)
@ stdcall -version=0x0502 -arch=x86_64 x86BiosCall(long ptr)
@ stdcall -version=0x0600+ -arch=i386,x86_64 x86BiosCall(long ptr)
@ stdcall -version=0x0502 -arch=x86_64 x86BiosFreeBuffer(long long)
@ stdcall -version=0x0600+ -arch=i386,x86_64 x86BiosFreeBuffer(long long)
@ stdcall -version=0x0502 -arch=x86_64 x86BiosReadMemory(long long ptr long)
@ stdcall -version=0x0600+ -arch=i386,x86_64 x86BiosReadMemory(long long ptr long)
@ stdcall -version=0x0502 -arch=x86_64 x86BiosWriteMemory(long long ptr long)
@ stdcall -version=0x0600+ -arch=i386,x86_64 x86BiosWriteMemory(long long ptr long)

; ARM64 HAL exports
@ fastcall -arch=arm64 HalSweepIcache()
@ fastcall -arch=arm64 HalSweepDcache()
@ fastcall -arch=arm64 HalSetGicPriorityMask(long)
@ fastcall -arch=arm64 HalGetGicPriorityMask()
@ fastcall -arch=arm64 HalGetInterruptSource()
@ stdcall -arch=arm64 HalPerformEndOfInterrupt()

; Internal kernel entry points needed by early ARM64 HAL code.
@ stdcall -arch=arm64 KxSaveFloatingPointState(ptr) ntoskrnl.KxSaveFloatingPointState
@ stdcall -arch=arm64 KxRestoreFloatingPointState(ptr) ntoskrnl.KxRestoreFloatingPointState
@ stdcall -arch=arm64 IopReserveIrqVectors(long long ptr)

@ stdcall -arch=arm64 VideoPortQuerySystemTime(ptr)
