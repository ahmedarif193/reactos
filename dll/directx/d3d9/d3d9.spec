@ stdcall Direct3DShaderValidatorCreate9()
@ stdcall PSGPError()
@ stdcall PSGPSampleTexture()
@ stdcall DebugSetLevel()
@ stdcall DebugSetMute(long)
@ stdcall Direct3DCreate9(long)

# Windows 10 DirectX 12 Support
@ stdcall -stub -version=0x600+ D3D12CreateDevice(ptr long ptr ptr)
@ stdcall -stub -version=0x600+ D3D12CreateRootSignatureDeserializer(ptr long ptr ptr)
@ stdcall -stub -version=0x600+ D3D12CreateVersionedRootSignatureDeserializer(ptr long ptr ptr)
@ stdcall -stub -version=0x600+ D3D12EnableExperimentalFeatures(long ptr ptr ptr)
@ stdcall -stub -version=0x600+ D3D12GetDebugInterface(ptr ptr)
@ stdcall -stub -version=0x600+ D3D12GetInterface(ptr ptr ptr)
@ stdcall -stub -version=0x600+ D3D12PIXEventsReplaceBlock()
@ stdcall -stub -version=0x600+ D3D12PIXGetThreadInfo(ptr)
@ stdcall -stub -version=0x600+ D3D12PIXNotifyWakeFromFenceSignal(ptr)
@ stdcall -stub -version=0x600+ D3D12PIXReportCounter(ptr int64)
@ stdcall -stub -version=0x600+ D3D12SerializeRootSignature(ptr long ptr ptr)
@ stdcall -stub -version=0x600+ D3D12SerializeVersionedRootSignature(ptr ptr ptr)

# DirectX Raytracing (DXR) Support
@ stdcall -stub -version=0x600+ D3D12CreateStateObject(ptr ptr ptr)
@ stdcall -stub -version=0x600+ D3D12CreateRaytracingAccelerationStructure(ptr ptr ptr)
@ stdcall -stub -version=0x600+ D3D12CreateRaytracingStateObject(ptr ptr ptr)

# Enhanced D3D11 Features for Windows 10
@ stdcall -stub -version=0x600+ D3D11CreateDevice(ptr long ptr long ptr ptr ptr ptr ptr)
@ stdcall -stub -version=0x600+ D3D11CreateDeviceAndSwapChain(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr)
@ stdcall -stub -version=0x600+ D3D11On12CreateDevice(ptr long ptr long ptr long long ptr ptr ptr ptr)

# Direct3D 10.1 and 11 Core Support
@ stdcall -stub -version=0x600+ D3D10CreateDevice1(ptr long ptr long long ptr)
@ stdcall -stub -version=0x600+ D3D10CreateDeviceAndSwapChain1(ptr long ptr long long ptr ptr ptr ptr ptr)

# Advanced Shader Support
@ stdcall -stub -version=0x600+ D3DCompile(ptr long str ptr ptr str str long long ptr ptr)
@ stdcall -stub -version=0x600+ D3DCompile2(ptr long str ptr ptr str str long long long ptr long ptr ptr)
@ stdcall -stub -version=0x600+ D3DCompileFromFile(wstr ptr ptr str str long long ptr ptr)
@ stdcall -stub -version=0x600+ D3DCreateLinker(ptr)
@ stdcall -stub -version=0x600+ D3DDisassemble(ptr long long str ptr)
@ stdcall -stub -version=0x600+ D3DGetBlobPart(ptr long long long ptr)
@ stdcall -stub -version=0x600+ D3DGetDebugInfo(ptr long ptr)
@ stdcall -stub -version=0x600+ D3DGetInputAndOutputSignatureBlob(ptr long ptr)
@ stdcall -stub -version=0x600+ D3DGetInputSignatureBlob(ptr long ptr)
@ stdcall -stub -version=0x600+ D3DGetOutputSignatureBlob(ptr long ptr)
@ stdcall -stub -version=0x600+ D3DGetTraceInstructionOffsets(ptr long ptr long ptr ptr)
@ stdcall -stub -version=0x600+ D3DLoadModule(ptr long ptr)
@ stdcall -stub -version=0x600+ D3DPreprocess(ptr long str ptr ptr ptr ptr)
@ stdcall -stub -version=0x600+ D3DReadFileToBlob(wstr ptr)
@ stdcall -stub -version=0x600+ D3DReflect(ptr long ptr ptr)
@ stdcall -stub -version=0x600+ D3DReflectLibrary(ptr long ptr ptr)
@ stdcall -stub -version=0x600+ D3DSetBlobPart(ptr long long long ptr long ptr)
@ stdcall -stub -version=0x600+ D3DStripShader(ptr long long ptr)
@ stdcall -stub -version=0x600+ D3DWriteBlobToFile(ptr wstr long)

# DirectML Support
@ stdcall -stub -version=0x600+ DMLCreateDevice(ptr long ptr ptr)
@ stdcall -stub -version=0x600+ DMLCreateDevice1(ptr long ptr ptr ptr)
