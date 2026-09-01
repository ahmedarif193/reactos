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
@ stdcall DpiGetSchedulerCallbackState(ptr)
@ stdcall DpiSetSchedulerCallbackState(ptr long)

; --- DxgCoreInterface (static callback table consumed by dxgmms2.sys) ---
@ extern DxgCoreInterface

; --- ETW tracing stubs ---
@ stdcall TraceDxgkBlockThread(long)
@ stdcall TraceDxgkContext(long ptr)
@ stdcall TraceDxgkDevice(long ptr)
@ stdcall TraceDxgkFunctionProfiler(long)
@ stdcall TraceDxgkPerformanceWarning(long ptr)

; --- VidMm ---
@ stdcall DxgkVidMmAllowFailOnOfferReclaimErrors()
