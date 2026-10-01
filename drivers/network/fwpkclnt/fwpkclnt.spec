@ stdcall FwpmBfeStateGet0() netio.FwpmBfeStateGet0
@ stdcall FwpmBfeStateSubscribeChanges0(ptr ptr ptr ptr) netio.FwpmBfeStateSubscribeChanges0
@ stdcall FwpmBfeStateUnsubscribeChanges0(ptr) netio.FwpmBfeStateUnsubscribeChanges0
@ stdcall FwpmCalloutAdd0(ptr ptr ptr ptr) netio.FwpmCalloutAdd0
@ stdcall FwpmCalloutDeleteById0(ptr long) netio.FwpmCalloutDeleteById0
@ stdcall FwpmCalloutDeleteByKey0(ptr ptr) netio.FwpmCalloutDeleteByKey0
@ stdcall FwpmEngineClose0(ptr) netio.FwpmEngineClose0
@ stdcall FwpmEngineOpen0(ptr long ptr ptr ptr) netio.FwpmEngineOpen0
@ stdcall FwpmFilterAdd0(ptr ptr ptr ptr) netio.FwpmFilterAdd0
@ stdcall FwpmFilterDeleteById0(ptr double) netio.FwpmFilterDeleteById0
@ stdcall FwpmFilterDeleteByKey0(ptr ptr) netio.FwpmFilterDeleteByKey0
@ stdcall FwpmFilterGetById0(ptr double ptr) netio.FwpmFilterGetById0
@ stdcall FwpmFilterGetByKey0(ptr ptr ptr) netio.FwpmFilterGetByKey0
@ stdcall FwpmFreeMemory0(ptr) netio.FwpmFreeMemory0
@ stdcall FwpmProviderAdd0(ptr ptr ptr) netio.FwpmProviderAdd0
@ stdcall FwpmProviderContextAdd3(ptr ptr ptr ptr) netio.FwpmProviderContextAdd3
@ stdcall FwpmProviderContextDeleteById0(ptr int64) netio.FwpmProviderContextDeleteById0
@ stdcall FwpmProviderDeleteByKey0(ptr ptr) netio.FwpmProviderDeleteByKey0
@ stdcall FwpmSubLayerAdd0(ptr ptr ptr) netio.FwpmSubLayerAdd0
@ stdcall FwpmTransactionAbort0(ptr) netio.FwpmTransactionAbort0
@ stdcall FwpmTransactionBegin0(ptr long) netio.FwpmTransactionBegin0
@ stdcall FwpmTransactionCommit0(ptr) netio.FwpmTransactionCommit0
@ stdcall FwpsAllocateCloneNetBufferList0(ptr ptr ptr long ptr) netio.FwpsAllocateCloneNetBufferList0
@ stdcall FwpsAllocateNetBufferAndNetBufferList0(ptr long long ptr long long ptr) netio.FwpsAllocateNetBufferAndNetBufferList0
@ stdcall FwpsCalloutRegister0(ptr ptr ptr) netio.FwpsCalloutRegister0
@ stdcall FwpsCalloutRegister3(ptr ptr ptr) netio.FwpsCalloutRegister3
@ stdcall FwpsCalloutUnregisterById0(long) netio.FwpsCalloutUnregisterById0
@ stdcall FwpsCalloutUnregisterByKey0(ptr) netio.FwpsCalloutUnregisterByKey0
@ stdcall FwpsFreeCloneNetBufferList0(ptr long) netio.FwpsFreeCloneNetBufferList0
@ stdcall FwpsFreeNetBufferList0(ptr) netio.FwpsFreeNetBufferList0
@ stdcall FwpsInjectNetworkReceiveAsync0(ptr ptr long long long long ptr ptr ptr) netio.FwpsInjectNetworkReceiveAsync0
@ stdcall FwpsInjectNetworkSendAsync0(ptr ptr long long ptr ptr ptr) netio.FwpsInjectNetworkSendAsync0
@ stdcall FwpsInjectionHandleCreate0(long long ptr) netio.FwpsInjectionHandleCreate0
@ stdcall FwpsInjectionHandleDestroy0(ptr) netio.FwpsInjectionHandleDestroy0
@ stdcall FwpsNetBufferListRelease0(ptr) netio.FwpsNetBufferListRelease0
@ stdcall FwpsQueryPacketInjectionState0(ptr ptr ptr) netio.FwpsQueryPacketInjectionState0
@ stdcall FwpsVirtualIfTunnelInfoSet0(ptr ptr) netio.FwpsVirtualIfTunnelInfoSet0
@ stdcall IPsecGetStatistics0(ptr ptr) netio.IPsecGetStatistics0
@ stdcall IPsecSaContextAddInbound1(ptr int64 ptr) netio.IPsecSaContextAddInbound1
@ stdcall IPsecSaContextAddOutbound1(ptr int64 ptr) netio.IPsecSaContextAddOutbound1
@ stdcall IPsecSaContextCreate1(ptr ptr ptr ptr ptr) netio.IPsecSaContextCreate1
@ stdcall IPsecSaContextCreateEnumHandle0(ptr ptr ptr) netio.IPsecSaContextCreateEnumHandle0
@ stdcall IPsecSaContextDeleteById0(ptr int64) netio.IPsecSaContextDeleteById0
@ stdcall IPsecSaContextEnum1(ptr ptr long ptr ptr) netio.IPsecSaContextEnum1
@ stdcall IPsecSaContextGetSpi1(ptr int64 ptr ptr) netio.IPsecSaContextGetSpi1
@ stdcall IPsecSaContextSetSpi0(ptr int64 ptr long) netio.IPsecSaContextSetSpi0
@ stdcall IPsecSaCreateEnumHandle0(ptr ptr ptr) netio.IPsecSaCreateEnumHandle0
@ stdcall IPsecSaDestroyEnumHandle0(ptr ptr) netio.IPsecSaDestroyEnumHandle0
@ stdcall IPsecSaEnum1(ptr ptr long ptr ptr) netio.IPsecSaEnum1
