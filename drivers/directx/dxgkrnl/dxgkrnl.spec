; dxgkrnl.sys exports
;
; The public name set is the clean-room-supported intersection with Windows 11
; 24H2 build 26100.1742. Ordinals are generated and are not claimed to match
; the native ARM64 binary. Miniport initialization is resolved through the
; displib control-device protocol and is intentionally not exported.

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
