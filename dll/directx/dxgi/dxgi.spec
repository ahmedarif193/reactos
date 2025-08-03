# DXGI (DirectX Graphics Infrastructure) Windows 10 Support
# Base DXGI 1.0 Functions
@ stdcall CreateDXGIFactory(ptr ptr)
@ stdcall CreateDXGIFactory1(ptr ptr)

# DXGI 1.2 Functions (Windows 8)
@ stdcall -stub -version=0x602+ CreateDXGIFactory2(long ptr ptr)

# DXGI 1.3 Functions (Windows 8.1)
@ stdcall -stub -version=0x603+ DXGIGetDebugInterface1(long ptr ptr)

# DXGI 1.4 Functions (Windows 10 RTM)
@ stdcall -stub -version=0x600+ DXGIDisableVBlankVirtualization()

# DXGI 1.5 Functions (Windows 10 Anniversary Update)
@ stdcall -stub -version=0x600+ DXGIDeclareAdapterRemovalSupport()

# DXGI 1.6 Functions (Windows 10 Creators Update and later)
@ stdcall -stub -version=0x600+ CreateDXGIFactory6(ptr ptr)
@ stdcall -stub -version=0x600+ DXGIGetDebugInterface2(long ptr ptr)

# Enhanced Factory Creation
@ stdcall -stub -version=0x600+ CreateDXGIFactory7(ptr ptr)

# HDR and Advanced Display Support
@ stdcall -stub -version=0x600+ DXGICheckHDRSupport(ptr)
@ stdcall -stub -version=0x600+ DXGIGetDisplayModeList2(ptr long ptr ptr ptr)
@ stdcall -stub -version=0x600+ DXGIEnumDisplayModes2(ptr long ptr ptr ptr)

# Variable Refresh Rate Support
@ stdcall -stub -version=0x600+ DXGICheckVariableRefreshRateSupport(ptr ptr)
@ stdcall -stub -version=0x600+ DXGISetVariableRefreshRate(ptr long)

# Multi-Adapter Support
@ stdcall -stub -version=0x600+ DXGIEnumAdapterByGpuPreference(ptr long ptr ptr)
@ stdcall -stub -version=0x600+ DXGIQueryResourceResidency2(ptr ptr long ptr)

# GPU Memory Management
@ stdcall -stub -version=0x600+ DXGIQueryVideoMemoryInfo2(ptr long ptr)
@ stdcall -stub -version=0x600+ DXGISetMemoryReservation2(ptr long int64)
@ stdcall -stub -version=0x600+ DXGIRegisterVideoMemoryBudgetChangeNotification2(ptr ptr ptr)
@ stdcall -stub -version=0x600+ DXGIUnregisterVideoMemoryBudgetChangeNotification2(ptr)

# Enhanced Swap Chain Support
@ stdcall -stub -version=0x600+ DXGICreateSwapChainForComposition2(ptr ptr ptr ptr ptr)
@ stdcall -stub -version=0x600+ DXGICreateSwapChainForCoreWindow2(ptr ptr ptr ptr ptr)
@ stdcall -stub -version=0x600+ DXGICreateSwapChainForHwnd2(ptr ptr ptr ptr ptr ptr)

# Frame Statistics and Performance
@ stdcall -stub -version=0x600+ DXGIGetFrameStatistics2(ptr ptr)
@ stdcall -stub -version=0x600+ DXGICheckPresentDurationSupport2(ptr ptr)
@ stdcall -stub -version=0x600+ DXGIGetLastPresentCount2(ptr ptr)

# Color Space and Display Management
@ stdcall -stub -version=0x600+ DXGICheckColorSpaceSupport2(ptr long ptr)
@ stdcall -stub -version=0x600+ DXGISetColorSpace2(ptr long)
@ stdcall -stub -version=0x600+ DXGIGetColorSpace2(ptr ptr)

# Hardware Composition Support
@ stdcall -stub -version=0x600+ DXGICheckHardwareCompositionSupport2(ptr ptr)
@ stdcall -stub -version=0x600+ DXGISetHardwareCompositionSupport2(ptr long)

# Enhanced Debug Support
@ stdcall -stub -version=0x600+ DXGIInfoQueueSetBreakOnSeverity(ptr long long)
@ stdcall -stub -version=0x600+ DXGIInfoQueueSetBreakOnID(ptr long long)
@ stdcall -stub -version=0x600+ DXGIInfoQueueSetMuteDebugOutput(ptr long)

# Modern Display Features
@ stdcall -stub -version=0x600+ DXGIEnumOutputs2(ptr long ptr)
@ stdcall -stub -version=0x600+ DXGIGetDisplaySurfaceData2(ptr ptr)
@ stdcall -stub -version=0x600+ DXGISetDisplaySurface2(ptr ptr)

# Advanced Synchronization
@ stdcall -stub -version=0x600+ DXGICreateSynchronizationObject(ptr ptr ptr)
@ stdcall -stub -version=0x600+ DXGISignalSynchronizationObject(ptr int64)
@ stdcall -stub -version=0x600+ DXGIWaitForSynchronizationObject(ptr int64)