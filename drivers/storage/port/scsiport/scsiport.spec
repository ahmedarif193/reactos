@ varargs ScsiDebugPrint(long str)
@ stdcall ScsiPortCompleteRequest(ptr long long long long)
@ stdcall ScsiPortConvertPhysicalAddressToUlong(long long)
@ stdcall -arch=win32 ScsiPortConvertUlongToPhysicalAddress(long) NTOSKRNL.RtlConvertUlongToLargeInteger
@ stdcall -arch=win64 ScsiPortConvertUlongToPhysicalAddress(long)
@ stdcall ScsiPortFlushDma(ptr)
@ stdcall ScsiPortFreeDeviceBase(ptr ptr)
@ stdcall ScsiPortGetBusData(ptr long long long ptr long)
@ stdcall ScsiPortGetDeviceBase(ptr long long long long long long)
@ stdcall ScsiPortGetLogicalUnit(ptr long long long)
@ stdcall ScsiPortGetPhysicalAddress(ptr ptr ptr long)
@ stdcall ScsiPortGetSrb(ptr long long long long)
@ stdcall ScsiPortGetUncachedExtension(ptr ptr long)
@ stdcall ScsiPortGetVirtualAddress(ptr long long)
@ stdcall ScsiPortInitialize(ptr ptr ptr ptr)
@ stdcall ScsiPortIoMapTransfer(ptr ptr long long)
@ stdcall ScsiPortLogError(ptr ptr long long long long long)
@ stdcall ScsiPortMoveMemory(ptr ptr long)
@ cdecl ScsiPortNotification()
@ stdcall -arch=win32 ScsiPortReadPortBufferUchar(ptr ptr long) HAL.READ_PORT_BUFFER_UCHAR
@ stdcall -arch=win32 ScsiPortReadPortBufferUshort(ptr ptr long) HAL.READ_PORT_BUFFER_USHORT
@ stdcall -arch=win32 ScsiPortReadPortBufferUlong(ptr ptr long) HAL.READ_PORT_BUFFER_ULONG
@ stdcall -arch=win32 ScsiPortReadPortUchar(ptr) HAL.READ_PORT_UCHAR
@ stdcall -arch=win32 ScsiPortReadPortUshort(ptr) HAL.READ_PORT_USHORT
@ stdcall -arch=win32 ScsiPortReadPortUlong(ptr) HAL.READ_PORT_ULONG
@ stdcall -arch=win32 ScsiPortReadRegisterBufferUchar(ptr ptr long) NTOSKRNL.READ_REGISTER_BUFFER_UCHAR
@ stdcall -arch=win32 ScsiPortReadRegisterBufferUshort(ptr ptr long) NTOSKRNL.READ_REGISTER_BUFFER_USHORT
@ stdcall -arch=win32 ScsiPortReadRegisterBufferUlong(ptr ptr long) NTOSKRNL.READ_REGISTER_BUFFER_ULONG
@ stdcall -arch=win32 ScsiPortReadRegisterUchar(ptr) NTOSKRNL.READ_REGISTER_UCHAR
@ stdcall -arch=win32 ScsiPortReadRegisterUshort(ptr) NTOSKRNL.READ_REGISTER_USHORT
@ stdcall -arch=win32 ScsiPortReadRegisterUlong(ptr) NTOSKRNL.READ_REGISTER_ULONG
@ stdcall -arch=win64 ScsiPortReadPortBufferUchar(ptr ptr long)
@ stdcall -arch=win64 ScsiPortReadPortBufferUshort(ptr ptr long)
@ stdcall -arch=win64 ScsiPortReadPortBufferUlong(ptr ptr long)
@ stdcall -arch=win64 ScsiPortReadPortUchar(ptr)
@ stdcall -arch=win64 ScsiPortReadPortUshort(ptr)
@ stdcall -arch=win64 ScsiPortReadPortUlong(ptr)
@ stdcall -arch=win64 ScsiPortReadRegisterBufferUchar(ptr ptr long)
@ stdcall -arch=win64 ScsiPortReadRegisterBufferUshort(ptr ptr long)
@ stdcall -arch=win64 ScsiPortReadRegisterBufferUlong(ptr ptr long)
@ stdcall -arch=win64 ScsiPortReadRegisterUchar(ptr)
@ stdcall -arch=win64 ScsiPortReadRegisterUshort(ptr)
@ stdcall -arch=win64 ScsiPortReadRegisterUlong(ptr)
@ stdcall ScsiPortSetBusDataByOffset(ptr long long long ptr long long)
@ stdcall ScsiPortStallExecution(long) HAL.KeStallExecutionProcessor
@ stdcall ScsiPortValidateRange(ptr long long long long long long)
@ stdcall -arch=win32 ScsiPortWritePortBufferUchar(ptr ptr long) HAL.WRITE_PORT_BUFFER_UCHAR
@ stdcall -arch=win32 ScsiPortWritePortBufferUshort(ptr ptr long) HAL.WRITE_PORT_BUFFER_USHORT
@ stdcall -arch=win32 ScsiPortWritePortBufferUlong(ptr ptr long) HAL.WRITE_PORT_BUFFER_ULONG
@ stdcall -arch=win32 ScsiPortWritePortUchar(ptr long) HAL.WRITE_PORT_UCHAR
@ stdcall -arch=win32 ScsiPortWritePortUshort(ptr long) HAL.WRITE_PORT_USHORT
@ stdcall -arch=win32 ScsiPortWritePortUlong(ptr long) HAL.WRITE_PORT_ULONG
@ stdcall -arch=win32 ScsiPortWriteRegisterBufferUchar(ptr ptr long) NTOSKRNL.WRITE_REGISTER_BUFFER_UCHAR
@ stdcall -arch=win32 ScsiPortWriteRegisterBufferUshort(ptr ptr long) NTOSKRNL.WRITE_REGISTER_BUFFER_USHORT
@ stdcall -arch=win32 ScsiPortWriteRegisterBufferUlong(ptr ptr long) NTOSKRNL.WRITE_REGISTER_BUFFER_ULONG
@ stdcall -arch=win32 ScsiPortWriteRegisterUchar(ptr long) NTOSKRNL.WRITE_REGISTER_UCHAR
@ stdcall -arch=win32 ScsiPortWriteRegisterUshort(ptr long) NTOSKRNL.WRITE_REGISTER_USHORT
@ stdcall -arch=win32 ScsiPortWriteRegisterUlong(ptr long) NTOSKRNL.WRITE_REGISTER_ULONG
@ stdcall -arch=win64 ScsiPortWritePortBufferUchar(ptr ptr long)
@ stdcall -arch=win64 ScsiPortWritePortBufferUshort(ptr ptr long)
@ stdcall -arch=win64 ScsiPortWritePortBufferUlong(ptr ptr long)
@ stdcall -arch=win64 ScsiPortWritePortUchar(ptr long)
@ stdcall -arch=win64 ScsiPortWritePortUshort(ptr long)
@ stdcall -arch=win64 ScsiPortWritePortUlong(ptr long)
@ stdcall -arch=win64 ScsiPortWriteRegisterBufferUchar(ptr ptr long)
@ stdcall -arch=win64 ScsiPortWriteRegisterBufferUshort(ptr ptr long)
@ stdcall -arch=win64 ScsiPortWriteRegisterBufferUlong(ptr ptr long)
@ stdcall -arch=win64 ScsiPortWriteRegisterUchar(ptr long)
@ stdcall -arch=win64 ScsiPortWriteRegisterUshort(ptr long)
@ stdcall -arch=win64 ScsiPortWriteRegisterUlong(ptr long)
