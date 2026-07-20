; dxgkrnl.sys exports
;
; Ordinals 1-26 match the Windows 7 SP1 export table layout.
; Groups: TDR (12 functions + 4 data), Display Port bridge (4+1 functions),
;         DxgCoreInterface (1 data), SQM (9 functions), ETW (6 functions + 1 data),
;         VidMm (1 function), plus the DxgkInitialize family (3 functions).

; --- TDR (Timeout Detection and Recovery) ---
@ stdcall TdrAllowToDebugEngineTimeout(ptr)
@ stdcall TdrCollectDbgInfoStage1(ptr)
@ stdcall TdrCollectDbgInfoStage2(ptr)
@ stdcall TdrCompleteRecoveryContext(ptr)
@ stdcall TdrCreateRecoveryContext(ptr ptr)
@ stdcall TdrHistoryInit(ptr)
@ stdcall TdrHistoryIsLimitExhausted(ptr ptr long)
@ stdcall TdrHistoryUpdate(ptr ptr)
@ stdcall TdrIsEnabled()
@ stdcall TdrIsRecoveryRequired(ptr)
@ stdcall TdrIsTimeoutForcedFlip(ptr)
@ stdcall TdrResetFromTimeout(ptr)
@ stdcall TdrUpdateDbgReport(ptr long)
@ extern g_TdrConfig
@ extern g_TdrForceDodPresentTimeout
@ extern g_TdrForceDodVSyncTimeout
@ extern g_TdrForceTimeout

; --- Display Port / Scheduler bridge ---
@ stdcall DpSynchronizeExecution(ptr ptr ptr long ptr)
@ stdcall DpiGetDriverVersion(ptr)
@ stdcall DpiGetDxgAdapter(ptr)
@ stdcall DpiGetSchedulerCallbackState(ptr ptr)
@ stdcall DpiSetSchedulerCallbackState(ptr long)

; --- DxgCoreInterface (static callback table for dxgmms1.sys) ---
@ extern DxgCoreInterface

; --- SQM telemetry stubs ---
@ stdcall DxgkSqmAddToStream(ptr long ptr long)
@ stdcall DxgkSqmCommonGeneric(long ptr long)
@ stdcall DxgkSqmCreateDwordStreamEntry(ptr long long)
@ stdcall DxgkSqmCreateStringStreamEntry(ptr long ptr)
@ stdcall DxgkSqmGenericDword(long long)
@ stdcall DxgkSqmGenericDword64(long int64)
@ stdcall DxgkSqmGenericString(long ptr)
@ stdcall DxgkSqmOptedIn()
@ stdcall DxgkSqmSetDword(long long)

; --- ETW tracing stubs ---
@ stdcall TraceDxgkBlockThread(long)
@ stdcall TraceDxgkContext(long ptr)
@ stdcall TraceDxgkDevice(long ptr)
@ stdcall TraceDxgkFunctionProfiler(long)
@ stdcall TraceDxgkPerformanceWarning(long ptr)
@ stdcall TraceDxgkPresentHistory(long ptr)
@ extern g_bVSyncEnabledForLogging
@ extern g_loggerInfo

; --- VidMm ---
@ stdcall DxgkVidMmAllowFailOnOfferReclaimErrors()

; --- VidSch (GPU scheduler interface) ---
@ stdcall VidSchInterface(ptr)

; --- DxgkInitialize family ---
@ stdcall DxgkInitialize(ptr ptr ptr)
@ stdcall DxgkInitializeEx(ptr ptr long ptr)
@ stdcall DxgkInitializeDisplayOnlyDriver(ptr ptr ptr)
@ stdcall DxgkUnInitialize(ptr)
