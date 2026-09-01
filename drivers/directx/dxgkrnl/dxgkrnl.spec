; dxgkrnl.sys exports
;
; The public name set is the clean-room-supported intersection with Windows 11
; 24H2 build 26100.1742. Verified Windows exports retain their native ordinals.
; Miniport initialization is resolved through the displib control-device
; protocol and is intentionally not exported.

; --- Optional telemetry admission ---
2 cdecl DxgKrnlTelemetryGlobal_LogTelemetryEvent()

; --- Display Port / Scheduler bridge ---
38 stdcall DpSynchronizeExecution(ptr ptr ptr long ptr)
39 stdcall DpiGetDriverVersion(ptr)
40 stdcall DpiGetDxgAdapter(ptr)
41 stdcall DpiGetSchedulerCallbackState(ptr)
42 stdcall DpiSetSchedulerCallbackState(ptr long)

; --- DxgCoreInterface (static callback table consumed by dxgmms2.sys) ---
43 extern DxgCoreInterface

; --- Layout-free TDR policy ---
23 stdcall TdrIsEnabled()
25 stdcall TdrIsTimeoutForcedFlip()

; --- dxgmms2 build, bugcheck, and timeout state ---
30 extern g_DxgMmsBugcheckExportIndex
31 extern g_IsInternalRelease
32 extern g_IsInternalReleaseOrDbg
35 extern ?g_TdrForceDodPresentTimeout@@3JC g_TdrForceDodPresentTimeout
36 extern ?g_TdrForceDodVSyncTimeout@@3JC g_TdrForceDodVSyncTimeout
37 extern g_TdrForceTimeout

; --- ETW tracing stubs ---
321 stdcall TraceDxgkBlockThread(long)
322 stdcall TraceDxgkContext(long ptr)
323 stdcall TraceDxgkDevice(long ptr)
324 stdcall TraceDxgkFunctionProfiler(long)
325 stdcall TraceDxgkPerformanceWarning(long ptr)

; --- VidMm ---
8 stdcall DxgkUnreferenceDxgAllocation(ptr)
9 stdcall DxgkUnreferenceDxgResource(ptr)
10 stdcall DxgkVidMmAllowFailOnOfferReclaimErrors()

; --- Hardware-context feature-disabled boundary ---
48 stdcall DxgkSubmitPresentBltToHwQueue(ptr)
83 stdcall NtDxgkSubmitPresentBltToHwQueue(ptr) DxgkSubmitPresentBltToHwQueue
134 stdcall NtGdiDdDDICreateHwContext(ptr) DxgkSubmitPresentBltToHwQueue
149 stdcall NtGdiDdDDIDestroyHwContext(ptr) DxgkSubmitPresentBltToHwQueue

; --- Shared terminal not-supported boundary ---
236 stdcall NtGdiDdDDIQueryFSEBlock(ptr) DxgkD3dkmtNotSupported
260 stdcall NtGdiDdDDISetFSEBlock(ptr) DxgkD3dkmtNotSupported
264 stdcall NtGdiDdDDISetMonitorColorSpaceTransform(ptr) DxgkD3dkmtNotSupported
