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
@ stdcall -arch=win64 ExAcquireFastMutex(ptr)
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
@ stdcall -arch=arm,win64 ExQueryDepthSList(ptr) RtlQueryDepthSList
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
@ stdcall -arch=win64 ExReleaseFastMutex(ptr)
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
@ stdcall -arch=win64 ExTryToAcquireFastMutex(ptr)
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
@ stdcall -arch=arm,win64 ExpInterlockedFlushSList(ptr) RtlInterlockedFlushSList
@ stdcall -arch=arm,win64 ExpInterlockedPopEntrySList(ptr) RtlInterlockedPopEntrySList
@ stdcall -arch=arm,win64 ExpInterlockedPushEntrySList(ptr ptr) RtlInterlockedPushEntrySList
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
@ stdcall InbvHasValidGopFrameBuffer()
@ stdcall InbvInstallDisplayStringFilter(ptr)
@ stdcall InbvIsBootDriverInstalled()
@ stdcall InbvNotifyDisplayOwnershipLost(ptr)
@ stdcall InbvQueryDisplayInfo(ptr)
@ stdcall InbvResetDisplay()
@ stdcall InbvSetScrollRegion(long long long long)
@ stdcall InbvSetTextColor(long)
@ stdcall InbvSolidColorFill(long long long long long)
@ extern -constant InitSafeBootMode
@ fastcall -arch=i386,arm,arm64 InterlockedCompareExchange(ptr long long)
@ fastcall -arch=i386,arm,arm64 InterlockedDecrement(ptr)
@ fastcall -arch=i386,arm,arm64 InterlockedExchange(ptr long)
@ fastcall -arch=i386,arm,arm64 InterlockedExchangeAdd(ptr long)
@ fastcall -arch=i386,arm,arm64 InterlockedIncrement(ptr)
@ fastcall -arch=i386 InterlockedPopEntrySList(ptr)
@ fastcall -arch=i386 InterlockedPushEntrySList(ptr ptr)
@ stdcall -arch=arm InterlockedPopEntrySList(ptr) RtlInterlockedPopEntrySList
@ stdcall -arch=arm InterlockedPushEntrySList(ptr ptr) RtlInterlockedPushEntrySList
@ stdcall -arch=win64 InitializeSListHead(ptr) RtlInitializeSListHead
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
@ stdcall IoCreateDeviceSecure(ptr long ptr long long long ptr ptr ptr)
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
@ stdcall -arch=win64 IoIs32bitProcess(ptr)
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
@ stdcall -version=0x602+ IoSynchronousCallDriver(ptr ptr)
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
@ stdcall -arch=win64 IoWMIDeviceObjectToProviderId(ptr)
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
@ cdecl -arch=win64 KeAcquireInStackQueuedSpinLock(ptr ptr)
@ fastcall KeAcquireInStackQueuedSpinLockAtDpcLevel(ptr ptr)
@ fastcall KeAcquireInStackQueuedSpinLockForDpc(ptr ptr)
@ cdecl -arch=win64 KeAcquireInStackQueuedSpinLockRaiseToSynch(ptr ptr)
@ stdcall KeAcquireInterruptSpinLock(ptr)
@ cdecl -arch=win64 KeAcquireQueuedSpinLock(long)
@ cdecl -arch=win64 KeAcquireQueuedSpinLockRaiseToSynch(long)
@ stdcall KeAcquireSpinLockAtDpcLevel(ptr)
@ fastcall KeAcquireSpinLockForDpc(ptr)
@ stdcall -arch=win64 KeAcquireSpinLockRaiseToDpc(ptr)
@ stdcall -arch=win64 KeAcquireSpinLockRaiseToSynch(ptr)
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
@ stdcall -arch=arm,win64 KeFlushIoBuffers(ptr long long)
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
@ stdcall -arch=i386,arm,win64 KeInitializeSpinLock(ptr) _KeInitializeSpinLock
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
@ stdcall KeIsExecutingDpc()
@ stdcall KeIsWaitListEmpty(ptr)
;@ cdecl -arch=x86_64,arm64 KeLastBranchMSR()
@ stdcall KeLeaveCriticalRegion() _KeLeaveCriticalRegion
@ stdcall KeLeaveGuardedRegion() _KeLeaveGuardedRegion
@ extern KeLoaderBlock
@ cdecl -arch=win64 -private KeLowerIrql(long) KxLowerIrql
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
@ stdcall -arch=i386,arm,riscv64 KeQueryInterruptTime()
@ stdcall KeQueryInterruptTimePrecise(ptr)
;@ cdecl -arch=x86_64,arm64 KeQueryMultiThreadProcessorSet
;@ cdecl -arch=x86_64,arm64 KeQueryPrcbAddress
@ stdcall KeQueryPriorityThread(ptr)
@ stdcall KeQueryRuntimeThread(ptr ptr)
@ stdcall -arch=win64 KeQueryPerformanceCounter(ptr) hal.KeQueryPerformanceCounter
@ stdcall -arch=i386,arm,riscv64 KeQuerySystemTime(ptr)
@ stdcall -version=0x602+ KeQuerySystemTimePrecise(ptr)
@ stdcall -arch=i386,arm,riscv64 KeQueryTickCount(ptr)
@ stdcall KeQueryTimeIncrement()
@ cdecl -arch=win64 KeRaiseIrqlToDpcLevel() KxRaiseIrqlToDpcLevel
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
@ cdecl -arch=win64 KeReleaseInStackQueuedSpinLock(ptr)
@ fastcall KeReleaseInStackQueuedSpinLockForDpc(ptr)
@ fastcall KeReleaseInStackQueuedSpinLockFromDpcLevel(ptr)
@ stdcall KeReleaseInterruptSpinLock(ptr long)
@ stdcall KeReleaseMutant(ptr long long long)
@ stdcall KeReleaseMutex(ptr long)
@ cdecl -arch=win64 KeReleaseQueuedSpinLock(long long)
@ stdcall KeReleaseSemaphore(ptr long long long)
@ stdcall -arch=win64 KeReleaseSpinLock(ptr long)
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
@ stdcall -arch=win64 KeRestoreFloatingPointState(ptr) KxRestoreFloatingPointState
@ stdcall KeRevertToUserAffinityThread()
@ stdcall KeRundownQueue(ptr)
@ stdcall -arch=i386 KeSaveFloatingPointState(ptr)
@ stdcall -arch=win64 KeSaveFloatingPointState(ptr) KxSaveFloatingPointState
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
@ stdcall -arch=win64 KeRevertToUserAffinityThreadEx(int64)
@ stdcall -arch=i386 KeSetSystemAffinityThreadEx(long)
@ stdcall -arch=win64 KeSetSystemAffinityThreadEx(int64)
@ stdcall KeSetTargetProcessorDpc(ptr long)
@ stdcall KeSetTimeIncrement(long long)
@ stdcall KeSetTimer(ptr long long ptr)
@ stdcall KeSetTimerEx(ptr long long long ptr)
@ stdcall KeSignalCallDpcDone(ptr)
@ stdcall KeSignalCallDpcSynchronize(ptr)
@ stdcall KeStackAttachProcess(ptr ptr)
@ stdcall -arch=win64 KeStallExecutionProcessor(long) hal.KeStallExecutionProcessor
@ stdcall KeSynchronizeExecution(ptr ptr ptr)
@ stdcall KeTerminateThread(long)
@ fastcall KeTestSpinLock(ptr)
@ extern -arch=i386,arm,riscv64 KeTickCount
@ fastcall KeTryToAcquireGuardedMutex(ptr)
@ cdecl -arch=win64 KeTryToAcquireQueuedSpinLock(long long)
@ cdecl -arch=win64 KeTryToAcquireQueuedSpinLockRaiseToSynch(long long)
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
@ cdecl -arch=win64 KfRaiseIrql(long) KxRaiseIrql
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
@ stdcall -arch=riscv64 KiRiscvClearSoftwareInterrupt(long)
@ stdcall -arch=riscv64 KiRiscvQueryFeatureFlags()
@ stdcall -arch=riscv64 KiRiscvRequestSoftwareInterrupt(long)
@ stdcall -arch=riscv64 KiRiscvSendSoftwareInterrupt(ptr long)
@ stdcall -arch=riscv64 KiRiscvSetInterruptEnabled(ptr long)
@ stdcall -arch=riscv64 KiRiscvUnimplemented(ptr)
@ cdecl -arch=i386,arm,arm64 KiUnexpectedInterrupt()
@ stdcall -arch=i386 Kii386SpinOnSpinLock(ptr long)
@ stdcall LdrAccessResource(ptr ptr ptr ptr)
@ stdcall LdrEnumResources(ptr ptr long ptr ptr)
@ stdcall LdrFindResourceDirectory_U(ptr ptr long ptr)
@ stdcall LdrFindResource_U(ptr ptr long ptr)
@ stdcall LdrResFindResource(ptr long long long ptr ptr ptr ptr long)
@ stdcall LdrResFindResourceDirectory(ptr long long ptr ptr ptr long ptr)
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
@ stdcall -version=0x602+ PoFxAddComponentRelation(ptr long ptr ptr)
@ stdcall -version=0x602+ PoFxCompleteDevicePowerNotRequired(ptr)
@ stdcall -version=0x602+ PoFxCompleteDirectedPowerDown(ptr)
@ stdcall -version=0x602+ PoFxCompleteIdleCondition(ptr long)
@ stdcall -version=0x602+ PoFxCompleteIdleState(ptr long)
@ stdcall -version=0x602+ PoFxIdleComponent(ptr long long)
@ stdcall -version=0x602+ -arch=i386 PoFxPowerControl(ptr ptr ptr long ptr long ptr)
@ stdcall -version=0x602+ -arch=win64 PoFxPowerControl(ptr ptr ptr ptr ptr ptr ptr)
@ stdcall -version=0x602+ PoFxRegisterDevice(ptr ptr ptr)
@ stdcall -version=0x602+ PoFxRemoveComponentRelation(ptr long ptr ptr)
@ stdcall -version=0x602+ PoFxReportDevicePoweredOn(ptr)
@ stdcall -version=0x602+ PoFxSetComponentLatency(ptr long int64)
@ stdcall -version=0x602+ PoFxSetComponentResidency(ptr long int64)
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
@ stdcall PoSetProcessorAggregatorParking(long ptr)
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
@ stdcall -arch=win64 PsGetCurrentProcessWow64Process()
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
@ stdcall -arch=win64 PsGetProcessPeb32(ptr)
@ stdcall PsGetProcessPriorityClass(ptr)
@ stdcall PsGetProcessSectionBaseAddress(ptr)
@ stdcall PsGetProcessSecurityPort(ptr)
@ stdcall PsGetProcessSessionId(ptr)
@ stdcall PsGetProcessSessionIdEx(ptr)
@ stdcall PsGetProcessWin32Process(ptr)
@ stdcall PsGetProcessMitigationPolicyFlags(ptr long)
@ stdcall PsGetProcessWin32WindowStation(ptr)
@ stdcall -arch=win64 PsGetProcessWow64Process(ptr)
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
@ stdcall PsSetCreateProcessNotifyRoutineEx(ptr long)
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
@ stdcall -arch=win64 RtlCopyMemory(ptr ptr int64) memmove
@ stdcall -arch=win64 RtlCopyMemoryNonTemporal(ptr ptr int64) memmove
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
@ stdcall -arch=i386,arm,arm64,riscv64 RtlFillMemoryUlong(ptr long long)
@ stdcall -arch=arm64,riscv64 RtlFillMemoryUlonglong(ptr long int64)
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
@ stdcall RtlGenerate8dot3Name(ptr long ptr ptr)
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
@ cdecl -arch=win64 RtlLookupFunctionEntry(double ptr ptr)
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
@ cdecl -arch=win64 RtlPcToFileHeader(ptr ptr)
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
@ cdecl -arch=win64 RtlRestoreContext(ptr ptr)
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
@ stdcall -arch=arm,win64 RtlUnwindEx(ptr ptr ptr ptr ptr ptr)
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
@ stdcall -arch=arm,win64 RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr)
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
@ cdecl -arch=arm,win64 __C_specific_handler(ptr long ptr ptr)
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
@ cdecl -arch=win64 _local_unwind()
@ cdecl _purecall()
@ cdecl -arch=arm,win64 _setjmp(ptr ptr)
@ cdecl -arch=arm,win64 _setjmpex(ptr ptr)
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
@ cdecl -arch=arm,win64 longjmp(ptr long)
@ cdecl mbstowcs()
@ cdecl mbtowc()
@ cdecl memchr()
@ cdecl -arch=i386,win64 memcmp()
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

; HAL dependencies
@ stdcall -arch=arm64,riscv64 KfLowerIrql(long)
@ stdcall -arch=arm64,riscv64 KeGetCurrentIrql()
@ stdcall -arch=x86_64 KeGetCurrentIrql() KxGetCurrentIrql
; ARM64 SMP diagnostics (smpdbg) recorders, called from the HAL
@ stdcall -arch=arm64 SmpDbgTimerBegin(long long)
@ stdcall -arch=arm64 SmpDbgTimerEoi(long long)
@ stdcall -arch=arm64 SmpDbgTimerReject(long long)
@ stdcall -arch=arm64 KxSaveFloatingPointState(ptr)
@ stdcall -arch=arm64 KxRestoreFloatingPointState(ptr)

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
@ stdcall -arch=win64 ExBlockPushLock()
@ stdcall -arch=win64 ExTimedWaitForUnblockPushLock()
@ stdcall -arch=win64 ExTryToAcquireResourceExclusiveLite()
@ stdcall -arch=win64 ExWaitForUnblockPushLock()
@ stdcall -arch=win64 FsRtlOplockBreakToNone()
@ stdcall -arch=arm64 KdLogDbgPrint()
@ stdcall -arch=win64 KeAlertThread()
@ stdcall -arch=win64 KeRemoveQueueApc()
@ stdcall -arch=win64 KeTestAlertThread()
@ stdcall -arch=arm64 KiDispatchInterrupt()
@ stdcall -arch=win64 MmCopyVirtualMemory()
@ stdcall -arch=arm64 MmLoadSystemImage()
@ stdcall -arch=win64 MmMapViewInSystemSpaceEx()
@ stdcall -arch=arm64 MmUnloadSystemImage()
@ stdcall -arch=win64 NtEnumerateSystemEnvironmentValuesEx()
@ stdcall -arch=win64 NtReadFileScatter()
@ stdcall -arch=win64 NtSetInformationToken()
@ stdcall -arch=win64 NtWriteFileGather()
@ stdcall ObDereferenceObjectDeferDelete(ptr)
@ stdcall -arch=win64 ObDuplicateObject()
@ stdcall -arch=win64 ObIsKernelHandle()
@ stdcall -arch=win64 ObReferenceObjectSafe()
@ stdcall -arch=win64 PsReferenceProcessFilePointer()
@ stdcall -arch=win64 PsResumeProcess()
@ stdcall -arch=win64 PsSuspendProcess()
@ stdcall -arch=win64 RtlAddAccessAllowedObjectAce()
@ stdcall -arch=win64 RtlAddAccessDeniedAceEx()
@ stdcall -arch=win64 RtlAddAccessDeniedObjectAce()
@ stdcall -arch=win64 RtlAddAuditAccessAceEx()
@ stdcall -arch=win64 RtlAddAuditAccessObjectAce()
@ stdcall -arch=win64 RtlCopyLuidAndAttributesArray()
@ stdcall -arch=win64 RtlCopySidAndAttributesArray()
@ stdcall -arch=win64 RtlCreateUnicodeStringFromAsciiz()
@ stdcall -arch=win64 RtlCreateUserThread()
@ stdcall -arch=win64 RtlCultureNameToLCID()
@ stdcall RtlDowncaseUnicodeChar(long)
@ stdcall -arch=win64 RtlDuplicateUnicodeString()
@ stdcall -arch=win64 RtlFindExportedRoutineByName()
@ stdcall -arch=win64 RtlFindNextForwardRunSet()
@ stdcall -arch=win64 RtlFirstFreeAce()
@ stdcall -arch=win64 RtlFormatMessage()
@ stdcall -arch=win64 RtlGetControlSecurityDescriptor()
@ stdcall -arch=win64 RtlGetNtProductType()
@ stdcall -arch=win64 RtlImageNtHeaderEx()
@ stdcall -arch=win64 RtlLCIDToCultureName()
@ stdcall -arch=win64 RtlLargeIntegerToChar()
@ stdcall -arch=win64 RtlLocalTimeToSystemTime()
@ stdcall -arch=win64 RtlLookupFirstMatchingElementGenericTableAvl()
@ stdcall -arch=win64 RtlOpenCurrentUser()
@ stdcall -arch=win64 RtlQueryInformationAcl()
@ stdcall -arch=win64 RtlRaiseStatus()
@ stdcall -arch=win64 RtlSetControlSecurityDescriptor()
@ stdcall -arch=win64 RtlSystemTimeToLocalTime()
@ stdcall RtlUTF8ToUnicodeN(ptr long ptr str long)
@ stdcall -arch=win64 RtlValidAcl()
@ stdcall -arch=win64 RtlValidateUnicodeString()
@ stdcall -arch=win64 SeCaptureSubjectContextEx()
@ stdcall -arch=win64 SeCreateAccessStateEx()
@ stdcall -arch=arm64 SeLocateProcessImageName()
@ stdcall SeTokenIsWriteRestricted(ptr)
@ stdcall ZwAllocateLocallyUniqueId(ptr)
@ stdcall -arch=win64 ZwCompareTokens()
@ stdcall -arch=i386,win64 ZwCreateIoCompletion(ptr long ptr long)
@ stdcall -arch=win64 ZwCreateProcessEx()
@ stdcall -arch=win64 ZwCreateSemaphore()
@ stdcall ZwFlushBuffersFile(ptr ptr)
@ stdcall -arch=win64 ZwGetWriteWatch()
@ stdcall -arch=win64 ZwImpersonateAnonymousToken()
@ stdcall -arch=win64 ZwLoadKeyEx()
@ stdcall -arch=win64 ZwLockFile()
@ stdcall -arch=win64 ZwLockProductActivationKeys()
@ stdcall -arch=win64 ZwLockVirtualMemory()
@ stdcall -arch=win64 ZwNotifyChangeDirectoryFile()
@ stdcall -arch=win64 ZwProtectVirtualMemory(ptr ptr ptr long ptr)
@ stdcall -arch=arm64 ZwQueryIntervalProfile()
@ stdcall -arch=win64 ZwQueryQuotaInformationFile()
@ stdcall -arch=win64 ZwQuerySystemEnvironmentValueEx()
@ stdcall -arch=arm64 ZwQueryTimerResolution()
@ stdcall ZwQueryVirtualMemory(ptr ptr long ptr long ptr)
@ stdcall -arch=win64 ZwReleaseSemaphore()
@ stdcall -arch=i386,win64 ZwRemoveIoCompletion(ptr ptr ptr ptr ptr)
@ stdcall -arch=win64 ZwRenameKey()
@ stdcall -arch=win64 ZwRequestPort()
@ stdcall -arch=win64 ZwResetWriteWatch()
@ stdcall -arch=win64 ZwResumeThread()
@ stdcall -arch=win64 ZwSetInformationKey()
@ stdcall -arch=win64 ZwSetInformationToken()
@ stdcall -arch=arm64 ZwSetIntervalProfile()
@ stdcall -arch=win64 ZwSetIoCompletion()
@ stdcall -arch=win64 ZwSetQuotaInformationFile()
@ stdcall -arch=win64 ZwSetSystemEnvironmentValueEx()
@ stdcall -arch=arm64 ZwSetTimerResolution()
@ stdcall -arch=arm64 ZwStartProfile()
@ stdcall -arch=arm64 ZwStopProfile()
@ stdcall -arch=win64 ZwSuspendThread()
@ stdcall -arch=win64 ZwSystemDebugControl()
@ stdcall -arch=win64 ZwTraceEvent()
@ stdcall -arch=win64 ZwUnloadKey2()
@ stdcall -arch=win64 ZwUnloadKeyEx()
@ stdcall -arch=win64 ZwUnlockFile()
@ stdcall -arch=win64 ZwUnlockVirtualMemory()
@ cdecl -arch=win64 _atoi64()
@ cdecl -arch=win64 _finite()
@ cdecl -arch=win64 _i64toa_s()
@ cdecl -arch=win64 _i64tow_s()
@ cdecl -arch=win64 _itoa_s()
@ cdecl -arch=win64 _itow_s()
@ cdecl -arch=win64 _ltoa_s()
@ cdecl -arch=win64 _ltow_s()
@ cdecl -arch=win64 _ui64toa_s()
@ cdecl -arch=win64 _ui64tow_s()
@ cdecl -arch=win64 _vswprintf()
@ cdecl -arch=win64 _wtoi()
@ cdecl -arch=win64 _wtol()
@ cdecl -arch=win64 bsearch()
@ stdcall -arch=win64 iswalnum()
@ cdecl -arch=win64 iswdigit()
@ cdecl -arch=win64 iswspace()
@ cdecl -arch=win64 strnlen(ptr int64)
@ cdecl -arch=win64 wcscat_s(ptr int64 ptr)
@ cdecl -arch=win64 wcscpy_s(ptr int64 ptr)
@ cdecl -arch=win64 wcsncat_s(ptr int64 ptr int64)
@ cdecl -arch=win64 wcsncpy_s(ptr int64 ptr int64)
@ cdecl -arch=win64 wcsnlen(ptr int64)
@ cdecl -arch=x86_64 sqrt(double)
@ cdecl -arch=win64 wcstoul()
# --- Data exports (already defined in the kernel) ---
@ extern -arch=arm64 NtBuildLab
@ extern -arch=win64 PsLoadedModuleList
@ extern -arch=win64 PsLoadedModuleResource
@ extern -arch=win64 SeSystemDefaultSd
# --- Data exports (ARM64 parity stubs and forwarded variables) ---
@ extern CmKeyObjectType
@ extern -arch=win64 ExActivationObjectType
@ extern -arch=win64 ExCompositionObjectType
@ extern -arch=win64 ExCoreMessagingObjectType
@ extern -arch=win64 ExRawInputManagerObjectType
@ extern ExTimerObjectType
@ extern IoCompletionObjectType
@ extern KdComPortInUse
@ extern -arch=win64 KdEventLoggingEnabled
@ extern KdHvComPortInUse
@ extern -arch=arm64 KeDynamicPartitioningSupported
@ extern -arch=win64 MmBadPointer
@ extern -arch=arm64 NtBuildGUID
@ extern -arch=win64 POGOBuffer
@ extern -arch=i386,win64 PsPartitionType
@ extern -arch=win64 PsSiloContextNonPagedType
@ extern -arch=win64 PsSiloContextPagedType
@ extern -arch=win64 PsUILanguageComitted
@ extern -arch=win64 SeILSigningPolicyPtr
@ extern TmEnlistmentObjectType
@ extern TmResourceManagerObjectType
@ extern TmTransactionManagerObjectType
@ extern TmTransactionObjectType
@ extern -arch=win64 psMUITest
@ stdcall -arch=win64 AlpcCreateSecurityContext(ptr ptr long ptr)
@ stdcall -arch=win64 AlpcGetHeaderSize(long)
@ stdcall -arch=win64 AlpcGetMessageAttribute(ptr long)
@ stdcall -arch=win64 AlpcInitializeMessageAttribute(long ptr long ptr)
# --- Unimplemented Win11 exports (auto-generated stubs raise STATUS via DbgPrint) ---
@ stub -arch=win64 BgkDisplayCharacter
@ stub -arch=win64 BgkGetConsoleState
@ stub -arch=win64 BgkGetCursorState
@ stub -arch=win64 BgkSetCursor
@ stub -arch=win64 CarCopyRuleViolationDetails
@ stub -arch=win64 CarCreateRuleViolationDetails
@ stub -arch=win64 CarDeleteRuleViolationDetails
@ stub -arch=win64 CarDeregisterRuleClassConfiguration
@ stub -arch=win64 CarDeregisterRuleOverride
@ stub -arch=win64 CarInitializeRuleViolationDetails
@ stub -arch=win64 CarQueryReportAction
@ stub -arch=win64 CarQueryReportActionForTriage
@ stub -arch=win64 CarRegisterDefaultRuleClassConfiguration
@ stub -arch=win64 CarRegisterRuleClassConfiguration
@ stub -arch=win64 CarRegisterRuleOverride
@ stub -arch=win64 CarRegisterRuleOverrideAllContexts
@ stub -arch=win64 CarRegisterRuleOverridesAllContexts
@ stub -arch=win64 CarReportDifPluginRuleViolation
@ stub -arch=win64 CarSetCustomIdInRuleOverride
@ stub -arch=win64 CarSetCustomRuleIdRange
@ stub -arch=win64 CcAddDirtyPagesToExternalCache
@ stub -arch=win64 CcAsyncCopyRead
@ stub -arch=win64 CcDeductDirtyPagesFromExternalCache
@ stub -arch=win64 CcErrorCallbackRoutine
@ stub -arch=win64 CcFlushCacheToLsn
@ stub -arch=win64 CcGetCachedDirtyPageCountForFile
@ stub -arch=win64 CcGetFileObjectFromSectionPtrsRef
@ stub -arch=win64 CcGetNumberOfMappedPages
@ stub -arch=win64 CcInitializeCacheMapEx
@ stub -arch=win64 CcInitializeCacheMapEx2
@ stub -arch=win64 CcIsCacheManagerCallbackNeeded
@ stub -arch=win64 CcIsThereDirtyDataEx
@ stub -arch=win64 CcIsThereDirtyLoggedPages
@ stub -arch=win64 CcRegisterExternalCache
@ stub -arch=win64 CcScheduleReadAheadEx
@ stub -arch=win64 CcSetFileSizesEx
@ stub -arch=win64 CcSetLogHandleForFileEx
@ stub -arch=win64 CcSetLoggedDataThreshold
@ stub -arch=win64 CcSetParallelFlushFile
@ stub -arch=win64 CcSetReadAheadGranularityEx
@ stub -arch=win64 CcTestControl
@ stub -arch=win64 CcUnmapFileOffsetFromSystemCache
@ stub -arch=win64 CcUnregisterExternalCache
@ stub -arch=win64 CcZeroDataOnDisk
@ stub -arch=arm64 CmCallbackGetKeyObjectID
@ stub -arch=arm64 CmCallbackGetKeyObjectIDEx
@ stub -arch=arm64 CmCallbackReleaseKeyObjectIDEx
@ stub -arch=arm64 CmGetBoundTransaction
@ stub -arch=arm64 CmGetCallbackVersion
@ stub -arch=arm64 CmRegisterCallbackEx
@ stub -arch=win64 CmRegisterMachineHiveLoadedNotification
@ stub -arch=arm64 CmSetCallbackObjectContext
@ stub -arch=win64 CmUnregisterMachineHiveLoadedNotification
@ stub -arch=win64 DbgSetDebugPrintCallback
@ stdcall -arch=i386,win64 DbgkLkmdRegisterCallback(ptr ptr long)
@ stdcall -arch=i386,win64 DbgkLkmdUnregisterCallback(ptr)
@ stdcall -arch=win64 DbgkWerCaptureLiveKernelDump(ptr long ptr ptr ptr ptr ptr ptr long)
@ stub -arch=win64 DbgkWerCaptureLiveKernelDump2
@ stub -arch=win64 DifEnumeratePluginData
@ stub -arch=win64 DifFindThreadContextData
@ stub -arch=win64 DifGetPluginPerDriverData
@ stub -arch=win64 DifObjTrkInsertItem
@ stub -arch=win64 DifObjTrkQeuryInvokeDeleteRange
@ stub -arch=win64 DifObjTrkRemoveItem
@ stub -arch=win64 DifPluginSimplePerfControl
@ stub -arch=win64 DifPopThreadContextData
@ stub -arch=win64 DifPushThreadContextData
@ stdcall -arch=win64 DifRegisterClassDriverPlugin(long ptr long ptr)
@ stub -arch=win64 DifRegisterObjectTracking
@ stub -arch=win64 DifRegisterPlugin
@ stub -arch=win64 DifUtilDbgPrint
@ stub -arch=win64 EmClientQueryRuleState
@ stub -arch=win64 EmClientRuleDeregisterNotification
@ stub -arch=win64 EmClientRuleEvaluate
@ stub -arch=win64 EmClientRuleRegisterNotification
@ stub -arch=win64 EmProviderDeregister
@ stub -arch=win64 EmProviderDeregisterEntry
@ stub -arch=win64 EmProviderRegister
@ stub -arch=win64 EmProviderRegisterEntry
@ stub -arch=win64 EmpProviderRegister
@ stdcall -arch=i386,win64 EtwActivityIdControl(long ptr)
@ stub -arch=win64 EtwEnableTrace
@ stdcall -arch=i386,win64 EtwEventEnabled(int64 ptr)
@ stdcall -arch=i386,win64 EtwProviderEnabled(int64 long int64)
@ stdcall EtwRegisterClassicProvider(ptr long ptr ptr ptr)
@ stub -arch=win64 EtwSendTraceBuffer
@ stdcall -arch=win64 EtwSetInformation(int64 long ptr long)
@ stdcall -arch=i386,win64 EtwTelemetryCoverageReport(ptr)
@ stub -arch=win64 EtwWriteEndScenario
@ stub -arch=win64 EtwWriteEx
@ stub -arch=win64 EtwWriteStartScenario
@ stub -arch=win64 EtwWriteString
@ stdcall -arch=win64 EtwWriteTransfer(int64 ptr ptr ptr long ptr)
@ stdcall -arch=i386,win64 EtwpDisableStackWalkApc()
@ stdcall -arch=i386,win64 EtwpReenableStackWalkApc(long)
@ stub -arch=win64 ExAccessByte
@ stub -arch=win64 ExAcquireAutoExpandPushLockExclusive
@ stub -arch=win64 ExAcquireAutoExpandPushLockShared
@ stub -arch=win64 ExAcquireCacheAwarePushLockExclusive
@ stub -arch=win64 ExAcquireCacheAwarePushLockExclusiveEx
@ stub -arch=win64 ExAcquireCacheAwarePushLockSharedEx
@ stdcall -arch=i386,win64 ExAcquireFastResourceExclusive(ptr ptr long) ExpAcquireFastResourceExclusive
@ stdcall -arch=i386,win64 ExAcquireFastResourceShared(ptr ptr long) ExpAcquireFastResourceShared
@ stub -arch=win64 ExAcquireFastResourceSharedStarveExclusive
@ stub -arch=win64 ExAcquireFastResourceWithFlags
@ fastcall -arch=win64 ExAcquirePushLockExclusiveEx(ptr long)
@ fastcall -arch=win64 ExAcquirePushLockSharedEx(ptr long)
@ stdcall -arch=i386,win64 ExAcquireSpinLockExclusive(ptr)
@ stdcall -arch=i386,win64 ExAcquireSpinLockExclusiveAtDpcLevel(ptr)
@ stdcall -arch=i386,win64 ExAcquireSpinLockShared(ptr)
@ stdcall -arch=i386,win64 ExAcquireSpinLockSharedAtDpcLevel(ptr)
@ stub -arch=win64 ExAllocateAutoExpandPushLock
@ stub -arch=win64 ExAllocateCacheAwarePushLock
@ stdcall -arch=win64 ExAllocateFromLookasideListEx(ptr) ExiAllocateFromLookasideListEx
@ stdcall -arch=win64 ExAllocateFromNPagedLookasideList(ptr) ExiAllocateFromNPagedLookasideList
@ stdcall -arch=win64 ExAllocatePool2(int64 int64 long)
@ stdcall -arch=win64 ExAllocatePool3(int64 long long ptr long)
@ stdcall -version=0x603+ -arch=i386,win64 ExAllocateTimer(ptr ptr long)
@ fastcall -arch=win64 ExBlockOnAddressPushLock(ptr ptr ptr int64 ptr)
@ stub -arch=win64 ExCancelDpcEventWait
@ stdcall -version=0x603+ -arch=i386,win64 ExCancelTimer(ptr ptr)
@ stub -arch=win64 ExCleanupAutoExpandPushLock
@ stub -arch=win64 ExCleanupRundownProtectionCacheAware
@ stub -arch=win64 ExConvertFastResourceExclusiveToShared
@ stub -arch=win64 ExConvertPushLockExclusiveToShared
@ stub -arch=win64 ExCreateDpcEvent
@ stub -arch=win64 ExCreatePool
@ stub -arch=win64 ExDeleteDpcEvent
@ stdcall -arch=i386,win64 ExDeleteFastResource(ptr) ExpDeleteFastResource
@ stdcall -version=0x603+ -arch=i386,win64 ExDeleteTimer(ptr long long ptr)
@ stub -arch=win64 ExDestroyPool
@ stub -arch=win64 ExDisownFastResource
@ stub -arch=win64 ExEnterPriorityRegionAndAcquireResourceExclusive
@ stdcall -arch=i386,win64 ExEnterPriorityRegionAndAcquireResourceShared(ptr) ExpEnterPriorityRegionAndAcquireResourceShared
@ stub -arch=win64 ExEnumerateSystemFirmwareTables
@ stub -arch=win64 ExFetchLicenseData
@ stub -arch=win64 ExFreeAutoExpandPushLock
@ stub -arch=win64 ExFreeCacheAwarePushLock
@ stdcall -arch=win64 ExFreePool2(ptr long ptr long)
@ stdcall -arch=i386,win64 ExFreeToLookasideListEx(ptr ptr) ExpFreeToLookasideListExExport
@ stdcall -arch=i386,win64 ExFreeToNPagedLookasideList(ptr ptr) ExpFreeToNPagedLookasideListExport
@ stdcall -arch=i386,win64 ExGetFirmwareEnvironmentVariable(ptr ptr ptr ptr ptr)
@ stdcall -arch=win64 ExGetFirmwareType()
@ stub -arch=win64 ExGetLicenseTamperState
@ stub -arch=win64 ExGetPrmInterface
@ stdcall -arch=win64 ExGetSystemFirmwareTable(long long ptr long ptr)
@ stub -arch=win64 ExInitializeAutoExpandPushLock
@ stdcall -arch=i386,win64 ExInitializeFastOwnerEntry(ptr) ExpInitializeFastOwnerEntry
@ stdcall -arch=i386,win64 ExInitializeFastResource(ptr) ExpInitializeFastResource
@ stub -arch=win64 ExInitializeFastResource2
@ stub -arch=win64 ExInitializeFastResourceAcquired
@ stdcall ExInitializePushLock(ptr)
@ stub -arch=win64 ExInitializeResourceLite2
@ stub -arch=win64 ExInitializeRundownProtectionCacheAwareEx
@ stdcall -arch=i386,win64 ExIsFastResourceContended(ptr) ExpIsFastResourceContended
@ stdcall -arch=i386,win64 ExIsFastResourceHeld(ptr) ExpIsFastResourceHeld
@ stdcall -arch=i386,win64 ExIsFastResourceHeldExclusive(ptr) ExpIsFastResourceHeldExclusive
@ stdcall -arch=i386,win64 ExIsManufacturingModeEnabled()
@ stdcall -arch=i386,win64 ExIsSoftBoot()
@ stub -arch=win64 ExMoveFastResourceOwnershipWithFlags
@ stub -arch=win64 ExNotifyBootDeviceRemoval
@ stdcall -arch=i386,win64 ExQueryFastCacheDevLicense()
@ stdcall -arch=win64 ExQueryTimerResolution(ptr ptr ptr)
@ stdcall -arch=win64 ExQueryWnfStateData(ptr ptr ptr ptr)
@ stub -arch=win64 ExQueueDpcEventWait
@ stub -arch=win64 ExRcuFreePool
@ stdcall -arch=arm64 ExRealTimeIsUniversal()
@ stub -arch=win64 ExRegisterBootDevice
@ stub -arch=win64 ExRegisterExtension
@ stub -arch=win64 ExReinitializeFastResource
@ stub -arch=win64 ExReleaseAutoExpandPushLockExclusive
@ stub -arch=win64 ExReleaseAutoExpandPushLockShared
@ stub -arch=win64 ExReleaseCacheAwarePushLockExclusive
@ stub -arch=win64 ExReleaseCacheAwarePushLockExclusiveEx
@ stub -arch=win64 ExReleaseCacheAwarePushLockSharedEx
@ stub -arch=win64 ExReleaseDisownedFastResource
@ stub -arch=win64 ExReleaseDisownedFastResourceExclusive
@ stub -arch=win64 ExReleaseDisownedFastResourceShared
@ stdcall -arch=i386,win64 ExReleaseFastResource(ptr ptr) ExpReleaseFastResource
@ stub -arch=win64 ExReleaseFastResourceExclusive
@ stub -arch=win64 ExReleaseFastResourceShared
@ fastcall -arch=win64 ExReleasePushLockEx(ptr long)
@ fastcall -arch=win64 ExReleasePushLockExclusiveEx(ptr long)
@ fastcall -arch=i386,win64 ExReleasePushLockSharedEx(ptr long)
@ stub -arch=win64 ExReleaseResourceAndLeavePriorityRegion
@ stdcall -arch=i386,win64 ExReleaseSpinLockExclusive(ptr long)
@ stdcall -arch=i386,win64 ExReleaseSpinLockExclusiveFromDpcLevel(ptr)
@ stdcall -arch=i386,win64 ExReleaseSpinLockShared(ptr long)
@ stdcall -arch=i386,win64 ExReleaseSpinLockSharedFromDpcLevel(ptr)
@ stub -arch=win64 ExSecurePoolUpdate
@ stub -arch=win64 ExSecurePoolValidate
@ stdcall ExSetFirmwareEnvironmentVariable(ptr ptr ptr long long)
@ stub -arch=win64 ExSetLicenseTamperState
@ stub -arch=win64 ExSetResourceOwnerPointerEx
@ stdcall -version=0x603+ -arch=i386,win64 ExSetTimer(ptr int64 int64 ptr)
@ stdcall -version=0x603+ -arch=arm64 ExShareAddressSpaceWithDevice(ptr ptr)
@ stub -arch=arm64 ExShareSystemAddressSpaceWithDevice
@ stub -arch=win64 ExSizeOfAutoExpandPushLock
@ stub -arch=arm64 ExStopSharingAddressSpaceWithDevice
@ stub -arch=arm64 ExStopSharingSystemAddressSpaceWithDevice
@ stdcall -arch=win64 ExSubscribeWnfStateChange(ptr ptr long long ptr ptr)
@ stub -arch=arm64 ExSvmBeginDeviceReset
@ stub -arch=arm64 ExSvmFinalizeDeviceReset
@ stub -arch=win64 ExTryAcquireAutoExpandPushLockExclusive
@ stub -arch=win64 ExTryAcquireAutoExpandPushLockShared
@ stub -arch=win64 ExTryAcquireCacheAwarePushLockExclusiveEx
@ stub -arch=win64 ExTryAcquireCacheAwarePushLockSharedEx
@ fastcall -arch=i386,win64 ExTryAcquirePushLockExclusiveEx(ptr long)
@ fastcall -arch=i386,win64 ExTryAcquirePushLockSharedEx(ptr long)
@ stdcall -arch=i386,win64 ExTryAcquireSpinLockExclusiveAtDpcLevel(ptr)
@ stdcall -arch=i386,win64 ExTryAcquireSpinLockSharedAtDpcLevel(ptr)
@ stub -arch=win64 ExTryConvertPushLockSharedToExclusiveEx
@ stdcall -arch=i386,win64 ExTryConvertSharedSpinLockExclusive(ptr)
@ stub -arch=win64 ExTryQueueWorkItem
@ stub -arch=win64 ExTryToConvertFastResourceSharedToExclusive
@ stub -arch=win64 ExUnblockOnAddressPushLockEx
@ stub -arch=win64 ExUnblockPushLockEx
@ stub -arch=win64 ExUnregisterExtension
@ stdcall -arch=win64 ExUnsubscribeWnfStateChange(ptr)
@ stub -arch=win64 ExUpdateLicenseData
@ stub -arch=win64 ExfTryAcquirePushLockShared
@ cdecl -arch=i386,win64 FirstEntrySList(ptr)
@ stdcall FsRtlAcknowledgeEcp(ptr)
@ stub -arch=win64 FsRtlAcquireEofLock
@ stub -arch=win64 FsRtlAcquireHeaderMutex
@ stub -arch=win64 FsRtlAddBaseMcbEntryEx
@ stub -arch=win64 FsRtlAddToTunnelCacheEx
@ stub -arch=win64 FsRtlAllocateAePushLock
@ stdcall FsRtlAllocateExtraCreateParameter(ptr long long ptr long ptr)
@ stdcall FsRtlAllocateExtraCreateParameterFromLookasideList(ptr long long ptr ptr ptr)
@ stdcall FsRtlAllocateExtraCreateParameterList(long ptr)
@ stdcall FsRtlCancellableWaitForMultipleObjects(long ptr long ptr ptr ptr)
@ stdcall FsRtlCancellableWaitForSingleObject(ptr ptr ptr)
@ stub -arch=win64 FsRtlChangeBackingFileObject
@ stub -arch=win64 FsRtlCheckFileSystemFilterCallbacksRegistered
@ stub -arch=win64 FsRtlCheckOplockEx2
@ stub -arch=win64 FsRtlCheckOplockForFsFilterCallback
@ stub -arch=win64 FsRtlCheckUpperOplock
@ stub -arch=win64 FsRtlCurrentOplock
@ stub -arch=win64 FsRtlDedupChangeInit
@ stub -arch=win64 FsRtlDedupChangeLogOverwriteOrFree
@ stub -arch=win64 FsRtlDedupChangeLogWrite
@ stub -arch=win64 FsRtlDedupChangeUninit
@ stdcall FsRtlDeleteExtraCreateParameterLookasideList(ptr long)
@ stub -arch=win64 FsRtlDisallowLegacyFilterOnDevice
@ stdcall FsRtlFindExtraCreateParameter(ptr ptr ptr ptr)
@ stub -arch=win64 FsRtlFindInTunnelCacheEx
@ stub -arch=win64 FsRtlFreeAePushLock
@ stdcall FsRtlFreeExtraCreateParameter(ptr)
@ stdcall FsRtlFreeExtraCreateParameterList(ptr)
@ stub -arch=win64 FsRtlGetCurrentProcessLoaderList
@ stub -arch=win64 FsRtlGetDirectImageOriginalBase
@ stub -arch=win64 FsRtlGetFileExtents
@ stub -arch=win64 FsRtlGetFileNameInformation
@ stub -arch=win64 FsRtlGetIoAtEof
@ stub -arch=win64 FsRtlGetSupportedFeatures
@ stub -arch=win64 FsRtlGetVirtualDiskNestingLevel
@ stub -arch=win64 FsRtlHeatInit
@ stub -arch=win64 FsRtlHeatLogIo
@ stub -arch=win64 FsRtlHeatLogTierMove
@ stub -arch=win64 FsRtlHeatUninit
@ stub -arch=win64 FsRtlIncrementCcFastMdlReadWait
@ stdcall FsRtlInitExtraCreateParameterLookasideList(ptr long ptr long)
@ stub -arch=win64 FsRtlInitializeBaseMcbEx
@ stub -arch=win64 FsRtlInitializeEofLock
@ stdcall FsRtlInitializeExtraCreateParameter(ptr long ptr long ptr ptr)
@ stdcall FsRtlInitializeExtraCreateParameterList(ptr)
@ stdcall FsRtlInsertExtraCreateParameter(ptr ptr)
@ stdcall FsRtlInsertPerFileContext(ptr ptr)
@ stub -arch=win64 FsRtlInsertPerFileContextWithReserve
@ stub -arch=win64 FsRtlIs32BitProcess
@ stub -arch=win64 FsRtlIsDaxVolume
@ stdcall FsRtlIsEcpAcknowledged(ptr)
@ stdcall FsRtlIsEcpFromUserMode(ptr)
@ stub -arch=win64 FsRtlIsExtentDangling
@ stub -arch=win64 FsRtlIsMobileOS
@ stub -arch=win64 FsRtlIsNameInUnUpcasedExpression
@ stub -arch=win64 FsRtlIsNonEmptyDirectoryReparsePointAllowed
@ stub -arch=win64 FsRtlIsSystemPagingFile
@ stub -arch=win64 FsRtlIssueDeviceIoControl
@ stub -arch=win64 FsRtlKernelFsControlFile
@ stub -arch=win64 FsRtlLogCcFlushError
@ stdcall FsRtlLookupPerFileContext(ptr ptr ptr)
@ stub -arch=win64 FsRtlMdlReadEx
@ stub -arch=win64 FsRtlMupGetProviderIdFromName
@ stub -arch=win64 FsRtlMupGetProviderInfoFromFileObject
@ stub -arch=win64 FsRtlNotifyCleanupAll
@ stub -arch=win64 FsRtlNotifyFilterChangeDirectoryLite
@ stub -arch=win64 FsRtlNotifyFilterReportChangeLite
@ stub -arch=win64 FsRtlNotifyFilterReportChangeLiteEx
@ stub -arch=win64 FsRtlNotifyVolumeEventEx
@ stub -arch=win64 FsRtlOpenFileSystemRegistryKeyFromFsGuid
@ stub -arch=win64 FsRtlOplockBreakH2
@ stub -arch=win64 FsRtlOplockBreakToNoneEx
@ stub -arch=win64 FsRtlOplockFsctrlEx
@ stub -arch=win64 FsRtlOplockGetAnyBreakOwnerProcess
@ stub -arch=win64 FsRtlOplockKeysEqual
@ stub -arch=win64 FsRtlPrepareMdlWriteEx
@ stdcall FsRtlPrepareToReuseEcp(ptr)
@ stub -arch=win64 FsRtlQueryCachedVdl
@ stub -arch=win64 FsRtlQueryInformationFile
@ stub -arch=win64 FsRtlQueryKernelEaFile
@ stub -arch=win64 FsRtlQueryMaximumVirtualDiskNestingLevel
@ stub -arch=win64 FsRtlRegisterFltMgrCalls
@ stub -arch=win64 FsRtlRegisterMupCalls
@ stub -arch=win64 FsRtlRegisterUncProviderEx
@ stub -arch=win64 FsRtlRegisterUncProviderEx2
@ stub -arch=win64 FsRtlReleaseEofLock
@ stub -arch=win64 FsRtlReleaseFileNameInformation
@ stub -arch=win64 FsRtlReleaseHeaderMutex
@ stdcall FsRtlRemoveExtraCreateParameter(ptr ptr ptr ptr)
@ stdcall FsRtlRemovePerFileContext(ptr ptr ptr)
@ stub -arch=win64 FsRtlRemovePerFileContextWithReserve
@ stub -arch=win64 FsRtlSendModernAppTermination
@ stub -arch=win64 FsRtlSetDriverBacking
@ stdcall FsRtlSetEcpListIntoIrp(ptr ptr)
@ stub -arch=win64 FsRtlSetKernelEaFile
@ stdcall FsRtlTeardownPerFileContexts(ptr)
@ stub -arch=win64 FsRtlTryToAcquireHeaderMutex
@ stub -arch=win64 FsRtlUpperOplockFsctrl
@ stub -arch=win64 FsRtlVolumeDeviceToCorrelationId
@ stub -arch=arm64 HalFlushIoBuffers
@ stub -arch=arm64 HviGetHardwareFeatures
@ stub -arch=arm64 HviGetHypervisorFeatures
@ stub -arch=arm64 HviIsAnyHypervisorPresent
@ stub -arch=arm64 HviIsHypervisorVendorMicrosoft
@ stub -arch=win64 HvlGetApicIdFromLpIndex
@ stdcall -arch=arm64 HvlGetHypervisorVendorId()
@ stub -arch=win64 HvlGetLpIndexFromApicId
@ stub -arch=win64 HvlGetLpIndexFromProcessorIndex
@ stub -arch=win64 HvlInvokeFastExtendedHypercall
@ stub -arch=win64 HvlInvokeHypercall
@ stub -arch=win64 HvlIsSchedulerAssistAvailable
@ stub -arch=win64 HvlMapDmaRanges
@ stub -arch=win64 HvlQueryActiveHypervisorProcessorCount
@ stub -arch=win64 HvlQueryActiveProcessors
@ stub -arch=win64 HvlQueryConnection
@ stub -arch=win64 HvlQueryHypervisorProcessorNodeNumber
@ stub -arch=win64 HvlQueryNumaDistance
@ stub -arch=win64 HvlQueryProcessorTopology
@ stub -arch=win64 HvlQueryProcessorTopologyCount
@ stub -arch=win64 HvlQueryProcessorTopologyEx
@ stub -arch=win64 HvlQueryProcessorTopologyHighestId
@ stub -arch=win64 HvlQueryStartedProcessors
@ stub -arch=win64 HvlReadPerformanceStateCounters
@ stub -arch=win64 HvlRegisterInterruptCallback
@ stub -arch=win64 HvlRegisterWheaErrorNotification
@ stub -arch=win64 HvlSchedulerAssistAcknowledgeEvents
@ stub -arch=win64 HvlUnmapDmaRanges
@ stub -arch=win64 HvlUnregisterInterruptCallback
@ stub -arch=win64 HvlUnregisterWheaErrorNotification
@ stub -arch=win64 HvlUpdatePerformanceStateCountersForLp
@ stdcall -arch=i386,win64 InbvNotifyDisplayOwnershipChange(long)
@ stdcall -arch=i386,win64 InbvSetVirtualFrameBuffer(ptr)
@ stub -arch=win64 InterlockedPushListSList
@ stub -arch=win64 IoAcquireKsrPersistentMemory
@ stub -arch=win64 IoAcquireKsrPersistentMemoryEx
@ stub -arch=win64 IoAddBugcheckTriageThread
@ stub -arch=win64 IoAdjustStackSizeForRedirection
@ stub -arch=win64 IoAllocateIrpEx
@ stub -arch=win64 IoAllocateMiniCompletionPacket
@ stub -arch=win64 IoAllocateSfioStreamIdentifier
@ stub -arch=win64 IoApplyPriorityInfoThread
@ stub -arch=win64 IoBoostThreadIo
@ stub -arch=win64 IoCancelMiniCompletionPacket
@ stub -arch=win64 IoCheckFileObjectOpenedAsCopyDestination
@ stub -arch=win64 IoCheckFileObjectOpenedAsCopySource
@ stub -arch=win64 IoCheckLinkShareAccess
@ stub -arch=win64 IoCheckRedirectionTrustLevel
@ stub -arch=win64 IoCheckShareAccessEx
@ stub -arch=win64 IoCleanupIrp
@ stub -arch=win64 IoClearActivityIdThread
@ stub -arch=win64 IoClearAdapterCryptoEngineExtension
@ stub -arch=win64 IoClearFsTrackOffsetState
@ stdcall IoClearIrpExtraCreateParameter(ptr)
@ stub -arch=win64 IoComputeRedirectionTrustLevel
@ stub -arch=win64 IoConvertFileHandleToKernelHandle
@ stub -arch=win64 IoCopyDeviceObjectHint
@ stub -arch=win64 IoCreateArcName
@ stub -arch=win64 IoCreateDriverProxyExtension
@ stdcall IoCreateFileEx(ptr long ptr ptr ptr long long long long ptr long long ptr long ptr)
@ stub -arch=win64 IoCreateStreamFileObjectEx2
@ stub -arch=win64 IoCreateSymbolicLink2
@ stub -arch=win64 IoCreateSystemThread
@ stub -arch=win64 IoDecrementKeepAliveCount
@ stub -arch=win64 IoDriverProxyCreateHotSwappableWorkerThread
@ stub -arch=win64 IoDuplicateDependency
@ stub -arch=win64 IoEnumerateKsrPersistentMemoryEx
@ stub -arch=win64 IoFreeKsrPersistentMemory
@ stub -arch=win64 IoFreeMiniCompletionPacket
@ stub -arch=win64 IoFreeSfioStreamIdentifier
@ stdcall IoGetActivityIdIrp(ptr ptr)
@ stub -arch=win64 IoGetActivityIdThread
@ stub -arch=win64 IoGetAdapterCryptoEngineExtension
@ stub -arch=win64 IoGetAffinityInterrupt
@ stub -arch=win64 IoGetBootDiskInformationLite
@ stub -arch=win64 IoGetContainerInformation
@ stub -arch=win64 IoGetCopyInformationExtension
@ stub -arch=win64 IoGetDeviceDirectory
@ stdcall -arch=i386,win64 IoGetDeviceInterfacePropertyData(ptr ptr long long long ptr ptr ptr)
@ stdcall -arch=i386,win64 IoGetDeviceNumaNode(ptr ptr)
@ stdcall IoGetDriverDirectory(ptr long long ptr)
@ stub -arch=win64 IoGetDriverProxyEndpointWrapper
@ stub -arch=win64 IoGetDriverProxyFeatures
@ stub -arch=win64 IoGetFsTrackOffsetState
@ stub -arch=win64 IoGetFsZeroingOffset
@ stub -arch=win64 IoGetGenericIrpExtension
@ stub -arch=win64 IoGetInitiatorProcess
@ stub -arch=win64 IoGetIoAttributionHandle
@ stdcall -arch=win64 IoGetIommuInterface(long ptr)
@ stdcall -arch=win64 IoGetIommuInterfaceEx(long int64 ptr)
@ stub -arch=win64 IoGetKsrPersistentMemoryBuffer
@ stub -arch=win64 IoGetOplockKeyContext
@ stub -arch=win64 IoGetOplockKeyContextEx
@ stub -arch=win64 IoGetSfioStreamIdentifier
@ stub -arch=win64 IoGetShadowFileInformation
@ stub -arch=win64 IoGetSilo
@ stub -arch=win64 IoGetSiloParameters
@ stub -arch=win64 IoGetSymlinkSupportInformation
@ stdcall IoGetTransactionParameterBlock(ptr)
@ stub -arch=win64 IoIncrementKeepAliveCount
@ stub -arch=win64 IoInitializeIrpEx
@ stub -arch=win64 IoInitializeMiniCompletionPacket
@ stdcall -arch=i386,win64 IoInitializeWorkItem(ptr ptr)
@ stub -arch=win64 IoIrpHasFsTrackOffsetExtensionType
@ stub -arch=win64 IoIsActivityTracingEnabled
@ stub -arch=win64 IoIsFileObjectIgnoringSharing
@ stub -arch=win64 IoIsInitiator32bitProcess
@ stub -arch=win64 IoIsValidIrpStatus
@ stub -arch=win64 IoMakeAssociatedIrpEx
@ stub -arch=win64 IoMapKsrPersistentMemoryEx
@ stdcall -arch=i386,win64 IoOpenDriverRegistryKey(ptr long long long ptr)
@ stub -arch=win64 IoPropagateActivityIdToThread
@ stub -arch=win64 IoPropagateIrpExtension
@ stub -arch=win64 IoPropagateIrpExtensionEx
@ stdcall -arch=i386,win64 IoQueryFullDriverPath(ptr ptr)
@ stub -arch=win64 IoQueryInformationByName
@ stub -arch=arm64 IoQueryInterface
@ stub -arch=win64 IoQueryKsrPersistentMemorySize
@ stub -arch=win64 IoQueryKsrPersistentMemorySizeEx
@ stub -arch=win64 IoQueueWorkItemToNode
@ stub -arch=win64 IoRecordIoAttribution
@ stdcall IoRegisterBootDriverCallback(ptr ptr)
@ stub -arch=win64 IoRegisterContainerNotification
@ stub -arch=win64 IoRegisterDriverProxyEndpoints
@ stub -arch=win64 IoRegisterFsRegistrationChangeMountAware
@ stub -arch=win64 IoRegisterIoTracking
@ stub -arch=win64 IoRegisterPriorityCallback
@ stub -arch=win64 IoRemoveIoCompletion
@ stub -arch=win64 IoRemoveLinkShareAccess
@ stub -arch=win64 IoRemoveLinkShareAccessEx
@ stdcall IoReplaceFileObjectName(ptr ptr long)
@ stub -arch=win64 IoReplacePartitionUnit
@ stdcall IoReportInterruptActive(ptr)
@ stdcall IoReportInterruptInactive(ptr)
@ stdcall -arch=i386,win64 IoReportRootDevice(ptr)
@ stub -arch=win64 IoRequestDeviceEjectEx
@ stub -arch=win64 IoRequestDeviceRemovalForReset
@ stub -arch=win64 IoReserveDependency
@ stub -arch=win64 IoReserveKsrPersistentMemory
@ stub -arch=win64 IoReserveKsrPersistentMemoryEx
@ stub -arch=win64 IoResolveDependency
@ stub -arch=win64 IoSetActivityIdIrp
@ stub -arch=win64 IoSetActivityIdThread
@ stub -arch=win64 IoSetAdapterCryptoEngineExtension
@ stub -arch=win64 IoSetDependency
@ stub -arch=win64 IoSetFileObjectIgnoreSharing
@ stub -arch=win64 IoSetFsTrackOffsetState
@ stub -arch=win64 IoSetFsZeroingOffset
@ stub -arch=win64 IoSetFsZeroingOffsetRequired
@ stub -arch=win64 IoSetGenericIrpExtension
@ stub -arch=win64 IoSetIoAttributionIrp
@ stub -arch=win64 IoSetIoCompletionEx
@ stub -arch=win64 IoSetIoCompletionEx3
@ stub -arch=win64 IoSetIoPriorityHint
@ stub -arch=win64 IoSetIoPriorityHintIntoFileObject
@ stub -arch=win64 IoSetIoPriorityHintIntoThread
@ stdcall IoSetIrpExtraCreateParameter(ptr ptr)
@ stub -arch=win64 IoSetLinkShareAccess
@ stub -arch=win64 IoSetShadowFileInformation
@ stub -arch=win64 IoSetShareAccessEx
@ stub -arch=win64 IoSizeOfIrpEx
@ stub -arch=win64 IoSizeofGenericIrpExtension
@ stdcall -arch=i386,win64 IoSizeofWorkItem()
@ stub -arch=win64 IoSteerInterrupt
@ stub -arch=win64 IoTestDependency
@ stub -arch=win64 IoTransferActivityId
@ stub -arch=win64 IoTryQueueWorkItem
@ stdcall IoUninitializeWorkItem(ptr)
@ stdcall IoUnregisterBootDriverCallback(ptr)
@ stub -arch=win64 IoUnregisterContainerNotification
@ stub -arch=win64 IoUnregisterIoTracking
@ stdcall -arch=win64 IoUnregisterPlugPlayNotificationEx(ptr)
@ stub -arch=win64 IoUnregisterPriorityCallback
@ stub -arch=win64 IoUpdateLinkShareAccess
@ stub -arch=win64 IoUpdateLinkShareAccessEx
@ stub -arch=win64 IoVolumeDeviceNameToGuid
@ stub -arch=win64 IoVolumeDeviceNameToGuidPath
@ stub -arch=win64 IoWithinStackLimits
@ stub -arch=win64 IoWriteKsrPersistentMemory
@ stub -arch=win64 IofGetDriverProxyWrapperFromEndpoint
@ stub -arch=arm64 KdAcquireDebuggerLock
@ stub -arch=win64 KdDeregisterPowerHandler
@ stub -arch=arm64 KdGetDebugDevice
@ stub -arch=arm64 KdPowerTransitionEx
@ stub -arch=win64 KdRegisterPowerHandler
@ stub -arch=arm64 KdReleaseDebuggerLock
@ stub -arch=arm64 KdSetEventLoggingPresent
@ stdcall -arch=win64 KeAddGroupAffinityEx(ptr long int64)
@ stdcall -arch=win64 KeAddProcessorAffinityEx(ptr long)
@ stdcall -arch=win64 KeAddProcessorGroupAffinity(ptr long)
@ stdcall -arch=i386 KeAddTriageDumpDataBlock(ptr ptr long)
@ stdcall -arch=win64 KeAddTriageDumpDataBlock(ptr ptr int64)
@ stub -arch=win64 KeAllocateCalloutStack
@ stub -arch=win64 KeAllocateCalloutStackEx
@ stub -arch=arm64 KeAllocateProcessorProfileStructures
@ stdcall -arch=win64 KeAndAffinityEx(ptr ptr ptr)
@ stdcall -arch=win64 KeAndAffinityEx2(ptr ptr ptr)
@ stdcall -arch=win64 KeAndGroupAffinityEx(ptr ptr ptr)
@ stub -arch=arm64 KeCancelTimer2
@ stdcall -arch=win64 KeCheckProcessorAffinityEx(ptr long)
@ stdcall -arch=win64 KeCheckProcessorGroupAffinity(ptr long)
@ stub -arch=arm64 KeClockInterruptNotify
@ stdcall -arch=win64 KeComplementAffinityEx(ptr ptr)
@ stdcall -arch=win64 KeComplementAffinityEx2(ptr ptr)
@ stdcall KeConvertAuxiliaryCounterToPerformanceCounter(int64 ptr ptr)
@ stub -arch=win64 KeConvertPerformanceCounterToAuxiliaryCounter
@ stdcall -arch=win64 KeCopyAffinityEx(ptr ptr)
@ stdcall -arch=win64 KeCopyAffinityEx2(ptr ptr)
@ stdcall -arch=win64 KeCountSetBitsAffinityEx(ptr)
@ stdcall -arch=win64 KeCountSetBitsGroupAffinity(ptr)
@ stdcall -arch=win64 KeDeregisterProcessorChangeCallback(ptr)
@ stdcall -arch=arm64 KeDispatchSecondaryInterrupt(long long ptr)
@ stdcall -arch=win64 KeEnumerateNextProcessor(ptr ptr)
@ stdcall -arch=win64 KeFindFirstSetLeftAffinityEx(ptr)
@ stdcall -arch=win64 KeFindFirstSetLeftGroupAffinity(ptr)
@ stdcall -arch=win64 KeFindFirstSetRightAffinityEx(ptr)
@ stdcall -arch=win64 KeFindFirstSetRightGroupAffinity(ptr)
@ stdcall -arch=win64 KeFirstGroupAffinityEx(ptr ptr)
@ stub -arch=win64 KeFreeCalloutStack
@ stub -arch=arm64 KeGetClockOwner
@ stub -arch=arm64 KeGetClockTimerResolution
@ stdcall -arch=win64 KeGetEffectiveIrql()
@ stub -arch=arm64 KeGetNextClockTickDuration
@ stdcall -arch=win64 KeGetProcessorIndexFromNumber(ptr)
@ stdcall -arch=win64 KeGetProcessorNumberFromIndex(long ptr)
@ stub -arch=win64 KeHwPolicyLocateResource
@ stdcall -arch=win64 KeInitializeAffinityEx(ptr)
@ stdcall -arch=win64 KeInitializeAffinityEx2(ptr long)
@ stdcall -arch=win64 KeInitializeEnumerationContext(ptr ptr)
@ stdcall -arch=win64 KeInitializeEnumerationContextFromAffinity(ptr long int64)
@ stdcall -arch=win64 KeInitializeEnumerationContextFromGroup(ptr ptr)
@ stdcall -arch=arm64 KeInitializeSecondaryInterruptServices()
@ stub -arch=arm64 KeInitializeTimer2
@ stdcall -arch=i386,win64 KeInitializeTriageDumpDataArray(ptr long)
@ stdcall -arch=win64 KeInterlockedClearProcessorAffinityEx(ptr long)
@ stdcall -arch=win64 KeInterlockedSetProcessorAffinityEx(ptr long)
@ fastcall -arch=i386,win64 KeInvalidateRangeAllCaches(ptr long)
@ stub -arch=arm64 KeInvalidateRangeAllCachesNoIpi
@ stdcall -arch=win64 KeIsEmptyAffinityEx(ptr)
@ stdcall -arch=win64 KeIsEqualAffinityEx(ptr ptr)
@ stdcall -arch=win64 KeIsSingleGroupAffinityEx(ptr ptr)
@ stdcall -arch=win64 KeIsSubsetAffinityEx(ptr ptr)
@ stub -arch=arm64 KeNotifyProcessorFreezeSupported
@ stdcall -arch=win64 KeOrAffinityEx(ptr ptr ptr)
@ stdcall -arch=win64 KeOrAffinityEx2(ptr ptr ptr)
@ stdcall -arch=win64 KeProcessorGroupAffinity(ptr long)
@ stdcall KeQueryActiveGroupCount()
@ stdcall -arch=win64 KeQueryActiveProcessorAffinity(ptr)
@ stdcall -arch=win64 KeQueryActiveProcessorAffinity2(ptr ptr)
@ stdcall -arch=win64 KeQueryAuxiliaryCounterFrequency(ptr)
@ stdcall -arch=win64 KeQueryDpcWatchdogInformation(ptr)
@ stdcall -arch=win64 KeQueryEffectivePriorityThread(ptr)
@ stdcall KeQueryGroupAffinity(long)
@ stdcall KeQueryGroupAffinityEx(ptr long)
@ stdcall -arch=win64 KeQueryHardwareCounterConfiguration(ptr long ptr)
@ stdcall -arch=win64 KeQueryHeteroCpuPolicyThread(ptr long)
@ stub -arch=win64 KeQueryInterruptPartitionCount
@ stub -arch=win64 KeQueryInterruptPartitionInformation
@ stdcall KeQueryLogicalProcessorRelationship(ptr long ptr ptr)
@ stdcall -arch=win64 KeQueryMaximumGroupCount()
@ stdcall KeQueryNodeActiveAffinity(long ptr ptr)
@ stdcall KeQueryNodeActiveAffinity2(long ptr ptr)
@ stdcall -arch=win64 KeQueryNodeActiveProcessorCount(long)
@ stdcall -arch=win64 KeQueryNodeMaximumProcessorCount(long)
@ stdcall -arch=win64 KeQueryPrcbAddress(long)
@ stub -arch=win64 KeQuerySystemCpuPartitionAffinity
@ stdcall -arch=arm64 KeQueryTotalCycleTimeProcess(ptr ptr)
@ stdcall -arch=win64 KeQueryTotalCycleTimeThread(ptr ptr)
@ stdcall -arch=win64 KeQueryTypeEvent(ptr)
@ stdcall -arch=win64 KeQueryUnbiasedInterruptTime()
@ stdcall KeQueryUnbiasedInterruptTimePrecise(ptr)
@ stub -arch=win64 KeRcuReadLock
@ stub -arch=win64 KeRcuReadUnlock
@ stub -arch=win64 KeRcuSynchronize
@ stdcall -arch=win64 KeRegisterProcessorChangeCallback(ptr ptr long)
@ stdcall -arch=win64 KeReinitializeAffinityEx(ptr)
@ stdcall -arch=win64 KeRemoveGroupAffinityEx(ptr long int64)
@ stdcall -arch=win64 KeRemoveProcessorAffinityEx(ptr long)
@ stdcall -arch=win64 KeRemoveProcessorGroupAffinity(ptr long)
@ stdcall KeRemoveQueueDpcEx(ptr long)
@ stdcall -arch=win64 KeRemoveQueueEx(ptr long long ptr ptr long)
@ stub -arch=win64 KeReportCacheIncoherentDevice
@ stdcall -arch=win64 KeRestoreExtendedProcessorState(ptr)
@ stub -arch=arm64 KeRestoreProcessorState
@ stdcall -arch=win64 KeRevertToUserGroupAffinityThread(ptr)
@ stdcall -arch=win64 KeSaveExtendedProcessorState(int64 ptr)
@ stdcall -arch=win64 KeSetActualBasePriorityThread(ptr long)
@ stdcall -arch=win64 KeSetHardwareCounterConfiguration(ptr long)
@ stdcall -arch=win64 KeSetHeteroCpuPolicyThread(ptr long long)
@ stdcall -arch=win64 KeSetSelectedCpuSetsThread(ptr long ptr)
@ stdcall -arch=win64 KeSetSystemGroupAffinityThread(ptr ptr)
@ stdcall -arch=i386,win64 KeSetTargetProcessorDpcEx(ptr ptr)
@ stdcall -version=0x603+ -arch=arm64 KeSetTimer2(ptr int64 int64 ptr)
@ stdcall -arch=win64 KeShouldYieldProcessor()
@ stdcall -arch=win64 KeSizeOfAffinityEx(long)
@ stub -arch=win64 KeSrcuAllocate
@ stub -arch=win64 KeSrcuFree
@ stub -arch=win64 KeSrcuReadLock
@ stub -arch=win64 KeSrcuReadUnlock
@ stub -arch=win64 KeSrcuSynchronize
@ stub -arch=arm64 KeStallWhileFrozen
@ stub -arch=win64 KeStartDynamicProcessor
@ stdcall -arch=win64 KeSubtractAffinityEx(ptr ptr ptr)
@ stdcall -arch=win64 KeSubtractAffinityEx2(ptr ptr ptr)
@ stub -arch=arm64 KeSweepIcacheRange
@ stub -arch=arm64 KeSweepLocalCaches
@ stub -arch=arm64 KeSynchronizeTimeToQpc
@ stub -arch=arm64 KeSystemFullyCacheCoherent
@ stub -arch=win64 KeUpdateThreadTag
@ stub -arch=arm64 KiConnectHalInterrupt
@ stub -arch=arm64 KiReplayInterrupt
@ stub -arch=win64 KitLogFeatureUsage
@ stub -arch=win64 KseQueryDeviceData
@ stub -arch=win64 KseQueryDeviceDataList
@ stub -arch=win64 KseQueryDeviceFlags
@ stub -arch=win64 KseRegisterShim
@ stub -arch=win64 KseRegisterShimEx
@ stub -arch=win64 KseSetDeviceFlags
@ stub -arch=win64 KseUnregisterShim
@ stub -arch=win64 LdrFindResourceEx_U
@ stub -arch=win64 LdrResSearchResource
@ stub -arch=win64 MmAddVerifierSpecialThunks
@ stub -arch=win64 MmAllocateContiguousMemoryEx
@ stdcall -arch=win64 MmAllocateContiguousMemorySpecifyCacheNode(long long long long long long long long long)
@ stdcall -arch=win64 MmAllocateContiguousNodeMemory(long long long long long long long long long)
@ stub -arch=win64 MmAllocateMappingAddressEx
@ stdcall MmAllocateMdlForIoSpace(ptr long ptr)
@ stub -arch=win64 MmAllocateMemoryRanges
@ stdcall -arch=win64 MmAllocateNodePagesForMdlEx(long long long long long long long long long long)
@ stub -arch=win64 MmAllocatePartitionNodePagesForMdlEx
@ stub -arch=win64 MmAreMdlPagesCached
@ stub -arch=win64 MmChangeImageProtection
@ stub -arch=win64 MmConfigureGraphicsPtes
@ stdcall -arch=i386 MmCopyMemory(ptr int64 long long ptr)
@ stdcall -arch=win64 MmCopyMemory(ptr int64 int64 long ptr)
@ stub -arch=win64 MmForceSectionClosedEx
@ stub -arch=win64 MmFreeMemoryRanges
@ stub -arch=win64 MmFreePagesFromMdlEx
@ stub -arch=win64 MmGetCacheAttribute
@ stub -arch=win64 MmGetCacheAttributeEx
@ stub -arch=win64 MmGetMaximumFileSectionSize
@ stub -arch=win64 MmGetPageBadStatus
@ stdcall -arch=i386,win64 MmGetPhysicalMemoryRangesEx(ptr)
@ stub -arch=win64 MmGetPhysicalMemoryRangesEx2
@ stub -arch=win64 MmGetSectionInformation
@ stub -arch=win64 MmIsDriverSuspectForVerifier
@ stdcall -arch=win64 MmIsDriverVerifyingByAddress(ptr)
@ stub -arch=win64 MmIsFileSectionActive
@ stub -arch=arm64 MmLockPreChargedPagedPool
@ stdcall -arch=win64 MmMapIoSpaceEx(long long long long)
@ stub -arch=win64 MmMapMdl
@ stub -arch=win64 MmMapMemoryDumpMdlEx
@ stdcall -arch=i386,win64 MmMapViewInSessionSpaceEx(ptr ptr ptr ptr ptr) MmpMapViewInSessionSpaceEx
@ stub -arch=win64 MmMdlPageContentsState
@ stub -arch=win64 MmMdlPagesAreZero
@ stub -arch=arm64 MmObtainChargesToLockPagedPool
@ stdcall -version=0x602+ -arch=i386,win64 MmPrefetchVirtualAddresses(ptr)
@ stub -arch=win64 MmProtectDriverSection
@ stub -arch=win64 MmQueryMemoryRanges
@ stub -arch=arm64 MmReturnChargesToLockPagedPool
@ stdcall -arch=i386,win64 MmRotatePhysicalView(ptr ptr ptr long ptr ptr)
@ stub -arch=win64 MmSecureVirtualMemoryEx
@ stub -arch=win64 MmSetGraphicsPtes
@ stub -arch=win64 MmSetPermanentCacheAttribute
@ stub -arch=arm64 MmUnlockPreChargedPagedPool
@ stdcall -arch=arm64 NtAlertThreadByThreadId(ptr)
@ stdcall NtCommitComplete(ptr ptr)
@ stdcall NtCommitEnlistment(ptr ptr)
@ stdcall NtCommitTransaction(ptr long)
@ stub -arch=win64 NtCompareSigningLevels
@ stub -arch=win64 NtCopyFileChunk
@ stub -arch=win64 NtCreateCrossVmEvent
@ stdcall NtCreateEnlistment(ptr long ptr ptr ptr long long ptr)
@ stdcall NtCreateResourceManager(ptr long ptr ptr ptr long ptr)
@ stdcall NtCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr)
@ stdcall NtCreateTransactionManager(ptr long ptr ptr long long)
@ stdcall NtEnumerateTransactionObject(ptr long ptr long ptr)
@ stub -arch=win64 NtFreezeTransactions
@ stub -arch=win64 NtGetEnvironmentVariableEx
@ stdcall NtGetNotificationResourceManager(ptr ptr long ptr ptr long ptr)
@ stub -arch=win64 NtImageInfo
@ stdcall -arch=win64 NtNotifyChangeDirectoryFileEx(ptr ptr ptr ptr ptr ptr long long long long)
@ stdcall NtOpenEnlistment(ptr long ptr ptr ptr)
@ stdcall NtOpenResourceManager(ptr long ptr ptr ptr)
@ stdcall NtOpenTransaction(ptr long ptr ptr ptr)
@ stdcall NtOpenTransactionManager(ptr long ptr ptr ptr long)
@ stdcall NtPrePrepareComplete(ptr ptr)
@ stdcall NtPrePrepareEnlistment(ptr ptr)
@ stdcall NtPrepareComplete(ptr ptr)
@ stdcall NtPrepareEnlistment(ptr ptr)
@ stdcall NtPropagationComplete(ptr long long ptr)
@ stdcall NtPropagationFailed(ptr long long)
@ stdcall -arch=win64 NtQueryDirectoryFileEx(ptr ptr ptr ptr ptr ptr long long long ptr)
@ stub -arch=win64 NtQueryEnvironmentVariableInfoEx
@ stdcall -arch=win64 NtQueryInformationByName(ptr ptr ptr long long)
@ stdcall NtQueryInformationEnlistment(ptr long ptr long ptr)
@ stdcall NtQueryInformationResourceManager(ptr long ptr long ptr)
@ stdcall NtQueryInformationTransaction(ptr long ptr long ptr)
@ stdcall NtQueryInformationTransactionManager(ptr long ptr long ptr)
@ stub -arch=win64 NtQuerySecurityAttributesToken
@ stdcall -version=0x601+ -arch=win64 NtQuerySystemInformationEx(long ptr long ptr long ptr)
@ stdcall NtReadOnlyEnlistment(ptr ptr)
@ stdcall NtRecoverEnlistment(ptr ptr)
@ stdcall NtRecoverResourceManager(ptr)
@ stdcall NtRecoverTransactionManager(ptr)
@ stdcall NtRollbackComplete(ptr ptr)
@ stdcall NtRollbackEnlistment(ptr ptr)
@ stdcall NtRollbackTransaction(ptr long)
@ stub -arch=win64 NtSetCachedSigningLevel
@ stdcall NtSetInformationEnlistment(ptr long ptr long)
@ stdcall NtSetInformationResourceManager(ptr long ptr long)
@ stdcall NtSetInformationTransaction(ptr long ptr long)
@ stdcall -arch=win64 NtSetInformationVirtualMemory(ptr long ptr ptr ptr long)
@ stub -arch=win64 NtThawTransactions
@ stub -arch=win64 NtTraceControl
@ stdcall -arch=arm64 NtWaitForAlertByThreadId(ptr ptr)
@ stub -arch=win64 ObDereferenceObjectDeferDeleteWithTag
@ stdcall ObGetFilterVersion()
@ stub -arch=win64 ObIsDosDeviceLocallyMapped
@ stub -arch=win64 ObOpenObjectByNameEx
@ stub -arch=win64 ObOpenObjectByPointerWithTag
@ stdcall -arch=i386,win64 ObReferenceObjectByHandleWithTag(ptr long ptr long long ptr ptr)
@ stub -arch=win64 ObReferenceObjectByPointerWithTag
@ stub -arch=win64 ObReferenceObjectSafeWithTag
@ stdcall ObRegisterCallbacks(ptr ptr)
@ stdcall ObUnRegisterCallbacks(ptr)
@ stdcall -arch=i386,win64 ObWaitForMultipleObjects(long ptr long long long long ptr)
@ stdcall -arch=i386,win64 ObWaitForSingleObject(ptr long long long ptr)
@ fastcall -arch=i386,win64 ObfDereferenceObjectWithTag(ptr long)
@ fastcall -arch=i386,win64 ObfReferenceObjectWithTag(ptr long)
@ stdcall -arch=win64 PcwAddInstance(ptr ptr long long ptr)
@ stdcall -arch=i386,win64 PcwCloseInstance(ptr)
@ stdcall -arch=i386,win64 PcwCreateInstance(ptr ptr ptr long ptr)
@ stdcall -arch=win64 PcwRegister(ptr ptr)
@ stdcall -arch=win64 PcwUnregister(ptr)
@ stub -arch=win64 PfFileInfoNotify
@ stub -arch=win64 PoClearPowerRequest
@ stub -arch=win64 PoCpuIdledSinceLastCallImprecise
@ stdcall PoCreatePowerLimitRequest(ptr ptr ptr ptr)
@ stub -arch=win64 PoCreatePowerRequest
@ stdcall PoDeletePowerLimitRequest(ptr)
@ stub -arch=win64 PoDeletePowerRequest
@ stub -arch=arm64 PoDirectedDripsClearDeviceFlags
@ stub -arch=arm64 PoDirectedDripsSetDeviceFlags
@ stub -arch=win64 PoDisableSleepStates
@ stub -arch=win64 PoEndDeviceBusy
@ stub -arch=win64 PoEnergyEstimationEnabled
@ stub -arch=arm64 PoFxAddDeviceRelation
@ stub -arch=win64 PoFxEnableDStateReporting
@ stdcall -version=0xA00+ PoFxIssueComponentPerfStateChange(ptr long long ptr ptr)
@ stub -arch=win64 PoFxIssueComponentPerfStateChangeMultiple
@ stub -arch=win64 PoFxNotifySurprisePowerOn
@ stdcall -version=0x603+ PoFxPowerOnCrashdumpDevice(ptr ptr)
@ stub -arch=win64 PoFxProcessorNotification
@ stdcall -version=0xA00+ PoFxQueryCurrentComponentPerfState(ptr long long long ptr)
@ stdcall -version=0xA00+ PoFxRegisterComponentPerfStates(ptr long int64 ptr ptr ptr)
@ stub -arch=win64 PoFxRegisterCoreDevice
@ stdcall -version=0x603+ PoFxRegisterCrashdumpDevice(ptr)
@ stub -arch=win64 PoFxRegisterDripsWatchdogCallback
@ stdcall -version=0xA00+ PoFxRegisterPlugin(ptr ptr)
@ stdcall -version=0xA00+ PoFxRegisterPluginEx(ptr int64 ptr)
@ stub -arch=win64 PoFxRegisterPrimaryDevice
@ stub -arch=arm64 PoFxRemoveDeviceRelation
@ stub -arch=win64 PoFxSetComponentWake
@ stub -arch=win64 PoFxSetTargetDripsDevicePowerState
@ stub -arch=arm64 PoGetProcessorIdleAccounting
@ stub -arch=arm64 PoInitiateProcessorWake
@ stdcall -arch=i386,win64 PoLatencySensitivityHint(long)
@ stub -arch=win64 PoNotifyMediaBuffering
@ stdcall -arch=i386,win64 PoNotifyVSyncChange(long)
@ stdcall PoQueryPowerLimitAttributes(ptr long ptr ptr)
@ stdcall PoQueryPowerLimitValue(ptr long ptr)
@ stub -arch=win64 PoReenableSleepStates
@ stub -arch=win64 PoRegisterCoalescingCallback
@ stub -arch=win64 PoRegisterForEffectivePowerModeNotifications
@ stub -arch=win64 PoSetDeviceBusyEx
@ stub -arch=win64 PoSetFixedWakeSource
@ stub -arch=win64 PoSetPowerButtonHoldState
@ stdcall PoSetPowerLimitValue(ptr ptr long ptr)
@ stub -arch=win64 PoSetPowerRequest
@ stub -arch=win64 PoSetSystemWakeDevice
@ stdcall -arch=i386,win64 PoSetUserPresent(long)
@ stub -arch=win64 PoStartDeviceBusy
@ stub -arch=win64 PoUnregisterCoalescingCallback
@ stub -arch=win64 PoUnregisterFromEffectivePowerModeNotifications
@ stub -arch=win64 PoUserShutdownCancelled
@ stub -arch=win64 PoUserShutdownInitiated
@ stdcall -arch=i386,win64 PsAcquireProcessExitSynchronization(ptr)
@ stub -arch=win64 PsAcquireSiloHardReference
@ stdcall -arch=i386,win64 PsAdjustWin32kPriorityFloor(ptr long)
@ stub -arch=win64 PsAllocSiloContextSlot
@ stub -arch=win64 PsAllocateAffinityToken
@ stub -arch=win64 PsAssignProcessToJobObject
@ stdcall -arch=i386,win64 PsAttachSiloToCurrentThread(ptr)
@ stub -arch=win64 PsChargeProcessWakeCounter
@ stub -arch=win64 PsCheckProcessFileSigningLevel
@ stub -arch=win64 PsCreateSiloContext
@ stub -arch=win64 PsCreateSystemThreadEx
@ stdcall -arch=i386,win64 PsDereferenceKernelStack(ptr)
@ stub -arch=win64 PsDereferenceSiloContext
@ stdcall -arch=i386,win64 PsDetachSiloFromCurrentThread(ptr)
@ stdcall -arch=i386,win64 PsEnterPriorityRegion()
@ stub -arch=win64 PsFreeAffinityToken
@ stub -arch=win64 PsFreeSiloContextSlot
@ stdcall -arch=i386,win64 PsGetCurrentServerSilo()
@ stub -arch=win64 PsGetCurrentServerSiloName
@ stub -arch=win64 PsGetCurrentSilo
@ stub -arch=win64 PsGetEffectiveContainerId
@ stub -arch=win64 PsGetEffectiveServerSilo
@ stdcall -arch=i386,win64 PsGetHostSilo()
@ stub -arch=win64 PsGetJobProperty
@ stub -arch=win64 PsGetJobServerSilo
@ stub -arch=win64 PsGetJobSilo
@ stub -arch=win64 PsGetParentSilo
@ stub -arch=win64 PsGetPermanentSiloContext
@ stub -arch=win64 PsGetProcessActiveThreadCount
@ stdcall -arch=i386,win64 PsGetProcessCommonJob(ptr ptr)
@ stdcall -arch=i386,win64 PsGetProcessDxgProcess(ptr)
@ stdcall -arch=i386,win64 PsGetProcessMachine(ptr)
@ stub -arch=win64 PsGetProcessProtection
@ stdcall -arch=i386,win64 PsGetProcessSequenceNumber(ptr)
@ stdcall -arch=i386,win64 PsGetProcessServerSilo(ptr)
@ stub -arch=win64 PsGetProcessSignatureLevel
@ stub -arch=win64 PsGetProcessSilo
@ stdcall -version=0xA00+ -arch=i386,win64 PsGetProcessStartKey(ptr)
@ stdcall -arch=i386,win64 PsGetServerSiloServiceSessionId(ptr)
@ stub -arch=win64 PsGetSiloContainerId
@ stub -arch=win64 PsGetSiloContext
@ stub -arch=win64 PsGetSiloIdentifier
@ stub -arch=win64 PsGetSiloMonitorContextSlot
@ stub -arch=win64 PsGetThreadCreateTime
@ stub -arch=win64 PsGetThreadExitStatus
@ stub -arch=win64 PsGetThreadProperty
@ stub -arch=win64 PsGetThreadServerSilo
@ stdcall -arch=i386,win64 PsGetWin32KFilterSet()
@ stub -arch=win64 PsInsertPermanentSiloContext
@ stub -arch=win64 PsInsertSiloContext
@ stub -arch=win64 PsIsComponentEnabled
@ stdcall -arch=i386,win64 PsIsCurrentThreadInServerSilo()
@ stub -arch=win64 PsIsCurrentThreadPrefetching
@ stdcall -arch=i386,win64 PsIsHostSilo(ptr)
@ stdcall -arch=i386,win64 PsIsProcessCommitRelinquished(ptr)
@ stub -arch=win64 PsIsProcessInAppSilo
@ stdcall -arch=i386,win64 PsIsProtectedProcess(ptr)
@ stdcall -arch=i386,win64 PsIsProtectedProcessLight(ptr)
@ stdcall -arch=i386,win64 PsIsWin32KFilterAuditEnabled()
@ stdcall -arch=i386,win64 PsIsWin32KFilterAuditEnabledForProcess(ptr)
@ stdcall -arch=i386,win64 PsIsWin32KFilterEnabled()
@ stdcall -arch=i386,win64 PsIsWin32KFilterEnabledForProcess(ptr)
@ stdcall -arch=i386,win64 PsLeavePriorityRegion()
@ stub -arch=win64 PsMakeSiloContextPermanent
@ stdcall -arch=i386,win64 PsQueryCurrentApiSetSchema()
@ stdcall -arch=i386,win64 PsQueryProcessAttributesByToken(ptr ptr ptr)
@ stub -arch=win64 PsQueryProcessAvailableCpus
@ stub -arch=win64 PsQueryProcessAvailableCpusCount
@ stub -arch=win64 PsQueryProcessCommandLine
@ stub -arch=win64 PsQueryProcessExceptionFlags
@ stub -arch=win64 PsQuerySyscallProviderInformation
@ stub -arch=win64 PsQuerySystemAvailableCpus
@ stub -arch=win64 PsQuerySystemAvailableCpusCount
@ stub -arch=win64 PsQueryTotalCycleTimeProcess
@ stdcall -arch=i386,win64 PsReferenceKernelStack(ptr)
@ stub -arch=win64 PsReferenceSiloContext
@ stub -arch=win64 PsRegisterAltSystemCallHandler
@ stub -arch=win64 PsRegisterPicoProvider
@ stub -arch=win64 PsRegisterProcessAvailableCpusChangeNotification
@ stub -arch=win64 PsRegisterSiloMonitor
@ stub -arch=win64 PsRegisterSyscallProvider
@ stub -arch=win64 PsRegisterSystemAvailableCpusChangeNotification
@ stdcall -arch=i386,win64 PsReleaseProcessExitSynchronization(ptr)
@ stub -arch=win64 PsReleaseProcessWakeCounter
@ stub -arch=win64 PsReleaseSiloHardReference
@ stub -arch=win64 PsRemoveSiloContext
@ stub -arch=win64 PsReplaceSiloContext
@ stub -arch=win64 PsRevertToUserMultipleGroupAffinityThread
@ stdcall -version=0xA00+ -arch=i386,win64 PsSetCreateProcessNotifyRoutineEx2(long ptr long)
@ stub -arch=win64 PsSetCreateThreadNotifyRoutineEx
@ stub -arch=win64 PsSetCurrentThreadPrefetching
@ stub -arch=win64 PsSetJobProperty
@ stub -arch=win64 PsSetLoadImageNotifyRoutineEx
@ stdcall -arch=i386,win64 PsSetProcessDxgProcess(ptr ptr)
@ stdcall -arch=i386,win64 PsSetProcessFaultInformation(ptr ptr)
@ stdcall -arch=i386,win64 PsSetProcessesWindowState(long ptr)
@ stub -arch=win64 PsSetSystemMultipleGroupAffinityThread
@ stub -arch=win64 PsSetThreadProperty
@ stub -arch=win64 PsStartSiloMonitor
@ stub -arch=win64 PsTerminateServerSilo
@ stdcall -arch=win64 PsTlsAlloc(ptr long ptr)
@ stdcall -arch=win64 PsTlsFree(long)
@ stdcall -arch=win64 PsTlsGetValue(long ptr)
@ stdcall -arch=win64 PsTlsSetValue(long ptr)
@ stdcall -arch=i386,win64 PsUnEstablishWin32Callouts()
@ stub -arch=win64 PsUnregisterAvailableCpusChangeNotification
@ stub -arch=win64 PsUnregisterSiloMonitor
@ stub -arch=win64 PsUnregisterSyscallProvider
@ stdcall -arch=win64 PsUpdateComponentPower(ptr long int64)
@ stub -arch=win64 PsUpdateNetworkCounters
@ stdcall -arch=i386,win64 PsWow64GetProcessMachine(ptr)
@ stub -arch=win64 PsWow64IsMachineSupported
@ stub -arch=arm64 ReadTimeStampCounter
@ stub -arch=win64 RtlAddAccessFilterAce
@ stdcall -arch=win64 RtlAddAtomToAtomTableEx(ptr wstr ptr long)
@ stdcall -arch=win64 RtlAddMandatoryAce(ptr long long long long ptr)
@ stub -arch=win64 RtlAddProcessTrustLabelAce
@ stub -arch=win64 RtlAddResourceAttributeAce
@ stdcall -arch=win64 RtlAreBitsClearEx(ptr int64 int64) RtlAreBitsClear64
@ stub -arch=win64 RtlAreBitsSetEx
@ stdcall RtlArmFeatureUsageProviderFlushNotification(ptr)
@ stdcall -arch=i386,win64 RtlAvlInsertNodeEx(ptr ptr long ptr)
@ stdcall -arch=i386,win64 RtlAvlRemoveNode(ptr ptr)
@ stdcall -arch=i386,win64 RtlCapabilityCheck(ptr ptr ptr)
@ stdcall -arch=i386,win64 RtlCapabilityCheckForSingleSessionSku(ptr ptr ptr)
@ stub -arch=win64 RtlCheckPortableOperatingSystem
@ stub -arch=win64 RtlCheckSystemBootStatusIntegrity
@ stub -arch=win64 RtlCheckTokenCapability
@ stdcall -arch=i386,win64 RtlCheckTokenMembership(ptr ptr ptr)
@ stub -arch=win64 RtlCheckTokenMembershipEx
@ stdcall -arch=win64 RtlClearAllBitsEx(ptr) RtlClearAllBits64
@ stdcall -arch=win64 RtlClearBitEx(ptr int64) RtlClearBit64
@ stub -arch=win64 RtlClearBitsEx
@ stdcall RtlCmDecodeMemIoResource(ptr ptr)
@ stdcall RtlCmEncodeMemIoResource(ptr long int64 int64)
@ stub -arch=win64 RtlCompareAltitudes
@ stub -arch=win64 RtlCompareExchangePointerMapping
@ stub -arch=win64 RtlCompareExchangePropertyStore
@ stdcall RtlCompareUnicodeStrings(wstr long wstr long long)
@ stub -arch=win64 RtlConstructCrossVmEventPath
@ stub -arch=win64 RtlConstructCrossVmMutexPath
@ stdcall -arch=i386,win64 RtlConvertHostPerfCounterToPerfCounter(int64 int64 ptr)
@ stdcall -arch=i386,win64 RtlCopyBitMap(ptr ptr long)
@ stdcall -arch=win64 RtlCopyBitMapEx(ptr ptr int64) RtlCopyBitMap64
@ stub -arch=win64 RtlCopyContext
@ stub -arch=win64 RtlCopyExtendedContext
@ stdcall RtlCrc32(ptr long long)
@ stdcall RtlCrc64(ptr long int64)
@ stdcall -arch=win64 RtlCreateAtomTableEx(long long ptr)
@ stub -arch=win64 RtlCreateHashTableEx
@ stub -arch=win64 RtlDecompressBufferEx
@ stub -arch=win64 RtlDecompressBufferEx2
@ stub -arch=win64 RtlDecompressFragmentEx
@ stub -arch=win64 RtlDeleteElementGenericTableAvlEx
@ stub -arch=win64 RtlDeriveCapabilitySidsFromName
@ stub -arch=win64 RtlDrainNonVolatileFlush
@ stub -arch=win64 RtlEndStrongEnumerationHashTable
@ stub -arch=win64 RtlEqualWnfChangeStamps
@ stdcall RtlEthernetAddressToStringA(ptr ptr)
@ stdcall RtlEthernetAddressToStringW(ptr ptr)
@ stdcall RtlEthernetStringToAddressA(str ptr ptr)
@ stdcall RtlEthernetStringToAddressW(wstr ptr ptr)
@ stub -arch=win64 RtlExtendCorrelationVector
@ stub -arch=win64 RtlExtractBitMap
@ stub -arch=win64 RtlExtractBitMapEx
@ stub -arch=win64 RtlFillMemoryNonTemporal
@ stub -arch=win64 RtlFillNonVolatileMemory
@ stub -arch=win64 RtlFindAceByType
@ stub -arch=win64 RtlFindClearBitsAndSetEx
@ stub -arch=win64 RtlFindClearBitsEx
@ stub -arch=win64 RtlFindClosestEncodableLength
@ stub -arch=win64 RtlFindNextForwardRunClearCapped
@ stub -arch=win64 RtlFindNextForwardRunClearEx
@ stub -arch=win64 RtlFindNextForwardRunSetEx
@ stub -arch=win64 RtlFindSetBitsAndClearEx
@ stdcall -arch=win64 RtlFindSetBitsEx(ptr int64 int64) RtlFindSetBits64
@ stdcall -arch=i386,win64 RtlFindUnicodeSubstring(ptr ptr long)
@ stub -arch=win64 RtlFlushFeatureUsage
@ stub -arch=win64 RtlFlushNonVolatileMemory
@ stub -arch=win64 RtlFlushNonVolatileMemoryRanges
@ stub -arch=win64 RtlFreeNonVolatileToken
@ stub -arch=win64 RtlFreeUTF8String
@ stdcall -arch=i386,win64 RtlGenerateClass5Guid(ptr ptr long ptr)
@ stdcall -arch=i386,win64 RtlGetAcesBufferSize(ptr ptr)
@ stdcall -arch=i386,win64 RtlGetActiveConsoleId()
@ stub -arch=win64 RtlGetAppContainerNamedObjectPath
@ stdcall -arch=win64 RtlGetAppContainerParent(ptr ptr)
@ stdcall -arch=i386,win64 RtlGetAppContainerSidType(ptr ptr)
@ stdcall -arch=i386,win64 RtlGetConsoleSessionForegroundProcessId()
@ stdcall -arch=i386,win64 RtlGetCurrentServiceSessionId()
@ stub -arch=win64 RtlGetEnabledExtendedAndSupervisorFeatures
@ stdcall -arch=win64 RtlGetEnabledExtendedFeatures(int64)
@ stub -arch=win64 RtlGetExtendedContextLength
@ stdcall -arch=i386,win64 RtlGetIntegerAtom(wstr ptr)
@ stub -arch=win64 RtlGetLastRange
@ stdcall -version=0xA00+ -arch=i386,win64 RtlGetMultiTimePrecise(ptr long ptr)
@ stub -arch=win64 RtlGetNonVolatileToken
@ stdcall -arch=i386,win64 RtlGetNtSystemRoot()
@ stub -arch=win64 RtlGetPersistedStateLocation
@ stdcall -arch=i386,win64 RtlGetProductInfo(long long long long ptr)
@ stub -arch=win64 RtlGetSessionProperties
@ stdcall -arch=i386,win64 RtlGetSuiteMask()
@ stub -arch=win64 RtlGetSystemBootStatus
@ stub -arch=win64 RtlGetSystemBootStatusEx
@ stdcall -arch=i386,win64 RtlGetSystemGlobalData(long ptr long)
@ stdcall -arch=i386,win64 RtlGetThreadLangIdByIndex(long long ptr ptr)
@ stub -arch=win64 RtlGetTokenNamedObjectPath
@ stub -arch=win64 RtlIdnToAscii
@ stub -arch=win64 RtlIdnToNameprepUnicode
@ stub -arch=win64 RtlIdnToUnicode
@ stub -arch=win64 RtlIncrementCorrelationVector
@ stub -arch=win64 RtlInitStringEx
@ stub -arch=win64 RtlInitStrongEnumerationHashTable
@ stub -arch=win64 RtlInitUTF8String
@ stub -arch=win64 RtlInitUTF8StringEx
@ stdcall -arch=win64 RtlInitializeBitMapEx(ptr ptr int64) RtlInitializeBitMap64
@ stub -arch=win64 RtlInitializeCorrelationVector
@ stub -arch=win64 RtlInitializeExtendedContext
@ varargs -arch=i386,win64 RtlInitializeSidEx(ptr ptr long)
@ stub -arch=win64 RtlInterlockedClearBitRun
@ stub -arch=win64 RtlInterlockedClearBitRunEx
@ stub -arch=win64 RtlInterlockedSetBitRun
@ stub -arch=win64 RtlInterlockedSetBitRunEx
@ stub -arch=win64 RtlInterlockedSetClearRun
@ stdcall -arch=i386,win64 RtlIntersectBitMaps(ptr ptr)
@ stdcall -arch=win64 RtlIntersectBitMapsEx(ptr ptr) RtlIntersectBitMaps64
@ stub -arch=win64 RtlInvertRangeListEx
@ stub -arch=win64 RtlIoDecodeMemIoResource
@ stub -arch=win64 RtlIoEncodeMemIoResource
@ stdcall -arch=i386,win64 RtlIsApiSetImplemented(str)
@ stub -arch=win64 RtlIsCloudFilesPlaceholder
@ stub -arch=win64 RtlIsElevatedRid
@ stub -arch=win64 RtlIsFunctionalityAvailable
@ stdcall -arch=i386,win64 RtlIsMultiSessionSku()
@ stub -arch=win64 RtlIsMultiUsersInSessionSku
@ stub -arch=win64 RtlIsNonEmptyDirectoryReparsePointAllowed
@ stub -arch=win64 RtlIsNormalizedString
@ stdcall RtlIsNtDdiVersionAvailable(long)
@ stub -arch=win64 RtlIsPartialPlaceholder
@ stub -arch=win64 RtlIsPartialPlaceholderFileHandle
@ stub -arch=win64 RtlIsPartialPlaceholderFileInfo
@ stub -arch=win64 RtlIsProcessorFeaturePresent
@ stub -arch=win64 RtlIsSandboxedToken
@ stdcall RtlIsServicePackVersionInstalled(long)
@ stdcall -arch=i386,win64 RtlIsStateSeparationEnabled()
@ stub -arch=win64 RtlIsUntrustedObject
@ stdcall -arch=i386,win64 RtlIsZeroMemory(ptr ptr)
@ stub -arch=win64 RtlLoadString
@ stub -arch=win64 RtlLocateSupervisorFeature
@ stdcall -arch=i386,win64 RtlLogUnexpectedCodepath()
@ stub -arch=win64 RtlMergeBitMaps
@ stub -arch=win64 RtlMergeBitMapsEx
@ stub -arch=win64 RtlNormalizeSecurityDescriptor
@ stub -arch=win64 RtlNormalizeString
@ stdcall RtlNotifyFeatureUsage(ptr)
@ stub -arch=win64 RtlNumberOfClearBitsEx
@ stub -arch=win64 RtlNumberOfClearBitsInRange
@ stub -arch=win64 RtlNumberOfSetBitsEx
@ stdcall -arch=i386,win64 RtlNumberOfSetBitsInRange(ptr long long)
@ stdcall -arch=win64 RtlNumberOfSetBitsInRangeEx(ptr int64 int64) RtlNumberOfSetBitsInRange64
@ stdcall RtlNumberOfSetBitsUlongPtr(long)
@ stub -arch=win64 RtlOpenImageFileOptionsKey
@ stub -arch=win64 RtlOsDeploymentState
@ stub -arch=win64 RtlOwnerAcesPresent
@ stub -arch=arm64 RtlPcToFileName
@ stub -arch=arm64 RtlPcToFilePath
@ stub -arch=win64 RtlQueryAllFeatureConfigurations
@ stub -arch=arm64 RtlQueryAllInternalFeatureConfigurations
@ stdcall RtlQueryDynamicTimeZoneInformation(ptr)
@ stdcall -arch=i386,win64 RtlQueryElevationFlags(ptr)
@ stdcall RtlQueryFeatureConfiguration(long long ptr ptr)
@ stdcall RtlQueryFeatureConfigurationChangeStamp()
@ stub -arch=win64 RtlQueryImageFileKeyOption
@ stdcall RtlQueryModuleInformation(ptr long ptr)
@ stdcall -arch=i386,win64 RtlQueryPackageClaims(ptr ptr ptr ptr ptr ptr ptr)
@ stdcall -arch=i386,win64 RtlQueryPackageIdentity(ptr ptr ptr ptr ptr ptr)
@ stub -arch=win64 RtlQueryPackageIdentityEx
@ stub -arch=win64 RtlQueryPointerMapping
@ stub -arch=win64 RtlQueryProcessPlaceholderCompatibilityMode
@ stub -arch=win64 RtlQueryPropertyStore
@ stub -arch=win64 RtlQueryRegistryValueWithFallback
@ stdcall -arch=win64 RtlQueryRegistryValuesEx(long wstr ptr ptr ptr)
@ stub -arch=win64 RtlQueryThreadPlaceholderCompatibilityMode
@ stub -arch=win64 RtlQueryValidationRunlevel
@ stub -arch=win64 RtlRaiseCustomSystemEventTrigger
@ stub -arch=win64 RtlRbInsertNodeEx
@ stub -arch=win64 RtlRbRemoveNode
@ stub -arch=win64 RtlRbReplaceNode
@ stdcall RtlRecordFeatureUsage(long long long ptr)
@ stdcall RtlRegisterFeatureConfigurationChangeNotification(ptr ptr ptr ptr)
@ stdcall RtlRegisterFeatureUsageProvider(ptr ptr)
@ stub -arch=win64 RtlRemovePointerMapping
@ stub -arch=win64 RtlRemovePropertyStore
@ stub -arch=win64 RtlReplaceSidInSd
@ stub -arch=win64 RtlRestoreSystemBootStatusDefaults
@ stdcall -arch=i386,win64 RtlRunOnceBeginInitialize(ptr long ptr)
@ stdcall -arch=i386,win64 RtlRunOnceComplete(ptr long ptr)
@ stdcall -arch=i386,win64 RtlRunOnceExecuteOnce(ptr ptr ptr ptr)
@ stdcall -arch=i386,win64 RtlRunOnceInitialize(ptr)
@ stdcall -arch=i386,win64 RtlSetActiveConsoleId(long)
@ stub -arch=win64 RtlSetAllBitsEx
@ stdcall -arch=win64 RtlSetBitEx(ptr int64) RtlSetBit64
@ stub -arch=win64 RtlSetBitsEx
@ stdcall -arch=i386,win64 RtlSetConsoleSessionForegroundProcessId(int64)
@ stub -arch=win64 RtlSetDynamicTimeZoneInformation
@ stub -arch=win64 RtlSetPortableOperatingSystem
@ stub -arch=win64 RtlSetProcessPlaceholderCompatibilityMode
@ stub -arch=win64 RtlSetSystemBootStatus
@ stub -arch=win64 RtlSetSystemBootStatusEx
@ stub -arch=win64 RtlSetSystemGlobalData
@ stub -arch=win64 RtlSetThreadPlaceholderCompatibilityMode
@ stub -arch=win64 RtlShiftLeftBitMap
@ stub -arch=win64 RtlShiftLeftBitMapEx
@ stub -arch=win64 RtlSidHashInitialize
@ stub -arch=win64 RtlSidHashLookup
@ stdcall RtlStringFromGUIDEx(ptr ptr long)
@ stub -arch=win64 RtlStronglyEnumerateEntryHashTable
@ stdcall RtlSuffixUnicodeString(ptr ptr long)
@ stub -arch=win64 RtlTestBitEx
@ stub -arch=win64 RtlUTF8StringToUnicodeString
@ stub -arch=arm64 RtlUdiv128
@ stub -arch=win64 RtlUnicodeStringToInt64
@ stub -arch=win64 RtlUnicodeStringToUTF8String
@ stdcall RtlUnregisterFeatureConfigurationChangeNotification(ptr)
@ stdcall RtlUnregisterFeatureUsageProvider(ptr)
@ stub -arch=arm64 RtlUnsignedMultiplyHigh
@ stub -arch=win64 RtlValidateCorrelationVector
@ stdcall -arch=win64 RtlVirtualUnwind2(long int64 int64 ptr ptr ptr ptr ptr ptr ptr ptr ptr long)
@ stub -arch=win64 RtlWriteNonVolatileMemory
@ stub -arch=win64 SeAccessCheckEx
@ stub -arch=win64 SeAccessCheckFromState
@ stub -arch=win64 SeAccessCheckFromStateEx
@ stub -arch=win64 SeAccessCheckWithHint
@ stub -arch=win64 SeAdjustAccessStateForAccessConstraints
@ stub -arch=win64 SeAdjustAccessStateForTrustLabel
@ stub -arch=win64 SeAdjustObjectSecurity
@ stub -arch=win64 SeAuditFipsCryptoSelftests
@ stub -arch=win64 SeAuditHardLinkCreationWithTransaction
@ stub -arch=win64 SeAuditTransactionStateChange
@ stub -arch=win64 SeAuditingAnyFileEventsWithContext
@ stub -arch=win64 SeAuditingAnyFileEventsWithContextEx
@ stub -arch=win64 SeAuditingFileEventsWithContextEx
@ stub -arch=win64 SeAuditingWithTokenForSubcategory
@ stub -arch=win64 SeCheckForCriticalAceRemoval
@ stub -arch=win64 SeCloseObjectAuditAlarmForNonObObject
@ stub -arch=win64 SeCompareSigningLevels
@ stub -arch=win64 SeComputeAutoInheritByObjectType
@ stub -arch=win64 SeConvertSecurityDescriptorToStringSecurityDescriptor
@ stub -arch=win64 SeConvertSidToStringSid
@ stdcall -arch=i386,win64 SeConvertStringSecurityDescriptorToSecurityDescriptor(wstr long ptr ptr)
@ stub -arch=win64 SeConvertStringSidToSid
@ stub -arch=win64 SeCreateAndRegisterAccessCheckDebugContext
@ stub -arch=win64 SeCreateClientSecurityEx
@ stub -arch=win64 SeCreateClientSecurityFromSubjectContextEx
@ stdcall -arch=i386,win64 SeDeleteClientSecurity(ptr)
@ stub -arch=win64 SeDeleteObjectAuditAlarmWithTransaction
@ stub -arch=win64 SeEtwWriteKMCveEvent
@ stub -arch=win64 SeExamineSacl
@ stub -arch=win64 SeGetCachedSigningLevel
@ stub -arch=win64 SeGetLinkedToken
@ stub -arch=win64 SeGetLogonSessionToken
@ stdcall -arch=i386,win64 SeIsParentOfChildAppContainer(long long long)
@ stub -arch=win64 SeMarkLogonSessionForTerminationNotificationEx
@ stub -arch=win64 SeOpenObjectAuditAlarmForNonObObject
@ stub -arch=win64 SeOpenObjectAuditAlarmWithTransaction
@ stub -arch=win64 SeOpenObjectForDeleteAuditAlarmWithTransaction
@ stub -arch=win64 SeQuerySecureBootPlatformManifest
@ stub -arch=win64 SeQuerySecureBootPolicyValue
@ stub -arch=win64 SeQuerySecurityAttributesToken
@ stub -arch=win64 SeQuerySecurityAttributesTokenAccessInformation
@ stub -arch=win64 SeQueryServerSiloToken
@ stub -arch=win64 SeQuerySessionIdTokenEx
@ stub -arch=win64 SeRegisterImageVerificationCallback
@ stub -arch=win64 SeRegisterLogonSessionTerminatedRoutineEx
@ stub -arch=win64 SeReportSecurityEventWithSubCategory
@ stdcall -arch=i386,win64 SeSecurityAttributePresent(ptr ptr)
@ stub -arch=win64 SeSetSecurityAttributesToken
@ stub -arch=win64 SeSetSecurityAttributesTokenEx
@ stub -arch=win64 SeSetSessionIdTokenWithLinked
@ stub -arch=win64 SeShouldCheckForAccessRightsFromParent
@ stub -arch=win64 SeSrpAccessCheck
@ stub -arch=win64 SeTokenFromAccessInformation
@ stub -arch=win64 SeUnRegisterAndFreeAccessCheckDebugContext
@ stub -arch=win64 SeUnregisterImageVerificationCallback
@ stub -arch=win64 SeUnregisterLogonSessionTerminatedRoutineEx
@ stub -arch=win64 SkAcquirePushLockExclusive
@ stub -arch=win64 SkAllocatePool
@ stub -arch=win64 SkFreePool
@ stub -arch=win64 SkInitializePushLock
@ stub -arch=win64 SkIsSecureKernel
@ stub -arch=win64 SkQuerySecureKernelInformation
@ stub -arch=win64 SkReleasePushLockExclusive
@ stub -arch=win64 TmCancelPropagationRequest
@ stdcall TmCommitComplete(ptr ptr)
@ stdcall TmCommitEnlistment(ptr ptr)
@ stdcall TmCommitTransaction(ptr long)
@ stdcall TmCreateEnlistment(ptr long long ptr ptr ptr long long ptr)
@ stub -arch=win64 TmCurrentTransaction
@ stdcall TmDereferenceEnlistmentKey(ptr ptr)
@ stdcall TmEnableCallbacks(ptr ptr ptr)
@ stub -arch=win64 TmEndPropagationRequest
@ stub -arch=win64 TmFreezeTransactions
@ stdcall TmGetTransactionId(ptr ptr)
@ stub -arch=win64 TmInitSystem
@ stub -arch=win64 TmInitSystemPhase2
@ stdcall TmInitializeTransactionManager(ptr ptr ptr long)
@ stub -arch=win64 TmIsKTMCommitCoordinator
@ stdcall TmIsTransactionActive(ptr)
@ stdcall TmPrePrepareComplete(ptr ptr)
@ stdcall TmPrePrepareEnlistment(ptr ptr)
@ stdcall TmPrepareComplete(ptr ptr)
@ stdcall TmPrepareEnlistment(ptr ptr)
@ stdcall TmPropagationComplete(ptr long long ptr)
@ stdcall TmPropagationFailed(ptr long long)
@ stdcall TmReadOnlyEnlistment(ptr ptr)
@ stdcall TmRecoverEnlistment(ptr ptr)
@ stdcall TmRecoverResourceManager(ptr)
@ stdcall TmRecoverTransactionManager(ptr ptr)
@ stdcall TmReferenceEnlistmentKey(ptr ptr)
@ stdcall TmRenameTransactionManager(ptr ptr)
@ stdcall TmRequestOutcomeEnlistment(ptr ptr)
@ stdcall TmRollbackComplete(ptr ptr)
@ stdcall TmRollbackEnlistment(ptr ptr)
@ stdcall TmRollbackTransaction(ptr long)
@ stub -arch=win64 TmSetCurrentTransaction
@ stdcall TmSinglePhaseReject(ptr ptr)
@ stub -arch=win64 TmThawTransactions
@ stdcall -arch=i386,win64 TtmNotifyDeviceArrival(long ptr ptr long ptr)
@ stdcall -arch=i386,win64 TtmNotifyDeviceDeparture(long ptr)
@ stdcall -arch=i386,win64 TtmNotifyDeviceInput(long ptr long)
@ stub -arch=win64 VfInsertContext
@ stub -arch=win64 VfQueryDeviceContext
@ stub -arch=win64 VfQueryDispatchTable
@ stub -arch=win64 VfQueryDriverContext
@ stub -arch=win64 VfQueryIrpContext
@ stub -arch=win64 VfRemoveContext
@ stub -arch=win64 VslCreateSecureSection
@ stub -arch=win64 VslDeleteSecureSection
@ stub -arch=win64 VslExchangeEntropy
@ stub -arch=arm64 VslGetSecurePciDeviceAlternateFunctionNumberForVtl0Dma
@ stub -arch=arm64 VslGetSecurePciDeviceBootConfiguration
@ stub -arch=arm64 VslGetSecurePciEnabled
@ stub -arch=arm64 VslQuerySecureDevice
@ stub -arch=win64 VslRetrieveMailbox
@ stub -arch=win64 WheaAddErrorSource
@ stub -arch=win64 WheaAddErrorSourceDeviceDriver
@ stub -arch=win64 WheaAddErrorSourceDeviceDriverV1
@ stub -arch=win64 WheaAddHwErrorReportSectionDeviceDriver
@ stub -arch=win64 WheaAttemptClearPoison
@ stub -arch=win64 WheaAttemptPhysicalPageOffline
@ stub -arch=win64 WheaAttemptRowOffline
@ stub -arch=win64 WheaConfigureErrorSource
@ stub -arch=win64 WheaCreateHwErrorReportDeviceDriver
@ stub -arch=win64 WheaDeferredRecoveryService
@ stub -arch=win64 WheaEnterCriticalState
@ stub -arch=win64 WheaErrorSourceGetState
@ stub -arch=win64 WheaExitCriticalState
@ stub -arch=win64 WheaGetCurrentProcessName
@ stub -arch=win64 WheaGetErrorSource
@ stub -arch=win64 WheaGetErrorSourceInfo
@ stub -arch=win64 WheaGetNotifyAllOfflinesPolicy
@ stub -arch=win64 WheaHighIrqlLogSelEventHandlerRegister
@ stub -arch=win64 WheaHighIrqlLogSelEventHandlerUnregister
@ stub -arch=win64 WheaHwErrorReportAbandonDeviceDriver
@ stub -arch=win64 WheaHwErrorReportGetLogDataBufferDeviceDriver
@ stub -arch=win64 WheaHwErrorReportMarkAsCriticalDeviceDriver
@ stub -arch=win64 WheaHwErrorReportSetFatalSeverityDeviceDriver
@ stub -arch=win64 WheaHwErrorReportSetSectionNameDeviceDriver
@ stub -arch=win64 WheaHwErrorReportSetSeverityDeviceDriver
@ stub -arch=win64 WheaHwErrorReportSubmitDeviceDriver
@ stub -arch=win64 WheaInitializeDeferredRecoveryObject
@ stub -arch=win64 WheaInitializeRecordHeader
@ stub -arch=win64 WheaIsCriticalState
@ stub -arch=win64 WheaIsLogSelHandlerInitialized
@ stub -arch=win64 WheaLogInternalEvent
@ stub -arch=win64 WheaPrmTranslateDimmAddress
@ stub -arch=win64 WheaPrmTranslatePhysicalAddress
@ stub -arch=win64 WheaProcessWaitingETWEvents
@ stub -arch=win64 WheaRecoveryBugCheck
@ stub -arch=win64 WheaRegisterErrorSourceOverride
@ stub -arch=win64 WheaRemoveErrorSource
@ stub -arch=win64 WheaRemoveErrorSourceDeviceDriver
@ stub -arch=win64 WheaReportFatalHwErrorDeviceDriverEx
@ stub -arch=win64 WheaReportHwError
@ stub -arch=win64 WheaReportHwErrorDeviceDriver
@ stub -arch=win64 WheaReportHwErrorDeviceDriverEx
@ stub -arch=win64 WheaRequestDeferredRecovery
@ stub -arch=win64 WheaSignalHandlerOverride
@ stub -arch=win64 WheaTerminateProcess
@ stub -arch=win64 WheaUnconfigureErrorSource
@ stub -arch=win64 WheaUnregisterErrorSourceOverride
@ stdcall -arch=arm64 ZwAlertThreadByThreadId(ptr)
@ stdcall -arch=i386,win64 ZwAssociateWaitCompletionPacket(ptr ptr ptr ptr ptr long ptr ptr)
@ stdcall -arch=i386,win64 ZwCancelWaitCompletionPacket(ptr long)
@ stdcall ZwCommitComplete(ptr ptr)
@ stdcall ZwCommitEnlistment(ptr ptr)
@ stub -arch=win64 ZwCommitRegistryTransaction
@ stdcall ZwCommitTransaction(ptr long)
@ stub -arch=win64 ZwCreateCpuPartition
@ stub -arch=win64 ZwCreateCrossVmEvent
@ stdcall ZwCreateEnlistment(ptr long ptr ptr ptr long long ptr)
@ stub -arch=arm64 ZwCreateKeyTransacted
@ stub -arch=win64 ZwCreatePartition
@ stub -arch=arm64 ZwCreateProfileEx
@ stub -arch=win64 ZwCreateRegistryTransaction
@ stdcall ZwCreateResourceManager(ptr long ptr ptr ptr long ptr)
@ stub -arch=win64 ZwCreateSectionEx
@ stdcall ZwCreateTransaction(ptr long ptr ptr ptr long long long ptr ptr)
@ stdcall ZwCreateTransactionManager(ptr long ptr ptr long long)
@ stdcall -arch=i386,win64 ZwCreateWaitCompletionPacket(ptr long ptr)
@ stdcall -version=0x602+ -arch=win64 ZwCreateWnfStateName(ptr long long long ptr long ptr)
@ stdcall -version=0x602+ -arch=win64 ZwDeleteWnfStateData(ptr ptr)
@ stdcall -version=0x602+ -arch=win64 ZwDeleteWnfStateName(ptr)
@ stdcall ZwEnumerateTransactionObject(ptr long ptr long ptr)
@ stdcall -arch=win64 ZwFlushBuffersFileEx(ptr long ptr long ptr)
@ stub -arch=win64 ZwGetCachedSigningLevel
@ stub -arch=win64 ZwGetNextProcess
@ stdcall -version=0x600+ ZwGetNextThread(ptr ptr long long long ptr)
@ stdcall ZwGetNotificationResourceManager(ptr ptr long ptr ptr long ptr)
@ stdcall -arch=i386,win64 ZwManagePartition(ptr ptr long ptr long)
@ stub -arch=win64 ZwMapViewOfSectionEx
@ stdcall -arch=win64 ZwNotifyChangeDirectoryFileEx(ptr ptr ptr ptr ptr ptr long long long long)
@ stub -arch=win64 ZwNotifyChangeSession
@ stub -arch=win64 ZwOpenCpuPartition
@ stdcall ZwOpenEnlistment(ptr long ptr ptr ptr)
@ stdcall -version=0x601+ ZwOpenKeyEx(ptr long ptr long)
@ stub -arch=arm64 ZwOpenKeyTransacted
@ stub -arch=arm64 ZwOpenKeyTransactedEx
@ stdcall -arch=i386,win64 ZwOpenPartition(ptr long ptr)
@ stub -arch=win64 ZwOpenRegistryTransaction
@ stdcall ZwOpenResourceManager(ptr long ptr ptr ptr)
@ stub -arch=win64 ZwOpenSession
@ stdcall ZwOpenTransaction(ptr long ptr ptr ptr)
@ stdcall ZwOpenTransactionManager(ptr long ptr ptr ptr long)
@ stdcall ZwPrePrepareComplete(ptr ptr)
@ stdcall ZwPrePrepareEnlistment(ptr ptr)
@ stdcall ZwPrepareComplete(ptr ptr)
@ stdcall ZwPrepareEnlistment(ptr ptr)
@ stdcall ZwPropagationComplete(ptr long long ptr)
@ stdcall ZwPropagationFailed(ptr long long)
@ stdcall -arch=win64 ZwQueryDirectoryFileEx(ptr ptr ptr ptr ptr ptr long long long ptr)
@ stdcall -arch=win64 ZwQueryInformationByName(ptr ptr ptr long long)
@ stub -arch=win64 ZwQueryInformationCpuPartition
@ stdcall ZwQueryInformationEnlistment(ptr long ptr long ptr)
@ stdcall ZwQueryInformationResourceManager(ptr long ptr long ptr)
@ stdcall ZwQueryInformationTransaction(ptr long ptr long ptr)
@ stdcall ZwQueryInformationTransactionManager(ptr long ptr long ptr)
@ stdcall -arch=i386,win64 ZwQueryLicenseValue(ptr ptr ptr long ptr)
@ stub -arch=win64 ZwQuerySecurityAttributesToken
@ stub -arch=win64 ZwQuerySecurityPolicy
@ stdcall -version=0x601+ -arch=win64 ZwQuerySystemInformationEx(long ptr long ptr long ptr)
@ stdcall -version=0x602+ -arch=win64 ZwQueryWnfStateData(ptr ptr ptr ptr ptr ptr)
@ stdcall -version=0x602+ -arch=win64 ZwQueryWnfStateNameInformation(ptr long ptr ptr long)
@ stdcall ZwReadOnlyEnlistment(ptr ptr)
@ stdcall ZwRecoverEnlistment(ptr ptr)
@ stdcall ZwRecoverResourceManager(ptr)
@ stdcall ZwRecoverTransactionManager(ptr)
@ stdcall ZwRollbackComplete(ptr ptr)
@ stdcall ZwRollbackEnlistment(ptr ptr)
@ stub -arch=win64 ZwRollbackRegistryTransaction
@ stdcall ZwRollbackTransaction(ptr long)
@ stub -arch=win64 ZwSetCachedSigningLevel
@ stub -arch=win64 ZwSetInformationCpuPartition
@ stdcall ZwSetInformationEnlistment(ptr long ptr long)
@ stdcall ZwSetInformationResourceManager(ptr long ptr long)
@ stdcall ZwSetInformationTransaction(ptr long ptr long)
@ stdcall ZwSetInformationVirtualMemory(ptr long ptr ptr ptr long)
@ stub -arch=win64 ZwSetTimerEx
@ stub -arch=win64 ZwTraceControl
@ stdcall -version=0x602+ -arch=win64 ZwUpdateWnfStateData(ptr ptr long ptr ptr long long)
@ stdcall -arch=arm64 ZwWaitForAlertByThreadId(ptr ptr)
@ stub -arch=win64 _makepath_s
@ cdecl _snprintf_s()
@ stub -arch=win64 _snscanf_s
@ cdecl _snwprintf_s()
@ cdecl _snwscanf_s()
@ stub -arch=win64 _splitpath_s
@ stub -arch=win64 _strnset_s
@ stub -arch=win64 _strset_s
@ stub -arch=win64 _strtoui64
@ stub -arch=win64 _ultoa_s
@ stub -arch=win64 _ultow_s
@ cdecl _vsnprintf_s()
@ cdecl _vsnwprintf_s()
@ stub -arch=win64 _wcslwr_s
@ stub -arch=win64 _wcsnset_s
@ stub -arch=win64 _wcsset_s
@ stub -arch=win64 _wmakepath_s
@ cdecl _wsplitpath_s()
@ stub -arch=win64 bsearch_s
@ cdecl memcpy_s()
@ cdecl memmove_s()
@ stub -arch=win64 qsort_s
@ cdecl sprintf_s()
@ cdecl sscanf_s()
@ cdecl strcat_s()
@ cdecl strcpy_s()
@ cdecl strncat_s()
@ cdecl strncpy_s()
@ cdecl strtok_s(str str ptr)
@ cdecl swprintf_s()
@ cdecl swscanf_s()
@ cdecl vsprintf_s()
@ cdecl vswprintf_s()
@ extern -arch=win64 IoRingObjectType
@ stub -arch=win64 CcRegisterExternalCacheEx
@ stub -arch=win64 CcUnregisterExternalCacheEx
@ stub -arch=win64 CcUpdateExternalCacheInfoEx
@ stub -arch=win64 ClipInitHandles
@ stub -arch=win64 ExAllocateTimerInternal
@ stub -arch=win64 ExAllocateTimerInternal2
@ stub -arch=win64 ExQueryLicenseValueInternal
@ stub -arch=win64 ExQueueWorkItemEx
@ stub -arch=win64 ExUpdateOsPfnInRegistry
@ stub -arch=win64 ExpEtwTraceLicensingCacheChange
@ stub -arch=win64 ExpGetKernelDataProtection
@ stub -arch=win64 ExpSetKernelDataProtection
@ stub -arch=win64 KeExpandKernelStackAndCalloutInternal
@ stub -arch=win64 KeYieldExecution
@ stub -arch=win64 MmGetNumberOfPhysicalPages
@ stub -arch=win64 NtCreateIoRing
@ stub -arch=win64 NtQueryIoRingCapabilities
@ stub -arch=win64 NtSetInformationIoRing
@ stub -arch=win64 NtSubmitIoRing
@ stub -arch=win64 PnpFreeSystemPdoList
@ stub -arch=win64 PnpGetDeviceInstancePropertyData
@ stub -arch=win64 PnpGetDeviceInstanceRegistryValue
@ stub -arch=win64 PnpGetSystemPdoList
@ stub -arch=win64 PsCaptureUserProcessParameters
@ stub -arch=win64 RtlGetHostNtSystemRoot
@ stub -arch=win64 RtlIsFeatureEnabledForEnterprise
@ stub -arch=win64 SeCodeIntegrityGetBuildExpiryTime
@ stub -arch=win64 VslCapturePgoData
@ stub -arch=win64 VslTestRoutine
@ stub -arch=win64 ZwCreateIoRing
@ stub -arch=win64 ZwQueryIoRingCapabilities
@ stub -arch=win64 ZwSetInformationIoRing
@ stub -arch=win64 ZwSubmitIoRing
@ stub -arch=x86_64 CsanRead16NoCheck
@ stub -arch=x86_64 CsanRead64NoCheck
@ stub -arch=x86_64 CsanRead8NoCheck
@ stub -arch=x86_64 CsanReadNoCheck
@ stub -arch=x86_64 CsanWrite16NoCheck
@ stub -arch=x86_64 CsanWrite64NoCheck
@ stub -arch=x86_64 CsanWrite8NoCheck
@ stub -arch=x86_64 CsanWriteNoCheck
@ stub -arch=x86_64 ExInitializeDeviceAts
@ stub -arch=x86_64 HvlLpReadMultipleMsr
@ stub -arch=x86_64 HvlLpWriteMultipleMsr
@ stub -arch=x86_64 HvlPerformEndOfInterrupt
@ stub -arch=x86_64 IoLoadCrashDumpDriver
@ stub -arch=x86_64 KasanMarkAddressInvalid
@ stub -arch=x86_64 KasanMarkAddressRedZone
@ stub -arch=x86_64 KasanMarkAddressValid
@ stub -arch=x86_64 KasanTrackAddress
@ stub -arch=x86_64 KasanValidateAddress
@ stub -arch=x86_64 KeConnectInterruptForHal
@ stub -arch=x86_64 KeDeregisterBoundCallback
@ stub -arch=x86_64 KeFlushCurrentTbImmediately
@ stub -arch=x86_64 KeGetXSaveFeatureFlags
@ stub -arch=x86_64 KeRegisterBoundCallback
@ stub -arch=x86_64 WheaIsAltContextAllocPossible
@ stub -arch=x86_64 WheaRegisterInUsePageOfflineNotification
@ stub -arch=x86_64 WheaUnregisterInUsePageOfflineNotification
@ stub -arch=x86_64 __asan_alloca_poison
@ stub -arch=x86_64 __asan_allocas_unpoison
@ stub -arch=x86_64 __asan_load1
@ stub -arch=x86_64 __asan_load16
@ stub -arch=x86_64 __asan_load16_volatile
@ stub -arch=x86_64 __asan_load1_volatile
@ stub -arch=x86_64 __asan_load2
@ stub -arch=x86_64 __asan_load2_volatile
@ stub -arch=x86_64 __asan_load4
@ stub -arch=x86_64 __asan_load4_volatile
@ stub -arch=x86_64 __asan_load8
@ stub -arch=x86_64 __asan_load8_volatile
@ stub -arch=x86_64 __asan_loadN
@ stub -arch=x86_64 __asan_loadN_volatile
@ stub -arch=x86_64 __asan_memcpy
@ stub -arch=x86_64 __asan_memmove
@ stub -arch=x86_64 __asan_memset
@ stub -arch=x86_64 __asan_report_load1
@ stub -arch=x86_64 __asan_report_load16
@ stub -arch=x86_64 __asan_report_load2
@ stub -arch=x86_64 __asan_report_load4
@ stub -arch=x86_64 __asan_report_load8
@ stub -arch=x86_64 __asan_report_loadN
@ stub -arch=x86_64 __asan_report_store1
@ stub -arch=x86_64 __asan_report_store16
@ stub -arch=x86_64 __asan_report_store2
@ stub -arch=x86_64 __asan_report_store4
@ stub -arch=x86_64 __asan_report_store8
@ stub -arch=x86_64 __asan_report_storeN
@ stub -arch=x86_64 __asan_set_shadow_00
@ stub -arch=x86_64 __asan_set_shadow_f8
@ stub -arch=x86_64 __asan_store1
@ stub -arch=x86_64 __asan_store16
@ stub -arch=x86_64 __asan_store16_volatile
@ stub -arch=x86_64 __asan_store1_volatile
@ stub -arch=x86_64 __asan_store2
@ stub -arch=x86_64 __asan_store2_volatile
@ stub -arch=x86_64 __asan_store4
@ stub -arch=x86_64 __asan_store4_volatile
@ stub -arch=x86_64 __asan_store8
@ stub -arch=x86_64 __asan_store8_volatile
@ stub -arch=x86_64 __asan_storeN
@ stub -arch=x86_64 __asan_storeN_volatile
@ stub -arch=x86_64 __asan_wrap_memchr
@ stub -arch=x86_64 __asan_wrap_memcmp
@ stub -arch=x86_64 __asan_wrap_strcat
@ stub -arch=x86_64 __asan_wrap_strcmp
@ stub -arch=x86_64 __asan_wrap_strcpy
@ stub -arch=x86_64 __asan_wrap_strlen
@ stub -arch=x86_64 __asan_wrap_strncmp
@ stub -arch=x86_64 __asan_wrap_strncpy
@ stub -arch=x86_64 __asan_wrap_wcslen
@ stub -arch=x86_64 __misaligned_access
@ cdecl -arch=x86_64 sqrtf()
@ stdcall -arch=win64 ZwCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr)
@ stdcall -arch=win64 ZwPlugPlayControl(long ptr long)
