# Workstation service client forwarders to netapi32

@ stdcall NetGetJoinInformation(wstr ptr ptr) netapi32.NetGetJoinInformation
@ stdcall NetGetJoinableOUs(wstr wstr wstr wstr ptr ptr) netapi32.NetGetJoinableOUs
@ stdcall NetWkstaGetInfo(wstr long ptr) netapi32.NetWkstaGetInfo
@ stdcall NetWkstaSetInfo(wstr long ptr ptr) netapi32.NetWkstaSetInfo
@ stdcall NetWkstaUserGetInfo(wstr long ptr) netapi32.NetWkstaUserGetInfo
@ stdcall NetWkstaUserSetInfo(wstr long ptr ptr) netapi32.NetWkstaUserSetInfo
@ stdcall NetWkstaUserEnum(wstr long ptr long ptr ptr ptr) netapi32.NetWkstaUserEnum
@ stdcall NetWkstaTransportEnum(wstr long ptr long ptr ptr ptr) netapi32.NetWkstaTransportEnum
@ stdcall NetWkstaTransportAdd(wstr long ptr ptr) netapi32.NetWkstaTransportAdd
@ stdcall NetWkstaTransportDel(wstr wstr long) netapi32.NetWkstaTransportDel
@ stdcall NetApiBufferFree(ptr) netapi32.NetApiBufferFree
@ stdcall -private DllInitialize(long long ptr) DllMain
