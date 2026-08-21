@ stdcall CcCanIWrite(ptr long long long)
@ stdcall CcCoherencyFlushAndPurgeCache(ptr ptr long ptr long)
@ stdcall CcCopyRead(ptr ptr long long ptr ptr)
@ stdcall -version=0x602+ CcCopyReadEx(ptr ptr long long ptr ptr ptr)
@ stdcall CcCopyWrite(ptr ptr long long ptr)
@ stdcall -version=0x602+ CcCopyWriteEx(ptr ptr long long ptr ptr)
@ stdcall CcCopyWriteWontFlush(ptr ptr long)
@ stdcall CcDeferWrite(ptr ptr ptr ptr long long)
@ stdcall CcFastCopyRead(ptr long long long ptr ptr)
@ stdcall CcFastCopyWrite(ptr long long ptr)
@ extern CcFastMdlReadWait
@ extern CcFastReadNotPossible
@ extern CcFastReadWait
@ stdcall CcFlushCache(ptr ptr long ptr)
@ stdcall CcGetDirtyPages(ptr ptr ptr ptr)
@ stdcall CcGetFileObjectFromBcb(ptr)
@ stdcall CcGetFileObjectFromSectionPtrs(ptr)
@ stdcall CcGetFlushedValidData(ptr long)
@ stdcall CcGetLsnForFileObject(ptr ptr)
@ stdcall CcInitializeCacheMap(ptr ptr long ptr ptr)
@ stdcall CcIsThereDirtyData(ptr)
@ stdcall CcMapData(ptr ptr long long ptr ptr)
@ stdcall CcMdlRead(ptr ptr long ptr ptr)
@ stdcall CcMdlReadComplete(ptr ptr)
@ stdcall CcMdlWriteAbort(ptr ptr)
@ stdcall CcMdlWriteComplete(ptr ptr ptr)
@ stdcall CcPinMappedData(ptr ptr long long ptr)
@ stdcall CcPinRead(ptr ptr long long ptr ptr)
@ stdcall CcPrepareMdlWrite(ptr ptr long ptr ptr)
@ stdcall CcPreparePinWrite(ptr ptr long long long ptr ptr)
@ stdcall CcPurgeCacheSection(ptr ptr long long)
@ stdcall CcRemapBcb(ptr)
@ stdcall CcRepinBcb(ptr)
@ stdcall CcScheduleReadAhead(ptr ptr long)
@ stdcall CcSetAdditionalCacheAttributes(ptr long long)
@ stdcall -version=0x602+ CcSetAdditionalCacheAttributesEx(ptr long)
@ stdcall CcSetBcbOwnerPointer(ptr ptr)
@ stdcall CcSetDirtyPageThreshold(ptr long)
@ stdcall CcSetDirtyPinnedData(ptr ptr)
@ stdcall CcSetFileSizes(ptr ptr)
@ stdcall CcSetLogHandleForFile(ptr ptr ptr)
@ stdcall CcSetReadAheadGranularity(ptr long)
@ stdcall CcUninitializeCacheMap(ptr ptr ptr)
@ stdcall CcUnpinData(ptr)
@ stdcall CcUnpinDataForThread(ptr ptr)
@ stdcall CcUnpinRepinnedBcb(ptr long ptr)
@ stdcall CcWaitForCurrentLazyWriterActivity()
@ stdcall CcZeroData(ptr ptr ptr long)
@ stdcall CmRegisterCallback(ptr ptr ptr)
@ stdcall CmUnRegisterCallback(long long)
@ stdcall DbgBreakPoint()
@ stdcall DbgBreakPointWithStatus(long)
@ stdcall DbgCommandString(ptr ptr)
@ stdcall DbgLoadImageSymbols(ptr ptr long)
@ varargs DbgPrint(str)
@ varargs DbgPrintEx(long long str)
@ varargs DbgPrintReturnControlC(str)
@ stdcall DbgPrompt(str ptr long)
@ stdcall DbgQueryDebugFilterState(long long)
@ stdcall DbgSetDebugFilterState(long long long)
@ stdcall -arch=x86_64,arm64 ExAcquireFastMutex(ptr)
@ fastcall ExAcquireFastMutexUnsafe(ptr)
@ stdcall ExAcquireResourceExclusiveLite(ptr long)
@ stdcall ExAcquireResourceSharedLite(ptr long)
@ fastcall ExAcquireRundownProtection(ptr) ExfAcquireRundownProtection
@ fastcall ExAcquireRundownProtectionCacheAware(ptr) ExfAcquireRundownProtectionCacheAware
@ fastcall ExAcquireRundownProtectionCacheAwareEx(ptr long) ExfAcquireRundownProtectionCacheAwareEx
@ fastcall ExAcquireRundownProtectionEx(ptr long) ExfAcquireRundownProtectionEx
@ stdcall ExAcquireSharedStarveExclusive(ptr long)
@ stdcall ExAcquireSharedWaitForExclusive(ptr long)
@ stdcall ExAllocateCacheAwareRundownProtection(long long)
@ stdcall ExAllocateFromPagedLookasideList(ptr) ExiAllocateFromPagedLookasideList
@ stdcall ExAllocatePool(long long)
@ stdcall ExAllocatePoolWithQuota(long long)
@ stdcall ExAllocatePoolWithQuotaTag(long long long)
@ stdcall ExAllocatePoolWithTag(long long long)
@ stdcall ExAllocatePoolWithTagPriority(long long long long)
@ stdcall ExConvertExclusiveToSharedLite(ptr)
@ stdcall ExCreateCallback(ptr ptr long long)
@ stdcall ExDeleteLookasideListEx(ptr)
@ stdcall ExDeleteNPagedLookasideList(ptr)
@ stdcall ExDeletePagedLookasideList(ptr)
@ stdcall ExDeleteResourceLite(ptr)
@ extern ExDesktopObjectType
@ stdcall ExDisableResourceBoostLite(ptr)
@ fastcall ExEnterCriticalRegionAndAcquireFastMutexUnsafe(ptr)
@ stdcall ExEnterCriticalRegionAndAcquireResourceExclusive(ptr)
@ stdcall ExEnterCriticalRegionAndAcquireResourceShared(ptr)
@ stdcall ExEnterCriticalRegionAndAcquireSharedWaitForExclusive(ptr)
@ stdcall ExEnumHandleTable(ptr ptr ptr ptr)
@ extern ExEventObjectType
@ stdcall ExExtendZone(ptr ptr long)
@ stdcall ExFlushLookasideListEx(ptr)
@ stdcall ExFreeCacheAwareRundownProtection(ptr)
@ stdcall EtwRegister(ptr ptr ptr ptr)
@ stdcall EtwUnregister(int64)
@ stdcall EtwWrite(int64 ptr ptr long ptr)
@ stdcall ExFreePool(ptr)
@ stdcall ExFreePoolWithTag(ptr long)
@ stdcall ExFreeToPagedLookasideList(ptr ptr) ExiFreeToPagedLookasideList
@ stdcall ExGetCurrentProcessorCounts(ptr ptr ptr)
@ stdcall ExGetCurrentProcessorCpuUsage(ptr)
@ stdcall ExGetExclusiveWaiterCount(ptr)
@ stdcall ExGetPreviousMode()
@ stdcall ExGetSharedWaiterCount(ptr)
@ stdcall ExInitializeLookasideListEx(ptr ptr ptr long long long long long)
@ stdcall ExInitializeNPagedLookasideList(ptr ptr ptr long long long long)
@ stdcall ExInitializePagedLookasideList(ptr ptr ptr long long long long)
@ stdcall ExInitializeResourceLite(ptr)
@ fastcall ExInitializeRundownProtection(ptr) ExfInitializeRundownProtection
@ stdcall ExInitializeRundownProtectionCacheAware(ptr long)
@ stdcall ExInitializeZone(ptr long ptr long)
@ stdcall ExInterlockedAddLargeInteger(ptr long long ptr)
@ fastcall -arch=i386 ExInterlockedAddLargeStatistic(ptr long)
@ stdcall ExInterlockedAddUlong(ptr long ptr)
@ fastcall -arch=i386 ExInterlockedCompareExchange64(ptr ptr ptr ptr)
@ stdcall -arch=i386 ExInterlockedDecrementLong(ptr ptr)
@ stdcall -arch=i386 ExInterlockedExchangeUlong(ptr long ptr)
@ stdcall ExInterlockedExtendZone(ptr ptr long ptr)
@ fastcall -arch=i386 ExInterlockedFlushSList(ptr)
@ stdcall -arch=i386 ExInterlockedIncrementLong(ptr ptr)
@ stdcall ExInterlockedInsertHeadList(ptr ptr ptr)
@ stdcall ExInterlockedInsertTailList(ptr ptr ptr)
@ stdcall ExInterlockedPopEntryList(ptr ptr)
@ fastcall -arch=i386 ExInterlockedPopEntrySList(ptr ptr)
@ stdcall ExInterlockedPushEntryList(ptr ptr ptr)
@ fastcall -arch=i386 ExInterlockedPushEntrySList(ptr ptr ptr)
@ stdcall ExInterlockedRemoveHeadList(ptr ptr)
@ stdcall ExIsProcessorFeaturePresent(long)
@ stdcall ExIsResourceAcquiredExclusiveLite(ptr)
@ stdcall ExIsResourceAcquiredSharedLite(ptr)
@ stdcall ExLocalTimeToSystemTime(ptr ptr)
@ stdcall ExNotifyCallback(ptr ptr ptr)
@ stdcall -arch=x86_64,arm64,arm ExQueryDepthSList(ptr) RtlQueryDepthSList
@ stdcall ExQueryPoolBlockSize(ptr ptr)
@ stdcall ExQueueWorkItem(ptr long)
@ stdcall ExRaiseAccessViolation()
@ stdcall ExRaiseDatatypeMisalignment()
@ stdcall ExRaiseException(ptr) RtlRaiseException
@ stdcall ExRaiseHardError(long long long ptr long ptr)
@ stdcall ExRaiseStatus(long) RtlRaiseStatus
@ fastcall ExReInitializeRundownProtection(ptr) ExfReInitializeRundownProtection
@ fastcall ExReInitializeRundownProtectionCacheAware(ptr) ExfReInitializeRundownProtectionCacheAware
@ stdcall ExRegisterCallback(ptr ptr ptr)
@ stdcall ExReinitializeResourceLite(ptr)
@ stdcall -arch=x86_64,arm64 ExReleaseFastMutex(ptr)
@ fastcall ExReleaseFastMutexUnsafe(ptr)
@ fastcall ExReleaseFastMutexUnsafeAndLeaveCriticalRegion(ptr)
@ fastcall ExReleaseResourceAndLeaveCriticalRegion(ptr)
@ stdcall ExReleaseResourceForThreadLite(ptr long)
@ fastcall ExReleaseResourceLite(ptr)
@ fastcall ExReleaseRundownProtection(ptr) ExfReleaseRundownProtection
@ fastcall ExReleaseRundownProtectionCacheAware(ptr) ExfReleaseRundownProtectionCacheAware
@ fastcall ExReleaseRundownProtectionCacheAwareEx(ptr long) ExfReleaseRundownProtectionCacheAwareEx
@ fastcall ExReleaseRundownProtectionEx(ptr long) ExfReleaseRundownProtectionEx
@ fastcall ExRundownCompleted(ptr) ExfRundownCompleted
@ fastcall ExRundownCompletedCacheAware(ptr) ExfRundownCompletedCacheAware
@ extern ExSemaphoreObjectType
@ stdcall ExSetResourceOwnerPointer(ptr ptr)
@ stdcall ExSetTimerResolution(long long)
@ stdcall ExSizeOfRundownProtectionCacheAware()
@ stdcall ExSystemExceptionFilter()
@ stdcall ExSystemTimeToLocalTime(ptr ptr)
@ stdcall -arch=x86_64,arm64 ExTryToAcquireFastMutex(ptr)
@ stdcall ExUnregisterCallback(ptr)
@ stdcall ExUuidCreate(ptr)
@ stdcall ExVerifySuite(long)
@ fastcall ExWaitForRundownProtectionRelease(ptr) ExfWaitForRundownProtectionRelease
@ fastcall ExWaitForRundownProtectionReleaseCacheAware(ptr) ExfWaitForRundownProtectionReleaseCacheAware
@ extern ExWindowStationObjectType
@ fastcall ExfAcquirePushLockExclusive(ptr)
@ fastcall ExfAcquirePushLockShared(ptr)
@ fastcall -arch=i386 ExfInterlockedAddUlong(ptr long ptr)
@ fastcall -arch=i386 ExfInterlockedCompareExchange64(ptr ptr ptr)
@ fastcall -arch=i386 ExfInterlockedInsertHeadList(ptr ptr ptr)
@ fastcall -arch=i386 ExfInterlockedInsertTailList(ptr ptr ptr)
@ fastcall -arch=i386 ExfInterlockedPopEntryList(ptr ptr)
@ fastcall -arch=i386 ExfInterlockedPushEntryList(ptr ptr ptr)
@ fastcall -arch=i386 ExfInterlockedRemoveHeadList(ptr ptr)
@ fastcall ExfReleasePushLock(ptr)
@ fastcall ExfReleasePushLockExclusive(ptr)
@ fastcall ExfReleasePushLockShared(ptr)
@ fastcall ExfTryToWakePushLock(ptr)
@ fastcall ExfUnblockPushLock(ptr ptr)
@ stdcall -arch=x86_64,arm64,arm ExpInterlockedFlushSList(ptr) RtlInterlockedFlushSList
@ stdcall -arch=x86_64,arm64,arm ExpInterlockedPopEntrySList(ptr ptr) RtlInterlockedPopEntrySList
@ stdcall -arch=x86_64,arm64,arm ExpInterlockedPushEntrySList(ptr ptr) RtlInterlockedPushEntrySList
@ fastcall -arch=i386 Exfi386InterlockedDecrementLong(ptr)
@ fastcall -arch=i386 Exfi386InterlockedExchangeUlong(ptr long)
@ fastcall -arch=i386 Exfi386InterlockedIncrementLong(ptr)
@ stdcall -arch=i386 Exi386InterlockedDecrementLong(ptr)
@ stdcall -arch=i386 Exi386InterlockedExchangeUlong(ptr long)
@ stdcall -arch=i386 Exi386InterlockedIncrementLong(ptr)
@ fastcall -arch=i386 ExiAcquireFastMutex(ptr) ExAcquireFastMutex
@ fastcall -arch=i386 ExiReleaseFastMutex(ptr) ExReleaseFastMutex
@ fastcall -arch=i386 ExiTryToAcquireFastMutex(ptr) ExTryToAcquireFastMutex
@ stdcall FsRtlAcquireFileExclusive(ptr)
@ stdcall FsRtlAddBaseMcbEntry(ptr long long long long long long)
@ stdcall FsRtlAddLargeMcbEntry(ptr long long long long long long)
@ stdcall FsRtlAddMcbEntry(ptr long long long)
@ stdcall FsRtlAddToTunnelCache(ptr long long ptr ptr long long ptr)
@ stdcall FsRtlAllocateFileLock(ptr ptr)
@ stdcall FsRtlAllocatePool(long long)
@ stdcall FsRtlAllocatePoolWithQuota(long long)
@ stdcall FsRtlAllocatePoolWithQuotaTag(long long long)
@ stdcall FsRtlAllocatePoolWithTag(long long long)
@ stdcall FsRtlAllocateResource()
@ stdcall FsRtlAreNamesEqual(ptr ptr long wstr)
@ stdcall FsRtlAreThereCurrentOrInProgressFileLocks(ptr)
@ stdcall -version=0x602+ FsRtlAreThereWaitingFileLocks(ptr)
@ stdcall FsRtlAreVolumeStartupApplicationsComplete()
@ stdcall FsRtlBalanceReads(ptr)
@ stdcall -version=0x602+ FsRtlCheckLockForOplockRequest(ptr ptr)
@ stdcall FsRtlCheckLockForReadAccess(ptr ptr)
@ stdcall FsRtlCheckLockForWriteAccess(ptr ptr)
@ stdcall FsRtlCheckOplock(ptr ptr ptr ptr ptr)
@ stdcall FsRtlCheckOplockEx(ptr ptr long ptr ptr ptr)
@ stdcall FsRtlCopyRead(ptr ptr long long long ptr ptr ptr)
@ stdcall FsRtlCopyWrite(ptr ptr long long long ptr ptr ptr)
@ stdcall FsRtlCurrentOplockH(ptr)
@ stdcall FsRtlCreateSectionForDataScan(ptr ptr ptr ptr long ptr ptr long long long)
@ stdcall FsRtlCurrentBatchOplock(ptr)
@ stdcall FsRtlDeleteKeyFromTunnelCache(ptr long long)
@ stdcall FsRtlDeleteTunnelCache(ptr)
@ stdcall FsRtlDeregisterUncProvider(ptr)
@ stdcall -version=0x602+ FsRtlDismountComplete(ptr long)
@ stdcall FsRtlDissectDbcs(long ptr ptr ptr)
@ stdcall FsRtlDissectName(long ptr ptr ptr)
@ stdcall FsRtlDoesDbcsContainWildCards(ptr)
@ stdcall FsRtlDoesNameContainWildCards(ptr)
@ stdcall FsRtlFastCheckLockForRead(ptr ptr ptr long ptr ptr)
@ stdcall FsRtlFastCheckLockForWrite(ptr ptr ptr long ptr ptr)
@ stdcall FsRtlFastUnlockAll(ptr ptr ptr ptr)
@ stdcall FsRtlFastUnlockAllByKey(ptr ptr ptr long ptr)
@ stdcall FsRtlFastUnlockSingle(ptr ptr ptr ptr ptr long ptr long)
@ stdcall FsRtlFindInTunnelCache(ptr long long ptr ptr ptr ptr ptr)
@ stdcall FsRtlFreeFileLock(ptr)
@ stdcall FsRtlGetEcpListFromIrp(ptr ptr)
@ stdcall FsRtlGetFileSize(ptr ptr)
@ stdcall FsRtlGetNextBaseMcbEntry(ptr long ptr ptr ptr)
@ stdcall FsRtlGetNextExtraCreateParameter(ptr ptr ptr ptr ptr)
@ stdcall FsRtlGetNextFileLock(ptr long)
@ stdcall FsRtlGetNextLargeMcbEntry(ptr long ptr ptr ptr)
@ stdcall -version=0x602+ FsRtlGetSectorSizeInformation(ptr ptr)
@ stdcall FsRtlGetNextMcbEntry(ptr long ptr ptr ptr)
@ stdcall FsRtlIncrementCcFastReadNoWait()
@ stdcall FsRtlIncrementCcFastReadNotPossible()
@ stdcall -version=0x602+ FsRtlUpdateDiskCounters(int64 int64)
@ stdcall FsRtlIncrementCcFastReadResourceMiss()
@ stdcall FsRtlIncrementCcFastReadWait()
@ stdcall FsRtlInitializeBaseMcb(ptr ptr)
@ stdcall FsRtlInitializeFileLock(ptr ptr ptr)
@ stdcall FsRtlInitializeLargeMcb(ptr long)
@ stdcall FsRtlInitializeMcb(ptr long)
@ stdcall FsRtlInitializeOplock(ptr)
@ stdcall FsRtlInitializeTunnelCache(ptr)
@ stdcall FsRtlInsertPerFileObjectContext(ptr ptr)
@ stdcall FsRtlInsertPerStreamContext(ptr ptr)
@ stdcall FsRtlIsDbcsInExpression(ptr ptr)
@ stdcall FsRtlIsFatDbcsLegal(long ptr long long long)
@ stdcall FsRtlIsHpfsDbcsLegal(long ptr long long long)
@ stdcall FsRtlIsNameInExpression(ptr ptr long wstr)
@ stdcall FsRtlIsNtstatusExpected(long)
@ stdcall FsRtlIsPagingFile(ptr)
@ stdcall FsRtlIsTotalDeviceFailure(ptr)
@ extern FsRtlLegalAnsiCharacterArray
@ stdcall FsRtlLookupBaseMcbEntry(ptr long long ptr ptr ptr ptr ptr)
@ stdcall FsRtlLookupLargeMcbEntry(ptr long long ptr ptr ptr ptr ptr)
@ stdcall FsRtlLookupLastBaseMcbEntry(ptr ptr ptr)
@ stdcall FsRtlLookupLastBaseMcbEntryAndIndex(ptr ptr ptr ptr)
@ stdcall FsRtlLookupLastLargeMcbEntry(ptr ptr ptr)
@ stdcall FsRtlLookupLastLargeMcbEntryAndIndex(ptr ptr ptr ptr)
@ stdcall FsRtlLookupLastMcbEntry(ptr ptr ptr)
@ stdcall FsRtlLookupMcbEntry(ptr long ptr ptr ptr)
@ stdcall FsRtlLookupPerFileObjectContext(ptr ptr ptr)
@ stdcall FsRtlLookupPerStreamContextInternal(ptr ptr ptr)
@ stdcall FsRtlMdlRead(ptr ptr long long ptr ptr)
@ stdcall FsRtlMdlReadComplete(ptr ptr)
@ stdcall FsRtlMdlReadCompleteDev(ptr ptr ptr)
@ stdcall FsRtlMdlReadDev(ptr ptr long long ptr ptr ptr)
@ stdcall FsRtlMdlWriteComplete(ptr ptr ptr)
@ stdcall FsRtlMdlWriteCompleteDev(ptr ptr ptr ptr)
@ stdcall FsRtlNormalizeNtstatus(long long)
@ stdcall FsRtlNotifyChangeDirectory(ptr ptr ptr ptr long long ptr)
@ stdcall FsRtlNotifyCleanup(ptr ptr ptr)
@ stdcall FsRtlNotifyFilterChangeDirectory(ptr ptr ptr ptr long long long ptr ptr ptr ptr)
@ stdcall FsRtlNotifyFilterReportChange(ptr ptr ptr long ptr ptr long long ptr ptr)
@ stdcall FsRtlNotifyFullChangeDirectory(ptr ptr ptr ptr long long long ptr ptr ptr)
@ stdcall FsRtlNotifyFullReportChange(ptr ptr ptr long ptr ptr long long ptr)
@ stdcall FsRtlNotifyInitializeSync(ptr)
@ stdcall FsRtlNotifyReportChange(ptr ptr ptr ptr long)
@ stdcall FsRtlNotifyUninitializeSync(ptr)
@ stdcall FsRtlNotifyVolumeEvent(ptr long)
@ stdcall FsRtlNumberOfRunsInBaseMcb(ptr)
@ stdcall FsRtlOplockBreakH(ptr ptr long ptr ptr ptr)
@ stdcall FsRtlOplockIsSharedRequest(ptr)
@ stdcall FsRtlNumberOfRunsInLargeMcb(ptr)
@ stdcall FsRtlNumberOfRunsInMcb(ptr)
@ stdcall FsRtlOplockFsctrl(ptr ptr long)
@ stdcall FsRtlOplockIsFastIoPossible(ptr)
@ stdcall FsRtlPostPagingFileStackOverflow(ptr ptr ptr)
@ stdcall FsRtlPostStackOverflow(ptr ptr ptr)
@ stdcall FsRtlPrepareMdlWrite(ptr ptr long long ptr ptr)
@ stdcall FsRtlPrepareMdlWriteDev(ptr ptr long long ptr ptr ptr)
@ stdcall FsRtlPrivateLock(ptr ptr ptr ptr ptr long long long ptr ptr ptr long)
@ stdcall FsRtlProcessFileLock(ptr ptr ptr)
@ stdcall FsRtlRegisterFileSystemFilterCallbacks(ptr ptr)
@ stdcall FsRtlRegisterUncProvider(ptr ptr long)
@ stdcall FsRtlReleaseFile(ptr)
@ stdcall FsRtlRemoveDotsFromPath(ptr long ptr)
@ stdcall FsRtlValidateReparsePointBuffer(long ptr)
@ stdcall FsRtlRemoveBaseMcbEntry(ptr long long long long)
@ stdcall FsRtlRemoveLargeMcbEntry(ptr long long long long)
@ stdcall FsRtlRemoveMcbEntry(ptr long long)
@ stdcall FsRtlRemovePerFileObjectContext(ptr ptr ptr)
@ stdcall FsRtlRemovePerStreamContext(ptr ptr ptr)
@ stdcall FsRtlResetBaseMcb(ptr)
@ stdcall FsRtlResetLargeMcb(ptr long)
@ stdcall FsRtlSplitBaseMcb(ptr long long long long)
@ stdcall FsRtlSplitLargeMcb(ptr long long long long)
@ stdcall FsRtlSyncVolumes(long long long)
@ stdcall FsRtlTeardownPerStreamContexts(ptr)
@ stdcall FsRtlTruncateBaseMcb(ptr long long)
@ stdcall FsRtlTruncateLargeMcb(ptr long long)
@ stdcall FsRtlTruncateMcb(ptr long)
@ stdcall FsRtlUninitializeBaseMcb(ptr)
@ stdcall FsRtlUninitializeFileLock(ptr)
@ stdcall FsRtlUninitializeLargeMcb(ptr)
@ stdcall FsRtlUninitializeMcb(ptr)
@ stdcall FsRtlUninitializeOplock(ptr)
@ extern HalDispatchTable
@ fastcall HalExamineMBR(ptr long long ptr)
@ extern HalPrivateDispatchTable
@ stdcall HeadlessDispatch(long ptr long ptr ptr)
@ stdcall InbvAcquireDisplayOwnership()
@ stdcall InbvCheckDisplayOwnership()
@ stdcall InbvDisplayString(str)
@ stdcall InbvEnableBootDriver(long)
@ stdcall InbvEnableDisplayString(long)
@ stdcall InbvGetGopFrameBufferInfo(ptr)
@ stdcall InbvInstallDisplayStringFilter(ptr)
@ stdcall InbvIsBootDriverInstalled()
@ stdcall InbvNotifyDisplayOwnershipLost(ptr)
@ stdcall InbvResetDisplay()
@ stdcall InbvSetScrollRegion(long long long long)
@ stdcall InbvSetTextColor(long)
@ stdcall InbvSolidColorFill(long long long long long)
@ extern InitSafeBootMode
@ fastcall -arch=i386,arm,arm64 InterlockedCompareExchange(ptr long long)
@ fastcall -arch=i386,arm,arm64 InterlockedDecrement(ptr)
@ fastcall -arch=i386,arm,arm64 InterlockedExchange(ptr long)
@ fastcall -arch=i386,arm,arm64 InterlockedExchangeAdd(ptr long)
@ fastcall -arch=i386,arm,arm64 InterlockedIncrement(ptr)
@ fastcall -arch=i386 InterlockedPopEntrySList(ptr)
@ fastcall -arch=i386 InterlockedPushEntrySList(ptr ptr)
@ stdcall -arch=arm InterlockedPopEntrySList(ptr) RtlInterlockedPopEntrySList
@ stdcall -arch=arm InterlockedPushEntrySList(ptr ptr) RtlInterlockedPushEntrySList
@ stdcall -arch=x86_64,arm64 InitializeSListHead(ptr) RtlInitializeSListHead
@ stdcall IoAcquireCancelSpinLock(ptr)
@ stdcall IoAcquireRemoveLockEx(ptr ptr str long long)
@ stdcall IoAcquireVpbSpinLock(ptr)
@ extern IoAdapterObjectType
@ stdcall IoAllocateAdapterChannel(ptr ptr long ptr ptr)
@ stdcall IoAllocateController(ptr ptr ptr ptr)
@ stdcall IoAllocateDriverObjectExtension(ptr ptr long ptr)
@ stdcall IoAllocateErrorLogEntry(ptr long)
@ stdcall IoAllocateIrp(long long)
@ stdcall IoAllocateMdl(ptr long long long ptr)
@ stdcall IoAllocateWorkItem(ptr)
@ fastcall IoAssignDriveLetters(ptr ptr ptr ptr)
@ stdcall IoAssignResources(ptr ptr ptr ptr ptr ptr)
@ stdcall IoAttachDevice(ptr ptr ptr)
@ stdcall IoAttachDeviceByPointer(ptr ptr)
@ stdcall IoAttachDeviceToDeviceStack(ptr ptr)
@ stdcall IoAttachDeviceToDeviceStackSafe(ptr ptr ptr)
@ stdcall IoBuildAsynchronousFsdRequest(long ptr ptr long ptr ptr)
@ stdcall IoBuildDeviceIoControlRequest(long ptr ptr long ptr long long ptr ptr)
@ stdcall IoBuildPartialMdl(ptr ptr ptr long)
@ stdcall IoBuildSynchronousFsdRequest(long ptr ptr long ptr ptr ptr)
@ stdcall IoCallDriver(ptr ptr)
@ stdcall IoCancelFileOpen(ptr ptr)
@ stdcall IoCancelIrp(ptr)
@ stdcall IoCheckDesiredAccess(ptr long)
@ stdcall IoCheckEaBufferValidity(ptr long ptr)
@ stdcall IoCheckFunctionAccess(long long long long ptr ptr)
@ stdcall IoCheckQuerySetFileInformation(long long long)
@ stdcall IoCheckQuerySetVolumeInformation(long long long)
@ stdcall IoCheckQuotaBufferValidity(ptr long ptr)
@ stdcall IoCheckShareAccess(long long ptr ptr long)
@ stdcall IoCompleteRequest(ptr long)
@ stdcall IoConnectInterrupt(ptr ptr ptr ptr long long long long long long long)
@ stdcall IoConnectInterruptEx(ptr)
@ stdcall IoCreateController(long)
@ stdcall IoCreateDevice(ptr long ptr long long long ptr)
@ stdcall IoCreateDisk(ptr ptr)
@ stdcall IoCreateDriver(ptr ptr)
@ stdcall IoCreateFile(ptr long ptr ptr ptr long long long long ptr long long ptr long)
@ stdcall IoCreateFileSpecifyDeviceObjectHint(ptr long ptr ptr ptr long long long long ptr long long ptr long ptr)
@ stdcall IoCreateNotificationEvent(ptr ptr)
@ stdcall IoCreateStreamFileObject(ptr ptr)
@ stdcall IoCreateStreamFileObjectEx(ptr ptr ptr)
@ stdcall IoCreateStreamFileObjectLite(ptr ptr)
@ stdcall IoCreateSymbolicLink(ptr ptr)
@ stdcall IoCreateSynchronizationEvent(ptr ptr)
@ stdcall IoCreateUnprotectedSymbolicLink(ptr ptr)
@ stdcall IoCsqInitialize(ptr ptr ptr ptr ptr ptr ptr)
@ stdcall IoCsqInitializeEx(ptr ptr ptr ptr ptr ptr ptr)
@ stdcall IoCsqInsertIrp(ptr ptr ptr)
@ stdcall IoCsqInsertIrpEx(ptr ptr ptr ptr)
@ stdcall IoCsqRemoveIrp(ptr ptr)
@ stdcall IoCsqRemoveNextIrp(ptr ptr)
@ stdcall IoDeleteController(ptr)
@ stdcall IoDeleteDevice(ptr)
@ stdcall IoDeleteDriver(ptr)
@ stdcall IoDeleteSymbolicLink(ptr)
@ stdcall IoDetachDevice(ptr)
@ extern IoDeviceHandlerObjectSize
@ extern IoDeviceHandlerObjectType
@ extern IoDeviceObjectType
@ stdcall IoDisconnectInterrupt(ptr)
@ stdcall IoDisconnectInterruptEx(ptr)
@ extern IoDriverObjectType
@ stdcall IoEnqueueIrp(ptr)
@ stdcall IoEnumerateDeviceObjectList(ptr ptr long ptr)
@ stdcall IoEnumerateRegisteredFiltersList(ptr long ptr)
@ stdcall IoFastQueryNetworkAttributes(ptr long long ptr ptr)
@ extern IoFileObjectType
@ stdcall IoForwardAndCatchIrp(ptr ptr) IoForwardIrpSynchronously
@ stdcall IoForwardIrpSynchronously(ptr ptr)
@ stdcall IoFreeController(ptr)
@ stdcall IoFreeErrorLogEntry(ptr)
@ stdcall IoFreeIrp(ptr)
@ stdcall IoFreeMdl(ptr)
@ stdcall IoFreeWorkItem(ptr)
@ stdcall IoGetAttachedDevice(ptr)
@ stdcall IoGetAttachedDeviceReference(ptr)
@ stdcall IoGetBaseFileSystemDeviceObject(ptr)
@ stdcall IoGetBootDiskInformation(ptr long)
@ stdcall IoGetConfigurationInformation()
@ stdcall IoGetCurrentProcess()
@ stdcall IoGetDeviceAttachmentBaseRef(ptr)
@ stdcall IoGetDeviceInterfaceAlias(ptr ptr ptr)
@ stdcall IoGetDeviceInterfaces(ptr ptr long ptr)
@ stdcall IoGetDeviceObjectPointer(ptr long ptr ptr)
@ stdcall IoGetDeviceProperty(ptr long long ptr ptr)
@ stdcall IoGetDeviceToVerify(ptr)
@ stdcall IoGetDiskDeviceObject(ptr ptr)
@ stdcall IoGetDmaAdapter(ptr ptr ptr)
@ stdcall IoGetDriverObjectExtension(ptr ptr)
@ stdcall IoGetFileObjectGenericMapping()
@ stdcall IoGetInitialStack()
@ stdcall IoGetIoPriorityHint(ptr)
@ stdcall IoGetIrpExtraCreateParameter(ptr ptr)
@ stdcall IoGetLowerDeviceObject(ptr)
@ fastcall IoGetPagingIoPriority(ptr)
@ stdcall IoGetRelatedDeviceObject(ptr)
@ stdcall IoGetRequestorProcess(ptr)
@ stdcall IoGetRequestorProcessId(ptr)
@ stdcall IoGetRequestorSessionId(ptr ptr)
@ stdcall IoGetStackLimits(ptr ptr)
@ stdcall IoGetTopLevelIrp()
@ stdcall IoInitializeIrp(ptr long long)
@ stdcall IoInitializeRemoveLockEx(ptr long long long long)
@ stdcall IoInitializeTimer(ptr ptr ptr)
@ stdcall IoInvalidateDeviceRelations(ptr long)
@ stdcall IoInvalidateDeviceState(ptr)
@ stdcall -arch=x86_64,arm64 IoIs32bitProcess(ptr)
@ stdcall IoIsFileOriginRemote(ptr)
@ stdcall IoIsOperationSynchronous(ptr)
@ stdcall IoIsSystemThread(ptr)
@ stdcall IoIsValidNameGraftingBuffer(ptr ptr)
@ stdcall IoIsWdmVersionAvailable(long long)
@ stdcall IoMakeAssociatedIrp(ptr long)
@ stdcall IoOpenDeviceInterfaceRegistryKey(ptr long ptr)
@ stdcall IoOpenDeviceRegistryKey(ptr long long ptr)
@ stdcall IoPageRead(ptr ptr ptr ptr ptr)
@ stdcall IoPnPDeliverServicePowerNotification(long long long long)
@ stdcall IoQueryDeviceDescription(ptr ptr ptr ptr ptr ptr ptr ptr)
@ stdcall IoQueryFileDosDeviceName(ptr ptr)
@ stdcall IoQueryFileInformation(ptr long long ptr ptr)
@ stdcall IoQueryVolumeInformation(ptr long long ptr ptr)
@ stdcall IoQueueThreadIrp(ptr)
@ stdcall IoQueueWorkItem(ptr ptr long ptr)
@ stdcall IoQueueWorkItemEx(ptr ptr long ptr)
@ stdcall IoRaiseHardError(ptr ptr ptr)
@ stdcall IoRaiseInformationalHardError(long ptr ptr)
@ stdcall IoReadDiskSignature(ptr long ptr)
@ extern IoReadOperationCount
@ fastcall IoReadPartitionTable(ptr long long ptr)
@ stdcall IoReadPartitionTableEx(ptr ptr)
@ extern IoReadTransferCount
@ stdcall IoRegisterBootDriverReinitialization(ptr ptr ptr)
@ stdcall IoRegisterDeviceInterface(ptr ptr ptr ptr)
@ stdcall IoRegisterDriverReinitialization(ptr ptr ptr)
@ stdcall IoRegisterFileSystem(ptr)
@ stdcall IoRegisterFsRegistrationChange(ptr ptr)
@ stdcall IoRegisterLastChanceShutdownNotification(ptr)
@ stdcall IoRegisterPlugPlayNotification(long long ptr ptr ptr ptr ptr)
@ stdcall IoRegisterShutdownNotification(ptr)
@ stdcall IoReleaseCancelSpinLock(long)
@ stdcall IoReleaseRemoveLockAndWaitEx(ptr ptr long)
@ stdcall IoReleaseRemoveLockEx(ptr ptr long)
@ stdcall IoReleaseVpbSpinLock(long)
@ stdcall IoRemoveShareAccess(ptr ptr)
@ stdcall IoReportDetectedDevice(ptr long long long ptr ptr long ptr)
@ stdcall IoReportHalResourceUsage(ptr ptr ptr long)
@ stdcall IoReportResourceForDetection(ptr ptr long ptr ptr long ptr)
@ stdcall IoReportResourceUsage(ptr ptr ptr long ptr ptr long long ptr)
@ stdcall IoReportTargetDeviceChange(ptr ptr)
@ stdcall IoReportTargetDeviceChangeAsynchronous(ptr ptr ptr ptr)
@ stdcall IoRequestDeviceEject(ptr)
@ stdcall -version=0x600+ IoRetrievePriorityInfo(ptr ptr ptr ptr)
@ stdcall IoReuseIrp(ptr long)
@ stdcall IoGetDevicePropertyData(ptr ptr long long long ptr ptr ptr)
@ stdcall IoSetCompletionRoutineEx(ptr ptr ptr ptr long long long)
@ stdcall IoSetDeviceInterfacePropertyData(ptr ptr long long long long ptr)
@ stdcall IoSetDeviceInterfaceState(ptr long)
@ stdcall IoSetDevicePropertyData(ptr ptr long long long long ptr)
@ stdcall IoSetDeviceToVerify(ptr ptr)
@ stdcall IoSetFileOrigin(ptr long)
@ stdcall IoSetHardErrorOrVerifyDevice(ptr ptr)
@ stdcall IoSetInformation(ptr ptr long ptr)
@ stdcall IoSetMasterIrpStatus(ptr long)
@ stdcall IoSetIoCompletion(ptr ptr ptr long ptr long)
@ fastcall IoSetPartitionInformation(ptr long long long)
@ stdcall IoSetPartitionInformationEx(ptr long ptr)
@ stdcall IoSetShareAccess(long long ptr ptr)
@ stdcall IoSetStartIoAttributes(ptr long long)
@ stdcall IoSetSystemPartition(ptr)
@ stdcall IoSetThreadHardErrorMode(long)
@ stdcall IoSetTopLevelIrp(ptr)
@ stdcall IoStartNextPacket(ptr long)
@ stdcall IoStartNextPacketByKey(ptr long long)
@ stdcall IoStartPacket(ptr ptr ptr ptr)
@ stdcall IoStartTimer(ptr)
@ extern IoStatisticsLock
@ stdcall IoStopTimer(ptr)
@ stdcall IoSynchronousInvalidateDeviceRelations(ptr long)
@ stdcall IoSynchronousPageWrite(ptr ptr ptr ptr ptr)
@ stdcall IoThreadToProcess(ptr)
@ stdcall IoTranslateBusAddress(long long long long ptr ptr)
@ stdcall IoUnregisterFileSystem(ptr)
@ stdcall IoUnregisterFsRegistrationChange(ptr ptr)
@ stdcall IoUnregisterPlugPlayNotification(ptr)
@ stdcall IoUnregisterShutdownNotification(ptr)
@ stdcall IoUpdateShareAccess(ptr ptr)
@ stdcall IoValidateDeviceIoControlAccess(ptr long)
@ stdcall IoVerifyPartitionTable(ptr long)
@ stdcall IoVerifyVolume(ptr long)
@ stdcall IoVolumeDeviceToDosName(ptr ptr)
@ stdcall -version=0x602+ IoVolumeDeviceToGuid(ptr ptr)
@ stdcall -version=0x602+ IoVolumeDeviceToGuidPath(ptr ptr)
@ stdcall IoWMIAllocateInstanceIds(ptr long ptr)
@ stdcall IoWMIDeviceObjectToInstanceName(ptr ptr ptr)
@ stdcall -arch=x86_64,arm64 IoWMIDeviceObjectToProviderId(ptr)
@ stdcall IoWMIExecuteMethod(ptr ptr long long ptr ptr)
@ stdcall IoWMIHandleToInstanceName(ptr ptr ptr)
@ stdcall IoWMIOpenBlock(ptr long ptr)
@ stdcall IoWMIQueryAllData(ptr ptr ptr)
@ stdcall IoWMIQueryAllDataMultiple(ptr long ptr ptr)
@ stdcall IoWMIQuerySingleInstance(ptr ptr ptr ptr)
@ stdcall IoWMIQuerySingleInstanceMultiple(ptr ptr long ptr ptr)
@ stdcall IoWMIRegistrationControl(ptr long)
@ stdcall IoWMISetNotificationCallback(ptr ptr ptr)
@ stdcall IoWMISetSingleInstance(ptr ptr long long ptr)
@ stdcall IoWMISetSingleItem(ptr ptr long long long ptr)
@ stdcall IoWMISuggestInstanceName(ptr ptr long ptr)
@ stdcall IoWMIWriteEvent(ptr)
@ stdcall IoWriteErrorLogEntry(ptr)
@ extern IoWriteOperationCount
@ fastcall IoWritePartitionTable(ptr long long long ptr)
@ stdcall IoWritePartitionTableEx(ptr ptr)
@ extern IoWriteTransferCount
@ fastcall IofCallDriver(ptr ptr)
@ fastcall IofCompleteRequest(ptr long)
@ stdcall KdChangeOption(long long ptr long ptr ptr)
@ extern KdDebuggerEnabled
@ extern KdDebuggerNotPresent
@ stdcall KdDisableDebugger()
@ stdcall KdEnableDebugger()
@ extern KdEnteredDebugger
@ stdcall KdPollBreakIn()
@ stdcall KdPowerTransition(long)
@ stdcall KdRefreshDebuggerNotPresent()
@ stdcall KdSystemDebugControl(long ptr long ptr long ptr long)
@ stdcall -arch=i386 Ke386CallBios(long ptr)
@ stdcall -arch=i386 Ke386IoSetAccessProcess(ptr long)
@ stdcall -arch=i386 Ke386QueryIoAccessMap(long ptr)
@ stdcall -arch=i386 Ke386SetIoAccessMap(long ptr)
@ fastcall KeAcquireGuardedMutex(ptr)
@ fastcall KeAcquireGuardedMutexUnsafe(ptr)
@ cdecl -arch=x86_64,arm64 KeAcquireInStackQueuedSpinLock(ptr ptr)
@ fastcall KeAcquireInStackQueuedSpinLockAtDpcLevel(ptr ptr)
@ fastcall KeAcquireInStackQueuedSpinLockForDpc(ptr ptr)
@ cdecl -arch=x86_64,arm64 KeAcquireInStackQueuedSpinLockRaiseToSynch(ptr ptr)
@ stdcall KeAcquireInterruptSpinLock(ptr)
@ cdecl -arch=x86_64,arm64 KeAcquireQueuedSpinLock(long)
@ cdecl -arch=x86_64,arm64 KeAcquireQueuedSpinLockRaiseToSynch(long)
@ stdcall KeAcquireSpinLockAtDpcLevel(ptr)
@ fastcall KeAcquireSpinLockForDpc(ptr)
@ stdcall -arch=x86_64,arm64 KeAcquireSpinLockRaiseToDpc(ptr)
@ stdcall -arch=x86_64,arm64 KeAcquireSpinLockRaiseToSynch(ptr)
@ stdcall KeAddSystemServiceTable(ptr ptr long ptr long)
@ stdcall KeAreAllApcsDisabled()
@ stdcall KeAreApcsDisabled()
@ stdcall KeAttachProcess(ptr)
@ stdcall KeBugCheck(long)
@ stdcall KeBugCheckEx(long ptr ptr ptr ptr)
@ stdcall KeCancelTimer(ptr)
@ stdcall KeCapturePersistentThreadState(ptr long long long long long ptr)
@ stdcall KeClearEvent(ptr)
@ stdcall KeConnectInterrupt(ptr)
@ stdcall KeDelayExecutionThread(long long ptr)
@ stdcall KeDeregisterBugCheckCallback(ptr)
@ stdcall KeDeregisterBugCheckReasonCallback(ptr)
@ stdcall KeDeregisterNmiCallback(ptr)
@ stdcall KeDetachProcess()
@ stdcall KeDisconnectInterrupt(ptr)
@ stdcall KeEnterCriticalRegion() _KeEnterCriticalRegion
@ stdcall KeEnterGuardedRegion() _KeEnterGuardedRegion
@ stdcall KeEnterKernelDebugger()
@ stdcall KeExpandKernelStackAndCallout(ptr ptr long)
@ stdcall -version=0x601+ KeExpandKernelStackAndCalloutEx(ptr ptr long long ptr)
@ stdcall KeFindConfigurationEntry(ptr long long ptr)
@ stdcall KeFindConfigurationNextEntry(ptr long long ptr ptr)
@ stdcall KeFlushEntireTb(long long)
@ stdcall -arch=x86_64,arm,arm64 KeFlushIoBuffers(ptr long long)
@ stdcall KeFlushQueuedDpcs()
@ stdcall KeGenericCallDpc(ptr ptr)
@ stdcall KeGetCurrentNodeNumber()
@ stdcall KeGetCurrentProcessorNumberEx(ptr)
@ stdcall KeGetCurrentThread()
@ stdcall KeGetPreviousMode()
@ stdcall KeGetRecommendedSharedDataAlignment()
@ stdcall -arch=i386 KeI386AbiosCall(long ptr ptr long)
@ stdcall -arch=i386 KeI386AllocateGdtSelectors(ptr long)
@ stdcall -arch=i386 KeI386Call16BitCStyleFunction(long long ptr long)
@ stdcall -arch=i386 KeI386Call16BitFunction(ptr)
@ stdcall -arch=i386 KeI386FlatToGdtSelector(long long long)
@ stdcall -arch=i386 KeI386GetLid(long long long ptr ptr)
@ extern -arch=i386 KeI386MachineType
@ stdcall -arch=i386 KeI386ReleaseGdtSelectors(ptr long)
@ stdcall -arch=i386 KeI386ReleaseLid(long ptr)
@ stdcall -arch=i386 KeI386SetGdtSelector(long ptr)
@ stdcall KeInitializeApc(ptr ptr long ptr ptr ptr long ptr)
@ stdcall KeInitializeCrashDumpHeader(long long ptr long ptr)
@ stdcall KeInitializeDeviceQueue(ptr)
@ stdcall KeInitializeDpc(ptr ptr ptr)
@ stdcall KeInitializeEvent(ptr long long)
@ fastcall KeInitializeGuardedMutex(ptr)
@ stdcall KeInitializeInterrupt(ptr ptr ptr ptr long long long long long long long)
@ stdcall KeInitializeMutant(ptr long)
@ stdcall KeInitializeMutex(ptr long)
@ stdcall KeInitializeQueue(ptr long)
@ stdcall KeInitializeSemaphore(ptr long long)
@ stdcall -arch=i386,x86_64,arm,arm64 KeInitializeSpinLock(ptr) _KeInitializeSpinLock
@ stdcall KeInitializeThreadedDpc(ptr ptr ptr)
@ stdcall KeInitializeTimer(ptr)
@ stdcall KeInitializeTimerEx(ptr long)
@ stdcall KeInsertByKeyDeviceQueue(ptr ptr long)
@ stdcall KeInsertDeviceQueue(ptr ptr)
@ stdcall KeInsertHeadQueue(ptr ptr)
@ stdcall KeInsertQueue(ptr ptr)
@ stdcall KeInsertQueueApc(ptr ptr ptr long)
@ stdcall KeInsertQueueDpc(ptr ptr ptr)
@ stdcall KeInvalidateAllCaches()
@ stdcall KeIpiGenericCall(ptr ptr)
@ stdcall KeIsAttachedProcess()
@ stdcall -arch=i386,arm,arm64 KeIsExecutingDpc()
@ stdcall KeIsWaitListEmpty(ptr)
;@ cdecl -arch=x86_64,arm64 KeLastBranchMSR()
@ stdcall KeLeaveCriticalRegion() _KeLeaveCriticalRegion
@ stdcall KeLeaveGuardedRegion() _KeLeaveGuardedRegion
@ extern KeLoaderBlock
@ cdecl -arch=x86_64,arm64 -private KeLowerIrql(long) KxLowerIrql
@ extern KeNumberProcessors
@ stdcall -arch=i386,arm,arm64 KeProfileInterrupt(ptr)
@ stdcall KeProfileInterruptWithSource(ptr long)
@ stdcall KePulseEvent(ptr long long)
@ stdcall KeQueryActiveProcessorCount(ptr)
@ stdcall KeQueryActiveProcessors()
@ stdcall KeQueryActiveProcessorCountEx(long)
@ stdcall KeQueryMaximumProcessorCount()
@ stdcall KeQueryMaximumProcessorCountEx(long)
@ stdcall KeQueryHighestNodeNumber()
@ stdcall -arch=i386,arm,arm64 KeQueryInterruptTime()
@ stdcall KeQueryInterruptTimePrecise(ptr)
;@ cdecl -arch=x86_64,arm64 KeQueryMultiThreadProcessorSet
;@ cdecl -arch=x86_64,arm64 KeQueryPrcbAddress
@ stdcall KeQueryPriorityThread(ptr)
@ stdcall KeQueryRuntimeThread(ptr ptr)
@ stdcall -arch=arm64 KeQueryPerformanceCounter(ptr) hal.KeQueryPerformanceCounter
@ stdcall -arch=i386,arm,arm64 KeQuerySystemTime(ptr)
@ stdcall KeQuerySystemTimePrecise(ptr)
@ stdcall -arch=i386,arm,arm64 KeQueryTickCount(ptr)
@ stdcall KeQueryTimeIncrement()
@ cdecl -arch=x86_64,arm64 KeRaiseIrqlToDpcLevel() KxRaiseIrqlToDpcLevel
@ stdcall KeRaiseUserException(long)
@ stdcall KeReadStateEvent(ptr)
@ stdcall KeReadStateMutant(ptr)
@ stdcall KeReadStateMutex(ptr) KeReadStateMutant
@ stdcall KeReadStateQueue(ptr)
@ stdcall KeReadStateSemaphore(ptr)
@ stdcall KeReadStateTimer(ptr)
@ stdcall KeRegisterBugCheckCallback(ptr ptr ptr long ptr)
@ stdcall KeRegisterBugCheckReasonCallback(ptr ptr ptr ptr)
@ stdcall KeRegisterNmiCallback(ptr ptr)
@ fastcall KeReleaseGuardedMutex(ptr)
@ fastcall KeReleaseGuardedMutexUnsafe(ptr)
@ cdecl -arch=x86_64,arm64 KeReleaseInStackQueuedSpinLock(ptr)
@ fastcall KeReleaseInStackQueuedSpinLockForDpc(ptr)
@ fastcall KeReleaseInStackQueuedSpinLockFromDpcLevel(ptr)
@ stdcall KeReleaseInterruptSpinLock(ptr long)
@ stdcall KeReleaseMutant(ptr long long long)
@ stdcall KeReleaseMutex(ptr long)
@ cdecl -arch=x86_64,arm64 KeReleaseQueuedSpinLock(long long)
@ stdcall KeReleaseSemaphore(ptr long long long)
@ stdcall -arch=x86_64,arm64 KeReleaseSpinLock(ptr long)
@ fastcall KeReleaseSpinLockForDpc(ptr long)
@ stdcall KeReleaseSpinLockFromDpcLevel(ptr)
@ stdcall KeRemoveByKeyDeviceQueue(ptr long)
@ stdcall KeRemoveByKeyDeviceQueueIfBusy(ptr long)
@ stdcall KeRemoveDeviceQueue(ptr)
@ stdcall KeRemoveEntryDeviceQueue(ptr ptr)
@ stdcall KeRemoveQueue(ptr long ptr)
@ stdcall KeRemoveQueueDpc(ptr)
@ stdcall KeRemoveSystemServiceTable(long)
@ stdcall KeResetEvent(ptr)
@ stdcall -arch=i386 KeRestoreFloatingPointState(ptr)
@ stdcall -arch=x86_64,arm64 KeRestoreFloatingPointState(ptr) KxRestoreFloatingPointState
@ stdcall KeRevertToUserAffinityThread()
@ stdcall KeRundownQueue(ptr)
@ stdcall -arch=i386 KeSaveFloatingPointState(ptr)
@ stdcall -arch=x86_64,arm64 KeSaveFloatingPointState(ptr) KxSaveFloatingPointState
@ cdecl KeSaveStateForHibernate(ptr)
@ extern KeServiceDescriptorTable
@ stdcall KeSetAffinityThread(ptr long)
@ stdcall KeSetBasePriorityThread(ptr long)
@ stdcall KeSetDmaIoCoherency(long)
@ stdcall KeSetEvent(ptr long long)
@ stdcall KeSetEventBoostPriority(ptr ptr)
@ stdcall KeSetIdealProcessorThread(ptr long)
@ stdcall KeSetImportanceDpc(ptr long)
@ stdcall KeSetKernelStackSwapEnable(long)
@ stdcall KeSetPriorityThread(ptr long)
@ stdcall KeSetProfileIrql(long)
@ stdcall KeSetSystemAffinityThread(long)
@ stdcall KeSetCoalescableTimer(ptr int64 long long ptr)
@ stdcall -arch=i386 KeRevertToUserAffinityThreadEx(long)
@ stdcall -arch=x86_64,arm64 KeRevertToUserAffinityThreadEx(int64)
@ stdcall -arch=i386 KeSetSystemAffinityThreadEx(long)
@ stdcall -arch=x86_64,arm64 KeSetSystemAffinityThreadEx(int64)
@ stdcall KeSetTargetProcessorDpc(ptr long)
@ stdcall KeSetTimeIncrement(long long)
@ stdcall KeSetTimer(ptr long long ptr)
@ stdcall KeSetTimerEx(ptr long long long ptr)
@ stdcall KeSignalCallDpcDone(ptr)
@ stdcall KeSignalCallDpcSynchronize(ptr)
@ stdcall KeStackAttachProcess(ptr ptr)
@ stdcall -arch=arm64 KeStallExecutionProcessor(long) hal.KeStallExecutionProcessor
@ stdcall KeSynchronizeExecution(ptr ptr ptr)
@ stdcall KeTerminateThread(long)
@ fastcall KeTestSpinLock(ptr)
@ extern -arch=i386,arm,arm64 KeTickCount
@ fastcall KeTryToAcquireGuardedMutex(ptr)
@ cdecl -arch=x86_64,arm64 KeTryToAcquireQueuedSpinLock(long long)
@ cdecl -arch=x86_64,arm64 KeTryToAcquireQueuedSpinLockRaiseToSynch(long long)
@ fastcall KeTryToAcquireSpinLockAtDpcLevel(ptr)
@ stdcall KeUnstackDetachProcess(ptr)
@ stdcall KeUpdateRunTime(ptr long)
@ fastcall KeUpdateSystemTime(ptr long long)
@ stdcall KeUserModeCallback(long ptr long ptr ptr)
@ stdcall KeWaitForMultipleObjects(long ptr long long long long ptr ptr)
@ stdcall KeWaitForMutexObject(ptr long long long ptr) KeWaitForSingleObject
@ stdcall KeWaitForSingleObject(ptr long long long ptr)
@ fastcall -arch=i386,arm,arm64 KefAcquireSpinLockAtDpcLevel(ptr)
@ fastcall -arch=i386,arm,arm64 KefReleaseSpinLockFromDpcLevel(ptr)
@ stdcall -arch=i386 Kei386EoiHelper()
@ cdecl -arch=x86_64,arm64 KfRaiseIrql(long) KxRaiseIrql
@ fastcall -arch=i386 KiEoiHelper(ptr) #ReactOS-Specific
@ fastcall -arch=i386,arm,arm64 KiAcquireSpinLock(ptr)
@ extern KiBugCheckData
@ stdcall KiCheckForKernelApcDelivery()
@ fastcall -arch=i386 KiCheckForSListAddress(ptr)
@ stdcall -arch=i386 KiCoprocessorError()
;@ cdecl -arch=x86_64,arm64 KiCpuId()
@ stdcall -arch=i386,arm,arm64 KiDeliverApc(long ptr ptr)
@ stdcall -arch=i386 KiDispatchInterrupt()
@ extern -arch=i386,arm,arm64 KiEnableTimerWatchdog
@ stdcall -arch=i386,arm,arm64 KiIpiServiceRoutine(ptr ptr)
@ fastcall -arch=i386,arm,arm64 KiReleaseSpinLock(ptr)
@ cdecl -arch=i386,arm,arm64 KiUnexpectedInterrupt()
@ stdcall -arch=i386 Kii386SpinOnSpinLock(ptr long)
@ stdcall LdrAccessResource(ptr ptr ptr ptr)
@ stdcall LdrEnumResources(ptr ptr long ptr ptr)
@ stdcall LdrFindResourceDirectory_U(ptr ptr long ptr)
@ stdcall LdrFindResource_U(ptr ptr long ptr)
@ extern LpcPortObjectType
@ stdcall LpcReplyWaitReplyPort(ptr long ptr)
@ stdcall LpcRequestPort(ptr ptr)
@ stdcall LpcRequestWaitReplyPort(ptr ptr ptr)
@ stdcall LpcRequestWaitReplyPortEx(ptr ptr ptr)
@ stdcall LpcSendWaitReceivePort(ptr long ptr ptr ptr ptr)
@ stdcall LsaCallAuthenticationPackage(long long ptr long ptr ptr ptr)
@ stdcall LsaDeregisterLogonProcess(long)
@ stdcall LsaFreeReturnBuffer(ptr)
@ stdcall LsaLogonUser(long ptr long long ptr long ptr ptr ptr ptr ptr ptr ptr ptr)
@ stdcall LsaLookupAuthenticationPackage(long ptr ptr)
@ stdcall LsaRegisterLogonProcess(ptr ptr ptr)
@ extern Mm64BitPhysicalAddress
@ stdcall MmAddPhysicalMemory(ptr ptr)
@ stdcall MmAddVerifierThunks(ptr long)
@ stdcall MmAdjustWorkingSetSize(long long long long)
@ stdcall MmAdvanceMdl(ptr long)
@ stdcall MmAllocateContiguousMemory(long long long)
@ stdcall MmAllocateContiguousMemorySpecifyCache(long long long long long long long long)
@ stdcall MmAllocateMappingAddress(long long)
@ stdcall MmAllocateNonCachedMemory(long)
@ stdcall MmAllocatePagesForMdl(ptr ptr ptr ptr ptr ptr ptr)
@ stdcall MmAllocatePagesForMdlEx(long long long long long long long long long)
@ stdcall MmBuildMdlForNonPagedPool(ptr)
@ stdcall MmCanFileBeTruncated(ptr ptr)
@ stdcall MmCommitSessionMappedView(ptr ptr)
@ stdcall MmCreateMdl(ptr ptr long)
@ stdcall MmCreateMirror()
@ stdcall MmCreateSection(ptr long ptr ptr long long ptr ptr)
@ stdcall MmDisableModifiedWriteOfSection(long)
@ stdcall MmDoesFileHaveUserWritableReferences(ptr)
@ stdcall MmFlushImageSection(ptr long)
@ stdcall MmForceSectionClosed(ptr long)
@ stdcall MmFreeContiguousMemory(ptr)
@ stdcall MmFreeContiguousMemorySpecifyCache(ptr long long)
@ stdcall MmFreeMappingAddress(ptr long)
@ stdcall MmFreeNonCachedMemory(ptr long)
@ stdcall MmFreePagesFromMdl(ptr)
@ stdcall MmGetPhysicalAddress(ptr)
@ stdcall MmGetPhysicalMemoryRanges()
@ stdcall MmGetSystemRoutineAddress(ptr)
@ stdcall MmGetVirtualForPhysical(long long)
@ stdcall MmGrowKernelStack(ptr)
@ extern MmHighestUserAddress
@ stdcall MmIsAddressValid(ptr)
@ stdcall MmIsDriverVerifying(ptr)
@ stdcall MmIsIoSpaceActive(long long ptr)
@ stdcall MmIsNonPagedSystemAddressValid(ptr)
@ stdcall MmIsRecursiveIoFault()
@ stdcall MmIsThisAnNtAsSystem()
@ stdcall MmIsVerifierEnabled(ptr)
@ stdcall MmLockPagableDataSection(ptr) MmLockPageableDataSection
@ stdcall MmLockPagableImageSection(ptr) MmLockPageableDataSection
@ stdcall MmLockPagableSectionByHandle(ptr) MmLockPageableSectionByHandle
@ stdcall MmMapIoSpace(long long long long)
@ stdcall MmMapLockedPages(ptr long)
@ stdcall MmMapLockedPagesSpecifyCache(ptr long long ptr long long)
@ stdcall MmMapLockedPagesWithReservedMapping(ptr long ptr long)
@ stdcall MmMapMemoryDumpMdl(ptr)
@ stdcall MmMapUserAddressesToPage(ptr long ptr)
@ stdcall MmMapVideoDisplay(long long long long)
@ stdcall MmMapViewInSessionSpace(ptr ptr ptr)
@ stdcall MmMapViewInSystemSpace(ptr ptr ptr)
@ stdcall MmMapViewOfSection(ptr ptr ptr long long ptr ptr long long long)
@ stdcall MmMarkPhysicalMemoryAsBad(ptr ptr)
@ stdcall MmMarkPhysicalMemoryAsGood(ptr ptr)
@ stdcall MmPageEntireDriver(ptr)
@ stdcall MmPrefetchPages(long ptr)
@ stdcall MmProbeAndLockPages(ptr long long)
@ stdcall MmProbeAndLockProcessPages(ptr ptr long long)
@ stdcall MmProbeAndLockSelectedPages(ptr ptr long long)
@ stdcall MmProtectMdlSystemAddress(ptr long)
@ stdcall MmQuerySystemSize()
@ stdcall MmRemovePhysicalMemory(ptr ptr)
@ stdcall MmResetDriverPaging(ptr)
@ extern MmSectionObjectType
@ stdcall MmSecureVirtualMemory(ptr long long)
@ stdcall MmSetAddressRangeModified(ptr long)
@ stdcall MmSetBankedSection(long long long long long long)
@ stdcall MmSizeOfMdl(ptr long)
@ extern MmSystemRangeStart
@ stdcall MmTrimAllSystemPagableMemory(long) MmTrimAllSystemPageableMemory
@ stdcall MmUnlockPagableImageSection(ptr) MmUnlockPageableImageSection
@ stdcall MmUnlockPages(ptr)
@ stdcall MmUnmapIoSpace(ptr long)
@ stdcall MmUnmapLockedPages(ptr ptr)
@ stdcall MmUnmapReservedMapping(ptr long ptr)
@ stdcall MmUnmapVideoDisplay(ptr long)
@ stdcall MmUnmapViewInSessionSpace(ptr)
@ stdcall MmUnmapViewInSystemSpace(ptr)
@ stdcall MmUnmapViewOfSection(ptr ptr)
@ stdcall MmUnsecureVirtualMemory(ptr)
@ extern MmUserProbeAddress
@ extern MmWriteableSharedUserData
@ extern NlsAnsiCodePage
@ extern NlsLeadByteInfo
@ extern NlsMbCodePageTag
@ extern NlsMbOemCodePageTag
@ extern NlsOemCodePage
@ extern NlsOemLeadByteInfo
@ stdcall NtAddAtom(wstr long ptr)
@ stdcall NtAdjustPrivilegesToken(ptr long ptr long ptr ptr)
@ stdcall -version=0xA00+ NtAlertMultipleThreadByThreadId(ptr long ptr ptr)
@ stdcall NtAllocateLocallyUniqueId(ptr)
@ stdcall -version=0x600+ NtAllocateReserveObject(ptr ptr long)
@ stdcall NtAllocateUuids(ptr ptr ptr ptr)
@ stdcall NtAllocateVirtualMemory(ptr ptr long ptr long long)
@ extern NtBuildNumber
@ stdcall NtClose(ptr)
@ stdcall -version=0x600+ NtCompareObjects(ptr ptr)
@ stdcall NtConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr)
@ stdcall -version=0xA00+ NtContinueEx(ptr ptr)
@ stdcall -version=0xA00+ NtConvertBetweenAuxiliaryCounterAndPerformanceCounter(long ptr ptr ptr)
@ stdcall NtCreateEvent(ptr long ptr long long)
@ stdcall NtCreateFile(ptr long ptr ptr ptr long long long long ptr long)
@ stdcall NtCreateSection(ptr long ptr ptr long long ptr)
@ stdcall NtDeleteAtom(ptr)
@ stdcall NtDeleteFile(ptr)
@ stdcall NtDeviceIoControlFile(ptr ptr ptr ptr ptr long ptr long ptr long)
@ stdcall NtDuplicateObject(ptr ptr ptr ptr long long long)
@ stdcall NtDuplicateToken(ptr long ptr long long ptr)
@ stdcall NtFindAtom(wstr long ptr)
@ stdcall NtFreeVirtualMemory(ptr ptr ptr long)
@ stdcall NtFsControlFile(ptr ptr ptr ptr ptr long ptr long ptr long)
@ extern NtGlobalFlag
@ stdcall NtLockFile(ptr ptr ptr ptr ptr ptr ptr long long long)
@ stdcall NtMakePermanentObject(ptr)
@ stdcall NtMapViewOfSection(ptr ptr ptr long long ptr ptr long long long)
@ stdcall NtNotifyChangeDirectoryFile(ptr ptr ptr ptr ptr ptr long long long)
@ stdcall NtOpenFile(ptr long ptr ptr long long)
@ stdcall NtOpenProcess(ptr long ptr ptr)
@ stdcall NtOpenProcessToken(ptr long ptr)
@ stdcall NtOpenProcessTokenEx(ptr long long ptr)
@ stdcall NtOpenThread(ptr long ptr ptr)
@ stdcall NtOpenThreadToken(ptr long long ptr)
@ stdcall NtOpenThreadTokenEx(ptr long long long ptr)
@ stdcall -version=0x601+ NtQueueApcThreadEx(long long ptr long long long)
@ stdcall -version=0xA00+ NtQueueApcThreadEx2(long long long ptr long long long)
@ stdcall NtQueryDirectoryFile(ptr ptr ptr ptr ptr ptr long long long ptr long)
@ stdcall NtQueryEaFile(ptr ptr ptr long long ptr long ptr long)
@ stdcall NtQueryInformationAtom(long long ptr long ptr)
@ stdcall NtQueryInformationFile(ptr ptr ptr long long)
@ stdcall NtQueryInformationProcess(ptr long ptr long ptr)
@ stdcall NtQueryInformationThread(ptr long ptr long ptr)
@ stdcall NtQueryInformationToken(ptr long ptr long ptr)
@ stdcall NtQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long)
@ stdcall NtQuerySecurityObject(ptr long ptr long ptr)
@ stdcall NtQuerySystemInformation(long ptr long ptr)
@ stdcall NtQueryVolumeInformationFile(ptr ptr ptr long long)
@ stdcall NtReadFile(ptr ptr ptr ptr ptr ptr long ptr ptr)
@ stdcall NtRequestPort(ptr ptr)
@ stdcall NtRequestWaitReplyPort(ptr ptr ptr)
@ stdcall NtSetEaFile(ptr ptr ptr long)
@ stdcall NtSetEvent(ptr ptr)
@ stdcall NtSetInformationFile(ptr ptr ptr long long)
@ stdcall NtSetInformationProcess(ptr long ptr long)
@ stdcall NtSetInformationThread(ptr long ptr long)
@ stdcall -version=0x600+ NtSetIoCompletionEx(ptr ptr long long long long)
@ stdcall NtSetQuotaInformationFile(ptr ptr ptr long)
@ stdcall NtSetSecurityObject(ptr long ptr)
@ stdcall NtSetVolumeInformationFile(ptr ptr ptr long long)
@ stdcall NtShutdownSystem(long)
@ stdcall NtTraceEvent(long long long ptr)
@ stdcall NtUnlockFile(ptr ptr ptr ptr long)
@ stdcall NtVdmControl(long ptr)
@ stdcall NtWaitForSingleObject(ptr long ptr)
@ stdcall NtWriteFile(ptr ptr ptr ptr ptr ptr long ptr ptr)
@ stdcall ObAssignSecurity(ptr ptr ptr ptr)
@ stdcall ObCheckCreateObjectAccess(ptr long ptr ptr long long ptr)
@ stdcall ObCheckObjectAccess(ptr ptr long long ptr)
@ stdcall ObCloseHandle(ptr long)
@ stdcall ObCreateObject(long ptr ptr long ptr long long long ptr)
@ stdcall ObCreateObjectType(ptr ptr ptr ptr)
@ stdcall ObCreateObjectTypeEx(ptr ptr ptr ptr ptr)
@ stdcall ObDeleteCapturedInsertInfo(ptr)
@ stdcall ObDereferenceObject(ptr)
@ stdcall ObDereferenceSecurityDescriptor(ptr long)
@ stdcall ObFindHandleForObject(ptr ptr ptr ptr ptr)
@ stdcall ObGetObjectSecurity(ptr ptr ptr)
@ stdcall ObGetObjectType(ptr)
@ stdcall ObInsertObject(ptr ptr long long ptr ptr)
@ stdcall ObLogSecurityDescriptor(ptr ptr long)
@ stdcall ObMakeTemporaryObject(ptr)
@ stdcall ObOpenObjectByName(ptr ptr long ptr long ptr ptr)
@ stdcall ObOpenObjectByPointer(ptr long ptr long ptr long ptr)
@ stdcall ObQueryNameInfo(ptr)
@ stdcall ObQueryNameString(ptr ptr long ptr)
@ stdcall ObQueryObjectAuditingByHandle(ptr ptr)
@ stdcall ObReferenceObjectByHandle(ptr long ptr long ptr ptr)
@ stdcall ObReferenceObjectByName(ptr long ptr long ptr long ptr ptr)
@ stdcall ObReferenceObjectByPointer(ptr long ptr long)
@ stdcall ObReferenceSecurityDescriptor(ptr long)
@ stdcall ObReleaseObjectSecurity(ptr long)
@ stdcall ObSetHandleAttributes(ptr ptr long)
@ stdcall ObSetSecurityDescriptorInfo(ptr ptr ptr ptr long ptr)
@ stdcall ObSetSecurityObjectByPointer(ptr long ptr)
@ fastcall ObfDereferenceObject(ptr)
@ fastcall ObfReferenceObject(ptr)
@ stdcall PfxFindPrefix(ptr ptr)
@ stdcall PfxInitialize(ptr)
@ stdcall PfxInsertPrefix(ptr ptr ptr)
@ stdcall PfxRemovePrefix(ptr ptr)
@ stdcall PoCallDriver(ptr ptr)
@ stdcall PoCancelDeviceNotify(ptr)
@ stdcall -version=0xA00+ PoCreateThermalRequest(ptr ptr ptr ptr long)
@ stdcall -version=0xA00+ PoDeleteThermalRequest(ptr)
@ stdcall -version=0x602+ PoFxActivateComponent(ptr long long)
@ stdcall -version=0x602+ PoFxCompleteDevicePowerNotRequired(ptr)
@ stdcall -version=0x602+ PoFxCompleteIdleCondition(ptr long)
@ stdcall -version=0x602+ PoFxCompleteIdleState(ptr long)
@ stdcall -version=0x602+ PoFxIdleComponent(ptr ptr long)
@ stdcall -version=0x602+ PoFxRegisterDevice(ptr ptr ptr)
@ stdcall -version=0x602+ PoFxReportDevicePoweredOn(ptr)
@ stdcall -version=0x602+ PoFxSetDeviceIdleTimeout(ptr int64)
@ stdcall -version=0x602+ PoFxStartDevicePowerManagement(ptr)
@ stdcall -version=0x602+ PoFxUnregisterDevice(ptr)
@ stdcall PoGetSystemWake(ptr)
@ stdcall -version=0xA00+ PoGetThermalRequestSupport(ptr long)
@ stdcall PoQueryWatchdogTime(ptr ptr)
@ stdcall PoQueueShutdownWorkItem(ptr)
@ stdcall PoRegisterDeviceForIdleDetection(ptr long long long)
@ stdcall PoRegisterDeviceNotify(ptr long long long ptr ptr)
@ stdcall PoRegisterPowerSettingCallback(ptr ptr ptr ptr ptr)
@ stdcall PoRegisterSystemState(ptr long)
@ stdcall PoRequestPowerIrp(ptr long long ptr ptr ptr)
@ stdcall PoRequestShutdownEvent(ptr)
@ stdcall PoSetHiberRange(ptr long ptr long long)
@ stdcall PoSetPowerState(ptr long long)
@ stdcall PoSetSystemState(long)
@ stdcall -arch=arm64 PoSetProcessorAggregatorParking(long ptr)
@ stdcall PoSetSystemWake(ptr)
@ stdcall -version=0xA00+ PoSetThermalActiveCooling(ptr long)
@ stdcall -version=0xA00+ PoSetThermalPassiveCooling(ptr long)
@ stdcall PoShutdownBugCheck(long long ptr ptr ptr ptr)
@ stdcall PoStartNextPowerIrp(ptr)
@ stdcall PoUnregisterPowerSettingCallback(ptr)
@ stdcall PoUnregisterSystemState(ptr)
@ stdcall ProbeForRead(ptr long long)
@ stdcall ProbeForWrite(ptr long long)
@ stdcall PsAssignImpersonationToken(ptr ptr)
@ stdcall PsChargePoolQuota(ptr long long)
@ stdcall PsChargeProcessNonPagedPoolQuota(ptr long)
@ stdcall PsChargeProcessPagedPoolQuota(ptr long)
@ stdcall PsChargeProcessPoolQuota(ptr long long)
@ stdcall PsCreateSystemProcess(ptr long ptr)
@ stdcall PsCreateSystemThread(ptr long ptr ptr ptr ptr ptr)
@ stdcall PsDereferenceImpersonationToken(ptr) PsDereferencePrimaryToken
@ stdcall PsDereferencePrimaryToken(ptr)
@ stdcall PsDisableImpersonation(ptr ptr)
@ stdcall PsEstablishWin32Callouts(ptr)
@ stdcall PsGetContextThread(ptr ptr long)
@ stdcall PsGetCurrentProcess() IoGetCurrentProcess
@ stdcall PsGetCurrentProcessId()
@ stdcall PsGetCurrentProcessSessionId()
@ stdcall PsGetCurrentProcessWin32Process()
@ stdcall -arch=x86_64,arm64 PsGetCurrentProcessWow64Process()
@ stdcall PsGetCurrentThread() KeGetCurrentThread
@ stdcall PsGetCurrentThreadId()
@ stdcall PsGetCurrentThreadPreviousMode()
@ stdcall PsGetCurrentThreadProcess()
@ stdcall PsGetCurrentThreadProcessId()
@ stdcall PsGetCurrentThreadStackBase()
@ stdcall PsGetCurrentThreadStackLimit()
@ stdcall PsGetCurrentThreadTeb()
@ stdcall PsGetCurrentThreadWin32Thread()
@ stdcall PsGetCurrentThreadWin32ThreadAndEnterCriticalRegion(ptr)
@ stdcall PsGetJobLock(ptr)
@ stdcall PsGetJobSessionId(ptr)
@ stdcall PsGetJobUIRestrictionsClass(ptr)
@ stdcall PsGetProcessCreateTimeQuadPart(ptr)
@ stdcall PsGetProcessDebugPort(ptr)
@ stdcall PsGetProcessExitProcessCalled(ptr)
@ stdcall PsGetProcessExitStatus(ptr)
@ stdcall PsGetProcessExitTime()
@ stdcall PsGetProcessId(ptr)
@ stdcall PsGetProcessImageFileName(ptr)
@ stdcall PsGetProcessInheritedFromUniqueProcessId(ptr)
@ stdcall PsGetProcessJob(ptr)
@ stdcall PsGetProcessPeb(ptr)
@ stdcall -arch=x86_64,arm64 PsGetProcessPeb32(ptr)
@ stdcall PsGetProcessPriorityClass(ptr)
@ stdcall PsGetProcessSectionBaseAddress(ptr)
@ stdcall PsGetProcessSecurityPort(ptr)
@ stdcall PsGetProcessSessionId(ptr)
@ stdcall PsGetProcessSessionIdEx(ptr)
@ stdcall PsGetProcessWin32Process(ptr)
@ stdcall PsGetProcessWin32WindowStation(ptr)
@ stdcall -arch=x86_64,arm64 PsGetProcessWow64Process(ptr)
@ stdcall PsGetThreadFreezeCount(ptr)
@ stdcall PsGetThreadHardErrorsAreDisabled(ptr)
@ stdcall PsGetThreadId(ptr)
@ stdcall PsGetThreadProcess(ptr)
@ stdcall PsGetThreadProcessId(ptr)
@ stdcall PsGetThreadSessionId(ptr)
@ stdcall PsGetThreadTeb(ptr)
@ stdcall PsGetThreadWin32Thread(ptr)
@ stdcall PsGetVersion(ptr ptr ptr ptr)
@ stdcall PsImpersonateClient(ptr ptr long long long)
@ extern PsInitialSystemProcess
@ stdcall PsIsProcessBeingDebugged(ptr)
@ stdcall PsIsSystemProcess(ptr)
@ stdcall -version=0x602+ PsIsDiskCountersEnabled()
@ stdcall -version=0x602+ PsUpdateDiskCounters(ptr int64 int64 long long long)
@ stdcall PsIsSystemThread(ptr)
@ stdcall PsIsThreadImpersonating(ptr)
@ stdcall PsIsThreadTerminating(ptr)
@ extern PsJobType
@ stdcall PsLookupProcessByProcessId(ptr ptr)
@ stdcall PsLookupProcessThreadByCid(ptr ptr ptr)
@ stdcall PsLookupThreadByThreadId(ptr ptr)
@ extern PsProcessType
@ stdcall PsReferenceImpersonationToken(ptr ptr ptr ptr)
@ stdcall PsReferencePrimaryToken(ptr)
@ stdcall PsRemoveCreateThreadNotifyRoutine(ptr)
@ stdcall PsRemoveLoadImageNotifyRoutine(ptr)
@ stdcall PsRestoreImpersonation(ptr ptr)
@ stdcall PsReturnPoolQuota(ptr long long)
@ stdcall PsReturnProcessNonPagedPoolQuota(ptr long)
@ stdcall PsReturnProcessPagedPoolQuota(ptr long)
@ stdcall PsRevertThreadToSelf(ptr)
@ stdcall PsRevertToSelf()
@ stdcall PsSetContextThread(ptr ptr long)
@ stdcall PsSetCreateProcessNotifyRoutine(ptr long)
@ stdcall PsSetCreateThreadNotifyRoutine(ptr)
@ stdcall PsSetJobUIRestrictionsClass(ptr long)
@ stdcall PsSetLegoNotifyRoutine(ptr)
@ stdcall PsSetLoadImageNotifyRoutine(ptr)
@ stdcall PsSetProcessPriorityByClass(ptr ptr)
@ stdcall PsSetProcessPriorityClass(ptr long)
@ stdcall PsSetProcessSecurityPort(ptr ptr)
@ stdcall PsSetProcessWin32Process(ptr ptr ptr)
@ stdcall PsSetProcessWindowStation(ptr ptr)
@ stdcall PsSetThreadHardErrorsAreDisabled(ptr long)
@ stdcall PsSetThreadWin32Thread(ptr ptr ptr)
@ stdcall PsTerminateSystemThread(long)
@ extern PsThreadType
@ stdcall PsWrapApcWow64Thread(ptr ptr)
@ stdcall -arch=i386,arm READ_REGISTER_BUFFER_UCHAR(ptr ptr long)
@ stdcall -arch=i386,arm READ_REGISTER_BUFFER_ULONG(ptr ptr long)
@ stdcall -arch=i386,arm READ_REGISTER_BUFFER_USHORT(ptr ptr long)
@ stdcall -arch=i386,arm READ_REGISTER_UCHAR(ptr)
@ stdcall -arch=i386,arm READ_REGISTER_ULONG(ptr)
@ stdcall -arch=i386,arm READ_REGISTER_USHORT(ptr)
@ stdcall RtlAbsoluteToSelfRelativeSD(ptr ptr ptr)
@ stdcall RtlAddAccessAllowedAce(ptr long long ptr)
@ stdcall RtlAddAccessAllowedAceEx(ptr long long long ptr)
@ stdcall RtlAddAce(ptr long long ptr long)
@ stdcall RtlAddAtomToAtomTable(ptr wstr ptr)
@ stdcall RtlAddRange(ptr long long long long long long ptr ptr)
@ stdcall RtlAllocateHeap(ptr long long)
@ stdcall RtlAnsiCharToUnicodeChar(ptr)
@ stdcall RtlAnsiStringToUnicodeSize(ptr) RtlxAnsiStringToUnicodeSize
@ stdcall RtlAnsiStringToUnicodeString(ptr ptr long)
@ stdcall RtlAppendAsciizToString(ptr str)
@ stdcall RtlAppendStringToString(ptr ptr)
@ stdcall RtlAppendUnicodeStringToString(ptr ptr)
@ stdcall RtlAppendUnicodeToString(ptr wstr)
@ stdcall RtlAreAllAccessesGranted(long long)
@ stdcall RtlAreAnyAccessesGranted(long long)
@ stdcall RtlAreBitsClear(ptr long long)
@ stdcall RtlAreBitsSet(ptr long long)
@ stdcall RtlAssert(str str long str)
@ stdcall RtlCaptureContext(ptr)
@ stdcall RtlCaptureStackBackTrace(long long ptr ptr)
@ stdcall RtlCharToInteger(str long ptr)
@ stdcall RtlCheckRegistryKey(long wstr)
@ stdcall RtlClearAllBits(ptr)
@ stdcall RtlClearBit(ptr long)
@ stdcall RtlClearBits(ptr long long)
@ stdcall RtlCompareMemory(ptr ptr long)
@ stdcall RtlCompareMemoryUlong(ptr long long)
@ stdcall RtlCompareString(ptr ptr long)
@ stdcall RtlCompareUnicodeString(ptr ptr long)
@ stdcall RtlCompressBuffer(long ptr long ptr long long ptr ptr)
@ stdcall RtlCompressChunks(ptr long ptr long ptr long ptr)
@ stdcall RtlConvertLongToLargeInteger(long)
@ stdcall RtlConvertSidToUnicodeString(ptr ptr long)
@ stdcall RtlConvertUlongToLargeInteger(long)
@ stdcall RtlCopyLuid(ptr ptr)
@ stdcall -arch=x86_64,arm64 RtlCopyMemory(ptr ptr int64) memmove
@ stdcall -arch=x86_64,arm64 RtlCopyMemoryNonTemporal(ptr ptr int64) memmove
@ stdcall RtlCopyRangeList(ptr ptr)
@ stdcall RtlCopySid(long ptr ptr)
@ stdcall RtlCopyString(ptr ptr)
@ stdcall RtlComputeCrc32(long ptr long)
@ stdcall RtlContractHashTable(ptr)
@ stdcall RtlCopyUnicodeString(ptr ptr)
@ stdcall RtlCreateAcl(ptr long long)
@ stdcall RtlCreateAtomTable(long ptr)
@ stdcall RtlCreateHashTable(ptr long long)
@ stdcall RtlCreateHeap(long ptr long long ptr ptr)
@ stdcall RtlCreateRegistryKey(long wstr)
@ stdcall RtlCreateSecurityDescriptor(ptr long)
@ stdcall RtlCreateSystemVolumeInformationFolder(ptr)
@ stdcall RtlCreateUnicodeString(ptr wstr)
@ stdcall RtlCustomCPToUnicodeN(ptr wstr long ptr ptr long)
@ stdcall RtlDecompressBuffer(long ptr long ptr long ptr)
@ stdcall RtlDecompressChunks(ptr long ptr long ptr long ptr)
@ stdcall RtlDecompressFragment(long ptr long ptr long long ptr ptr)
@ stdcall RtlDelete(ptr)
@ stdcall RtlDeleteAce(ptr long)
@ stdcall RtlDeleteAtomFromAtomTable(ptr ptr)
@ stdcall RtlDeleteHashTable(ptr)
@ stdcall RtlDeleteElementGenericTable(ptr ptr)
@ stdcall RtlDeleteElementGenericTableAvl(ptr ptr)
@ stdcall RtlDeleteNoSplay(ptr ptr)
@ stdcall RtlDeleteOwnersRanges(ptr ptr)
@ stdcall RtlDeleteRange(ptr long long long long ptr)
@ stdcall RtlDeleteRegistryValue(long wstr wstr)
@ stdcall RtlDescribeChunk(long ptr ptr ptr ptr)
@ stdcall RtlDestroyAtomTable(ptr)
@ stdcall RtlDestroyHeap(ptr)
@ stdcall RtlDowncaseUnicodeString(ptr ptr long)
@ stdcall RtlEmptyAtomTable(ptr long)
@ stdcall RtlEndEnumerationHashTable(ptr ptr)
@ stdcall RtlEndWeakEnumerationHashTable(ptr ptr)
@ stdcall -arch=win32 RtlEnlargedIntegerMultiply(long long)
@ stdcall -arch=win32 RtlEnlargedUnsignedDivide(long long long ptr)
@ stdcall -arch=win32 RtlEnlargedUnsignedMultiply(long long)
@ stdcall RtlEnumerateEntryHashTable(ptr ptr)
@ stdcall RtlEnumerateGenericTable(ptr long)
@ stdcall RtlEnumerateGenericTableAvl(ptr long)
@ stdcall RtlEnumerateGenericTableLikeADirectory(ptr ptr ptr long ptr ptr ptr)
@ stdcall RtlEnumerateGenericTableWithoutSplaying(ptr ptr)
@ stdcall RtlEnumerateGenericTableWithoutSplayingAvl(ptr ptr)
@ stdcall RtlEqualLuid(ptr ptr)
@ stdcall RtlExpandHashTable(ptr)
@ stdcall RtlEqualSid(ptr ptr)
@ stdcall RtlEqualString(ptr ptr long)
@ stdcall RtlEqualUnicodeString(ptr ptr long)
@ stdcall -arch=win32 RtlExtendedIntegerMultiply(long long long)
@ stdcall -arch=win32 RtlExtendedLargeIntegerDivide(long long long ptr)
@ stdcall -arch=win32 RtlExtendedMagicDivide(long long long long long)
@ stdcall RtlFillMemory(ptr long long)
@ stdcall -arch=i386,arm,arm64 RtlFillMemoryUlong(ptr long long)
@ stdcall RtlFindClearBits(ptr long long)
@ stdcall RtlFindClearBitsAndSet(ptr long long)
@ stdcall RtlFindClearRuns(ptr ptr long long)
@ stdcall RtlFindFirstRunClear(ptr ptr)
@ stdcall RtlFindLastBackwardRunClear(ptr long ptr)
@ stdcall RtlFindLeastSignificantBit(long long)
@ stdcall RtlFindLongestRunClear(ptr ptr)
@ stdcall RtlFindMessage(ptr long long long ptr)
@ stdcall RtlFindMostSignificantBit(long long)
@ stdcall RtlFindNextForwardRunClear(ptr long ptr)
@ stdcall RtlFindRange(ptr long long long long long long long long ptr ptr ptr)
@ stdcall RtlFindSetBits(ptr long long)
@ stdcall RtlFindSetBitsAndClear(ptr long long)
@ stdcall RtlFindUnicodePrefix(ptr ptr long)
@ stdcall RtlFormatCurrentUserKeyPath(ptr)
@ stdcall RtlFreeAnsiString(ptr)
@ stdcall RtlFreeHeap(ptr long ptr)
@ stdcall RtlFreeOemString(ptr)
@ stdcall RtlFreeRangeList(ptr)
@ stdcall RtlFreeUnicodeString(ptr)
@ stdcall RtlGUIDFromString(ptr ptr)
@ stdcall RtlGenerate8dot3Name(ptr ptr long ptr)
@ stdcall RtlGetAce(ptr long ptr)
@ stdcall RtlGetCallersAddress(ptr ptr)
@ stdcall RtlGetCompressionWorkSpaceSize(long ptr ptr)
@ stdcall RtlGetDaclSecurityDescriptor(ptr ptr ptr ptr)
@ stdcall RtlGetDefaultCodePage(ptr ptr)
@ stdcall RtlGetElementGenericTable(ptr long)
@ stdcall RtlGetElementGenericTableAvl(ptr long)
@ stdcall RtlGetFirstRange(ptr ptr ptr)
@ stdcall RtlGetGroupSecurityDescriptor(ptr ptr ptr)
@ stdcall RtlGetNextEntryHashTable(ptr ptr)
@ stdcall RtlGetNextRange(ptr ptr long)
@ stdcall RtlGetNtGlobalFlags()
@ stdcall RtlGetOwnerSecurityDescriptor(ptr ptr ptr)
@ stdcall RtlGetSaclSecurityDescriptor(ptr ptr ptr ptr)
@ stdcall RtlGetSetBootStatusData(ptr long long ptr long long)
@ stdcall RtlGetVersion(ptr)
@ stdcall RtlHashUnicodeString(ptr long long ptr)
@ stdcall RtlImageDirectoryEntryToData(ptr long long ptr)
@ stdcall RtlImageNtHeader(ptr)
@ stdcall RtlInitAnsiString(ptr str)
@ stdcall RtlInitAnsiStringEx(ptr str)
@ stdcall RtlInitCodePageTable(ptr ptr)
@ stdcall RtlInitString(ptr str)
@ stdcall RtlInitEnumerationHashTable(ptr ptr)
@ stdcall RtlInitUnicodeString(ptr wstr)
@ stdcall RtlInitWeakEnumerationHashTable(ptr ptr)
@ stdcall RtlInitUnicodeStringEx(ptr wstr)
@ stdcall RtlInitializeBitMap(ptr ptr long)
@ stdcall RtlInitializeGenericTable(ptr ptr ptr ptr ptr)
@ stdcall RtlInitializeGenericTableAvl(ptr ptr ptr ptr ptr)
@ stdcall RtlInitializeRangeList(ptr)
@ stdcall RtlInitializeSid(ptr ptr long)
@ stdcall RtlInitializeUnicodePrefix(ptr)
@ stdcall RtlInsertElementGenericTable(ptr ptr long ptr)
@ stdcall RtlInsertEntryHashTable(ptr ptr long ptr)
@ stdcall RtlInsertElementGenericTableAvl(ptr ptr long ptr)
@ stdcall RtlInsertElementGenericTableFull(ptr ptr long ptr ptr long)
@ stdcall RtlInsertElementGenericTableFullAvl(ptr ptr long ptr ptr ptr)
@ stdcall RtlInsertUnicodePrefix(ptr ptr ptr)
@ stdcall RtlInt64ToUnicodeString(long long long ptr)
@ stdcall RtlIntegerToChar(long long long ptr)
@ stdcall RtlIntegerToUnicode(long long long ptr)
@ stdcall RtlIntegerToUnicodeString(long long ptr)
@ stdcall RtlInvertRangeList(ptr ptr)
@ stdcall RtlIpv4AddressToStringA(ptr ptr)
@ stdcall RtlIpv4AddressToStringExA(ptr long ptr ptr)
@ stdcall RtlIpv4AddressToStringExW(ptr long ptr ptr)
@ stdcall RtlIpv4AddressToStringW(ptr ptr)
@ stdcall RtlIpv4StringToAddressA(str long ptr ptr)
@ stdcall RtlIpv4StringToAddressExA(str long ptr ptr)
@ stdcall RtlIpv4StringToAddressExW(wstr long ptr ptr)
@ stdcall RtlIpv4StringToAddressW(wstr long ptr ptr)
@ stdcall RtlIpv6AddressToStringA(ptr ptr)
@ stdcall RtlIpv6AddressToStringExA(ptr long long ptr ptr)
@ stdcall RtlIpv6AddressToStringExW(ptr long long ptr ptr)
@ stdcall RtlIpv6AddressToStringW(ptr ptr)
@ stdcall RtlIpv6StringToAddressA(str ptr ptr)
@ stdcall RtlIpv6StringToAddressExA(str ptr ptr ptr)
@ stdcall RtlIpv6StringToAddressExW(wstr ptr ptr ptr)
@ stdcall RtlIpv6StringToAddressW(wstr ptr ptr)
@ stdcall RtlIsGenericTableEmpty(ptr)
@ stdcall RtlIsGenericTableEmptyAvl(ptr)
@ stdcall RtlIsNameLegalDOS8Dot3(ptr ptr ptr)
@ stdcall RtlIsRangeAvailable(ptr long long long long long long ptr ptr ptr)
@ stdcall RtlIsValidOemCharacter(ptr)
@ stdcall -arch=win32 RtlLargeIntegerAdd(long long long long)
@ stdcall -arch=win32 RtlLargeIntegerArithmeticShift(long long long)
@ stdcall -arch=win32 RtlLargeIntegerDivide(long long long long ptr)
@ stdcall -arch=win32 RtlLargeIntegerNegate(long long)
@ stdcall -arch=win32 RtlLargeIntegerShiftLeft(long long long)
@ stdcall -arch=win32 RtlLargeIntegerShiftRight(long long long)
@ stdcall -arch=win32 RtlLargeIntegerSubtract(long long long long)
@ stdcall RtlLengthRequiredSid(long)
@ stdcall RtlLengthSecurityDescriptor(ptr)
@ stdcall RtlLengthSid(ptr)
@ stdcall RtlLockBootStatusData(ptr)
@ stdcall RtlLookupAtomInAtomTable(ptr wstr ptr)
@ stdcall RtlLookupElementGenericTable(ptr ptr)
@ stdcall RtlLookupEntryHashTable(ptr long ptr)
@ stdcall RtlLookupElementGenericTableAvl(ptr ptr)
@ stdcall RtlLookupElementGenericTableFull(ptr ptr ptr ptr)
@ stdcall RtlLookupElementGenericTableFullAvl(ptr ptr ptr ptr)
@ cdecl -arch=x86_64,arm64 RtlLookupFunctionEntry(double ptr ptr)
@ stdcall RtlMapGenericMask(ptr ptr)
@ stdcall RtlMapSecurityErrorToNtStatus(long)
@ stdcall RtlMergeRangeLists(ptr ptr ptr long)
@ stdcall RtlMoveMemory(ptr ptr long)
@ stdcall RtlMultiByteToUnicodeN(ptr long ptr str long)
@ stdcall RtlMultiByteToUnicodeSize(ptr str long)
@ stdcall RtlNextUnicodePrefix(ptr long)
@ stdcall RtlNtStatusToDosError(long)
@ stdcall RtlNtStatusToDosErrorNoTeb(long)
@ stdcall RtlNumberGenericTableElements(ptr)
@ stdcall RtlNumberGenericTableElementsAvl(ptr)
@ stdcall RtlNumberOfClearBits(ptr)
@ stdcall RtlNumberOfSetBits(ptr)
@ stdcall RtlOemStringToCountedUnicodeString(ptr ptr long)
@ stdcall RtlOemStringToUnicodeSize(ptr) RtlxOemStringToUnicodeSize
@ stdcall RtlOemStringToUnicodeString(ptr ptr long)
@ stdcall RtlOemToUnicodeN(wstr long ptr ptr long)
@ cdecl -arch=x86_64,arm64 RtlPcToFileHeader(ptr ptr)
@ stdcall RtlPinAtomInAtomTable(ptr ptr)
@ fastcall RtlPrefetchMemoryNonTemporal(ptr long)
@ stdcall RtlPrefixString(ptr ptr long)
@ stdcall RtlPrefixUnicodeString(ptr ptr long)
@ stdcall RtlQueryAtomInAtomTable(ptr long ptr ptr ptr ptr)
@ stdcall RtlQueryRegistryValues(long wstr ptr ptr ptr)
@ stdcall RtlQueryTimeZoneInformation(ptr)
@ stdcall RtlRaiseException(ptr)
@ stdcall RtlRandom(ptr)
@ stdcall RtlRandomEx(ptr)
@ stdcall RtlRealPredecessor(ptr)
@ stdcall RtlRealSuccessor(ptr)
@ stdcall RtlRemoveEntryHashTable(ptr ptr ptr)
@ stdcall RtlRemoveUnicodePrefix(ptr ptr)
@ stdcall RtlReserveChunk(long ptr ptr ptr long)
@ cdecl -arch=x86_64,arm64 RtlRestoreContext(ptr ptr)
@ stdcall RtlSecondsSince1970ToTime(long ptr)
@ stdcall RtlSecondsSince1980ToTime(long ptr)
@ stdcall RtlSelfRelativeToAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr)
@ stdcall RtlSelfRelativeToAbsoluteSD2(ptr long)
@ stdcall RtlSetAllBits(ptr)
@ stdcall RtlSetBit(ptr long)
@ stdcall RtlSetBits(ptr long long)
@ stdcall RtlSetDaclSecurityDescriptor(ptr long ptr long)
@ stdcall RtlSetGroupSecurityDescriptor(ptr ptr long)
@ stdcall RtlSetOwnerSecurityDescriptor(ptr ptr long)
@ stdcall RtlSetSaclSecurityDescriptor(ptr long ptr long)
@ stdcall RtlSetTimeZoneInformation(ptr)
@ stdcall RtlSizeHeap(ptr long ptr)
@ stdcall RtlSplay(ptr)
@ stdcall RtlStringFromGUID(ptr ptr)
@ stdcall RtlSubAuthorityCountSid(ptr)
@ stdcall RtlSubAuthoritySid(ptr long)
@ stdcall RtlSubtreePredecessor(ptr)
@ stdcall RtlSubtreeSuccessor(ptr)
@ stdcall RtlTestBit(ptr long)
@ stdcall RtlTimeFieldsToTime(ptr ptr)
@ stdcall RtlTimeToElapsedTimeFields(ptr ptr)
@ stdcall RtlTimeToSecondsSince1970(ptr ptr)
@ stdcall RtlTimeToSecondsSince1980(ptr ptr)
@ stdcall RtlTimeToTimeFields(ptr ptr)
@ stdcall RtlTraceDatabaseAdd(ptr long ptr ptr)
@ stdcall RtlTraceDatabaseCreate(long ptr long long ptr)
@ stdcall RtlTraceDatabaseDestroy(ptr)
@ stdcall RtlTraceDatabaseEnumerate(ptr ptr ptr)
@ stdcall RtlTraceDatabaseFind(ptr long ptr ptr)
@ stdcall RtlTraceDatabaseLock(ptr)
@ stdcall RtlTraceDatabaseUnlock(ptr)
@ stdcall RtlTraceDatabaseValidate(ptr)
@ fastcall -arch=i386,arm,arm64 RtlUlongByteSwap(long)
@ fastcall -arch=i386,arm,arm64 RtlUlonglongByteSwap(long long)
@ stdcall RtlUnicodeStringToAnsiSize(ptr) RtlxUnicodeStringToAnsiSize
@ stdcall RtlUnicodeStringToAnsiString(ptr ptr long)
@ stdcall RtlUnicodeStringToCountedOemString(ptr ptr long)
@ stdcall RtlUnicodeStringToInteger(ptr long ptr)
@ stdcall RtlUnicodeStringToOemSize(ptr) RtlxUnicodeStringToOemSize
@ stdcall RtlUnicodeStringToOemString(ptr ptr long)
@ stdcall RtlUnicodeToCustomCPN(ptr ptr long ptr wstr long)
@ stdcall RtlUnicodeToMultiByteN(ptr long ptr wstr long)
@ stdcall RtlUnicodeToMultiByteSize(ptr wstr long)
@ stdcall RtlUnicodeToOemN(ptr long ptr wstr long)
@ stdcall RtlUnicodeToUTF8N(ptr long ptr wstr long)
@ stdcall RtlUnlockBootStatusData(ptr)
@ stdcall RtlUnwind(ptr ptr ptr ptr)
@ stdcall -arch=x86_64,arm64,arm RtlUnwindEx(ptr ptr ptr ptr ptr ptr)
@ stdcall RtlUpcaseUnicodeChar(long)
@ stdcall RtlUpcaseUnicodeString(ptr ptr long)
@ stdcall RtlUpcaseUnicodeStringToAnsiString(ptr ptr long)
@ stdcall RtlUpcaseUnicodeStringToCountedOemString(ptr ptr long)
@ stdcall RtlUpcaseUnicodeStringToOemString(ptr ptr long)
@ stdcall RtlUpcaseUnicodeToCustomCPN(ptr ptr long ptr wstr long)
@ stdcall RtlUpcaseUnicodeToMultiByteN(ptr long ptr wstr long)
@ stdcall RtlUpcaseUnicodeToOemN(ptr long ptr wstr long)
@ stdcall RtlUpperChar(long)
@ stdcall RtlUpperString(ptr ptr)
@ fastcall -arch=i386,arm,arm64 RtlUshortByteSwap(long)
@ stdcall RtlValidRelativeSecurityDescriptor(ptr long long)
@ stdcall RtlValidSecurityDescriptor(ptr)
@ stdcall RtlValidSid(ptr)
@ stdcall RtlVerifyVersionInfo(ptr long long long)
@ stdcall -arch=x86_64,arm64,arm RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr)
@ stdcall RtlVolumeDeviceToDosName(ptr ptr) IoVolumeDeviceToDosName
@ stdcall RtlWalkFrameChain(ptr long long)
@ stdcall RtlWeaklyEnumerateEntryHashTable(ptr ptr)
@ stdcall RtlWriteRegistryValue(long wstr wstr long ptr long)
@ stdcall RtlZeroHeap(ptr long)
@ stdcall RtlZeroMemory(ptr long)
@ stdcall RtlxAnsiStringToUnicodeSize(ptr)
@ stdcall RtlxOemStringToUnicodeSize(ptr)
@ stdcall RtlxUnicodeStringToAnsiSize(ptr)
@ stdcall RtlxUnicodeStringToOemSize(ptr)
@ stdcall SeAccessCheck(ptr ptr ptr long long ptr ptr long ptr ptr)
@ stdcall SeAppendPrivileges(ptr ptr)
@ stdcall SeAssignSecurity(ptr ptr ptr long ptr ptr ptr)
@ stdcall SeAssignSecurityEx(ptr ptr ptr ptr long long ptr ptr ptr)
@ stdcall SeAuditHardLinkCreation(ptr ptr long)
@ stdcall SeAuditingFileEvents(long ptr)
@ stdcall SeAuditingFileEventsWithContext(long ptr ptr)
@ stdcall SeAuditingFileOrGlobalEvents(long ptr ptr)
@ stdcall SeAuditingHardLinkEvents(long ptr)
@ stdcall SeAuditingHardLinkEventsWithContext(long ptr ptr)
@ stdcall SeCaptureSecurityDescriptor(ptr long long long ptr)
@ stdcall SeCaptureSubjectContext(ptr)
@ stdcall SeCloseObjectAuditAlarm(ptr ptr long)
@ stdcall SeCreateAccessState(ptr ptr long ptr)
@ stdcall SeCreateClientSecurity(ptr ptr long ptr)
@ stdcall SeCreateClientSecurityFromSubjectContext(ptr ptr long ptr)
@ stdcall SeDeassignSecurity(ptr)
@ stdcall SeDeleteAccessState(ptr)
@ stdcall SeDeleteObjectAuditAlarm(ptr ptr)
@ extern SeExports
@ stdcall SeFilterToken(ptr long ptr ptr ptr ptr)
@ stdcall SeFreePrivileges(ptr)
@ stdcall SeImpersonateClient(ptr ptr)
@ stdcall SeImpersonateClientEx(ptr ptr)
@ stdcall SeLockSubjectContext(ptr)
@ stdcall SeMarkLogonSessionForTerminationNotification(ptr)
@ stdcall SeOpenObjectAuditAlarm(ptr ptr ptr ptr ptr long long long ptr)
@ stdcall SeOpenObjectForDeleteAuditAlarm(ptr ptr ptr ptr ptr long long long ptr)
@ stdcall SePrivilegeCheck(ptr ptr long)
@ stdcall SePrivilegeObjectAuditAlarm(ptr ptr long ptr long long)
@ extern SePublicDefaultDacl
@ stdcall SeQueryAuthenticationIdToken(ptr ptr)
@ stdcall SeQueryInformationToken(ptr long ptr)
@ stdcall SeQuerySecurityDescriptorInfo(ptr ptr ptr ptr)
@ stdcall SeQuerySessionIdToken(ptr ptr)
@ stdcall SeRegisterLogonSessionTerminatedRoutine(ptr)
@ stdcall SeReleaseSecurityDescriptor(ptr long long)
@ stdcall SeReleaseSubjectContext(ptr)
@ stdcall SeReportSecurityEvent(long ptr ptr ptr)
@ stdcall SeSetAccessStateGenericMapping(ptr ptr)
@ stdcall SeSetAuditParameter(ptr long long ptr)
@ stdcall SeSetSecurityDescriptorInfo(ptr ptr ptr ptr long ptr)
@ stdcall SeSetSecurityDescriptorInfoEx(ptr ptr ptr ptr long long ptr)
@ stdcall SeSinglePrivilegeCheck(long long long)
@ extern SeSystemDefaultDacl
@ stdcall SeTokenImpersonationLevel(ptr)
@ stdcall SeTokenIsAdmin(ptr)
@ stdcall SeTokenIsRestricted(ptr)
@ extern SeTokenObjectType
@ stdcall SeTokenType(ptr)
@ stdcall SeUnlockSubjectContext(ptr)
@ stdcall SeUnregisterLogonSessionTerminatedRoutine(ptr)
@ stdcall SeValidSecurityDescriptor(long ptr)
@ stdcall VerSetConditionMask(long long long long)
@ cdecl VfFailDeviceNode(ptr long long long ptr ptr ptr)
@ cdecl VfFailDriver(long long long ptr ptr ptr)
@ cdecl VfFailSystemBIOS(long long long ptr ptr ptr)
@ stdcall VfIsVerificationEnabled(long ptr)
@ stdcall -arch=i386,arm WRITE_REGISTER_BUFFER_UCHAR(ptr ptr long)
@ stdcall -arch=i386,arm WRITE_REGISTER_BUFFER_ULONG(ptr ptr long)
@ stdcall -arch=i386,arm WRITE_REGISTER_BUFFER_USHORT(ptr ptr long)
@ stdcall -arch=i386,arm WRITE_REGISTER_UCHAR(ptr long)
@ stdcall -arch=i386,arm WRITE_REGISTER_ULONG(ptr long)
@ stdcall -arch=i386,arm WRITE_REGISTER_USHORT(ptr long)
@ stdcall WmiFlushTrace(ptr)
@ fastcall WmiGetClock(long ptr)
@ stdcall WmiQueryTrace(ptr)
@ stdcall WmiQueryTraceInformation(long ptr long ptr ptr)
@ stdcall WmiStartTrace(ptr)
@ stdcall WmiStopTrace(ptr)
@ fastcall WmiTraceFastEvent(ptr)
@ varargs WmiTraceMessage(int64 long ptr long)
@ stdcall WmiTraceMessageVa(int64 long ptr long ptr)
@ stdcall WmiUpdateTrace(ptr)
@ stdcall XIPDispatch(long ptr long)
@ stdcall ZwAccessCheckAndAuditAlarm(ptr ptr ptr ptr ptr long ptr long ptr ptr ptr)
@ stdcall ZwAddBootEntry(ptr long)
@ stdcall ZwAddDriverEntry(ptr long)
@ stdcall ZwAdjustPrivilegesToken(ptr long ptr long ptr ptr)
@ stdcall ZwAlertThread(ptr)
@ stdcall -version=0x600+ ZwAllocateReserveObject(ptr ptr long)
@ stdcall ZwAllocateVirtualMemory(ptr ptr long ptr long long)
@ stdcall ZwAllocateVirtualMemoryEx(ptr ptr ptr long long ptr long)
@ stdcall ZwAssignProcessToJobObject(ptr ptr)
@ stdcall ZwCancelIoFile(ptr ptr)
@ stdcall ZwCancelIoFileEx(ptr ptr ptr)
@ stdcall ZwCancelTimer(ptr ptr)
@ stdcall ZwClearEvent(ptr)
@ stdcall ZwClose(ptr)
@ stdcall ZwCloseObjectAuditAlarm(ptr ptr long)
@ stdcall ZwConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr)
@ stdcall ZwAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long)
@ stdcall ZwAlpcCancelMessage(ptr long ptr)
@ stdcall ZwAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr)
@ stdcall ZwAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr)
@ stdcall ZwAlpcCreatePort(ptr ptr ptr)
@ stdcall ZwAlpcCreatePortSection(ptr long ptr long ptr ptr)
@ stdcall ZwAlpcCreateResourceReserve(ptr long long ptr)
@ stdcall ZwAlpcCreateSectionView(ptr long ptr)
@ stdcall ZwAlpcCreateSecurityContext(ptr long ptr)
@ stdcall ZwAlpcDeletePortSection(ptr long ptr)
@ stdcall ZwAlpcDeleteResourceReserve(ptr long long)
@ stdcall ZwAlpcDeleteSectionView(ptr long ptr)
@ stdcall ZwAlpcDeleteSecurityContext(ptr long ptr)
@ stdcall ZwAlpcDisconnectPort(ptr long)
@ stdcall ZwAlpcOpenSenderProcess(ptr ptr ptr long long ptr)
@ stdcall ZwAlpcOpenSenderThread(ptr ptr ptr long long ptr)
@ stdcall ZwAlpcQueryInformation(ptr long ptr long ptr)
@ stdcall ZwAlpcQueryInformationMessage(ptr ptr long ptr long ptr)
@ stdcall ZwAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr)
@ stdcall ZwAlpcSetInformation(ptr long ptr long)
@ stdcall ZwCreateDirectoryObject(ptr long ptr)
@ stdcall ZwCreateEvent(ptr long ptr long long)
@ stdcall ZwCreateFile(ptr long ptr ptr ptr long long long long ptr long)
@ stdcall ZwCreateJobObject(ptr long ptr)
@ stdcall ZwCreateKey(ptr long ptr long ptr long ptr)
@ stdcall ZwCreateSection(ptr long ptr ptr long long ptr)
@ stdcall ZwCreateSymbolicLinkObject(ptr long ptr ptr)
@ stdcall ZwCreateTimer(ptr long ptr long)
@ stdcall ZwDeleteBootEntry(long)
@ stdcall ZwDeleteDriverEntry(long)
@ stdcall ZwDeleteFile(ptr)
@ stdcall ZwDeleteKey(ptr)
@ stdcall ZwDeleteValueKey(ptr ptr)
@ stdcall ZwDeviceIoControlFile(ptr ptr ptr ptr ptr long ptr long ptr long)
@ stdcall ZwDisplayString(ptr)
@ stdcall ZwDuplicateObject(ptr ptr ptr ptr long long long)
@ stdcall ZwDuplicateToken(ptr long ptr long long ptr)
@ stdcall ZwEnumerateBootEntries(ptr ptr)
@ stdcall ZwEnumerateDriverEntries(ptr ptr)
@ stdcall ZwEnumerateKey(ptr long long ptr long ptr)
@ stdcall ZwEnumerateValueKey(ptr long long ptr long ptr)
@ stdcall ZwFlushInstructionCache(ptr ptr long)
@ stdcall ZwFlushKey(ptr)
@ stdcall ZwFlushProcessWriteBuffers()
@ stdcall ZwFlushVirtualMemory(ptr ptr ptr ptr)
@ stdcall ZwFreeVirtualMemory(ptr ptr ptr long)
@ stdcall ZwFsControlFile(ptr ptr ptr ptr ptr long ptr long ptr long)
@ stdcall ZwInitiatePowerAction(long long long long)
@ stdcall ZwIsProcessInJob(ptr ptr)
@ stdcall ZwLoadDriver(ptr)
@ stdcall ZwLoadKey(ptr ptr)
@ stdcall ZwMakeTemporaryObject(ptr)
@ stdcall ZwMapViewOfSection(ptr ptr ptr long long ptr ptr long long long)
@ stdcall ZwModifyBootEntry(ptr)
@ stdcall ZwModifyDriverEntry(ptr)
@ stdcall ZwNotifyChangeKey(ptr ptr ptr ptr ptr long long ptr long long)
@ stdcall ZwOpenDirectoryObject(ptr long ptr)
@ stdcall ZwOpenEvent(ptr long ptr)
@ stdcall ZwOpenFile(ptr long ptr ptr long long)
@ stdcall ZwOpenJobObject(ptr long ptr)
@ stdcall ZwOpenKey(ptr long ptr)
@ stdcall ZwOpenProcess(ptr long ptr ptr)
@ stdcall ZwOpenProcessToken(ptr long ptr)
@ stdcall ZwOpenProcessTokenEx(ptr long long ptr)
@ stdcall ZwOpenSection(ptr long ptr)
@ stdcall ZwOpenSymbolicLinkObject(ptr long ptr)
@ stdcall ZwOpenThread(ptr long ptr ptr)
@ stdcall ZwOpenThreadToken(ptr long long ptr)
@ stdcall ZwOpenThreadTokenEx(ptr long long long ptr)
@ stdcall ZwOpenTimer(ptr long ptr)
@ stdcall ZwPowerInformation(long ptr long ptr long)
@ stdcall ZwPulseEvent(ptr ptr)
@ stdcall ZwQueryBootEntryOrder(ptr ptr)
@ stdcall ZwQueryBootOptions(ptr ptr)
@ stdcall ZwQueryDefaultLocale(long ptr)
@ stdcall ZwQueryDefaultUILanguage(ptr)
@ stdcall ZwQueryDirectoryFile(ptr ptr ptr ptr ptr ptr long long long ptr long)
@ stdcall ZwQueryDirectoryObject(ptr ptr long long long ptr ptr)
@ stdcall ZwQueryDriverEntryOrder(ptr ptr)
@ stdcall ZwQueryEaFile(ptr ptr ptr long long ptr long ptr long)
@ stdcall ZwQueryFullAttributesFile(ptr ptr)
@ stdcall ZwQueryInformationFile(ptr ptr ptr long long)
@ stdcall ZwQueryInformationJobObject(ptr long ptr long ptr)
@ stdcall ZwQueryInformationProcess(ptr long ptr long ptr)
@ stdcall ZwQueryInformationThread(ptr long ptr long ptr)
@ stdcall ZwQueryInformationToken(ptr long long long ptr)
@ stdcall ZwQueryInstallUILanguage(ptr)
@ stdcall ZwQueryKey(ptr long ptr long ptr)
@ stdcall ZwQueryObject(ptr long ptr long ptr)
@ stdcall ZwQuerySection(ptr long ptr long ptr)
@ stdcall ZwQuerySecurityObject(ptr long ptr long ptr)
@ stdcall ZwQuerySymbolicLinkObject(ptr ptr ptr)
@ stdcall ZwQuerySystemInformation(long ptr long ptr)
@ stdcall ZwQueryValueKey(ptr ptr long ptr long ptr)
@ stdcall ZwQueryVolumeInformationFile(ptr ptr ptr long long)
@ stdcall ZwReadFile(ptr ptr ptr ptr ptr ptr long ptr ptr)
@ stdcall ZwReplaceKey(ptr ptr ptr)
@ stdcall ZwRequestWaitReplyPort(ptr ptr ptr)
@ stdcall ZwResetEvent(ptr ptr)
@ stdcall ZwRestoreKey(ptr ptr long)
@ stdcall ZwSaveKey(ptr ptr)
@ stdcall ZwSaveKeyEx(ptr ptr long)
@ stdcall ZwSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr)
@ stdcall ZwSetBootEntryOrder(ptr ptr)
@ stdcall ZwSetBootOptions(ptr long)
@ stdcall ZwSetDefaultLocale(long long)
@ stdcall ZwSetDefaultUILanguage(long)
@ stdcall ZwSetDriverEntryOrder(ptr ptr)
@ stdcall ZwSetEaFile(ptr ptr ptr long)
@ stdcall ZwSetEvent(ptr ptr)
@ stdcall ZwSetInformationFile(ptr ptr ptr long long)
@ stdcall ZwSetInformationJobObject(ptr long ptr long)
@ stdcall ZwSetInformationObject(ptr long ptr long)
@ stdcall ZwSetInformationProcess(ptr long ptr long)
@ stdcall ZwSetInformationThread(ptr long ptr long)
@ stdcall -version=0x600+ ZwRemoveIoCompletionEx(ptr ptr long ptr ptr long)
@ stdcall -version=0x600+ ZwSetIoCompletionEx(ptr ptr long long long long)
@ stdcall ZwSetSecurityObject(ptr long ptr)
@ stdcall ZwSetSystemInformation(long ptr long)
@ stdcall ZwSetSystemTime(ptr ptr)
@ stdcall ZwSetTimer(ptr ptr ptr ptr long long ptr)
@ stdcall ZwSetValueKey(ptr ptr long long ptr long)
@ stdcall ZwSetVolumeInformationFile(ptr ptr ptr long long)
@ stdcall ZwTerminateJobObject(ptr long)
@ stdcall ZwTerminateProcess(ptr long)
@ stdcall ZwTranslateFilePath(ptr long ptr long)
@ stdcall ZwUnloadDriver(ptr)
@ stdcall ZwUnloadKey(ptr)
@ stdcall ZwUnmapViewOfSection(ptr ptr)
@ stdcall ZwWaitForMultipleObjects(long ptr long long ptr)
@ stdcall ZwWaitForSingleObject(ptr long ptr)
@ stdcall ZwWriteFile(ptr ptr ptr ptr ptr ptr long ptr ptr)
@ stdcall ZwYieldExecution()
@ cdecl -arch=x86_64,arm64,arm __C_specific_handler(ptr long ptr ptr)
@ cdecl -arch=arm __jump_unwind()
@ cdecl -arch=x86_64,arm64 __chkstk()
;@ cdecl -arch=x86_64,arm64 __misaligned_access()
@ cdecl -arch=i386 _CIcos()
@ cdecl -arch=i386 _CIsin()
@ cdecl -arch=i386 _CIsqrt()
@ cdecl -arch=i386,arm,arm64 _abnormal_termination()
@ cdecl -arch=i386 _alldiv()
@ cdecl -arch=i386 _alldvrm()
@ cdecl -arch=i386 _allmul()
@ cdecl -arch=i386 _alloca_probe()
@ cdecl -arch=i386 _allrem()
@ cdecl -arch=i386 _allshl()
@ cdecl -arch=i386 _allshr()
@ cdecl -arch=i386 _aulldiv()
@ cdecl -arch=i386 _aulldvrm()
@ cdecl -arch=i386 _aullrem()
@ cdecl -arch=i386 _aullshr()
@ cdecl -arch=i386,arm,arm64 _except_handler2()
@ cdecl -arch=i386,arm,arm64 _except_handler3()
@ cdecl -arch=i386,arm,arm64 _global_unwind2()
@ cdecl _itoa()
@ cdecl _itow()
@ cdecl -arch=i386,arm,arm64 _local_unwind2()
@ cdecl -arch=x86_64,arm64 _local_unwind()
@ cdecl _purecall()
@ cdecl -arch=x86_64,arm64,arm _setjmp(ptr ptr)
@ cdecl -arch=x86_64,arm64,arm _setjmpex(ptr ptr)
@ cdecl _snprintf()
@ cdecl _snwprintf()
@ cdecl _stricmp()
@ cdecl _strlwr()
@ cdecl _strnicmp()
@ cdecl _strnset()
@ cdecl _strrev()
@ cdecl _strset()
@ cdecl _strupr()
@ cdecl -version=0x400-0x502 -impsym _swprintf() swprintf # Compatibility with pre NT6
@ cdecl -version=0x600+ _swprintf()
@ cdecl _vsnprintf()
@ cdecl _vsnwprintf()
@ cdecl _wcsicmp()
@ cdecl _wcslwr()
@ cdecl _wcsnicmp()
@ cdecl _wcsnset()
@ cdecl _wcsrev()
@ cdecl _wcsupr()
@ cdecl atoi()
@ cdecl atol()
@ cdecl isdigit()
@ cdecl islower()
@ cdecl isprint()
@ cdecl isspace()
@ cdecl isupper()
@ cdecl isxdigit()
@ cdecl -arch=x86_64,arm64,arm longjmp(ptr long)
@ cdecl mbstowcs()
@ cdecl mbtowc()
@ cdecl memchr()
@ cdecl -arch=x86_64,arm64 memcmp()
@ cdecl memcpy()
@ cdecl memmove()
@ cdecl memset()
@ cdecl qsort()
@ cdecl rand()
@ varargs sprintf(ptr str)
@ cdecl srand()
@ cdecl strcat()
@ cdecl strchr()
@ cdecl strcmp()
@ cdecl strcpy()
@ cdecl strlen()
@ cdecl strncat()
@ cdecl strncmp()
@ cdecl strncpy()
@ cdecl strrchr()
@ cdecl strspn()
@ cdecl strstr()
@ cdecl swprintf() _swprintf # Non-conforming swprintf
@ cdecl tolower()
@ cdecl toupper() toupper_nt_mb
@ cdecl towlower()
@ cdecl towupper()
@ stdcall vDbgPrintEx(long long str ptr)
@ stdcall vDbgPrintExWithPrefix(str long long str ptr)
@ cdecl vsprintf(ptr str ptr)
@ cdecl wcscat()
@ cdecl wcschr()
@ cdecl wcscmp()
@ cdecl wcscpy()
@ cdecl wcscspn()
@ cdecl wcslen()
@ cdecl wcsncat()
@ cdecl wcsncmp()
@ cdecl wcsncpy()
@ cdecl wcsrchr()
@ cdecl wcsspn()
@ cdecl wcsstr()
@ cdecl wcstombs()
@ cdecl wctomb()

# FIXME: check if this is correct
@ stdcall -arch=arm __dtoi64()
@ stdcall -arch=arm __dtou64()
@ stdcall -arch=arm __i64tod()
@ stdcall -arch=arm __u64tod()
@ stdcall -arch=arm __rt_sdiv()
@ stdcall -arch=arm __rt_sdiv64()
@ stdcall -arch=arm __rt_udiv()
@ stdcall -arch=arm __rt_udiv64()
@ stdcall -arch=arm __rt_srsh()

; ARM64 HAL dependencies
@ stdcall -arch=arm64 KfLowerIrql(long)
@ stdcall -arch=arm64 KfAcquireSpinLock(ptr)
@ stdcall -arch=arm64 KfReleaseSpinLock(ptr long)
@ stdcall -arch=arm64 KeGetCurrentIrql()
@ stdcall -arch=x86_64 KeGetCurrentIrql() KxGetCurrentIrql
; ARM64 SMP diagnostics (smpdbg) recorders, called from the HAL
@ stdcall -arch=arm64 SmpDbgTimerBegin(long long)
@ stdcall -arch=arm64 SmpDbgTimerEoi(long long)
@ stdcall -arch=arm64 SmpDbgTimerReject(long long)
@ stdcall -arch=arm64 KxSaveFloatingPointState(ptr)
@ stdcall -arch=arm64 KxRestoreFloatingPointState(ptr)
@ stdcall -arch=arm64 KeAcquireSpinLock(ptr ptr)
@ stdcall -arch=arm64 KeRaiseIrql(long ptr)
@ stdcall -arch=arm64 KeRaiseIrqlToSynchLevel()

# ==========================================================================
# Win11 ARM64 export parity (arm64 only). Generated batch; see commit msg.
# ==========================================================================
# --- HAL functions re-exported by the kernel (forwarded to hal.dll, real impls) ---
@ stdcall -arch=arm64 HalAcpiGetTableEx() hal.HalAcpiGetTableEx
@ stdcall -arch=arm64 HalAllProcessorsStarted() hal.HalAllProcessorsStarted
@ stdcall -arch=arm64 HalAllocateCrashDumpRegisters() hal.HalAllocateCrashDumpRegisters
@ stdcall -arch=arm64 HalAllocateHardwareCounters() hal.HalAllocateHardwareCounters
@ stdcall -arch=arm64 HalBeginSystemInterruptUnspecified() hal.HalBeginSystemInterruptUnspecified
@ stdcall -arch=arm64 HalBugCheckSystem() hal.HalBugCheckSystem
@ stdcall -arch=arm64 HalCalibratePerformanceCounter() hal.HalCalibratePerformanceCounter
@ stdcall -arch=arm64 HalConvertDeviceIdtToIrql(long) hal.HalConvertDeviceIdtToIrql
@ stdcall -arch=arm64 HalDisableInterrupt(ptr) hal.HalDisableInterrupt
@ stdcall -arch=arm64 HalDmaAllocateCrashDumpRegistersEx() hal.HalDmaAllocateCrashDumpRegistersEx
@ stdcall -arch=arm64 HalDmaFreeCrashDumpRegistersEx() hal.HalDmaFreeCrashDumpRegistersEx
@ stdcall -arch=arm64 HalEnableInterrupt(ptr) hal.HalEnableInterrupt
@ stdcall -arch=arm64 HalEndSystemInterrupt() hal.HalEndSystemInterrupt
@ stdcall -arch=arm64 HalEnumerateEnvironmentVariablesEx() hal.HalEnumerateEnvironmentVariablesEx
@ stdcall -arch=arm64 HalEnumerateProcessors() hal.HalEnumerateProcessors
@ stdcall -arch=arm64 HalFreeHardwareCounters(ptr) hal.HalFreeHardwareCounters
@ stdcall -arch=arm64 HalGetBusDataByOffset() hal.HalGetBusDataByOffset
@ stdcall -arch=arm64 HalGetEnvironmentVariable() hal.HalGetEnvironmentVariable
@ stdcall -arch=arm64 HalGetEnvironmentVariableEx() hal.HalGetEnvironmentVariableEx
@ stdcall -arch=arm64 HalGetInterruptTargetInformation(ptr) hal.HalGetInterruptTargetInformation
@ stdcall -arch=arm64 HalGetMemoryCachingRequirements() hal.HalGetMemoryCachingRequirements
@ stdcall -arch=arm64 HalGetMessageRoutingInfo(ptr) hal.HalGetMessageRoutingInfo
@ stdcall -arch=arm64 HalGetProcessorIdByNtNumber() hal.HalGetProcessorIdByNtNumber
@ stdcall -arch=arm64 HalGetVectorInput() hal.HalGetVectorInput
@ stdcall -arch=arm64 HalInitSystem() hal.HalInitSystem
@ stdcall -arch=arm64 HalInitializeOnResume(ptr) hal.HalInitializeOnResume
@ stdcall -arch=arm64 HalInitializeProcessor() hal.HalInitializeProcessor
@ stdcall -arch=arm64 HalIsHyperThreadingEnabled() hal.HalIsHyperThreadingEnabled
@ stdcall -arch=arm64 HalProcessorIdle() hal.HalProcessorIdle
@ stdcall -arch=arm64 HalQueryEnvironmentVariableInfoEx() hal.HalQueryEnvironmentVariableInfoEx
@ stdcall -arch=arm64 HalQueryMaximumProcessorCount() hal.HalQueryMaximumProcessorCount
@ stdcall -arch=arm64 HalQueryRealTimeClock(ptr) hal.HalQueryRealTimeClock
@ stdcall -arch=arm64 HalRegisterDynamicProcessor() hal.HalRegisterDynamicProcessor
@ stdcall -arch=arm64 HalRegisterErrataCallbacks(ptr) hal.HalRegisterErrataCallbacks
@ stdcall -arch=arm64 HalReportResourceUsage() hal.HalReportResourceUsage
@ stdcall -arch=arm64 HalRequestClockInterrupt(long) hal.HalRequestClockInterrupt
@ stdcall -arch=arm64 HalRequestDeferredRecoveryServiceInterrupt() hal.HalRequestDeferredRecoveryServiceInterrupt
@ stdcall -arch=arm64 HalRequestIpi(long ptr) hal.HalRequestIpi
@ stdcall -arch=arm64 HalRequestIpiSpecifyVector(long ptr long) hal.HalRequestIpiSpecifyVector
@ fastcall -arch=arm64 HalRequestSoftwareInterrupt(long) hal.HalRequestSoftwareInterrupt
@ stdcall -arch=arm64 HalReturnToFirmware(long) hal.HalReturnToFirmware
@ stdcall -arch=arm64 HalSendSoftwareInterrupt() hal.HalSendSoftwareInterrupt
@ stdcall -arch=arm64 HalSetBusDataByOffset() hal.HalSetBusDataByOffset
@ stdcall -arch=arm64 HalSetEnvironmentVariable() hal.HalSetEnvironmentVariable
@ stdcall -arch=arm64 HalSetEnvironmentVariableEx() hal.HalSetEnvironmentVariableEx
@ stdcall -arch=arm64 HalSetMpam0(int64) hal.HalSetMpam0
@ stdcall -arch=arm64 HalSetProfileInterval(long) hal.HalSetProfileInterval
@ stdcall -arch=arm64 HalSetRealTimeClock(ptr) hal.HalSetRealTimeClock
@ stdcall -arch=arm64 HalStartDynamicProcessor() hal.HalStartDynamicProcessor
@ stdcall -arch=arm64 HalStartNextProcessor() hal.HalStartNextProcessor
@ stdcall -arch=arm64 HalStartProfileInterrupt(long) hal.HalStartProfileInterrupt
@ stdcall -arch=arm64 HalStopProfileInterrupt(long) hal.HalStopProfileInterrupt
@ stdcall -arch=arm64 HalTranslateBusAddress() hal.HalTranslateBusAddress
@ stdcall -arch=arm64 HalWheaHandleSea(ptr) hal.HalWheaHandleSea
@ stdcall -arch=arm64 HalWheaHandleSei(ptr) hal.HalWheaHandleSei
@ stdcall -arch=arm64 HalWheaUpdateCmciPolicy(ptr) hal.HalWheaUpdateCmciPolicy
@ stdcall -arch=arm64 KeFlushWriteBuffer() hal.KeFlushWriteBuffer
@ stdcall -arch=arm64 READ_PORT_BUFFER_UCHAR() hal.READ_PORT_BUFFER_UCHAR
@ stdcall -arch=arm64 READ_PORT_BUFFER_ULONG() hal.READ_PORT_BUFFER_ULONG
@ stdcall -arch=arm64 READ_PORT_BUFFER_USHORT() hal.READ_PORT_BUFFER_USHORT
@ stdcall -arch=arm64 READ_PORT_UCHAR(ptr) hal.READ_PORT_UCHAR
@ stdcall -arch=arm64 READ_PORT_ULONG(ptr) hal.READ_PORT_ULONG
@ stdcall -arch=arm64 READ_PORT_USHORT(ptr) hal.READ_PORT_USHORT
@ stdcall -arch=arm64 WRITE_PORT_BUFFER_UCHAR() hal.WRITE_PORT_BUFFER_UCHAR
@ stdcall -arch=arm64 WRITE_PORT_BUFFER_ULONG() hal.WRITE_PORT_BUFFER_ULONG
@ stdcall -arch=arm64 WRITE_PORT_BUFFER_USHORT() hal.WRITE_PORT_BUFFER_USHORT
@ stdcall -arch=arm64 WRITE_PORT_UCHAR() hal.WRITE_PORT_UCHAR
@ stdcall -arch=arm64 WRITE_PORT_ULONG() hal.WRITE_PORT_ULONG
@ stdcall -arch=arm64 WRITE_PORT_USHORT() hal.WRITE_PORT_USHORT
# --- Already-implemented ntoskrnl functions, now exported on arm64 ---
@ stdcall -arch=arm64 ExBlockPushLock()
@ stdcall -arch=arm64 ExTimedWaitForUnblockPushLock()
@ stdcall -arch=arm64 ExTryToAcquireResourceExclusiveLite()
@ stdcall -arch=arm64 ExWaitForUnblockPushLock()
@ stdcall -arch=arm64 FsRtlOplockBreakToNone()
@ stdcall -arch=arm64 KdLogDbgPrint()
@ stdcall -arch=arm64 KeAlertThread()
@ stdcall -arch=arm64 KeRemoveQueueApc()
@ stdcall -arch=arm64 KeTestAlertThread()
@ stdcall -arch=arm64 KiDispatchInterrupt()
@ stdcall -arch=arm64 MmCopyVirtualMemory()
@ stdcall -arch=arm64 MmLoadSystemImage()
@ stdcall -arch=arm64 MmMapViewInSystemSpaceEx()
@ stdcall -arch=arm64 MmUnloadSystemImage()
@ stdcall -arch=arm64 NtEnumerateSystemEnvironmentValuesEx()
@ stdcall -arch=arm64 NtReadFileScatter()
@ stdcall -arch=arm64 NtSetInformationToken()
@ stdcall -arch=arm64 NtWriteFileGather()
@ stdcall -arch=arm64 ObDereferenceObjectDeferDelete()
@ stdcall -arch=arm64 ObDuplicateObject()
@ stdcall -arch=arm64 ObIsKernelHandle()
@ stdcall -arch=arm64 ObReferenceObjectSafe()
@ stdcall -arch=arm64 PsReferenceProcessFilePointer()
@ stdcall -arch=arm64 PsResumeProcess()
@ stdcall -arch=arm64 PsSuspendProcess()
@ stdcall -arch=arm64 RtlAddAccessAllowedObjectAce()
@ stdcall -arch=arm64 RtlAddAccessDeniedAceEx()
@ stdcall -arch=arm64 RtlAddAccessDeniedObjectAce()
@ stdcall -arch=arm64 RtlAddAuditAccessAceEx()
@ stdcall -arch=arm64 RtlAddAuditAccessObjectAce()
@ stdcall -arch=arm64 RtlCopyLuidAndAttributesArray()
@ stdcall -arch=arm64 RtlCopySidAndAttributesArray()
@ stdcall -arch=arm64 RtlCreateUnicodeStringFromAsciiz()
@ stdcall -arch=arm64 RtlCreateUserThread()
@ stdcall -arch=arm64 RtlCultureNameToLCID()
@ stdcall -arch=arm64 RtlDowncaseUnicodeChar()
@ stdcall -arch=arm64 RtlDuplicateUnicodeString()
@ stdcall -arch=arm64 RtlFillMemoryUlonglong()
@ stdcall -arch=arm64 RtlFindExportedRoutineByName()
@ stdcall -arch=arm64 RtlFindNextForwardRunSet()
@ stdcall -arch=arm64 RtlFirstFreeAce()
@ stdcall -arch=arm64 RtlFormatMessage()
@ stdcall -arch=arm64 RtlGetControlSecurityDescriptor()
@ stdcall -arch=arm64 RtlGetNtProductType()
@ stdcall -arch=arm64 RtlImageNtHeaderEx()
@ stdcall -arch=arm64 RtlLCIDToCultureName()
@ stdcall -arch=arm64 RtlLargeIntegerToChar()
@ stdcall -arch=arm64 RtlLocalTimeToSystemTime()
@ stdcall -arch=arm64 RtlLookupFirstMatchingElementGenericTableAvl()
@ stdcall -arch=arm64 RtlOpenCurrentUser()
@ stdcall -arch=arm64 RtlQueryInformationAcl()
@ stdcall -arch=arm64 RtlRaiseStatus()
@ stdcall -arch=arm64 RtlSetControlSecurityDescriptor()
@ stdcall -arch=arm64 RtlSystemTimeToLocalTime()
@ stdcall RtlUTF8ToUnicodeN(ptr long ptr str long)
@ stdcall -arch=arm64 RtlValidAcl()
@ stdcall -arch=arm64 RtlValidateUnicodeString()
@ stdcall -arch=arm64 SeCaptureSubjectContextEx()
@ stdcall -arch=arm64 SeCreateAccessStateEx()
@ stdcall -arch=arm64 SeLocateProcessImageName()
@ stdcall -arch=arm64 SeTokenIsWriteRestricted()
@ stdcall -arch=arm64 ZwAllocateLocallyUniqueId()
@ stdcall -arch=arm64 ZwCompareTokens()
@ stdcall -arch=arm64 ZwCreateIoCompletion()
@ stdcall -arch=arm64 ZwCreateProcessEx()
@ stdcall -arch=arm64 ZwCreateSemaphore()
@ stdcall -arch=arm64 ZwFlushBuffersFile()
@ stdcall -arch=arm64 ZwGetWriteWatch()
@ stdcall -arch=arm64 ZwImpersonateAnonymousToken()
@ stdcall -arch=arm64 ZwLoadKeyEx()
@ stdcall -arch=arm64 ZwLockFile()
@ stdcall -arch=arm64 ZwLockProductActivationKeys()
@ stdcall -arch=arm64 ZwLockVirtualMemory()
@ stdcall -arch=arm64 ZwNotifyChangeDirectoryFile()
@ stdcall -arch=arm64 ZwProtectVirtualMemory()
@ stdcall -arch=arm64 ZwQueryIntervalProfile()
@ stdcall -arch=arm64 ZwQueryQuotaInformationFile()
@ stdcall -arch=arm64 ZwQuerySystemEnvironmentValueEx()
@ stdcall -arch=arm64 ZwQueryTimerResolution()
@ stdcall -arch=arm64 ZwQueryVirtualMemory()
@ stdcall -arch=arm64 ZwReleaseSemaphore()
@ stdcall -arch=arm64 ZwRemoveIoCompletion()
@ stdcall -arch=arm64 ZwRenameKey()
@ stdcall -arch=arm64 ZwRequestPort()
@ stdcall -arch=arm64 ZwResetWriteWatch()
@ stdcall -arch=arm64 ZwResumeThread()
@ stdcall -arch=arm64 ZwSetInformationKey()
@ stdcall -arch=arm64 ZwSetInformationToken()
@ stdcall -arch=arm64 ZwSetIntervalProfile()
@ stdcall -arch=arm64 ZwSetIoCompletion()
@ stdcall -arch=arm64 ZwSetQuotaInformationFile()
@ stdcall -arch=arm64 ZwSetSystemEnvironmentValueEx()
@ stdcall -arch=arm64 ZwSetTimerResolution()
@ stdcall -arch=arm64 ZwStartProfile()
@ stdcall -arch=arm64 ZwStopProfile()
@ stdcall -arch=arm64 ZwSuspendThread()
@ stdcall -arch=arm64 ZwSystemDebugControl()
@ stdcall -arch=arm64 ZwTraceEvent()
@ stdcall -arch=arm64 ZwUnloadKey2()
@ stdcall -arch=arm64 ZwUnloadKeyEx()
@ stdcall -arch=arm64 ZwUnlockFile()
@ stdcall -arch=arm64 ZwUnlockVirtualMemory()
@ cdecl -arch=arm64 _atoi64()
@ cdecl -arch=arm64 _finite()
@ cdecl -arch=arm64 _i64toa_s()
@ cdecl -arch=arm64 _i64tow_s()
@ cdecl -arch=arm64 _itoa_s()
@ cdecl -arch=arm64 _itow_s()
@ cdecl -arch=arm64 _ltoa_s()
@ cdecl -arch=arm64 _ltow_s()
@ cdecl -arch=arm64 _ui64toa_s()
@ cdecl -arch=arm64 _ui64tow_s()
@ cdecl -arch=arm64 _vswprintf()
@ cdecl -arch=arm64 _wtoi()
@ cdecl -arch=arm64 _wtol()
@ cdecl -arch=arm64 bsearch()
@ stdcall -arch=arm64 iswalnum()
@ cdecl -arch=arm64 iswdigit()
@ cdecl -arch=arm64 iswspace()
@ stdcall -arch=arm64 strnlen()
@ stdcall -arch=arm64 wcscat_s()
@ stdcall -arch=arm64 wcscpy_s()
@ cdecl -arch=x86_64 wcscpy_s(ptr int64 ptr)
@ stdcall -arch=arm64 wcsncat_s()
@ stdcall -arch=arm64 wcsncpy_s()
@ stdcall -arch=arm64 wcsnlen()
@ cdecl -arch=arm64 wcstoul()
# --- Data exports (already defined in the kernel) ---
@ extern -arch=arm64 NtBuildLab
@ extern -arch=arm64 PsLoadedModuleList
@ extern -arch=arm64 PsLoadedModuleResource
@ extern -arch=arm64 SeSystemDefaultSd
# --- Data exports (ARM64 parity stub variables, defined in arm64_export_stubs.c) ---
@ extern CmKeyObjectType
@ extern -arch=arm64 ExActivationObjectType
@ extern -arch=arm64 ExCompositionObjectType
@ extern -arch=arm64 ExCoreMessagingObjectType
@ extern -arch=arm64 ExRawInputManagerObjectType
@ extern ExTimerObjectType
@ extern IoCompletionObjectType
@ extern -arch=arm64 KdComPortInUse
@ extern -arch=arm64 KdEventLoggingEnabled
@ extern -arch=arm64 KdHvComPortInUse
@ extern -arch=arm64 KeDynamicPartitioningSupported
@ extern -arch=arm64 MmBadPointer
@ extern -arch=arm64 NtBuildGUID
@ extern -arch=arm64 POGOBuffer
@ extern -arch=arm64 PsPartitionType
@ extern -arch=arm64 PsSiloContextNonPagedType
@ extern -arch=arm64 PsSiloContextPagedType
@ extern -arch=arm64 PsUILanguageComitted
@ extern -arch=arm64 SeILSigningPolicyPtr
@ extern -arch=arm64 TmEnlistmentObjectType
@ extern -arch=arm64 TmResourceManagerObjectType
@ extern -arch=arm64 TmTransactionManagerObjectType
@ extern -arch=arm64 TmTransactionObjectType
@ extern -arch=arm64 psMUITest
@ stdcall -arch=arm64,x86_64 AlpcCreateSecurityContext(ptr ptr long ptr)
@ stdcall -arch=arm64,x86_64 AlpcGetHeaderSize(long)
@ stdcall -arch=arm64,x86_64 AlpcGetMessageAttribute(ptr long)
@ stdcall -arch=arm64,x86_64 AlpcInitializeMessageAttribute(long ptr long ptr)
# --- Unimplemented Win11 exports (auto-generated stubs raise STATUS via DbgPrint) ---
@ stub -arch=arm64 BgkDisplayCharacter
@ stub -arch=arm64 BgkGetConsoleState
@ stub -arch=arm64 BgkGetCursorState
@ stub -arch=arm64 BgkSetCursor
@ stub -arch=arm64 CarCopyRuleViolationDetails
@ stub -arch=arm64 CarCreateRuleViolationDetails
@ stub -arch=arm64 CarDeleteRuleViolationDetails
@ stub -arch=arm64 CarDeregisterRuleClassConfiguration
@ stub -arch=arm64 CarDeregisterRuleOverride
@ stub -arch=arm64 CarInitializeRuleViolationDetails
@ stub -arch=arm64 CarQueryReportAction
@ stub -arch=arm64 CarQueryReportActionForTriage
@ stub -arch=arm64 CarRegisterDefaultRuleClassConfiguration
@ stub -arch=arm64 CarRegisterRuleClassConfiguration
@ stub -arch=arm64 CarRegisterRuleOverride
@ stub -arch=arm64 CarRegisterRuleOverrideAllContexts
@ stub -arch=arm64 CarRegisterRuleOverridesAllContexts
@ stub -arch=arm64 CarReportDifPluginRuleViolation
@ stub -arch=arm64 CarSetCustomIdInRuleOverride
@ stub -arch=arm64 CarSetCustomRuleIdRange
@ stub -arch=arm64 CcAddDirtyPagesToExternalCache
@ stub -arch=arm64 CcAsyncCopyRead
@ stub -arch=arm64 CcDeductDirtyPagesFromExternalCache
@ stub -arch=arm64 CcErrorCallbackRoutine
@ stub -arch=arm64 CcFlushCacheToLsn
@ stub -arch=arm64 CcGetCachedDirtyPageCountForFile
@ stub -arch=arm64 CcGetFileObjectFromSectionPtrsRef
@ stub -arch=arm64 CcGetNumberOfMappedPages
@ stub -arch=arm64 CcInitializeCacheMapEx
@ stub -arch=arm64 CcInitializeCacheMapEx2
@ stub -arch=arm64 CcIsCacheManagerCallbackNeeded
@ stub -arch=arm64 CcIsThereDirtyDataEx
@ stub -arch=arm64 CcIsThereDirtyLoggedPages
@ stub -arch=arm64 CcRegisterExternalCache
@ stub -arch=arm64 CcScheduleReadAheadEx
@ stub -arch=arm64 CcSetFileSizesEx
@ stub -arch=arm64 CcSetLogHandleForFileEx
@ stub -arch=arm64 CcSetLoggedDataThreshold
@ stub -arch=arm64 CcSetParallelFlushFile
@ stub -arch=arm64 CcSetReadAheadGranularityEx
@ stub -arch=arm64 CcTestControl
@ stub -arch=arm64 CcUnmapFileOffsetFromSystemCache
@ stub -arch=arm64 CcUnregisterExternalCache
@ stub -arch=arm64 CcZeroDataOnDisk
@ stub -arch=arm64 CmCallbackGetKeyObjectID
@ stub -arch=arm64 CmCallbackGetKeyObjectIDEx
@ stub -arch=arm64 CmCallbackReleaseKeyObjectIDEx
@ stub -arch=arm64 CmGetBoundTransaction
@ stub -arch=arm64 CmGetCallbackVersion
@ stub -arch=arm64 CmRegisterCallbackEx
@ stub -arch=arm64 CmRegisterMachineHiveLoadedNotification
@ stub -arch=arm64 CmSetCallbackObjectContext
@ stub -arch=arm64 CmUnregisterMachineHiveLoadedNotification
@ stub -arch=arm64 DbgSetDebugPrintCallback
@ stub -arch=arm64 DbgkLkmdRegisterCallback
@ stub -arch=arm64 DbgkLkmdUnregisterCallback
@ stdcall -arch=x86_64,arm64 DbgkWerCaptureLiveKernelDump(ptr long ptr ptr ptr ptr ptr ptr long)
@ stub -arch=arm64 DbgkWerCaptureLiveKernelDump2
@ stub -arch=arm64 DifEnumeratePluginData
@ stub -arch=arm64 DifFindThreadContextData
@ stub -arch=arm64 DifGetPluginPerDriverData
@ stub -arch=arm64 DifObjTrkInsertItem
@ stub -arch=arm64 DifObjTrkQeuryInvokeDeleteRange
@ stub -arch=arm64 DifObjTrkRemoveItem
@ stub -arch=arm64 DifPluginSimplePerfControl
@ stub -arch=arm64 DifPopThreadContextData
@ stub -arch=arm64 DifPushThreadContextData
@ stub -arch=arm64 DifRegisterClassDriverPlugin
@ stub -arch=arm64 DifRegisterObjectTracking
@ stub -arch=arm64 DifRegisterPlugin
@ stub -arch=arm64 DifUtilDbgPrint
@ stub -arch=arm64 EmClientQueryRuleState
@ stub -arch=arm64 EmClientRuleDeregisterNotification
@ stub -arch=arm64 EmClientRuleEvaluate
@ stub -arch=arm64 EmClientRuleRegisterNotification
@ stub -arch=arm64 EmProviderDeregister
@ stub -arch=arm64 EmProviderDeregisterEntry
@ stub -arch=arm64 EmProviderRegister
@ stub -arch=arm64 EmProviderRegisterEntry
@ stub -arch=arm64 EmpProviderRegister
@ stub -arch=arm64 EtwActivityIdControl
@ stub -arch=arm64 EtwEnableTrace
@ stub -arch=arm64 EtwEventEnabled
@ stub -arch=arm64 EtwProviderEnabled
@ stdcall EtwRegisterClassicProvider(ptr long ptr ptr ptr)
@ stub -arch=arm64 EtwSendTraceBuffer
@ stdcall -arch=x86_64,arm64 EtwSetInformation(int64 long ptr long)
@ stub -arch=arm64 EtwTelemetryCoverageReport
@ stub -arch=arm64 EtwWriteEndScenario
@ stub -arch=arm64 EtwWriteEx
@ stub -arch=arm64 EtwWriteStartScenario
@ stub -arch=arm64 EtwWriteString
@ stdcall -arch=x86_64,arm64 EtwWriteTransfer(int64 ptr ptr ptr long ptr)
@ stub -arch=arm64 EtwpDisableStackWalkApc
@ stub -arch=arm64 EtwpReenableStackWalkApc
@ stub -arch=arm64 ExAccessByte
@ stub -arch=arm64 ExAcquireAutoExpandPushLockExclusive
@ stub -arch=arm64 ExAcquireAutoExpandPushLockShared
@ stub -arch=arm64 ExAcquireCacheAwarePushLockExclusive
@ stub -arch=arm64 ExAcquireCacheAwarePushLockExclusiveEx
@ stub -arch=arm64 ExAcquireCacheAwarePushLockSharedEx
@ stub -arch=arm64 ExAcquireFastResourceExclusive
@ stub -arch=arm64 ExAcquireFastResourceShared
@ stub -arch=arm64 ExAcquireFastResourceSharedStarveExclusive
@ stub -arch=arm64 ExAcquireFastResourceWithFlags
@ fastcall -arch=x86_64,arm64 ExAcquirePushLockExclusiveEx(ptr long)
@ fastcall -arch=x86_64,arm64 ExAcquirePushLockSharedEx(ptr long)
@ stub -arch=arm64 ExAcquireSpinLockExclusive
@ stub -arch=arm64 ExAcquireSpinLockExclusiveAtDpcLevel
@ stub -arch=arm64 ExAcquireSpinLockShared
@ stub -arch=arm64 ExAcquireSpinLockSharedAtDpcLevel
@ stub -arch=arm64 ExAllocateAutoExpandPushLock
@ stub -arch=arm64 ExAllocateCacheAwarePushLock
@ stdcall -arch=arm64 ExAllocateFromLookasideListEx(ptr) ExiAllocateFromLookasideListEx
@ stdcall -arch=arm64 ExAllocateFromNPagedLookasideList(ptr) ExiAllocateFromNPagedLookasideList
@ stdcall -arch=x86_64,arm64 ExAllocatePool2(int64 int64 long)
@ stdcall -arch=arm64 ExAllocatePool3(int64 long long ptr long)
@ stdcall -version=0x603+ -arch=arm64 ExAllocateTimer(ptr ptr long)
@ fastcall -arch=arm64 ExBlockOnAddressPushLock(ptr ptr ptr int64 ptr)
@ stub -arch=arm64 ExCancelDpcEventWait
@ stdcall -version=0x603+ -arch=arm64 ExCancelTimer(ptr ptr)
@ stub -arch=arm64 ExCleanupAutoExpandPushLock
@ stub -arch=arm64 ExCleanupRundownProtectionCacheAware
@ stub -arch=arm64 ExConvertFastResourceExclusiveToShared
@ stub -arch=arm64 ExConvertPushLockExclusiveToShared
@ stub -arch=arm64 ExCreateDpcEvent
@ stub -arch=arm64 ExCreatePool
@ stub -arch=arm64 ExDeleteDpcEvent
@ stub -arch=arm64 ExDeleteFastResource
@ stdcall -version=0x603+ -arch=arm64 ExDeleteTimer(ptr long long ptr)
@ stub -arch=arm64 ExDestroyPool
@ stub -arch=arm64 ExDisownFastResource
@ stub -arch=arm64 ExEnterPriorityRegionAndAcquireResourceExclusive
@ stub -arch=arm64 ExEnterPriorityRegionAndAcquireResourceShared
@ stub -arch=arm64 ExEnumerateSystemFirmwareTables
@ stub -arch=arm64 ExFetchLicenseData
@ stub -arch=arm64 ExFreeAutoExpandPushLock
@ stub -arch=arm64 ExFreeCacheAwarePushLock
@ stdcall -arch=arm64 ExFreePool2(ptr long ptr long)
@ stub -arch=arm64 ExFreeToLookasideListEx
@ stub -arch=arm64 ExFreeToNPagedLookasideList
@ stdcall -arch=arm64 ExGetFirmwareEnvironmentVariable(ptr ptr ptr ptr ptr)
@ stdcall -arch=arm64 ExGetFirmwareType()
@ stub -arch=arm64 ExGetLicenseTamperState
@ stub -arch=arm64 ExGetPrmInterface
@ stdcall -arch=arm64 ExGetSystemFirmwareTable(long long ptr long ptr)
@ stub -arch=arm64 ExInitializeAutoExpandPushLock
@ stub -arch=arm64 ExInitializeFastOwnerEntry
@ stub -arch=arm64 ExInitializeFastResource
@ stub -arch=arm64 ExInitializeFastResource2
@ stub -arch=arm64 ExInitializeFastResourceAcquired
@ stub -arch=arm64 ExInitializePushLock
@ stub -arch=arm64 ExInitializeResourceLite2
@ stub -arch=arm64 ExInitializeRundownProtectionCacheAwareEx
@ stub -arch=arm64 ExIsFastResourceContended
@ stub -arch=arm64 ExIsFastResourceHeld
@ stub -arch=arm64 ExIsFastResourceHeldExclusive
@ stub -arch=arm64 ExIsManufacturingModeEnabled
@ stub -arch=arm64 ExIsSoftBoot
@ stub -arch=arm64 ExMoveFastResourceOwnershipWithFlags
@ stub -arch=arm64 ExNotifyBootDeviceRemoval
@ stub -arch=arm64 ExQueryFastCacheDevLicense
@ stdcall -arch=arm64 ExQueryTimerResolution(ptr ptr ptr)
@ stdcall -arch=arm64 ExQueryWnfStateData(ptr ptr ptr ptr)
@ stub -arch=arm64 ExQueueDpcEventWait
@ stub -arch=arm64 ExRcuFreePool
@ stdcall -arch=arm64 ExRealTimeIsUniversal()
@ stub -arch=arm64 ExRegisterBootDevice
@ stub -arch=arm64 ExRegisterExtension
@ stub -arch=arm64 ExReinitializeFastResource
@ stub -arch=arm64 ExReleaseAutoExpandPushLockExclusive
@ stub -arch=arm64 ExReleaseAutoExpandPushLockShared
@ stub -arch=arm64 ExReleaseCacheAwarePushLockExclusive
@ stub -arch=arm64 ExReleaseCacheAwarePushLockExclusiveEx
@ stub -arch=arm64 ExReleaseCacheAwarePushLockSharedEx
@ stub -arch=arm64 ExReleaseDisownedFastResource
@ stub -arch=arm64 ExReleaseDisownedFastResourceExclusive
@ stub -arch=arm64 ExReleaseDisownedFastResourceShared
@ stub -arch=arm64 ExReleaseFastResource
@ stub -arch=arm64 ExReleaseFastResourceExclusive
@ stub -arch=arm64 ExReleaseFastResourceShared
@ fastcall -arch=x86_64,arm64 ExReleasePushLockEx(ptr long)
@ fastcall -arch=x86_64,arm64 ExReleasePushLockExclusiveEx(ptr long)
@ stub -arch=arm64 ExReleasePushLockSharedEx
@ stub -arch=arm64 ExReleaseResourceAndLeavePriorityRegion
@ stub -arch=arm64 ExReleaseSpinLockExclusive
@ stub -arch=arm64 ExReleaseSpinLockExclusiveFromDpcLevel
@ stub -arch=arm64 ExReleaseSpinLockShared
@ stub -arch=arm64 ExReleaseSpinLockSharedFromDpcLevel
@ stub -arch=arm64 ExSecurePoolUpdate
@ stub -arch=arm64 ExSecurePoolValidate
@ stub -arch=arm64 ExSetFirmwareEnvironmentVariable
@ stub -arch=arm64 ExSetLicenseTamperState
@ stub -arch=arm64 ExSetResourceOwnerPointerEx
@ stdcall -version=0x603+ -arch=arm64 ExSetTimer(ptr int64 int64 ptr)
@ stub -arch=arm64 ExShareAddressSpaceWithDevice
@ stub -arch=arm64 ExShareSystemAddressSpaceWithDevice
@ stub -arch=arm64 ExSizeOfAutoExpandPushLock
@ stub -arch=arm64 ExStopSharingAddressSpaceWithDevice
@ stub -arch=arm64 ExStopSharingSystemAddressSpaceWithDevice
@ stdcall -arch=arm64 ExSubscribeWnfStateChange(ptr ptr long ptr ptr)
@ stub -arch=arm64 ExSvmBeginDeviceReset
@ stub -arch=arm64 ExSvmFinalizeDeviceReset
@ stub -arch=arm64 ExTryAcquireAutoExpandPushLockExclusive
@ stub -arch=arm64 ExTryAcquireAutoExpandPushLockShared
@ stub -arch=arm64 ExTryAcquireCacheAwarePushLockExclusiveEx
@ stub -arch=arm64 ExTryAcquireCacheAwarePushLockSharedEx
@ stub -arch=arm64 ExTryAcquirePushLockExclusiveEx
@ stub -arch=arm64 ExTryAcquirePushLockSharedEx
@ stub -arch=arm64 ExTryAcquireSpinLockExclusiveAtDpcLevel
@ stub -arch=arm64 ExTryAcquireSpinLockSharedAtDpcLevel
@ stub -arch=arm64 ExTryConvertPushLockSharedToExclusiveEx
@ stub -arch=arm64 ExTryConvertSharedSpinLockExclusive
@ stub -arch=arm64 ExTryQueueWorkItem
@ stub -arch=arm64 ExTryToConvertFastResourceSharedToExclusive
@ stub -arch=arm64 ExUnblockOnAddressPushLockEx
@ stub -arch=arm64 ExUnblockPushLockEx
@ stub -arch=arm64 ExUnregisterExtension
@ stdcall -arch=arm64 ExUnsubscribeWnfStateChange(ptr)
@ stub -arch=arm64 ExUpdateLicenseData
@ stub -arch=arm64 ExfTryAcquirePushLockShared
@ stub -arch=arm64 FirstEntrySList
@ stub -arch=arm64 FsRtlAcknowledgeEcp
@ stub -arch=arm64 FsRtlAcquireEofLock
@ stub -arch=arm64 FsRtlAcquireHeaderMutex
@ stub -arch=arm64 FsRtlAddBaseMcbEntryEx
@ stub -arch=arm64 FsRtlAddToTunnelCacheEx
@ stub -arch=arm64 FsRtlAllocateAePushLock
@ stub -arch=arm64 FsRtlAllocateExtraCreateParameter
@ stub -arch=arm64 FsRtlAllocateExtraCreateParameterFromLookasideList
@ stub -arch=arm64 FsRtlAllocateExtraCreateParameterList
@ stub -arch=arm64 FsRtlCancellableWaitForMultipleObjects
@ stub -arch=arm64 FsRtlCancellableWaitForSingleObject
@ stub -arch=arm64 FsRtlChangeBackingFileObject
@ stub -arch=arm64 FsRtlCheckFileSystemFilterCallbacksRegistered
@ stub -arch=arm64 FsRtlCheckOplockEx2
@ stub -arch=arm64 FsRtlCheckOplockForFsFilterCallback
@ stub -arch=arm64 FsRtlCheckUpperOplock
@ stub -arch=arm64 FsRtlCurrentOplock
@ stub -arch=arm64 FsRtlDedupChangeInit
@ stub -arch=arm64 FsRtlDedupChangeLogOverwriteOrFree
@ stub -arch=arm64 FsRtlDedupChangeLogWrite
@ stub -arch=arm64 FsRtlDedupChangeUninit
@ stub -arch=arm64 FsRtlDeleteExtraCreateParameterLookasideList
@ stub -arch=arm64 FsRtlDisallowLegacyFilterOnDevice
@ stub -arch=arm64 FsRtlFindExtraCreateParameter
@ stub -arch=arm64 FsRtlFindInTunnelCacheEx
@ stub -arch=arm64 FsRtlFreeAePushLock
@ stub -arch=arm64 FsRtlFreeExtraCreateParameter
@ stub -arch=arm64 FsRtlFreeExtraCreateParameterList
@ stub -arch=arm64 FsRtlGetCurrentProcessLoaderList
@ stub -arch=arm64 FsRtlGetDirectImageOriginalBase
@ stub -arch=arm64 FsRtlGetFileExtents
@ stub -arch=arm64 FsRtlGetFileNameInformation
@ stub -arch=arm64 FsRtlGetIoAtEof
@ stub -arch=arm64 FsRtlGetSupportedFeatures
@ stub -arch=arm64 FsRtlGetVirtualDiskNestingLevel
@ stub -arch=arm64 FsRtlHeatInit
@ stub -arch=arm64 FsRtlHeatLogIo
@ stub -arch=arm64 FsRtlHeatLogTierMove
@ stub -arch=arm64 FsRtlHeatUninit
@ stub -arch=arm64 FsRtlIncrementCcFastMdlReadWait
@ stub -arch=arm64 FsRtlInitExtraCreateParameterLookasideList
@ stub -arch=arm64 FsRtlInitializeBaseMcbEx
@ stub -arch=arm64 FsRtlInitializeEofLock
@ stub -arch=arm64 FsRtlInitializeExtraCreateParameter
@ stub -arch=arm64 FsRtlInitializeExtraCreateParameterList
@ stub -arch=arm64 FsRtlInsertExtraCreateParameter
@ stub -arch=arm64 FsRtlInsertPerFileContext
@ stub -arch=arm64 FsRtlInsertPerFileContextWithReserve
@ stub -arch=arm64 FsRtlIs32BitProcess
@ stub -arch=arm64 FsRtlIsDaxVolume
@ stub -arch=arm64 FsRtlIsEcpAcknowledged
@ stub -arch=arm64 FsRtlIsEcpFromUserMode
@ stub -arch=arm64 FsRtlIsExtentDangling
@ stub -arch=arm64 FsRtlIsMobileOS
@ stub -arch=arm64 FsRtlIsNameInUnUpcasedExpression
@ stub -arch=arm64 FsRtlIsNonEmptyDirectoryReparsePointAllowed
@ stub -arch=arm64 FsRtlIsSystemPagingFile
@ stub -arch=arm64 FsRtlIssueDeviceIoControl
@ stub -arch=arm64 FsRtlKernelFsControlFile
@ stub -arch=arm64 FsRtlLogCcFlushError
@ stub -arch=arm64 FsRtlLookupPerFileContext
@ stub -arch=arm64 FsRtlMdlReadEx
@ stub -arch=arm64 FsRtlMupGetProviderIdFromName
@ stub -arch=arm64 FsRtlMupGetProviderInfoFromFileObject
@ stub -arch=arm64 FsRtlNotifyCleanupAll
@ stub -arch=arm64 FsRtlNotifyFilterChangeDirectoryLite
@ stub -arch=arm64 FsRtlNotifyFilterReportChangeLite
@ stub -arch=arm64 FsRtlNotifyFilterReportChangeLiteEx
@ stub -arch=arm64 FsRtlNotifyVolumeEventEx
@ stub -arch=arm64 FsRtlOpenFileSystemRegistryKeyFromFsGuid
@ stub -arch=arm64 FsRtlOplockBreakH2
@ stub -arch=arm64 FsRtlOplockBreakToNoneEx
@ stub -arch=arm64 FsRtlOplockFsctrlEx
@ stub -arch=arm64 FsRtlOplockGetAnyBreakOwnerProcess
@ stub -arch=arm64 FsRtlOplockKeysEqual
@ stub -arch=arm64 FsRtlPrepareMdlWriteEx
@ stub -arch=arm64 FsRtlPrepareToReuseEcp
@ stub -arch=arm64 FsRtlQueryCachedVdl
@ stub -arch=arm64 FsRtlQueryInformationFile
@ stub -arch=arm64 FsRtlQueryKernelEaFile
@ stub -arch=arm64 FsRtlQueryMaximumVirtualDiskNestingLevel
@ stub -arch=arm64 FsRtlRegisterFltMgrCalls
@ stub -arch=arm64 FsRtlRegisterMupCalls
@ stub -arch=arm64 FsRtlRegisterUncProviderEx
@ stub -arch=arm64 FsRtlRegisterUncProviderEx2
@ stub -arch=arm64 FsRtlReleaseEofLock
@ stub -arch=arm64 FsRtlReleaseFileNameInformation
@ stub -arch=arm64 FsRtlReleaseHeaderMutex
@ stub -arch=arm64 FsRtlRemoveExtraCreateParameter
@ stub -arch=arm64 FsRtlRemovePerFileContext
@ stub -arch=arm64 FsRtlRemovePerFileContextWithReserve
@ stub -arch=arm64 FsRtlSendModernAppTermination
@ stub -arch=arm64 FsRtlSetDriverBacking
@ stub -arch=arm64 FsRtlSetEcpListIntoIrp
@ stub -arch=arm64 FsRtlSetKernelEaFile
@ stub -arch=arm64 FsRtlTeardownPerFileContexts
@ stub -arch=arm64 FsRtlTryToAcquireHeaderMutex
@ stub -arch=arm64 FsRtlUpperOplockFsctrl
@ stub -arch=arm64 FsRtlVolumeDeviceToCorrelationId
@ stub -arch=arm64 HalFlushIoBuffers
@ stub -arch=arm64 HviGetHardwareFeatures
@ stub -arch=arm64 HviGetHypervisorFeatures
@ stub -arch=arm64 HviIsAnyHypervisorPresent
@ stub -arch=arm64 HviIsHypervisorVendorMicrosoft
@ stub -arch=arm64 HvlGetApicIdFromLpIndex
@ stub -arch=arm64 HvlGetHypervisorVendorId
@ stub -arch=arm64 HvlGetLpIndexFromApicId
@ stub -arch=arm64 HvlGetLpIndexFromProcessorIndex
@ stub -arch=arm64 HvlInvokeFastExtendedHypercall
@ stub -arch=arm64 HvlInvokeHypercall
@ stub -arch=arm64 HvlIsSchedulerAssistAvailable
@ stub -arch=arm64 HvlMapDmaRanges
@ stub -arch=arm64 HvlQueryActiveHypervisorProcessorCount
@ stub -arch=arm64 HvlQueryActiveProcessors
@ stub -arch=arm64 HvlQueryConnection
@ stub -arch=arm64 HvlQueryHypervisorProcessorNodeNumber
@ stub -arch=arm64 HvlQueryNumaDistance
@ stub -arch=arm64 HvlQueryProcessorTopology
@ stub -arch=arm64 HvlQueryProcessorTopologyCount
@ stub -arch=arm64 HvlQueryProcessorTopologyEx
@ stub -arch=arm64 HvlQueryProcessorTopologyHighestId
@ stub -arch=arm64 HvlQueryStartedProcessors
@ stub -arch=arm64 HvlReadPerformanceStateCounters
@ stub -arch=arm64 HvlRegisterInterruptCallback
@ stub -arch=arm64 HvlRegisterWheaErrorNotification
@ stub -arch=arm64 HvlSchedulerAssistAcknowledgeEvents
@ stub -arch=arm64 HvlUnmapDmaRanges
@ stub -arch=arm64 HvlUnregisterInterruptCallback
@ stub -arch=arm64 HvlUnregisterWheaErrorNotification
@ stub -arch=arm64 HvlUpdatePerformanceStateCountersForLp
@ stub -arch=arm64 InbvNotifyDisplayOwnershipChange
@ stub -arch=arm64 InbvSetVirtualFrameBuffer
@ stub -arch=arm64 InterlockedPushListSList
@ stub -arch=arm64 IoAcquireKsrPersistentMemory
@ stub -arch=arm64 IoAcquireKsrPersistentMemoryEx
@ stub -arch=arm64 IoAddBugcheckTriageThread
@ stub -arch=arm64 IoAdjustStackSizeForRedirection
@ stub -arch=arm64 IoAllocateIrpEx
@ stub -arch=arm64 IoAllocateMiniCompletionPacket
@ stub -arch=arm64 IoAllocateSfioStreamIdentifier
@ stub -arch=arm64 IoApplyPriorityInfoThread
@ stub -arch=arm64 IoBoostThreadIo
@ stub -arch=arm64 IoCancelMiniCompletionPacket
@ stub -arch=arm64 IoCheckFileObjectOpenedAsCopyDestination
@ stub -arch=arm64 IoCheckFileObjectOpenedAsCopySource
@ stub -arch=arm64 IoCheckLinkShareAccess
@ stub -arch=arm64 IoCheckRedirectionTrustLevel
@ stub -arch=arm64 IoCheckShareAccessEx
@ stub -arch=arm64 IoCleanupIrp
@ stub -arch=arm64 IoClearActivityIdThread
@ stub -arch=arm64 IoClearAdapterCryptoEngineExtension
@ stub -arch=arm64 IoClearFsTrackOffsetState
@ stub -arch=arm64 IoClearIrpExtraCreateParameter
@ stub -arch=arm64 IoComputeRedirectionTrustLevel
@ stub -arch=arm64 IoConvertFileHandleToKernelHandle
@ stub -arch=arm64 IoCopyDeviceObjectHint
@ stub -arch=arm64 IoCreateArcName
@ stub -arch=arm64 IoCreateDeviceSecure
@ stub -arch=arm64 IoCreateDriverProxyExtension
@ stub -arch=arm64 IoCreateFileEx
@ stub -arch=arm64 IoCreateStreamFileObjectEx2
@ stub -arch=arm64 IoCreateSymbolicLink2
@ stub -arch=arm64 IoCreateSystemThread
@ stub -arch=arm64 IoDecrementKeepAliveCount
@ stub -arch=arm64 IoDriverProxyCreateHotSwappableWorkerThread
@ stub -arch=arm64 IoDuplicateDependency
@ stub -arch=arm64 IoEnumerateKsrPersistentMemoryEx
@ stub -arch=arm64 IoFreeKsrPersistentMemory
@ stub -arch=arm64 IoFreeMiniCompletionPacket
@ stub -arch=arm64 IoFreeSfioStreamIdentifier
@ stub -arch=arm64 IoGetActivityIdIrp
@ stub -arch=arm64 IoGetActivityIdThread
@ stub -arch=arm64 IoGetAdapterCryptoEngineExtension
@ stub -arch=arm64 IoGetAffinityInterrupt
@ stub -arch=arm64 IoGetBootDiskInformationLite
@ stub -arch=arm64 IoGetContainerInformation
@ stub -arch=arm64 IoGetCopyInformationExtension
@ stub -arch=arm64 IoGetDeviceDirectory
@ stub -arch=arm64 IoGetDeviceInterfacePropertyData
@ stdcall -arch=x86_64,arm64 IoGetDeviceNumaNode(ptr ptr)
@ stub -arch=arm64 IoGetDriverDirectory
@ stub -arch=arm64 IoGetDriverProxyEndpointWrapper
@ stub -arch=arm64 IoGetDriverProxyFeatures
@ stub -arch=arm64 IoGetFsTrackOffsetState
@ stub -arch=arm64 IoGetFsZeroingOffset
@ stub -arch=arm64 IoGetGenericIrpExtension
@ stub -arch=arm64 IoGetInitiatorProcess
@ stub -arch=arm64 IoGetIoAttributionHandle
@ stub -arch=arm64 IoGetIommuInterface
@ stub -arch=arm64 IoGetIommuInterfaceEx
@ stub -arch=arm64 IoGetKsrPersistentMemoryBuffer
@ stub -arch=arm64 IoGetOplockKeyContext
@ stub -arch=arm64 IoGetOplockKeyContextEx
@ stub -arch=arm64 IoGetSfioStreamIdentifier
@ stub -arch=arm64 IoGetShadowFileInformation
@ stub -arch=arm64 IoGetSilo
@ stub -arch=arm64 IoGetSiloParameters
@ stub -arch=arm64 IoGetSymlinkSupportInformation
@ stub -arch=arm64 IoGetTransactionParameterBlock
@ stub -arch=arm64 IoIncrementKeepAliveCount
@ stub -arch=arm64 IoInitializeIrpEx
@ stub -arch=arm64 IoInitializeMiniCompletionPacket
@ stub -arch=arm64 IoInitializeWorkItem
@ stub -arch=arm64 IoIrpHasFsTrackOffsetExtensionType
@ stub -arch=arm64 IoIsActivityTracingEnabled
@ stub -arch=arm64 IoIsFileObjectIgnoringSharing
@ stub -arch=arm64 IoIsInitiator32bitProcess
@ stub -arch=arm64 IoIsValidIrpStatus
@ stub -arch=arm64 IoMakeAssociatedIrpEx
@ stub -arch=arm64 IoMapKsrPersistentMemoryEx
@ stub -arch=arm64 IoOpenDriverRegistryKey
@ stub -arch=arm64 IoPropagateActivityIdToThread
@ stub -arch=arm64 IoPropagateIrpExtension
@ stub -arch=arm64 IoPropagateIrpExtensionEx
@ stub -arch=arm64 IoQueryFullDriverPath
@ stub -arch=arm64 IoQueryInformationByName
@ stub -arch=arm64 IoQueryInterface
@ stub -arch=arm64 IoQueryKsrPersistentMemorySize
@ stub -arch=arm64 IoQueryKsrPersistentMemorySizeEx
@ stub -arch=arm64 IoQueueWorkItemToNode
@ stub -arch=arm64 IoRecordIoAttribution
@ stub -arch=arm64 IoRegisterBootDriverCallback
@ stub -arch=arm64 IoRegisterContainerNotification
@ stub -arch=arm64 IoRegisterDriverProxyEndpoints
@ stub -arch=arm64 IoRegisterFsRegistrationChangeMountAware
@ stub -arch=arm64 IoRegisterIoTracking
@ stub -arch=arm64 IoRegisterPriorityCallback
@ stub -arch=arm64 IoRemoveIoCompletion
@ stub -arch=arm64 IoRemoveLinkShareAccess
@ stub -arch=arm64 IoRemoveLinkShareAccessEx
@ stub -arch=arm64 IoReplaceFileObjectName
@ stub -arch=arm64 IoReplacePartitionUnit
@ stub -arch=arm64 IoReportInterruptActive
@ stub -arch=arm64 IoReportInterruptInactive
@ stub -arch=arm64 IoReportRootDevice
@ stub -arch=arm64 IoRequestDeviceEjectEx
@ stub -arch=arm64 IoRequestDeviceRemovalForReset
@ stub -arch=arm64 IoReserveDependency
@ stub -arch=arm64 IoReserveKsrPersistentMemory
@ stub -arch=arm64 IoReserveKsrPersistentMemoryEx
@ stub -arch=arm64 IoResolveDependency
@ stub -arch=arm64 IoSetActivityIdIrp
@ stub -arch=arm64 IoSetActivityIdThread
@ stub -arch=arm64 IoSetAdapterCryptoEngineExtension
@ stub -arch=arm64 IoSetDependency
@ stub -arch=arm64 IoSetFileObjectIgnoreSharing
@ stub -arch=arm64 IoSetFsTrackOffsetState
@ stub -arch=arm64 IoSetFsZeroingOffset
@ stub -arch=arm64 IoSetFsZeroingOffsetRequired
@ stub -arch=arm64 IoSetGenericIrpExtension
@ stub -arch=arm64 IoSetIoAttributionIrp
@ stub -arch=arm64 IoSetIoCompletionEx
@ stub -arch=arm64 IoSetIoCompletionEx3
@ stub -arch=arm64 IoSetIoPriorityHint
@ stub -arch=arm64 IoSetIoPriorityHintIntoFileObject
@ stub -arch=arm64 IoSetIoPriorityHintIntoThread
@ stub -arch=arm64 IoSetIrpExtraCreateParameter
@ stub -arch=arm64 IoSetLinkShareAccess
@ stub -arch=arm64 IoSetShadowFileInformation
@ stub -arch=arm64 IoSetShareAccessEx
@ stub -arch=arm64 IoSizeOfIrpEx
@ stub -arch=arm64 IoSizeofGenericIrpExtension
@ stub -arch=arm64 IoSizeofWorkItem
@ stub -arch=arm64 IoSteerInterrupt
@ stub -arch=arm64 IoSynchronousCallDriver
@ stub -arch=arm64 IoTestDependency
@ stub -arch=arm64 IoTransferActivityId
@ stub -arch=arm64 IoTryQueueWorkItem
@ stub -arch=arm64 IoUninitializeWorkItem
@ stub -arch=arm64 IoUnregisterBootDriverCallback
@ stub -arch=arm64 IoUnregisterContainerNotification
@ stub -arch=arm64 IoUnregisterIoTracking
@ stdcall -arch=x86_64,arm64 IoUnregisterPlugPlayNotificationEx(ptr)
@ stub -arch=arm64 IoUnregisterPriorityCallback
@ stub -arch=arm64 IoUpdateLinkShareAccess
@ stub -arch=arm64 IoUpdateLinkShareAccessEx
@ stub -arch=arm64 IoVolumeDeviceNameToGuid
@ stub -arch=arm64 IoVolumeDeviceNameToGuidPath
@ stub -arch=arm64 IoWithinStackLimits
@ stub -arch=arm64 IoWriteKsrPersistentMemory
@ stub -arch=arm64 IofGetDriverProxyWrapperFromEndpoint
@ stub -arch=arm64 KdAcquireDebuggerLock
@ stub -arch=arm64 KdDeregisterPowerHandler
@ stub -arch=arm64 KdGetDebugDevice
@ stub -arch=arm64 KdPowerTransitionEx
@ stub -arch=arm64 KdRegisterPowerHandler
@ stub -arch=arm64 KdReleaseDebuggerLock
@ stub -arch=arm64 KdSetEventLoggingPresent
@ stdcall -arch=arm64 KeAddGroupAffinityEx(ptr long int64)
@ stdcall -arch=arm64 KeAddProcessorAffinityEx(ptr long)
@ stdcall -arch=x86_64,arm64 KeAddProcessorGroupAffinity(ptr long)
@ stub -arch=arm64 KeAddTriageDumpDataBlock
@ stub -arch=arm64 KeAllocateCalloutStack
@ stub -arch=arm64 KeAllocateCalloutStackEx
@ stub -arch=arm64 KeAllocateProcessorProfileStructures
@ stdcall -arch=arm64 KeAndAffinityEx(ptr ptr ptr)
@ stdcall -arch=x86_64,arm64 KeAndAffinityEx2(ptr ptr ptr)
@ stdcall -arch=x86_64,arm64 KeAndGroupAffinityEx(ptr ptr ptr)
@ stub -arch=arm64 KeCancelTimer2
@ stdcall -arch=arm64 KeCheckProcessorAffinityEx(ptr long)
@ stdcall -arch=x86_64,arm64 KeCheckProcessorGroupAffinity(ptr long)
@ stub -arch=arm64 KeClockInterruptNotify
@ stdcall -arch=x86_64,arm64 KeComplementAffinityEx(ptr ptr)
@ stdcall -arch=x86_64,arm64 KeComplementAffinityEx2(ptr ptr)
@ stub -arch=arm64 KeConvertAuxiliaryCounterToPerformanceCounter
@ stub -arch=arm64 KeConvertPerformanceCounterToAuxiliaryCounter
@ stdcall -arch=arm64 KeCopyAffinityEx(ptr ptr)
@ stdcall -arch=x86_64,arm64 KeCopyAffinityEx2(ptr ptr)
@ stdcall -arch=arm64 KeCountSetBitsAffinityEx(ptr)
@ stdcall -arch=x86_64,arm64 KeCountSetBitsGroupAffinity(ptr)
@ stdcall -arch=x86_64,arm64 KeDeregisterProcessorChangeCallback(ptr)
@ stub -arch=arm64 KeDispatchSecondaryInterrupt
@ stdcall -arch=x86_64,arm64 KeEnumerateNextProcessor(ptr ptr)
@ stdcall -arch=x86_64,arm64 KeFindFirstSetLeftAffinityEx(ptr)
@ stdcall -arch=x86_64,arm64 KeFindFirstSetLeftGroupAffinity(ptr)
@ stdcall -arch=x86_64,arm64 KeFindFirstSetRightAffinityEx(ptr)
@ stdcall -arch=x86_64,arm64 KeFindFirstSetRightGroupAffinity(ptr)
@ stdcall -arch=x86_64,arm64 KeFirstGroupAffinityEx(ptr ptr)
@ stub -arch=arm64 KeFreeCalloutStack
@ stub -arch=arm64 KeGetClockOwner
@ stub -arch=arm64 KeGetClockTimerResolution
@ stdcall -arch=x86_64,arm64 KeGetEffectiveIrql()
@ stub -arch=arm64 KeGetNextClockTickDuration
@ stdcall -arch=arm64 KeGetProcessorIndexFromNumber(ptr)
@ stdcall -arch=arm64 KeGetProcessorNumberFromIndex(long ptr)
@ stub -arch=arm64 KeHwPolicyLocateResource
@ stdcall -arch=arm64 KeInitializeAffinityEx(ptr)
@ stdcall -arch=x86_64,arm64 KeInitializeAffinityEx2(ptr long)
@ stdcall -arch=x86_64,arm64 KeInitializeEnumerationContext(ptr ptr)
@ stdcall -arch=x86_64,arm64 KeInitializeEnumerationContextFromAffinity(ptr long int64)
@ stdcall -arch=x86_64,arm64 KeInitializeEnumerationContextFromGroup(ptr ptr)
@ stub -arch=arm64 KeInitializeSecondaryInterruptServices
@ stub -arch=arm64 KeInitializeTimer2
@ stub -arch=arm64 KeInitializeTriageDumpDataArray
@ stdcall -arch=x86_64,arm64 KeInterlockedClearProcessorAffinityEx(ptr long)
@ stdcall -arch=x86_64,arm64 KeInterlockedSetProcessorAffinityEx(ptr long)
@ stub -arch=arm64 KeInvalidateRangeAllCaches
@ stub -arch=arm64 KeInvalidateRangeAllCachesNoIpi
@ stdcall -arch=arm64 KeIsEmptyAffinityEx(ptr)
@ stdcall -arch=arm64 KeIsEqualAffinityEx(ptr ptr)
@ stdcall -arch=arm64 KeIsSingleGroupAffinityEx(ptr ptr)
@ stdcall -arch=arm64 KeIsSubsetAffinityEx(ptr ptr)
@ stub -arch=arm64 KeNotifyProcessorFreezeSupported
@ stdcall -arch=arm64 KeOrAffinityEx(ptr ptr ptr)
@ stdcall -arch=x86_64,arm64 KeOrAffinityEx2(ptr ptr ptr)
@ stdcall -arch=x86_64,arm64 KeProcessorGroupAffinity(ptr long)
@ stdcall KeQueryActiveGroupCount()
@ stdcall -arch=arm64 KeQueryActiveProcessorAffinity(ptr)
@ stdcall -arch=arm64 KeQueryActiveProcessorAffinity2(ptr ptr)
@ stdcall -arch=arm64 KeQueryAuxiliaryCounterFrequency(ptr)
@ stdcall -arch=x86_64,arm64 KeQueryDpcWatchdogInformation(ptr)
@ stdcall -arch=arm64 KeQueryEffectivePriorityThread(ptr)
@ stdcall KeQueryGroupAffinity(long)
@ stdcall KeQueryGroupAffinityEx(ptr long)
@ stdcall -arch=arm64 KeQueryHardwareCounterConfiguration(ptr long ptr)
@ stdcall -arch=arm64 KeQueryHeteroCpuPolicyThread(ptr long)
@ stub -arch=arm64 KeQueryInterruptPartitionCount
@ stub -arch=arm64 KeQueryInterruptPartitionInformation
@ stdcall KeQueryLogicalProcessorRelationship(ptr long ptr ptr)
@ stdcall -arch=arm64 KeQueryMaximumGroupCount()
@ stdcall KeQueryNodeActiveAffinity(long ptr ptr)
@ stdcall KeQueryNodeActiveAffinity2(long ptr ptr)
@ stdcall -arch=arm64 KeQueryNodeActiveProcessorCount(long)
@ stdcall -arch=arm64 KeQueryNodeMaximumProcessorCount(long)
@ stdcall -arch=arm64 KeQueryPrcbAddress(long)
@ stub -arch=arm64 KeQuerySystemCpuPartitionAffinity
@ stdcall -arch=arm64 KeQueryTotalCycleTimeProcess(ptr ptr)
@ stdcall -arch=arm64 KeQueryTotalCycleTimeThread(ptr ptr)
@ stdcall -arch=arm64 KeQueryTypeEvent(ptr)
@ stdcall -arch=arm64 KeQueryUnbiasedInterruptTime()
@ stdcall -arch=arm64 KeQueryUnbiasedInterruptTimePrecise(ptr)
@ stub -arch=arm64 KeRcuReadLock
@ stub -arch=arm64 KeRcuReadUnlock
@ stub -arch=arm64 KeRcuSynchronize
@ stdcall -arch=x86_64,arm64 KeRegisterProcessorChangeCallback(ptr ptr long)
@ stdcall -arch=arm64 KeReinitializeAffinityEx(ptr)
@ stdcall -arch=arm64 KeRemoveGroupAffinityEx(ptr long int64)
@ stdcall -arch=arm64 KeRemoveProcessorAffinityEx(ptr long)
@ stdcall -arch=x86_64,arm64 KeRemoveProcessorGroupAffinity(ptr long)
@ stdcall -arch=arm64 KeRemoveQueueDpcEx(ptr long)
@ stdcall -arch=arm64 KeRemoveQueueEx(ptr long long ptr ptr long)
@ stub -arch=arm64 KeReportCacheIncoherentDevice
@ stdcall -arch=arm64 KeRestoreExtendedProcessorState(ptr)
@ stub -arch=arm64 KeRestoreProcessorState
@ stdcall -arch=arm64 KeRevertToUserGroupAffinityThread(ptr)
@ stdcall -arch=arm64 KeSaveExtendedProcessorState(int64 ptr)
@ stdcall -arch=arm64 KeSetActualBasePriorityThread(ptr long)
@ stdcall -arch=arm64 KeSetHardwareCounterConfiguration(ptr long)
@ stdcall -arch=arm64 KeSetHeteroCpuPolicyThread(ptr long long)
@ stdcall -arch=arm64 KeSetSelectedCpuSetsThread(ptr long ptr)
@ stdcall -arch=x86_64,arm64 KeSetSystemGroupAffinityThread(ptr ptr)
@ stdcall -arch=x86_64,arm64 KeSetTargetProcessorDpcEx(ptr ptr)
@ stdcall -version=0x603+ -arch=arm64 KeSetTimer2(ptr int64 int64 ptr)
@ stdcall -arch=x86_64,arm64 KeShouldYieldProcessor()
@ stdcall -arch=arm64 KeSizeOfAffinityEx(long)
@ stub -arch=arm64 KeSrcuAllocate
@ stub -arch=arm64 KeSrcuFree
@ stub -arch=arm64 KeSrcuReadLock
@ stub -arch=arm64 KeSrcuReadUnlock
@ stub -arch=arm64 KeSrcuSynchronize
@ stub -arch=arm64 KeStallWhileFrozen
@ stub -arch=arm64 KeStartDynamicProcessor
@ stdcall -arch=x86_64,arm64 KeSubtractAffinityEx(ptr ptr ptr)
@ stdcall -arch=x86_64,arm64 KeSubtractAffinityEx2(ptr ptr ptr)
@ stub -arch=arm64 KeSweepIcacheRange
@ stub -arch=arm64 KeSweepLocalCaches
@ stub -arch=arm64 KeSynchronizeTimeToQpc
@ stub -arch=arm64 KeSystemFullyCacheCoherent
@ stub -arch=arm64 KeUpdateThreadTag
@ stub -arch=arm64 KiConnectHalInterrupt
@ stub -arch=arm64 KiReplayInterrupt
@ stub -arch=arm64 KitLogFeatureUsage
@ stub -arch=arm64 KseQueryDeviceData
@ stub -arch=arm64 KseQueryDeviceDataList
@ stub -arch=arm64 KseQueryDeviceFlags
@ stub -arch=arm64 KseRegisterShim
@ stub -arch=arm64 KseRegisterShimEx
@ stub -arch=arm64 KseSetDeviceFlags
@ stub -arch=arm64 KseUnregisterShim
@ stub -arch=arm64 LdrFindResourceEx_U
@ stub -arch=arm64 LdrResFindResource
@ stub -arch=arm64 LdrResFindResourceDirectory
@ stub -arch=arm64 LdrResSearchResource
@ stub -arch=arm64 MmAddVerifierSpecialThunks
@ stub -arch=arm64 MmAllocateContiguousMemoryEx
@ stdcall -arch=arm64 MmAllocateContiguousMemorySpecifyCacheNode(long long long long long long long long long)
@ stdcall -arch=arm64 MmAllocateContiguousNodeMemory(long long long long long long long long long)
@ stub -arch=arm64 MmAllocateMappingAddressEx
@ stub -arch=arm64 MmAllocateMdlForIoSpace
@ stub -arch=arm64 MmAllocateMemoryRanges
@ stdcall -arch=arm64 MmAllocateNodePagesForMdlEx(long long long long long long long long long long)
@ stub -arch=arm64 MmAllocatePartitionNodePagesForMdlEx
@ stub -arch=arm64 MmAreMdlPagesCached
@ stub -arch=arm64 MmChangeImageProtection
@ stub -arch=arm64 MmConfigureGraphicsPtes
@ stdcall -arch=arm64 MmCopyMemory(ptr long long long long ptr)
@ stub -arch=arm64 MmForceSectionClosedEx
@ stub -arch=arm64 MmFreeMemoryRanges
@ stub -arch=arm64 MmFreePagesFromMdlEx
@ stub -arch=arm64 MmGetCacheAttribute
@ stub -arch=arm64 MmGetCacheAttributeEx
@ stub -arch=arm64 MmGetMaximumFileSectionSize
@ stub -arch=arm64 MmGetPageBadStatus
@ stub -arch=arm64 MmGetPhysicalMemoryRangesEx
@ stub -arch=arm64 MmGetPhysicalMemoryRangesEx2
@ stub -arch=arm64 MmGetSectionInformation
@ stub -arch=arm64 MmIsDriverSuspectForVerifier
@ stdcall -arch=x86_64,arm64 MmIsDriverVerifyingByAddress(ptr)
@ stub -arch=arm64 MmIsFileSectionActive
@ stub -arch=arm64 MmLockPreChargedPagedPool
@ stdcall -arch=x86_64,arm64 MmMapIoSpaceEx(long long long long)
@ stub -arch=arm64 MmMapMdl
@ stub -arch=arm64 MmMapMemoryDumpMdlEx
@ stub -arch=arm64 MmMapViewInSessionSpaceEx
@ stub -arch=arm64 MmMdlPageContentsState
@ stub -arch=arm64 MmMdlPagesAreZero
@ stub -arch=arm64 MmObtainChargesToLockPagedPool
@ stub -arch=arm64 MmPrefetchVirtualAddresses
@ stub -arch=arm64 MmProtectDriverSection
@ stub -arch=arm64 MmQueryMemoryRanges
@ stub -arch=arm64 MmReturnChargesToLockPagedPool
@ stub -arch=arm64 MmRotatePhysicalView
@ stub -arch=arm64 MmSecureVirtualMemoryEx
@ stub -arch=arm64 MmSetGraphicsPtes
@ stub -arch=arm64 MmSetPermanentCacheAttribute
@ stub -arch=arm64 MmUnlockPreChargedPagedPool
@ stdcall -arch=arm64 NtAlertThreadByThreadId(ptr)
@ stub -arch=arm64 NtCommitComplete
@ stub -arch=arm64 NtCommitEnlistment
@ stub -arch=arm64 NtCommitTransaction
@ stub -arch=arm64 NtCompareSigningLevels
@ stub -arch=arm64 NtCopyFileChunk
@ stub -arch=arm64 NtCreateCrossVmEvent
@ stub -arch=arm64 NtCreateEnlistment
@ stub -arch=arm64 NtCreateResourceManager
@ stub -arch=arm64 NtCreateTransaction
@ stub -arch=arm64 NtCreateTransactionManager
@ stub -arch=arm64 NtEnumerateTransactionObject
@ stub -arch=arm64 NtFreezeTransactions
@ stub -arch=arm64 NtGetEnvironmentVariableEx
@ stub -arch=arm64 NtGetNotificationResourceManager
@ stub -arch=arm64 NtImageInfo
@ stdcall -arch=arm64 NtNotifyChangeDirectoryFileEx(ptr ptr ptr ptr ptr ptr long long long long)
@ stub -arch=arm64 NtOpenEnlistment
@ stub -arch=arm64 NtOpenResourceManager
@ stub -arch=arm64 NtOpenTransaction
@ stub -arch=arm64 NtOpenTransactionManager
@ stub -arch=arm64 NtPrePrepareComplete
@ stub -arch=arm64 NtPrePrepareEnlistment
@ stub -arch=arm64 NtPrepareComplete
@ stub -arch=arm64 NtPrepareEnlistment
@ stub -arch=arm64 NtPropagationComplete
@ stub -arch=arm64 NtPropagationFailed
@ stdcall -arch=arm64 NtQueryDirectoryFileEx(ptr ptr ptr ptr ptr ptr long long long ptr)
@ stub -arch=arm64 NtQueryEnvironmentVariableInfoEx
@ stdcall -arch=arm64 NtQueryInformationByName(ptr ptr ptr long long)
@ stub -arch=arm64 NtQueryInformationEnlistment
@ stub -arch=arm64 NtQueryInformationResourceManager
@ stub -arch=arm64 NtQueryInformationTransaction
@ stub -arch=arm64 NtQueryInformationTransactionManager
@ stub -arch=arm64 NtQuerySecurityAttributesToken
@ stdcall -version=0x601+ -arch=arm64 NtQuerySystemInformationEx(long ptr long ptr long ptr)
@ stub -arch=arm64 NtReadOnlyEnlistment
@ stub -arch=arm64 NtRecoverEnlistment
@ stub -arch=arm64 NtRecoverResourceManager
@ stub -arch=arm64 NtRecoverTransactionManager
@ stub -arch=arm64 NtRollbackComplete
@ stub -arch=arm64 NtRollbackEnlistment
@ stub -arch=arm64 NtRollbackTransaction
@ stub -arch=arm64 NtSetCachedSigningLevel
@ stub -arch=arm64 NtSetInformationEnlistment
@ stub -arch=arm64 NtSetInformationResourceManager
@ stub -arch=arm64 NtSetInformationTransaction
@ stdcall -arch=arm64 NtSetInformationVirtualMemory(ptr long ptr ptr ptr long)
@ stub -arch=arm64 NtThawTransactions
@ stub -arch=arm64 NtTraceControl
@ stdcall -arch=arm64 NtWaitForAlertByThreadId(ptr ptr)
@ stub -arch=arm64 ObDereferenceObjectDeferDeleteWithTag
@ stub -arch=arm64 ObGetFilterVersion
@ stub -arch=arm64 ObIsDosDeviceLocallyMapped
@ stub -arch=arm64 ObOpenObjectByNameEx
@ stub -arch=arm64 ObOpenObjectByPointerWithTag
@ stub -arch=arm64 ObReferenceObjectByHandleWithTag
@ stub -arch=arm64 ObReferenceObjectByPointerWithTag
@ stub -arch=arm64 ObReferenceObjectSafeWithTag
@ stub -arch=arm64 ObRegisterCallbacks
@ stub -arch=arm64 ObUnRegisterCallbacks
@ stub -arch=arm64 ObWaitForMultipleObjects
@ stub -arch=arm64 ObWaitForSingleObject
@ stub -arch=arm64 ObfDereferenceObjectWithTag
@ stub -arch=arm64 ObfReferenceObjectWithTag
@ stdcall -arch=x86_64,arm64 PcwAddInstance(ptr ptr long long ptr)
@ stub -arch=arm64 PcwCloseInstance
@ stub -arch=arm64 PcwCreateInstance
@ stdcall -arch=x86_64,arm64 PcwRegister(ptr ptr)
@ stdcall -arch=x86_64,arm64 PcwUnregister(ptr)
@ stub -arch=arm64 PfFileInfoNotify
@ stub -arch=arm64 PoClearPowerRequest
@ stub -arch=arm64 PoCpuIdledSinceLastCallImprecise
@ stub -arch=arm64 PoCreatePowerLimitRequest
@ stub -arch=arm64 PoCreatePowerRequest
@ stub -arch=arm64 PoDeletePowerLimitRequest
@ stub -arch=arm64 PoDeletePowerRequest
@ stub -arch=arm64 PoDirectedDripsClearDeviceFlags
@ stub -arch=arm64 PoDirectedDripsSetDeviceFlags
@ stub -arch=arm64 PoDisableSleepStates
@ stub -arch=arm64 PoEndDeviceBusy
@ stub -arch=arm64 PoEnergyEstimationEnabled
@ stub -arch=arm64 PoFxAddComponentRelation
@ stub -arch=arm64 PoFxAddDeviceRelation
@ stub -arch=arm64 PoFxCompleteDirectedPowerDown
@ stub -arch=arm64 PoFxEnableDStateReporting
@ stub -arch=arm64 PoFxIssueComponentPerfStateChange
@ stub -arch=arm64 PoFxIssueComponentPerfStateChangeMultiple
@ stub -arch=arm64 PoFxNotifySurprisePowerOn
@ stub -arch=arm64 PoFxPowerControl
@ stub -arch=arm64 PoFxPowerOnCrashdumpDevice
@ stub -arch=arm64 PoFxProcessorNotification
@ stub -arch=arm64 PoFxQueryCurrentComponentPerfState
@ stub -arch=arm64 PoFxRegisterComponentPerfStates
@ stub -arch=arm64 PoFxRegisterCoreDevice
@ stub -arch=arm64 PoFxRegisterCrashdumpDevice
@ stub -arch=arm64 PoFxRegisterDripsWatchdogCallback
@ stub -arch=arm64 PoFxRegisterPlugin
@ stub -arch=arm64 PoFxRegisterPluginEx
@ stub -arch=arm64 PoFxRegisterPrimaryDevice
@ stub -arch=arm64 PoFxRemoveComponentRelation
@ stub -arch=arm64 PoFxRemoveDeviceRelation
@ stub -arch=arm64 PoFxSetComponentLatency
@ stub -arch=arm64 PoFxSetComponentResidency
@ stub -arch=arm64 PoFxSetComponentWake
@ stub -arch=arm64 PoFxSetTargetDripsDevicePowerState
@ stub -arch=arm64 PoGetProcessorIdleAccounting
@ stub -arch=arm64 PoInitiateProcessorWake
@ stub -arch=arm64 PoLatencySensitivityHint
@ stub -arch=arm64 PoNotifyMediaBuffering
@ stub -arch=arm64 PoNotifyVSyncChange
@ stub -arch=arm64 PoQueryPowerLimitAttributes
@ stub -arch=arm64 PoQueryPowerLimitValue
@ stub -arch=arm64 PoReenableSleepStates
@ stub -arch=arm64 PoRegisterCoalescingCallback
@ stub -arch=arm64 PoRegisterForEffectivePowerModeNotifications
@ stub -arch=arm64 PoSetDeviceBusyEx
@ stub -arch=arm64 PoSetFixedWakeSource
@ stub -arch=arm64 PoSetPowerButtonHoldState
@ stub -arch=arm64 PoSetPowerLimitValue
@ stub -arch=arm64 PoSetPowerRequest
@ stub -arch=arm64 PoSetSystemWakeDevice
@ stub -arch=arm64 PoSetUserPresent
@ stub -arch=arm64 PoStartDeviceBusy
@ stub -arch=arm64 PoUnregisterCoalescingCallback
@ stub -arch=arm64 PoUnregisterFromEffectivePowerModeNotifications
@ stub -arch=arm64 PoUserShutdownCancelled
@ stub -arch=arm64 PoUserShutdownInitiated
@ stub -arch=arm64 PsAcquireProcessExitSynchronization
@ stub -arch=arm64 PsAcquireSiloHardReference
@ stub -arch=arm64 PsAdjustWin32kPriorityFloor
@ stub -arch=arm64 PsAllocSiloContextSlot
@ stub -arch=arm64 PsAllocateAffinityToken
@ stub -arch=arm64 PsAssignProcessToJobObject
@ stub -arch=arm64 PsAttachSiloToCurrentThread
@ stub -arch=arm64 PsChargeProcessWakeCounter
@ stub -arch=arm64 PsCheckProcessFileSigningLevel
@ stub -arch=arm64 PsCreateSiloContext
@ stub -arch=arm64 PsCreateSystemThreadEx
@ stub -arch=arm64 PsDereferenceKernelStack
@ stub -arch=arm64 PsDereferenceSiloContext
@ stub -arch=arm64 PsDetachSiloFromCurrentThread
@ stub -arch=arm64 PsEnterPriorityRegion
@ stub -arch=arm64 PsFreeAffinityToken
@ stub -arch=arm64 PsFreeSiloContextSlot
@ stub -arch=arm64 PsGetCurrentServerSilo
@ stub -arch=arm64 PsGetCurrentServerSiloName
@ stub -arch=arm64 PsGetCurrentSilo
@ stub -arch=arm64 PsGetEffectiveContainerId
@ stub -arch=arm64 PsGetEffectiveServerSilo
@ stub -arch=arm64 PsGetHostSilo
@ stub -arch=arm64 PsGetJobProperty
@ stub -arch=arm64 PsGetJobServerSilo
@ stub -arch=arm64 PsGetJobSilo
@ stub -arch=arm64 PsGetParentSilo
@ stub -arch=arm64 PsGetPermanentSiloContext
@ stub -arch=arm64 PsGetProcessActiveThreadCount
@ stub -arch=arm64 PsGetProcessCommonJob
@ stub -arch=arm64 PsGetProcessDxgProcess
@ stub -arch=arm64 PsGetProcessMachine
@ stub -arch=arm64 PsGetProcessProtection
@ stub -arch=arm64 PsGetProcessSequenceNumber
@ stub -arch=arm64 PsGetProcessServerSilo
@ stub -arch=arm64 PsGetProcessSignatureLevel
@ stub -arch=arm64 PsGetProcessSilo
@ stub -arch=arm64 PsGetProcessStartKey
@ stub -arch=arm64 PsGetServerSiloServiceSessionId
@ stub -arch=arm64 PsGetSiloContainerId
@ stub -arch=arm64 PsGetSiloContext
@ stub -arch=arm64 PsGetSiloIdentifier
@ stub -arch=arm64 PsGetSiloMonitorContextSlot
@ stub -arch=arm64 PsGetThreadCreateTime
@ stub -arch=arm64 PsGetThreadExitStatus
@ stub -arch=arm64 PsGetThreadProperty
@ stub -arch=arm64 PsGetThreadServerSilo
@ stub -arch=arm64 PsGetWin32KFilterSet
@ stub -arch=arm64 PsInsertPermanentSiloContext
@ stub -arch=arm64 PsInsertSiloContext
@ stub -arch=arm64 PsIsComponentEnabled
@ stub -arch=arm64 PsIsCurrentThreadInServerSilo
@ stub -arch=arm64 PsIsCurrentThreadPrefetching
@ stub -arch=arm64 PsIsHostSilo
@ stub -arch=arm64 PsIsProcessCommitRelinquished
@ stub -arch=arm64 PsIsProcessInAppSilo
@ stub -arch=arm64 PsIsProtectedProcess
@ stub -arch=arm64 PsIsProtectedProcessLight
@ stub -arch=arm64 PsIsWin32KFilterAuditEnabled
@ stub -arch=arm64 PsIsWin32KFilterAuditEnabledForProcess
@ stub -arch=arm64 PsIsWin32KFilterEnabled
@ stub -arch=arm64 PsIsWin32KFilterEnabledForProcess
@ stub -arch=arm64 PsLeavePriorityRegion
@ stub -arch=arm64 PsMakeSiloContextPermanent
@ stub -arch=arm64 PsQueryCurrentApiSetSchema
@ stub -arch=arm64 PsQueryProcessAttributesByToken
@ stub -arch=arm64 PsQueryProcessAvailableCpus
@ stub -arch=arm64 PsQueryProcessAvailableCpusCount
@ stub -arch=arm64 PsQueryProcessCommandLine
@ stub -arch=arm64 PsQueryProcessExceptionFlags
@ stub -arch=arm64 PsQuerySyscallProviderInformation
@ stub -arch=arm64 PsQuerySystemAvailableCpus
@ stub -arch=arm64 PsQuerySystemAvailableCpusCount
@ stub -arch=arm64 PsQueryTotalCycleTimeProcess
@ stub -arch=arm64 PsReferenceKernelStack
@ stub -arch=arm64 PsReferenceSiloContext
@ stub -arch=arm64 PsRegisterAltSystemCallHandler
@ stub -arch=arm64 PsRegisterPicoProvider
@ stub -arch=arm64 PsRegisterProcessAvailableCpusChangeNotification
@ stub -arch=arm64 PsRegisterSiloMonitor
@ stub -arch=arm64 PsRegisterSyscallProvider
@ stub -arch=arm64 PsRegisterSystemAvailableCpusChangeNotification
@ stub -arch=arm64 PsReleaseProcessExitSynchronization
@ stub -arch=arm64 PsReleaseProcessWakeCounter
@ stub -arch=arm64 PsReleaseSiloHardReference
@ stub -arch=arm64 PsRemoveSiloContext
@ stub -arch=arm64 PsReplaceSiloContext
@ stub -arch=arm64 PsRevertToUserMultipleGroupAffinityThread
@ stub -arch=arm64 PsSetCreateProcessNotifyRoutineEx
@ stub -arch=arm64 PsSetCreateProcessNotifyRoutineEx2
@ stub -arch=arm64 PsSetCreateThreadNotifyRoutineEx
@ stub -arch=arm64 PsSetCurrentThreadPrefetching
@ stub -arch=arm64 PsSetJobProperty
@ stub -arch=arm64 PsSetLoadImageNotifyRoutineEx
@ stub -arch=arm64 PsSetProcessDxgProcess
@ stub -arch=arm64 PsSetProcessFaultInformation
@ stub -arch=arm64 PsSetProcessesWindowState
@ stub -arch=arm64 PsSetSystemMultipleGroupAffinityThread
@ stub -arch=arm64 PsSetThreadProperty
@ stub -arch=arm64 PsStartSiloMonitor
@ stub -arch=arm64 PsTerminateServerSilo
@ stub -arch=arm64 PsTlsAlloc
@ stub -arch=arm64 PsTlsFree
@ stub -arch=arm64 PsTlsGetValue
@ stub -arch=arm64 PsTlsSetValue
@ stub -arch=arm64 PsUnEstablishWin32Callouts
@ stub -arch=arm64 PsUnregisterAvailableCpusChangeNotification
@ stub -arch=arm64 PsUnregisterSiloMonitor
@ stub -arch=arm64 PsUnregisterSyscallProvider
@ stub -arch=arm64 PsUpdateComponentPower
@ stub -arch=arm64 PsUpdateNetworkCounters
@ stub -arch=arm64 PsWow64GetProcessMachine
@ stub -arch=arm64 PsWow64IsMachineSupported
@ stub -arch=arm64 ReadTimeStampCounter
@ stub -arch=arm64 RtlAddAccessFilterAce
@ stub -arch=arm64 RtlAddAtomToAtomTableEx
@ stub -arch=arm64 RtlAddMandatoryAce
@ stub -arch=arm64 RtlAddProcessTrustLabelAce
@ stub -arch=arm64 RtlAddResourceAttributeAce
@ stub -arch=arm64 RtlAreBitsClearEx
@ stub -arch=arm64 RtlAreBitsSetEx
@ stdcall RtlArmFeatureUsageProviderFlushNotification(ptr)
@ stub -arch=arm64 RtlAvlInsertNodeEx
@ stub -arch=arm64 RtlAvlRemoveNode
@ stub -arch=arm64 RtlCapabilityCheck
@ stub -arch=arm64 RtlCapabilityCheckForSingleSessionSku
@ stub -arch=arm64 RtlCheckPortableOperatingSystem
@ stub -arch=arm64 RtlCheckSystemBootStatusIntegrity
@ stub -arch=arm64 RtlCheckTokenCapability
@ stub -arch=arm64 RtlCheckTokenMembership
@ stub -arch=arm64 RtlCheckTokenMembershipEx
@ stub -arch=arm64 RtlClearAllBitsEx
@ stub -arch=arm64 RtlClearBitEx
@ stdcall -arch=x86_64 RtlClearAllBitsEx(ptr) RtlClearAllBits64
@ stdcall -arch=x86_64 RtlClearBitEx(ptr int64) RtlClearBit64
@ stub -arch=arm64 RtlClearBitsEx
@ stub -arch=arm64 RtlCmDecodeMemIoResource
@ stub -arch=arm64 RtlCmEncodeMemIoResource
@ stub -arch=arm64 RtlCompareAltitudes
@ stub -arch=arm64 RtlCompareExchangePointerMapping
@ stub -arch=arm64 RtlCompareExchangePropertyStore
@ stdcall RtlCompareUnicodeStrings(wstr long wstr long long)
@ stub -arch=arm64 RtlConstructCrossVmEventPath
@ stub -arch=arm64 RtlConstructCrossVmMutexPath
@ stub -arch=arm64 RtlConvertHostPerfCounterToPerfCounter
@ stub -arch=arm64 RtlCopyBitMap
@ stub -arch=arm64 RtlCopyBitMapEx
@ stub -arch=arm64 RtlCopyContext
@ stub -arch=arm64 RtlCopyExtendedContext
@ stdcall RtlCrc32(ptr long long)
@ stdcall RtlCrc64(ptr long int64)
@ stub -arch=arm64 RtlCreateAtomTableEx
@ stub -arch=arm64 RtlCreateHashTableEx
@ stub -arch=arm64 RtlDecompressBufferEx
@ stub -arch=arm64 RtlDecompressBufferEx2
@ stub -arch=arm64 RtlDecompressFragmentEx
@ stub -arch=arm64 RtlDeleteElementGenericTableAvlEx
@ stub -arch=arm64 RtlDeriveCapabilitySidsFromName
@ stub -arch=arm64 RtlDrainNonVolatileFlush
@ stub -arch=arm64 RtlEndStrongEnumerationHashTable
@ stub -arch=arm64 RtlEqualWnfChangeStamps
@ stdcall RtlEthernetAddressToStringA(ptr ptr)
@ stdcall RtlEthernetAddressToStringW(ptr ptr)
@ stdcall RtlEthernetStringToAddressA(str ptr ptr)
@ stdcall RtlEthernetStringToAddressW(wstr ptr ptr)
@ stub -arch=arm64 RtlExtendCorrelationVector
@ stub -arch=arm64 RtlExtractBitMap
@ stub -arch=arm64 RtlExtractBitMapEx
@ stub -arch=arm64 RtlFillMemoryNonTemporal
@ stub -arch=arm64 RtlFillNonVolatileMemory
@ stub -arch=arm64 RtlFindAceByType
@ stub -arch=arm64 RtlFindClearBitsAndSetEx
@ stub -arch=arm64 RtlFindClearBitsEx
@ stub -arch=arm64 RtlFindClosestEncodableLength
@ stub -arch=arm64 RtlFindNextForwardRunClearCapped
@ stub -arch=arm64 RtlFindNextForwardRunClearEx
@ stub -arch=arm64 RtlFindNextForwardRunSetEx
@ stub -arch=arm64 RtlFindSetBitsAndClearEx
@ stub -arch=arm64 RtlFindSetBitsEx
@ stub -arch=arm64 RtlFindUnicodeSubstring
@ stub -arch=arm64 RtlFlushFeatureUsage
@ stub -arch=arm64 RtlFlushNonVolatileMemory
@ stub -arch=arm64 RtlFlushNonVolatileMemoryRanges
@ stub -arch=arm64 RtlFreeNonVolatileToken
@ stub -arch=arm64 RtlFreeUTF8String
@ stub -arch=arm64 RtlGenerateClass5Guid
@ stub -arch=arm64 RtlGetAcesBufferSize
@ stub -arch=arm64 RtlGetActiveConsoleId
@ stub -arch=arm64 RtlGetAppContainerNamedObjectPath
@ stub -arch=arm64 RtlGetAppContainerParent
@ stub -arch=arm64 RtlGetAppContainerSidType
@ stub -arch=arm64 RtlGetConsoleSessionForegroundProcessId
@ stub -arch=arm64 RtlGetCurrentServiceSessionId
@ stub -arch=arm64 RtlGetEnabledExtendedAndSupervisorFeatures
@ stdcall -arch=arm64 RtlGetEnabledExtendedFeatures(int64)
@ stub -arch=arm64 RtlGetExtendedContextLength
@ stub -arch=arm64 RtlGetIntegerAtom
@ stub -arch=arm64 RtlGetLastRange
@ stub -arch=arm64 RtlGetMultiTimePrecise
@ stub -arch=arm64 RtlGetNonVolatileToken
@ stub -arch=arm64 RtlGetNtSystemRoot
@ stub -arch=arm64 RtlGetPersistedStateLocation
@ stub -arch=arm64 RtlGetProductInfo
@ stub -arch=arm64 RtlGetSessionProperties
@ stub -arch=arm64 RtlGetSuiteMask
@ stub -arch=arm64 RtlGetSystemBootStatus
@ stub -arch=arm64 RtlGetSystemBootStatusEx
@ stub -arch=arm64 RtlGetSystemGlobalData
@ stub -arch=arm64 RtlGetThreadLangIdByIndex
@ stub -arch=arm64 RtlGetTokenNamedObjectPath
@ stub -arch=arm64 RtlIdnToAscii
@ stub -arch=arm64 RtlIdnToNameprepUnicode
@ stub -arch=arm64 RtlIdnToUnicode
@ stub -arch=arm64 RtlIncrementCorrelationVector
@ stub -arch=arm64 RtlInitStringEx
@ stub -arch=arm64 RtlInitStrongEnumerationHashTable
@ stub -arch=arm64 RtlInitUTF8String
@ stub -arch=arm64 RtlInitUTF8StringEx
@ stub -arch=arm64 RtlInitializeBitMapEx
@ stdcall -arch=x86_64 RtlInitializeBitMapEx(ptr ptr int64) RtlInitializeBitMap64
@ stub -arch=arm64 RtlInitializeCorrelationVector
@ stub -arch=arm64 RtlInitializeExtendedContext
@ stub -arch=arm64 RtlInitializeSidEx
@ stub -arch=arm64 RtlInterlockedClearBitRun
@ stub -arch=arm64 RtlInterlockedClearBitRunEx
@ stub -arch=arm64 RtlInterlockedSetBitRun
@ stub -arch=arm64 RtlInterlockedSetBitRunEx
@ stub -arch=arm64 RtlInterlockedSetClearRun
@ stub -arch=arm64 RtlIntersectBitMaps
@ stub -arch=arm64 RtlIntersectBitMapsEx
@ stub -arch=arm64 RtlInvertRangeListEx
@ stub -arch=arm64 RtlIoDecodeMemIoResource
@ stub -arch=arm64 RtlIoEncodeMemIoResource
@ stub -arch=arm64 RtlIsApiSetImplemented
@ stub -arch=arm64 RtlIsCloudFilesPlaceholder
@ stub -arch=arm64 RtlIsElevatedRid
@ stub -arch=arm64 RtlIsFunctionalityAvailable
@ stub -arch=arm64 RtlIsMultiSessionSku
@ stub -arch=arm64 RtlIsMultiUsersInSessionSku
@ stub -arch=arm64 RtlIsNonEmptyDirectoryReparsePointAllowed
@ stub -arch=arm64 RtlIsNormalizedString
@ stub -arch=arm64 RtlIsNtDdiVersionAvailable
@ stub -arch=arm64 RtlIsPartialPlaceholder
@ stub -arch=arm64 RtlIsPartialPlaceholderFileHandle
@ stub -arch=arm64 RtlIsPartialPlaceholderFileInfo
@ stub -arch=arm64 RtlIsProcessorFeaturePresent
@ stub -arch=arm64 RtlIsSandboxedToken
@ stub -arch=arm64 RtlIsServicePackVersionInstalled
@ stub -arch=arm64 RtlIsStateSeparationEnabled
@ stub -arch=arm64 RtlIsUntrustedObject
@ stub -arch=arm64 RtlIsZeroMemory
@ stub -arch=arm64 RtlLoadString
@ stub -arch=arm64 RtlLocateSupervisorFeature
@ stub -arch=arm64 RtlLogUnexpectedCodepath
@ stub -arch=arm64 RtlMergeBitMaps
@ stub -arch=arm64 RtlMergeBitMapsEx
@ stub -arch=arm64 RtlNormalizeSecurityDescriptor
@ stub -arch=arm64 RtlNormalizeString
@ stdcall RtlNotifyFeatureUsage(ptr)
@ stub -arch=arm64 RtlNumberOfClearBitsEx
@ stub -arch=arm64 RtlNumberOfClearBitsInRange
@ stub -arch=arm64 RtlNumberOfSetBitsEx
@ stub -arch=arm64 RtlNumberOfSetBitsInRange
@ stub -arch=arm64 RtlNumberOfSetBitsInRangeEx
@ stdcall RtlNumberOfSetBitsUlongPtr(long)
@ stub -arch=arm64 RtlOpenImageFileOptionsKey
@ stub -arch=arm64 RtlOsDeploymentState
@ stub -arch=arm64 RtlOwnerAcesPresent
@ stub -arch=arm64 RtlPcToFileName
@ stub -arch=arm64 RtlPcToFilePath
@ stub -arch=arm64 RtlQueryAllFeatureConfigurations
@ stub -arch=arm64 RtlQueryAllInternalFeatureConfigurations
@ stub -arch=arm64 RtlQueryDynamicTimeZoneInformation
@ stub -arch=arm64 RtlQueryElevationFlags
@ stdcall RtlQueryFeatureConfiguration(long long ptr ptr)
@ stdcall RtlQueryFeatureConfigurationChangeStamp()
@ stub -arch=arm64 RtlQueryImageFileKeyOption
@ stdcall RtlQueryModuleInformation(ptr long ptr)
@ stub -arch=arm64 RtlQueryPackageClaims
@ stub -arch=arm64 RtlQueryPackageIdentity
@ stub -arch=arm64 RtlQueryPackageIdentityEx
@ stub -arch=arm64 RtlQueryPointerMapping
@ stub -arch=arm64 RtlQueryProcessPlaceholderCompatibilityMode
@ stub -arch=arm64 RtlQueryPropertyStore
@ stub -arch=arm64 RtlQueryRegistryValueWithFallback
@ stdcall -arch=arm64 RtlQueryRegistryValuesEx(long wstr ptr ptr ptr)
@ stub -arch=arm64 RtlQueryThreadPlaceholderCompatibilityMode
@ stub -arch=arm64 RtlQueryValidationRunlevel
@ stub -arch=arm64 RtlRaiseCustomSystemEventTrigger
@ stub -arch=arm64 RtlRbInsertNodeEx
@ stub -arch=arm64 RtlRbRemoveNode
@ stub -arch=arm64 RtlRbReplaceNode
@ stdcall RtlRecordFeatureUsage(long long long ptr)
@ stdcall RtlRegisterFeatureConfigurationChangeNotification(ptr ptr ptr ptr)
@ stdcall RtlRegisterFeatureUsageProvider(ptr ptr)
@ stub -arch=arm64 RtlRemovePointerMapping
@ stub -arch=arm64 RtlRemovePropertyStore
@ stub -arch=arm64 RtlReplaceSidInSd
@ stub -arch=arm64 RtlRestoreSystemBootStatusDefaults
@ stub -arch=arm64 RtlRunOnceBeginInitialize
@ stub -arch=arm64 RtlRunOnceComplete
@ stub -arch=arm64 RtlRunOnceExecuteOnce
@ stdcall -arch=x86_64 RtlRunOnceExecuteOnce(ptr ptr ptr ptr)
@ stub -arch=arm64 RtlRunOnceInitialize
@ stub -arch=arm64 RtlSetActiveConsoleId
@ stub -arch=arm64 RtlSetAllBitsEx
@ stub -arch=arm64 RtlSetBitEx
@ stdcall -arch=x86_64 RtlSetBitEx(ptr int64) RtlSetBit64
@ stub -arch=arm64 RtlSetBitsEx
@ stub -arch=arm64 RtlSetConsoleSessionForegroundProcessId
@ stub -arch=arm64 RtlSetDynamicTimeZoneInformation
@ stub -arch=arm64 RtlSetPortableOperatingSystem
@ stub -arch=arm64 RtlSetProcessPlaceholderCompatibilityMode
@ stub -arch=arm64 RtlSetSystemBootStatus
@ stub -arch=arm64 RtlSetSystemBootStatusEx
@ stub -arch=arm64 RtlSetSystemGlobalData
@ stub -arch=arm64 RtlSetThreadPlaceholderCompatibilityMode
@ stub -arch=arm64 RtlShiftLeftBitMap
@ stub -arch=arm64 RtlShiftLeftBitMapEx
@ stub -arch=arm64 RtlSidHashInitialize
@ stub -arch=arm64 RtlSidHashLookup
@ stdcall RtlStringFromGUIDEx(ptr ptr long)
@ stub -arch=arm64 RtlStronglyEnumerateEntryHashTable
@ stdcall RtlSuffixUnicodeString(ptr ptr long)
@ stub -arch=arm64 RtlTestBitEx
@ stub -arch=arm64 RtlUTF8StringToUnicodeString
@ stub -arch=arm64 RtlUdiv128
@ stub -arch=arm64 RtlUnicodeStringToInt64
@ stub -arch=arm64 RtlUnicodeStringToUTF8String
@ stdcall RtlUnregisterFeatureConfigurationChangeNotification(ptr)
@ stdcall RtlUnregisterFeatureUsageProvider(ptr)
@ stub -arch=arm64 RtlUnsignedMultiplyHigh
@ stub -arch=arm64 RtlValidateCorrelationVector
@ stub -arch=arm64 RtlVirtualUnwind2
@ stub -arch=arm64 RtlWriteNonVolatileMemory
@ stub -arch=arm64 SeAccessCheckEx
@ stub -arch=arm64 SeAccessCheckFromState
@ stub -arch=arm64 SeAccessCheckFromStateEx
@ stub -arch=arm64 SeAccessCheckWithHint
@ stub -arch=arm64 SeAdjustAccessStateForAccessConstraints
@ stub -arch=arm64 SeAdjustAccessStateForTrustLabel
@ stub -arch=arm64 SeAdjustObjectSecurity
@ stub -arch=arm64 SeAuditFipsCryptoSelftests
@ stub -arch=arm64 SeAuditHardLinkCreationWithTransaction
@ stub -arch=arm64 SeAuditTransactionStateChange
@ stub -arch=arm64 SeAuditingAnyFileEventsWithContext
@ stub -arch=arm64 SeAuditingAnyFileEventsWithContextEx
@ stub -arch=arm64 SeAuditingFileEventsWithContextEx
@ stub -arch=arm64 SeAuditingWithTokenForSubcategory
@ stub -arch=arm64 SeCheckForCriticalAceRemoval
@ stub -arch=arm64 SeCloseObjectAuditAlarmForNonObObject
@ stub -arch=arm64 SeCompareSigningLevels
@ stub -arch=arm64 SeComputeAutoInheritByObjectType
@ stub -arch=arm64 SeConvertSecurityDescriptorToStringSecurityDescriptor
@ stub -arch=arm64 SeConvertSidToStringSid
@ stub -arch=arm64 SeConvertStringSecurityDescriptorToSecurityDescriptor
@ stub -arch=arm64 SeConvertStringSidToSid
@ stub -arch=arm64 SeCreateAndRegisterAccessCheckDebugContext
@ stub -arch=arm64 SeCreateClientSecurityEx
@ stub -arch=arm64 SeCreateClientSecurityFromSubjectContextEx
@ stub -arch=arm64 SeDeleteClientSecurity
@ stub -arch=arm64 SeDeleteObjectAuditAlarmWithTransaction
@ stub -arch=arm64 SeEtwWriteKMCveEvent
@ stub -arch=arm64 SeExamineSacl
@ stub -arch=arm64 SeGetCachedSigningLevel
@ stub -arch=arm64 SeGetLinkedToken
@ stub -arch=arm64 SeGetLogonSessionToken
@ stub -arch=arm64 SeIsParentOfChildAppContainer
@ stub -arch=arm64 SeMarkLogonSessionForTerminationNotificationEx
@ stub -arch=arm64 SeOpenObjectAuditAlarmForNonObObject
@ stub -arch=arm64 SeOpenObjectAuditAlarmWithTransaction
@ stub -arch=arm64 SeOpenObjectForDeleteAuditAlarmWithTransaction
@ stub -arch=arm64 SeQuerySecureBootPlatformManifest
@ stub -arch=arm64 SeQuerySecureBootPolicyValue
@ stub -arch=arm64 SeQuerySecurityAttributesToken
@ stub -arch=arm64 SeQuerySecurityAttributesTokenAccessInformation
@ stub -arch=arm64 SeQueryServerSiloToken
@ stub -arch=arm64 SeQuerySessionIdTokenEx
@ stub -arch=arm64 SeRegisterImageVerificationCallback
@ stub -arch=arm64 SeRegisterLogonSessionTerminatedRoutineEx
@ stub -arch=arm64 SeReportSecurityEventWithSubCategory
@ stub -arch=arm64 SeSecurityAttributePresent
@ stub -arch=arm64 SeSetSecurityAttributesToken
@ stub -arch=arm64 SeSetSecurityAttributesTokenEx
@ stub -arch=arm64 SeSetSessionIdTokenWithLinked
@ stub -arch=arm64 SeShouldCheckForAccessRightsFromParent
@ stub -arch=arm64 SeSrpAccessCheck
@ stub -arch=arm64 SeTokenFromAccessInformation
@ stub -arch=arm64 SeUnRegisterAndFreeAccessCheckDebugContext
@ stub -arch=arm64 SeUnregisterImageVerificationCallback
@ stub -arch=arm64 SeUnregisterLogonSessionTerminatedRoutineEx
@ stub -arch=arm64 SkAcquirePushLockExclusive
@ stub -arch=arm64 SkAllocatePool
@ stub -arch=arm64 SkFreePool
@ stub -arch=arm64 SkInitializePushLock
@ stub -arch=arm64 SkIsSecureKernel
@ stub -arch=arm64 SkQuerySecureKernelInformation
@ stub -arch=arm64 SkReleasePushLockExclusive
@ stub -arch=arm64 TmCancelPropagationRequest
@ stub -arch=arm64 TmCommitComplete
@ stub -arch=arm64 TmCommitEnlistment
@ stub -arch=arm64 TmCommitTransaction
@ stub -arch=arm64 TmCreateEnlistment
@ stub -arch=arm64 TmCurrentTransaction
@ stub -arch=arm64 TmDereferenceEnlistmentKey
@ stub -arch=arm64 TmEnableCallbacks
@ stub -arch=arm64 TmEndPropagationRequest
@ stub -arch=arm64 TmFreezeTransactions
@ stub -arch=arm64 TmGetTransactionId
@ stub -arch=arm64 TmInitSystem
@ stub -arch=arm64 TmInitSystemPhase2
@ stub -arch=arm64 TmInitializeTransactionManager
@ stub -arch=arm64 TmIsKTMCommitCoordinator
@ stub -arch=arm64 TmIsTransactionActive
@ stub -arch=arm64 TmPrePrepareComplete
@ stub -arch=arm64 TmPrePrepareEnlistment
@ stub -arch=arm64 TmPrepareComplete
@ stub -arch=arm64 TmPrepareEnlistment
@ stub -arch=arm64 TmPropagationComplete
@ stub -arch=arm64 TmPropagationFailed
@ stub -arch=arm64 TmReadOnlyEnlistment
@ stub -arch=arm64 TmRecoverEnlistment
@ stub -arch=arm64 TmRecoverResourceManager
@ stub -arch=arm64 TmRecoverTransactionManager
@ stub -arch=arm64 TmReferenceEnlistmentKey
@ stub -arch=arm64 TmRenameTransactionManager
@ stub -arch=arm64 TmRequestOutcomeEnlistment
@ stub -arch=arm64 TmRollbackComplete
@ stub -arch=arm64 TmRollbackEnlistment
@ stub -arch=arm64 TmRollbackTransaction
@ stub -arch=arm64 TmSetCurrentTransaction
@ stub -arch=arm64 TmSinglePhaseReject
@ stub -arch=arm64 TmThawTransactions
@ stub -arch=arm64 TtmNotifyDeviceArrival
@ stub -arch=arm64 TtmNotifyDeviceDeparture
@ stub -arch=arm64 TtmNotifyDeviceInput
@ stub -arch=arm64 VfInsertContext
@ stub -arch=arm64 VfQueryDeviceContext
@ stub -arch=arm64 VfQueryDispatchTable
@ stub -arch=arm64 VfQueryDriverContext
@ stub -arch=arm64 VfQueryIrpContext
@ stub -arch=arm64 VfRemoveContext
@ stub -arch=arm64 VslCreateSecureSection
@ stub -arch=arm64 VslDeleteSecureSection
@ stub -arch=arm64 VslExchangeEntropy
@ stub -arch=arm64 VslGetSecurePciDeviceAlternateFunctionNumberForVtl0Dma
@ stub -arch=arm64 VslGetSecurePciDeviceBootConfiguration
@ stub -arch=arm64 VslGetSecurePciEnabled
@ stub -arch=arm64 VslQuerySecureDevice
@ stub -arch=arm64 VslRetrieveMailbox
@ stub -arch=arm64 WheaAddErrorSource
@ stub -arch=arm64 WheaAddErrorSourceDeviceDriver
@ stub -arch=arm64 WheaAddErrorSourceDeviceDriverV1
@ stub -arch=arm64 WheaAddHwErrorReportSectionDeviceDriver
@ stub -arch=arm64 WheaAttemptClearPoison
@ stub -arch=arm64 WheaAttemptPhysicalPageOffline
@ stub -arch=arm64 WheaAttemptRowOffline
@ stub -arch=arm64 WheaConfigureErrorSource
@ stub -arch=arm64 WheaCreateHwErrorReportDeviceDriver
@ stub -arch=arm64 WheaDeferredRecoveryService
@ stub -arch=arm64 WheaEnterCriticalState
@ stub -arch=arm64 WheaErrorSourceGetState
@ stub -arch=arm64 WheaExitCriticalState
@ stub -arch=arm64 WheaGetCurrentProcessName
@ stub -arch=arm64 WheaGetErrorSource
@ stub -arch=arm64 WheaGetErrorSourceInfo
@ stub -arch=arm64 WheaGetNotifyAllOfflinesPolicy
@ stub -arch=arm64 WheaHighIrqlLogSelEventHandlerRegister
@ stub -arch=arm64 WheaHighIrqlLogSelEventHandlerUnregister
@ stub -arch=arm64 WheaHwErrorReportAbandonDeviceDriver
@ stub -arch=arm64 WheaHwErrorReportGetLogDataBufferDeviceDriver
@ stub -arch=arm64 WheaHwErrorReportMarkAsCriticalDeviceDriver
@ stub -arch=arm64 WheaHwErrorReportSetFatalSeverityDeviceDriver
@ stub -arch=arm64 WheaHwErrorReportSetSectionNameDeviceDriver
@ stub -arch=arm64 WheaHwErrorReportSetSeverityDeviceDriver
@ stub -arch=arm64 WheaHwErrorReportSubmitDeviceDriver
@ stub -arch=arm64 WheaInitializeDeferredRecoveryObject
@ stub -arch=arm64 WheaInitializeRecordHeader
@ stub -arch=arm64 WheaIsCriticalState
@ stub -arch=arm64 WheaIsLogSelHandlerInitialized
@ stub -arch=arm64 WheaLogInternalEvent
@ stub -arch=arm64 WheaPrmTranslateDimmAddress
@ stub -arch=arm64 WheaPrmTranslatePhysicalAddress
@ stub -arch=arm64 WheaProcessWaitingETWEvents
@ stub -arch=arm64 WheaRecoveryBugCheck
@ stub -arch=arm64 WheaRegisterErrorSourceOverride
@ stub -arch=arm64 WheaRemoveErrorSource
@ stub -arch=arm64 WheaRemoveErrorSourceDeviceDriver
@ stub -arch=arm64 WheaReportFatalHwErrorDeviceDriverEx
@ stub -arch=arm64 WheaReportHwError
@ stub -arch=arm64 WheaReportHwErrorDeviceDriver
@ stub -arch=arm64 WheaReportHwErrorDeviceDriverEx
@ stub -arch=arm64 WheaRequestDeferredRecovery
@ stub -arch=arm64 WheaSignalHandlerOverride
@ stub -arch=arm64 WheaTerminateProcess
@ stub -arch=arm64 WheaUnconfigureErrorSource
@ stub -arch=arm64 WheaUnregisterErrorSourceOverride
@ stdcall -arch=arm64 ZwAlertThreadByThreadId(ptr)
@ stub -arch=arm64 ZwAssociateWaitCompletionPacket
@ stub -arch=arm64 ZwCancelWaitCompletionPacket
@ stub -arch=arm64 ZwCommitComplete
@ stub -arch=arm64 ZwCommitEnlistment
@ stub -arch=arm64 ZwCommitRegistryTransaction
@ stub -arch=arm64 ZwCommitTransaction
@ stub -arch=arm64 ZwCreateCpuPartition
@ stub -arch=arm64 ZwCreateCrossVmEvent
@ stub -arch=arm64 ZwCreateEnlistment
@ stub -arch=arm64 ZwCreateKeyTransacted
@ stub -arch=arm64 ZwCreatePartition
@ stub -arch=arm64 ZwCreateProfileEx
@ stub -arch=arm64 ZwCreateRegistryTransaction
@ stub -arch=arm64 ZwCreateResourceManager
@ stub -arch=arm64 ZwCreateSectionEx
@ stub -arch=arm64 ZwCreateTransaction
@ stub -arch=arm64 ZwCreateTransactionManager
@ stub -arch=arm64 ZwCreateWaitCompletionPacket
@ stdcall -version=0x602+ -arch=arm64 ZwCreateWnfStateName(ptr long long long ptr long ptr)
@ stdcall -version=0x602+ -arch=arm64 ZwDeleteWnfStateData(ptr ptr)
@ stdcall -version=0x602+ -arch=arm64 ZwDeleteWnfStateName(ptr)
@ stub -arch=arm64 ZwEnumerateTransactionObject
@ stdcall -arch=arm64 ZwFlushBuffersFileEx(ptr long ptr long ptr)
@ stub -arch=arm64 ZwGetCachedSigningLevel
@ stub -arch=arm64 ZwGetNextProcess
@ stub -arch=arm64 ZwGetNextThread
@ stub -arch=arm64 ZwGetNotificationResourceManager
@ stub -arch=arm64 ZwManagePartition
@ stub -arch=arm64 ZwMapViewOfSectionEx
@ stdcall -arch=arm64 ZwNotifyChangeDirectoryFileEx(ptr ptr ptr ptr ptr ptr long long long long)
@ stub -arch=arm64 ZwNotifyChangeSession
@ stub -arch=arm64 ZwOpenCpuPartition
@ stub -arch=arm64 ZwOpenEnlistment
@ stub -arch=arm64 ZwOpenKeyEx
@ stub -arch=arm64 ZwOpenKeyTransacted
@ stub -arch=arm64 ZwOpenKeyTransactedEx
@ stub -arch=arm64 ZwOpenPartition
@ stub -arch=arm64 ZwOpenRegistryTransaction
@ stub -arch=arm64 ZwOpenResourceManager
@ stub -arch=arm64 ZwOpenSession
@ stub -arch=arm64 ZwOpenTransaction
@ stub -arch=arm64 ZwOpenTransactionManager
@ stub -arch=arm64 ZwPrePrepareComplete
@ stub -arch=arm64 ZwPrePrepareEnlistment
@ stub -arch=arm64 ZwPrepareComplete
@ stub -arch=arm64 ZwPrepareEnlistment
@ stub -arch=arm64 ZwPropagationComplete
@ stub -arch=arm64 ZwPropagationFailed
@ stdcall -arch=arm64 ZwQueryDirectoryFileEx(ptr ptr ptr ptr ptr ptr long long long ptr)
@ stdcall -arch=arm64 ZwQueryInformationByName(ptr ptr ptr long long)
@ stub -arch=arm64 ZwQueryInformationCpuPartition
@ stub -arch=arm64 ZwQueryInformationEnlistment
@ stub -arch=arm64 ZwQueryInformationResourceManager
@ stub -arch=arm64 ZwQueryInformationTransaction
@ stub -arch=arm64 ZwQueryInformationTransactionManager
@ stub -arch=arm64 ZwQueryLicenseValue
@ stub -arch=arm64 ZwQuerySecurityAttributesToken
@ stub -arch=arm64 ZwQuerySecurityPolicy
@ stdcall -version=0x601+ -arch=arm64 ZwQuerySystemInformationEx(long ptr long ptr long ptr)
@ stdcall -version=0x602+ -arch=arm64 ZwQueryWnfStateData(ptr ptr ptr ptr ptr ptr)
@ stdcall -version=0x602+ -arch=arm64 ZwQueryWnfStateNameInformation(ptr long ptr ptr long)
@ stub -arch=arm64 ZwReadOnlyEnlistment
@ stub -arch=arm64 ZwRecoverEnlistment
@ stub -arch=arm64 ZwRecoverResourceManager
@ stub -arch=arm64 ZwRecoverTransactionManager
@ stub -arch=arm64 ZwRollbackComplete
@ stub -arch=arm64 ZwRollbackEnlistment
@ stub -arch=arm64 ZwRollbackRegistryTransaction
@ stub -arch=arm64 ZwRollbackTransaction
@ stub -arch=arm64 ZwSetCachedSigningLevel
@ stub -arch=arm64 ZwSetInformationCpuPartition
@ stub -arch=arm64 ZwSetInformationEnlistment
@ stub -arch=arm64 ZwSetInformationResourceManager
@ stub -arch=arm64 ZwSetInformationTransaction
@ stdcall -arch=arm64 ZwSetInformationVirtualMemory()
@ stub -arch=arm64 ZwSetTimerEx
@ stub -arch=arm64 ZwTraceControl
@ stdcall -version=0x602+ -arch=arm64 ZwUpdateWnfStateData(ptr ptr long ptr ptr long long)
@ stdcall -arch=arm64 ZwWaitForAlertByThreadId(ptr ptr)
@ stub -arch=arm64 _makepath_s
@ cdecl _snprintf_s()
@ stub -arch=arm64 _snscanf_s
@ cdecl _snwprintf_s()
@ stub -arch=arm64 _snwscanf_s
@ stub -arch=arm64 _splitpath_s
@ stub -arch=arm64 _strnset_s
@ stub -arch=arm64 _strset_s
@ stub -arch=arm64 _strtoui64
@ stub -arch=arm64 _ultoa_s
@ stub -arch=arm64 _ultow_s
@ cdecl _vsnprintf_s()
@ cdecl _vsnwprintf_s()
@ stub -arch=arm64 _wcslwr_s
@ stub -arch=arm64 _wcsnset_s
@ stub -arch=arm64 _wcsset_s
@ stub -arch=arm64 _wmakepath_s
@ stub -arch=arm64 _wsplitpath_s
@ stub -arch=arm64 bsearch_s
@ cdecl memcpy_s()
@ cdecl memmove_s()
@ stub -arch=arm64 qsort_s
@ cdecl sprintf_s()
@ stub -arch=arm64 sscanf_s
@ cdecl strcat_s()
@ cdecl strcpy_s()
@ cdecl strncat_s()
@ cdecl strncpy_s()
@ stub -arch=arm64 strtok_s
@ cdecl swprintf_s()
@ stub -arch=arm64 swscanf_s
@ cdecl vsprintf_s()
@ cdecl vswprintf_s()
