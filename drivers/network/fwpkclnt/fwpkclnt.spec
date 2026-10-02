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
@ stub DPChannelCreate0
@ stub DPChannelDestroy0
@ stub DPGetProcessIdFromProfileName0
@ stub DPReceiveNetBufferListComplete0
@ stub DPSendNetBufferList0
@ stub FwpiCalloutRegisterAndAddWithoutDevice0
@ stub FwpiCalloutRegisterWithoutDeviceFast0
@ stub FwpiCalloutUnregisterAndDeleteByKey0
@ stub FwpiFlowAssociateContextFast
@ stub FwpiFlowRemoveContextFast
@ stub FwpiGetConnectionLuid0
@ stub FwpiGetEndpointInformationFromClassifyMetavalues
@ stub FwpiGetValueFromClassifyContext
@ stub FwpiIsConnectionEdgeTraversed0
@ stub FwpiIsPreviouslyLocalRedirected
@ stub FwpiNetBufferListAssociateContextWithoutDevice0
@ stub FwpiParseIPv6Protocol0
@ stub FwpiReleaseFlowLocation
@ stub FwpiReserveFlowLocation
@ stub FwpiSetEndpointBindingInterface
@ stub FwpmBfeStateSubscribeChangesWithoutDevice0
@ stub FwpmCalloutCreateEnumHandle0
@ stub FwpmCalloutDestroyEnumHandle0
@ stub FwpmCalloutEnum0
@ stub FwpmCalloutGetById0
@ stub FwpmCalloutGetByKey0
@ stub FwpmCalloutGetSecurityInfoByKey0
@ stub FwpmCalloutSetSecurityInfoByKey0
@ stub FwpmConnectionCreateEnumHandle0
@ stub FwpmConnectionDestroyEnumHandle0
@ stub FwpmConnectionEnum0
@ stub FwpmConnectionGetById0
@ stub FwpmConnectionGetSecurityInfo0
@ stub FwpmConnectionPolicyAdd0
@ stub FwpmConnectionPolicyDeleteByKey0
@ stub FwpmConnectionSetSecurityInfo0
@ stub FwpmDiagnoseNetFailure0
@ stub FwpmEngineGetOption0
@ stub FwpmEngineGetSecurityInfo0
@ stub FwpmEngineSetOption0
@ stub FwpmEngineSetSecurityInfo0
@ stub FwpmFilterCreateEnumHandle0
@ stub FwpmFilterDestroyEnumHandle0
@ stub FwpmFilterEnum0
@ stub FwpmFilterGetSecurityInfoByKey0
@ stub FwpmFilterSetSecurityInfoByKey0
@ stub FwpmIPsecS2STunnelAddConditions0
@ stub FwpmIPsecS2STunnelRemoveConditions0
@ stub FwpmIPsecTunnelAdd0
@ stub FwpmIPsecTunnelAdd1
@ stub FwpmIPsecTunnelAdd2
@ stub FwpmIPsecTunnelAdd3
@ stub FwpmIPsecTunnelAddConditions0
@ stub FwpmIPsecTunnelDeleteByKey0
@ stub FwpmLayerCreateEnumHandle0
@ stub FwpmLayerDestroyEnumHandle0
@ stub FwpmLayerEnum0
@ stub FwpmLayerGetById0
@ stub FwpmLayerGetByKey0
@ stub FwpmLayerGetSecurityInfoByKey0
@ stub FwpmLayerSetSecurityInfoByKey0
@ stub FwpmProviderContextAdd0
@ stub FwpmProviderContextAdd1
@ stub FwpmProviderContextAdd2
@ stub FwpmProviderContextCreateEnumHandle0
@ stub FwpmProviderContextDeleteByKey0
@ stub FwpmProviderContextDestroyEnumHandle0
@ stub FwpmProviderContextEnum0
@ stub FwpmProviderContextEnum1
@ stub FwpmProviderContextEnum2
@ stub FwpmProviderContextEnum3
@ stub FwpmProviderContextGetById0
@ stub FwpmProviderContextGetById1
@ stub FwpmProviderContextGetById2
@ stub FwpmProviderContextGetById3
@ stub FwpmProviderContextGetByKey0
@ stub FwpmProviderContextGetByKey1
@ stub FwpmProviderContextGetByKey2
@ stub FwpmProviderContextGetByKey3
@ stub FwpmProviderContextGetSecurityInfoByKey0
@ stub FwpmProviderContextSetSecurityInfoByKey0
@ stub FwpmProviderCreateEnumHandle0
@ stub FwpmProviderDestroyEnumHandle0
@ stub FwpmProviderEnum0
@ stub FwpmProviderGetByKey0
@ stub FwpmProviderGetSecurityInfoByKey0
@ stub FwpmProviderSetSecurityInfoByKey0
@ stub FwpmSecureSocketAddAsync0
@ stub FwpmSecureSocketDeleteByKeyAsync0
@ stub FwpmSessionCreateEnumHandle0
@ stub FwpmSessionDestroyEnumHandle0
@ stub FwpmSessionEnum0
@ stub FwpmSubLayerCreateEnumHandle0
@ stdcall FwpmSubLayerDeleteByKey0(ptr ptr) netio.FwpmSubLayerDeleteByKey0
@ stub FwpmSubLayerDestroyEnumHandle0
@ stub FwpmSubLayerEnum0
@ stub FwpmSubLayerGetByKey0
@ stub FwpmSubLayerGetSecurityInfoByKey0
@ stub FwpmSubLayerSetSecurityInfoByKey0
@ stub FwpmvSwitchEventFire0
@ stub FwpmvSwitchEventsGetSecurityInfo0
@ stub FwpmvSwitchEventsSetSecurityInfo0
@ stub FwppAllocateNetioCloneNetBufferList
@ stub FwppBfeStateGetResetCount0
@ stub FwppDPDispatchTableAndGlobalsSet0
@ stub FwppDeepCloneNetBufferList
@ stub FwppDereferencevSwitchNblContext
@ stub FwppDispatchDevCtl0
@ stub FwppEdpDispatchTableAndGlobalsSet0
@ stub FwppFreeDeepCloneNetBufferList
@ stub FwppGetvSwitchNblContext
@ stub FwppNetBufferListAssociateContext
@ stub FwppNetBufferListEventNotify
@ stub FwppProcessorAddHandler
@ stub FwppReferencevSwitchNblContext
@ stub FwppVpnTriggerEventFire0
@ stub FwppvSwitchCopyVmSwitchNblInfo
@ stub FwppvSwitchCreateNotify
@ stub FwppvSwitchDeleteNotify
@ stub FwppvSwitchFreeVmSwitchNblInfo
@ stub FwppvSwitchGetDestinationArray
@ stub FwppvSwitchGetDestinationInterface
@ stub FwppvSwitchLwfReorderEventNotify
@ stub FwppvSwitchNicEventNotify
@ stub FwppvSwitchNotifyCompletionFnSet
@ stub FwppvSwitchPolicyEventNotify
@ stub FwppvSwitchPortEventNotify
@ stub FwppvSwitchRuntimeStateRestoreNotify
@ stub FwppvSwitchRuntimeStateSaveNotify
@ stub FwpsAcquireClassifyHandle0
@ stub FwpsAcquireWritableLayerDataPointer0
@ stub FwpsAleEndpointCreateEnumHandle0
@ stub FwpsAleEndpointDestroyEnumHandle0
@ stub FwpsAleEndpointEnum0
@ stub FwpsAleEndpointGetById0
@ stub FwpsAleEndpointGetSecurityInfo0
@ stub FwpsAleEndpointSetSecurityInfo0
@ stub FwpsAleExplicitCredentialsQuery0
@ stub FwpsAllocateDeepCloneNetBufferList0
@ stub FwpsApplyModifiedLayerData0
@ stdcall FwpsCalloutRegister1(ptr ptr ptr) netio.FwpsCalloutRegister1
@ stdcall FwpsCalloutRegister2(ptr ptr ptr) netio.FwpsCalloutRegister2
@ stub FwpsCalloutRegisterWithoutDevice0
@ stub FwpsCancelEndpointDeleteNotification0
@ stub FwpsClassifyOptionSet0
@ stub FwpsCloneStreamData0
@ stub FwpsCompleteClassify0
@ stub FwpsCompleteOperation0
@ stdcall FwpsConstructIpHeaderForTransportPacket0(ptr long long ptr ptr long int64 ptr long long ptr long long) netio.FwpsConstructIpHeaderForTransportPacket0
@ stdcall FwpsCopyStreamDataToBuffer0(ptr ptr ptr ptr) netio.FwpsCopyStreamDataToBuffer0
@ stdcall FwpsDereferenceNetBufferList0(ptr long) netio.FwpsDereferenceNetBufferList0
@ stub FwpsDereferencevSwitchPacketContext0
@ stub FwpsDiscardClonedStreamData0
@ stub FwpsFlowAbort0
@ stdcall FwpsFlowAssociateContext0(int64 long long int64) netio.FwpsFlowAssociateContext0
@ stdcall FwpsFlowRemoveContext0(int64 long long) netio.FwpsFlowRemoveContext0
@ stub FwpsForceReclassifyLayer0
@ stub FwpsFreeMemory0
@ stub FwpsGetPacketListSecurityInformation0
@ stub FwpsIPSecGetPacketListSecurityInformation
@ stub FwpsInjectForwardAsync0
@ stub FwpsInjectMacReceiveAsync0
@ stub FwpsInjectMacSendAsync0
@ stdcall FwpsInjectTransportReceiveAsync0(ptr ptr ptr long long long long long ptr ptr ptr) netio.FwpsInjectTransportReceiveAsync0
@ stdcall FwpsInjectTransportSendAsync0(ptr ptr int64 long ptr long long ptr ptr ptr) netio.FwpsInjectTransportSendAsync0
@ stdcall FwpsInjectTransportSendAsync1(ptr ptr int64 long ptr long long ptr ptr ptr) netio.FwpsInjectTransportSendAsync1
@ stub FwpsInjectvSwitchEthernetIngressAsync0
@ stub FwpsL2DispatchTableAndGlobalsSet0
@ stub FwpsL2DispatchTableClear0
@ stub FwpsNetBufferListAssociateContext0
@ stub FwpsNetBufferListAssociateContext1
@ stub FwpsNetBufferListGetTagForContext0
@ stub FwpsNetBufferListRemoveContext0
@ stub FwpsNetBufferListRetrieveContext0
@ stub FwpsPendClassify0
@ stub FwpsPendOperation0
@ stub FwpsProxiedEndpointClassifiableFieldGet
@ stub FwpsProxiedEndpointDereferenceEndpoint
@ stub FwpsProxiedEndpointMetadataValueGet
@ stub FwpsProxiedEndpointReferenceEndpoint
@ stub FwpsProxiedEndpointRegisterForExitingEndpoint
@ stub FwpsProxiedEndpointUnRegisterForExitingEndpoint
@ stub FwpsProxiedEndpointWasRedirectedToProxy
@ stub FwpsQueryConnectionRedirectState0
@ stub FwpsQueryConnectionSioFormatRedirectRecords0
@ stub FwpsQueryOutstandingNbls0
@ stub FwpsReassembleForwardFragmentGroup0
@ stub FwpsRedirectHandleCreate0
@ stub FwpsRedirectHandleDestroy0
@ stdcall FwpsReferenceNetBufferList0(ptr long) netio.FwpsReferenceNetBufferList0
@ stub FwpsReferencevSwitchPacketContext0
@ stub FwpsReleaseClassifyHandle0
@ stub FwpsRequestEndpointDeleteNotification0
@ stub FwpsSignalIPsecDecryptCompleteIkeV20
@ stub FwpsStreamContinue0
@ stub FwpsStreamInjectAsync0
@ stub FwpsTcpIpDispatchTableAndGlobalsSet0
@ stub FwpsTcpIpDispatchTableClear0
@ stub FwpsVirtualIfTunnelInfoGet0
@ stub FwpsvSwitchEventsSubscribe0
@ stub FwpsvSwitchEventsUnsubscribe0
@ stub FwpsvSwitchNotifyComplete0
@ stub GetUnifiedTraceHandle
@ stub IPsecDospGetSecurityInfo0
@ stub IPsecDospGetStatistics0
@ stub IPsecDospSetSecurityInfo0
@ stub IPsecDospStateCreateEnumHandle0
@ stub IPsecDospStateDestroyEnumHandle0
@ stub IPsecDospStateEnum0
@ stub IPsecDriverExpire
@ stub IPsecDriverInitiateAcquire
@ stub IPsecDriverProcessClearTextResponse
@ stub IPsecDriverSaOffloaded
@ stub IPsecGetStatistics1
@ stub IPsecSaContextAddInbound0
@ stub IPsecSaContextAddOutbound0
@ stub IPsecSaContextCreate0
@ stub IPsecSaContextDestroyEnumHandle0
@ stub IPsecSaContextEnum0
@ stub IPsecSaContextExpire0
@ stub IPsecSaContextGetById0
@ stub IPsecSaContextGetById1
@ stub IPsecSaContextGetSpi0
@ stub IPsecSaContextUpdate0
@ stub IPsecSaDbGetSecurityInfo0
@ stub IPsecSaDbSetSecurityInfo0
@ stub IPsecSaEnum0
@ stub IkeextGetStatistics0
@ stub IkeextGetStatistics1
@ stub IkeextSaCreateEnumHandle0
@ stub IkeextSaDbGetSecurityInfo0
@ stub IkeextSaDbSetSecurityInfo0
@ stub IkeextSaDeleteById0
@ stub IkeextSaDestroyEnumHandle0
@ stub IkeextSaEnum0
@ stub IkeextSaEnum1
@ stub IkeextSaEnum2
@ stub IkeextSaGetById0
@ stub IkeextSaGetById1
@ stub IkeextSaGetById2
@ stub NikEdpFreeMemory0
@ stub NikEdpGetEnterpriseId0
@ stub NikEdpGetEnterpriseIdAsync0
@ stub NikEdpGetEnterpriseIdClose0
@ stub NikEdpNotifyProcessTokenChange0
