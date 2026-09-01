/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Windows 11 24H2 dxgkrnl public NtGdi export inventory
 * COPYRIGHT:   Copyright 2026 ReactOS Contributors
 */

#pragma once

/*
 * ReactOS uses each verified native dxgkrnl ordinal as the stable operation
 * number for this internal bridge.  The list contains only public D3DKMT
 * syscall entries independently declared by the WDK.  Private dxgkrnl
 * services and data tables are deliberately not admitted through it.
 */
#define RXGK_NTGDI_EXPORTS_0(X) \
    X(119, CheckExclusiveOwnership)

#define RXGK_NTGDI_EXPORTS_1(X) \
    X(112, AcquireKeyedMutex) \
    X(113, AcquireKeyedMutex2) \
    X(116, AdjustFullscreenGamma) \
    X(118, ChangeVideoMemoryReservation) \
    X(120, CheckMonitorPowerState) \
    X(121, CheckMultiPlaneOverlaySupport) \
    X(122, CheckMultiPlaneOverlaySupport2) \
    X(123, CheckMultiPlaneOverlaySupport3) \
    X(124, CheckOcclusion) \
    X(125, CheckSharedResourceAccess) \
    X(126, CheckVidPnExclusiveOwnership) \
    X(127, CloseAdapter) \
    X(128, ConfigureSharedResource) \
    X(129, CreateAllocation) \
    X(131, CreateContext) \
    X(132, CreateContextVirtual) \
    X(133, CreateDevice) \
    X(134, CreateHwContext) \
    X(135, CreateHwQueue) \
    X(136, CreateKeyedMutex) \
    X(137, CreateKeyedMutex2) \
    X(138, CreateOutputDupl) \
    X(139, CreateOverlay) \
    X(140, CreatePagingQueue) \
    X(141, CreateProtectedSession) \
    X(143, CreateSynchronizationObject) \
    X(145, DestroyAllocation) \
    X(146, DestroyAllocation2) \
    X(147, DestroyContext) \
    X(148, DestroyDevice) \
    X(149, DestroyHwContext) \
    X(150, DestroyHwQueue) \
    X(151, DestroyKeyedMutex) \
    X(152, DestroyOutputDupl) \
    X(153, DestroyOverlay) \
    X(154, DestroyPagingQueue) \
    X(155, DestroyProtectedSession) \
    X(156, DestroySynchronizationObject) \
    X(160, EnumAdapters) \
    X(161, EnumAdapters2) \
    X(162, Escape) \
    X(163, Evict) \
    X(165, FlipOverlay) \
    X(166, FlushHeapTransitions) \
    X(167, FreeGpuVirtualAddress) \
    X(168, GetAllocationPriority) \
    X(170, GetContextInProcessSchedulingPriority) \
    X(171, GetContextSchedulingPriority) \
    X(172, GetDWMVerticalBlankEvent) \
    X(173, GetDeviceState) \
    X(174, GetDisplayModeList) \
    X(176, GetMultiPlaneOverlayCaps) \
    X(177, GetMultisampleMethodList) \
    X(178, GetOverlayState) \
    X(179, GetPostCompositionCaps) \
    X(180, GetPresentHistory) \
    X(181, GetProcessDeviceRemovalSupport) \
    X(184, GetResourcePresentPrivateDriverData) \
    X(185, GetRuntimeData) \
    X(186, GetScanLine) \
    X(188, GetSharedPrimaryHandle) \
    X(189, GetSharedResourceAdapterLuid) \
    X(193, InvalidateActiveVidPn) \
    X(194, InvalidateCache) \
    X(195, Lock) \
    X(196, Lock2) \
    X(197, MakeResident) \
    X(198, MapGpuVirtualAddress) \
    X(199, MarkDeviceAsError) \
    X(205, OfferAllocations) \
    X(206, OpenAdapterFromDeviceName) \
    X(207, OpenAdapterFromHdc) \
    X(208, OpenAdapterFromLuid) \
    X(210, OpenKeyedMutex) \
    X(211, OpenKeyedMutex2) \
    X(212, OpenKeyedMutexFromNtHandle) \
    X(213, OpenNtHandleFromName) \
    X(214, OpenProtectedSessionFromNtHandle) \
    X(215, OpenResource) \
    X(216, OpenResourceFromNtHandle) \
    X(218, OpenSyncObjectFromNtHandle) \
    X(219, OpenSyncObjectFromNtHandle2) \
    X(220, OpenSyncObjectNtHandleFromName) \
    X(221, OpenSynchronizationObject) \
    X(222, OutputDuplGetFrameInfo) \
    X(223, OutputDuplGetMetaData) \
    X(224, OutputDuplGetPointerShapeData) \
    X(225, OutputDuplPresent) \
    X(226, OutputDuplReleaseFrame) \
    X(227, PollDisplayChildren) \
    X(228, Present) \
    X(229, PresentMultiPlaneOverlay) \
    X(230, PresentMultiPlaneOverlay2) \
    X(231, PresentMultiPlaneOverlay3) \
    X(232, PresentRedirected) \
    X(233, QueryAdapterInfo) \
    X(234, QueryAllocationResidency) \
    X(235, QueryClockCalibration) \
    X(236, QueryFSEBlock) \
    X(237, QueryProcessOfferInfo) \
    X(238, QueryProtectedSessionInfoFromNtHandle) \
    X(239, QueryProtectedSessionStatus) \
    X(240, QueryRemoteVidPnSourceFromGdiDisplayName) \
    X(241, QueryResourceInfo) \
    X(242, QueryResourceInfoFromNtHandle) \
    X(243, QueryStatistics) \
    X(244, QueryVidPnExclusiveOwnership) \
    X(245, QueryVideoMemoryInfo) \
    X(246, ReclaimAllocations) \
    X(247, ReclaimAllocations2) \
    X(248, ReleaseKeyedMutex) \
    X(249, ReleaseKeyedMutex2) \
    X(250, ReleaseProcessVidPnSourceOwners) \
    X(253, Render) \
    X(254, ReserveGpuVirtualAddress) \
    X(255, SetAllocationPriority) \
    X(256, SetContextInProcessSchedulingPriority) \
    X(257, SetContextSchedulingPriority) \
    X(258, SetDisplayMode) \
    X(260, SetFSEBlock) \
    X(261, SetGammaRamp) \
    X(262, SetHwProtectionTeardownRecovery) \
    X(264, SetMonitorColorSpaceTransform) \
    X(268, SetQueuedLimit) \
    X(269, SetStablePowerState) \
    X(271, SetSyncRefreshCountWaitTarget) \
    X(272, SetVidPnSourceHwProtection) \
    X(273, SetVidPnSourceOwner) \
    X(276, SignalSynchronizationObject) \
    X(277, SignalSynchronizationObjectFromCpu) \
    X(278, SignalSynchronizationObjectFromGpu) \
    X(279, SignalSynchronizationObjectFromGpu2) \
    X(280, SubmitCommand) \
    X(281, SubmitCommandToHwQueue) \
    X(282, SubmitSignalSyncObjectsToHwQueue) \
    X(283, SubmitWaitForSyncObjectsToHwQueue) \
    X(284, TrimProcessCommitment) \
    X(286, Unlock) \
    X(287, Unlock2) \
    X(288, UpdateAllocationProperty) \
    X(289, UpdateGpuVirtualAddress) \
    X(290, UpdateOverlay) \
    X(291, WaitForIdle) \
    X(292, WaitForSynchronizationObject) \
    X(293, WaitForSynchronizationObjectFromCpu) \
    X(294, WaitForSynchronizationObjectFromGpu) \
    X(295, WaitForVerticalBlankEvent) \
    X(296, WaitForVerticalBlankEvent2)

#define RXGK_NTGDI_EXPORTS_2(X) \
    X(183, GetProcessSchedulingPriorityClass) \
    X(267, SetProcessSchedulingPriorityClass)

#define RXGK_NTGDI_EXPORTS_5(X) \
    X(275, ShareObjects)
