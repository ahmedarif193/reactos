/*
 * fwpvi.h
 *
 * Windows Filtering Platform — version identifiers and well-known GUIDs
 * for built-in providers, sublayers, and layers.
 *
 * This file is part of the ReactOS DDK package.
 *
 * Contributors:
 *   Created for the dev-nt6-1 branch as part of the NT 5.2 -> NT 6.1 upgrade.
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#pragma once
#define _FWPVI_

#ifdef __cplusplus
extern "C" {
#endif

#if ((NTDDI_VERSION >= NTDDI_WIN6))

#define FWPM_DISPLAY_DATA FWPM_DISPLAY_DATA0

#define FWPM_SESSION FWPM_SESSION0

#define FWPM_CLASSIFY_OPTION FWPM_CLASSIFY_OPTION0

#define FWPM_CLASSIFY_OPTIONS FWPM_CLASSIFY_OPTIONS0

#if ((NTDDI_VERSION >= NTDDI_WIN10_RS3))

#define FWPM_PROVIDER_CONTEXT FWPM_PROVIDER_CONTEXT3

#endif

#define FWPM_SUBLAYER FWPM_SUBLAYER0

#define FWPM_CALLOUT FWPM_CALLOUT0

#define FWPM_FILTER_CONDITION FWPM_FILTER_CONDITION0

#define FWPM_FILTER FWPM_FILTER0

#define FWPS_FILTER_CONDITION FWPS_FILTER_CONDITION0

#if ((NTDDI_VERSION >= NTDDI_WIN10_RS3))

#define FWPS_FILTER FWPS_FILTER3

#endif

#define FWPS_INCOMING_VALUE FWPS_INCOMING_VALUE0

#define FWPS_INCOMING_VALUES FWPS_INCOMING_VALUES0

#define FWPS_CLASSIFY_OUT FWPS_CLASSIFY_OUT0

#define FWP_VALUE  FWP_VALUE0

#define FWP_RANGE  FWP_RANGE0

#define FWP_CONDITION_VALUE  FWP_CONDITION_VALUE0

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define IPSEC_KEYING_POLICY IPSEC_KEYING_POLICY1

#endif
#if !((NTDDI_VERSION >= NTDDI_WIN8))

#define IPSEC_KEYING_POLICY IPSEC_KEYING_POLICY0

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN7))

#define IPSEC_DOSP_OPTIONS IPSEC_DOSP_OPTIONS0

#endif

#define FwpmFreeMemory FwpmFreeMemory0

#define FwpmBfeStateGet FwpmBfeStateGet0

#define FwpmBfeStateSubscribeChanges FwpmBfeStateSubscribeChanges0

#define FwpmBfeStateUnsubscribeChanges FwpmBfeStateUnsubscribeChanges0

#define FwpmEngineOpen FwpmEngineOpen0

#define FwpmEngineClose FwpmEngineClose0

#define FwpmTransactionBegin FwpmTransactionBegin0

#define FwpmTransactionCommit FwpmTransactionCommit0

#define FwpmTransactionAbort FwpmTransactionAbort0

#define FwpmSubLayerAdd FwpmSubLayerAdd0

#define FwpmCalloutAdd FwpmCalloutAdd0

#define FwpmCalloutGetById FwpmCalloutGetById0

#define FwpmFilterAdd FwpmFilterAdd0

#define FwpmFilterDeleteById FwpmFilterDeleteById0

#define FwpmFilterDeleteByKey FwpmFilterDeleteByKey0

#define FwpmFilterGetById FwpmFilterGetById0

#define FwpmFilterGetByKey FwpmFilterGetByKey0

#define FwpmSubLayerDeleteByKey FwpmSubLayerDeleteByKey0

#define FwpmSubLayerGetByKey FwpmSubLayerGetByKey0

#define FwpmCalloutDeleteById FwpmCalloutDeleteById0

#define FwpmCalloutDeleteByKey FwpmCalloutDeleteByKey0

#define FwpmProviderAdd FwpmProviderAdd0

#define FwpmProviderDeleteByKey FwpmProviderDeleteByKey0

#define FwpmGetAppIdFromFileName FwpmGetAppIdFromFileName0

#define FWPM_PROVIDER FWPM_PROVIDER0

#define FWPS_INCOMING_METADATA_VALUES FWPS_INCOMING_METADATA_VALUES0

#if ((NTDDI_VERSION >= NTDDI_WIN10_RS3))

#define FWPS_CALLOUT_CLASSIFY_FN FWPS_CALLOUT_CLASSIFY_FN3

#define FWPS_CALLOUT_NOTIFY_FN FWPS_CALLOUT_NOTIFY_FN3

#endif

#define FWPS_CALLOUT_FLOW_DELETE_NOTIFY_FN FWPS_CALLOUT_FLOW_DELETE_NOTIFY_FN0

#if ((NTDDI_VERSION >= NTDDI_WIN10_RS3))

#define FWPS_CALLOUT FWPS_CALLOUT3

#define FwpsCalloutRegister FwpsCalloutRegister3

#endif

#define FwpsCalloutUnregisterById FwpsCalloutUnregisterById0

#define FwpsCalloutUnregisterByKey FwpsCalloutUnregisterByKey0

#define FwpsFlowAssociateContext FwpsFlowAssociateContext0

#define FwpsFlowRemoveContext FwpsFlowRemoveContext0

#define FWPS_PACKET_LIST_INFORMATION FWPS_PACKET_LIST_INFORMATION0

#define FwpsGetPacketListSecurityInformation FwpsGetPacketListSecurityInformation0

#define FwpsPendOperation FwpsPendOperation0

#define FwpsCompleteOperation FwpsCompleteOperation0

#if ((NTDDI_VERSION >= NTDDI_WIN7))

#define FwpsAcquireClassifyHandle FwpsAcquireClassifyHandle0

#define FwpsReleaseClassifyHandle FwpsReleaseClassifyHandle0

#define FwpsPendClassify FwpsPendClassify0

#define FwpsCompleteClassify FwpsCompleteClassify0

#define FwpsAcquireWritableLayerDataPointer FwpsAcquireWritableLayerDataPointer0

#define FwpsApplyModifiedLayerData FwpsApplyModifiedLayerData0

#define FWPS_CONNECT_REQUEST FWPS_CONNECT_REQUEST0

#define FWPS_BIND_REQUEST FWPS_BIND_REQUEST0

#endif

#define FwpsInjectionHandleCreate FwpsInjectionHandleCreate0

#define FwpsInjectionHandleDestroy FwpsInjectionHandleDestroy0

#define FWPS_INJECT_COMPLETE FWPS_INJECT_COMPLETE0

#define FwpsAllocateNetBufferAndNetBufferList FwpsAllocateNetBufferAndNetBufferList0

#define FwpsFreeNetBufferList FwpsFreeNetBufferList0

#define FwpsAllocateCloneNetBufferList FwpsAllocateCloneNetBufferList0

#define FwpsFreeCloneNetBufferList FwpsFreeCloneNetBufferList0

#define FwpsInjectNetworkSendAsync FwpsInjectNetworkSendAsync0

#define FwpsInjectForwardAsync FwpsInjectForwardAsync0

#define FwpsConstructIpHeaderForTransportPacket FwpsConstructIpHeaderForTransportPacket0

#if ((NTDDI_VERSION >= NTDDI_WIN7))

#define FWPS_TRANSPORT_SEND_PARAMS FWPS_TRANSPORT_SEND_PARAMS1

#define FwpsInjectTransportSendAsync FwpsInjectTransportSendAsync1

#endif
#if !((NTDDI_VERSION >= NTDDI_WIN7))

#define FWPS_TRANSPORT_SEND_PARAMS FWPS_TRANSPORT_SEND_PARAMS0

#define FwpsInjectTransportSendAsync FwpsInjectTransportSendAsync0

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define FwpsInjectMacSendAsync FwpsInjectMacSendAsync0

#define FwpsInjectMacReceiveAsync FwpsInjectMacReceiveAsync0

#define FwpsInjectvSwitchEthernetIngressAsync FwpsInjectvSwitchEthernetIngressAsync0

#define FwpsReferencevSwitchPacketContext FwpsReferencevSwitchPacketContext0

#define FwpsDereferencevSwitchPacketContext FwpsDereferencevSwitchPacketContext0

#define FwpsRedirectHandleCreate FwpsRedirectHandleCreate0

#define FwpsRedirectHandleDestroy FwpsRedirectHandleDestroy0

#define FwpsQueryConnectionRedirectState FwpsQueryConnectionRedirectState0

#endif

#define FwpsInjectTransportReceiveAsync FwpsInjectTransportReceiveAsync0

#define FwpsInjectNetworkReceiveAsync FwpsInjectNetworkReceiveAsync0

#define FwpsReferenceNetBufferList FwpsReferenceNetBufferList0

#define FwpsDereferenceNetBufferList FwpsDereferenceNetBufferList0

#define FwpsQueryPacketInjectionState FwpsQueryPacketInjectionState0

#define FWPS_STREAM_DATA FWPS_STREAM_DATA0

#define FWPS_STREAM_CALLOUT_IO_PACKET FWPS_STREAM_CALLOUT_IO_PACKET0

#define FwpsStreamInjectAsync FwpsStreamInjectAsync0

#define FwpsCopyStreamDataToBuffer FwpsCopyStreamDataToBuffer0

#define FwpsCloneStreamData FwpsCloneStreamData0

#define FwpsDiscardClonedStreamData FwpsDiscardClonedStreamData0

#endif

#if ((NTDDI_VERSION >= NTDDI_WIN6))

#define FwpmEngineGetOption FwpmEngineGetOption0

#define FwpmEngineSetOption FwpmEngineSetOption0

#define FwpmEngineGetSecurityInfo FwpmEngineGetSecurityInfo0

#define FwpmEngineSetSecurityInfo FwpmEngineSetSecurityInfo0

#define FwpmSessionCreateEnumHandle FwpmSessionCreateEnumHandle0

#define FwpmSessionEnum FwpmSessionEnum0

#define FwpmSessionDestroyEnumHandle FwpmSessionDestroyEnumHandle0

#define FwpmProviderGetByKey FwpmProviderGetByKey0

#define FwpmProviderCreateEnumHandle FwpmProviderCreateEnumHandle0

#define FwpmProviderEnum FwpmProviderEnum0

#define FwpmProviderDestroyEnumHandle FwpmProviderDestroyEnumHandle0

#define FwpmProviderGetSecurityInfoByKey FwpmProviderGetSecurityInfoByKey0

#define FwpmProviderSetSecurityInfoByKey FwpmProviderSetSecurityInfoByKey0

#define FwpmProviderSubscribeChanges FwpmProviderSubscribeChanges0

#define FwpmProviderUnsubscribeChanges FwpmProviderUnsubscribeChanges0

#define FwpmProviderSubscriptionsGet FwpmProviderSubscriptionsGet0

#if ((NTDDI_VERSION >= NTDDI_WIN10_RS3))

#define FwpmProviderContextAdd FwpmProviderContextAdd3

#define FwpmProviderContextGetById FwpmProviderContextGetById3

#define FwpmProviderContextGetByKey FwpmProviderContextGetByKey3

#define FwpmProviderContextEnum FwpmProviderContextEnum3

#endif

#define FwpmProviderContextDeleteById FwpmProviderContextDeleteById0

#define FwpmProviderContextDeleteByKey FwpmProviderContextDeleteByKey0

#define FwpmProviderContextCreateEnumHandle FwpmProviderContextCreateEnumHandle0

#define FwpmProviderContextDestroyEnumHandle FwpmProviderContextDestroyEnumHandle0

#define FwpmProviderContextGetSecurityInfoByKey FwpmProviderContextGetSecurityInfoByKey0

#define FwpmProviderContextSetSecurityInfoByKey FwpmProviderContextSetSecurityInfoByKey0

#define FwpmProviderContextSubscribeChanges FwpmProviderContextSubscribeChanges0

#define FwpmProviderContextUnsubscribeChanges FwpmProviderContextUnsubscribeChanges0

#define FwpmProviderContextSubscriptionsGet FwpmProviderContextSubscriptionsGet0

#define FwpmSubLayerCreateEnumHandle FwpmSubLayerCreateEnumHandle0

#define FwpmSubLayerEnum FwpmSubLayerEnum0

#define FwpmSubLayerDestroyEnumHandle FwpmSubLayerDestroyEnumHandle0

#define FwpmSubLayerGetSecurityInfoByKey FwpmSubLayerGetSecurityInfoByKey0

#define FwpmSubLayerSetSecurityInfoByKey FwpmSubLayerSetSecurityInfoByKey0

#define FwpmSubLayerSubscribeChanges FwpmSubLayerSubscribeChanges0

#define FwpmSubLayerUnsubscribeChanges FwpmSubLayerUnsubscribeChanges0

#define FwpmSubLayerSubscriptionsGet FwpmSubLayerSubscriptionsGet0

#define FwpmLayerGetById FwpmLayerGetById0

#define FwpmLayerGetByKey FwpmLayerGetByKey0

#define FwpmLayerCreateEnumHandle FwpmLayerCreateEnumHandle0

#define FwpmLayerEnum FwpmLayerEnum0

#define FwpmLayerDestroyEnumHandle FwpmLayerDestroyEnumHandle0

#define FwpmLayerGetSecurityInfoByKey FwpmLayerGetSecurityInfoByKey0

#define FwpmLayerSetSecurityInfoByKey FwpmLayerSetSecurityInfoByKey0

#define FwpmCalloutGetByKey FwpmCalloutGetByKey0

#define FwpmCalloutCreateEnumHandle FwpmCalloutCreateEnumHandle0

#define FwpmCalloutEnum FwpmCalloutEnum0

#define FwpmCalloutDestroyEnumHandle FwpmCalloutDestroyEnumHandle0

#define FwpmCalloutGetSecurityInfoByKey FwpmCalloutGetSecurityInfoByKey0

#define FwpmCalloutSetSecurityInfoByKey FwpmCalloutSetSecurityInfoByKey0

#define FwpmCalloutSubscribeChanges FwpmCalloutSubscribeChanges0

#define FwpmCalloutUnsubscribeChanges FwpmCalloutUnsubscribeChanges0

#define FwpmCalloutSubscriptionsGet FwpmCalloutSubscriptionsGet0

#define FwpmFilterCreateEnumHandle FwpmFilterCreateEnumHandle0

#define FwpmFilterEnum FwpmFilterEnum0

#define FwpmFilterDestroyEnumHandle FwpmFilterDestroyEnumHandle0

#define FwpmFilterGetSecurityInfoByKey FwpmFilterGetSecurityInfoByKey0

#define FwpmFilterSetSecurityInfoByKey FwpmFilterSetSecurityInfoByKey0

#define FwpmFilterSubscribeChanges FwpmFilterSubscribeChanges0

#define FwpmFilterUnsubscribeChanges FwpmFilterUnsubscribeChanges0

#define FwpmFilterSubscriptionsGet FwpmFilterSubscriptionsGet0

#if ((NTDDI_VERSION >= NTDDI_WIN10_RS3))

#define FwpmIPsecTunnelAdd FwpmIPsecTunnelAdd3

#endif

#define FwpmIPsecTunnelDeleteByKey FwpmIPsecTunnelDeleteByKey0

#if ((NTDDI_VERSION >= NTDDI_WINTHRESHOLD))

#define FwpsVirtualIfTunnelInfoGet FwpsVirtualIfTunnelInfoGet0

#define FwpsVirtualIfTunnelInfoSet FwpsVirtualIfTunnelInfoSet0

#endif

#define FwpmNetEventCreateEnumHandle FwpmNetEventCreateEnumHandle0

#if ((NTDDI_VERSION >= NTDDI_WIN10_RS5))

#define FwpmNetEventEnum FwpmNetEventEnum5

#endif

#define FwpmNetEventDestroyEnumHandle FwpmNetEventDestroyEnumHandle0

#define FwpmNetEventsGetSecurityInfo FwpmNetEventsGetSecurityInfo0

#define FwpmNetEventsSetSecurityInfo FwpmNetEventsSetSecurityInfo0

#if ((NTDDI_VERSION >= NTDDI_WIN10_RS5))

#define FwpmNetEventSubscribe FwpmNetEventSubscribe4

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN7))

#define FwpmNetEventUnsubscribe FwpmNetEventUnsubscribe0

#define FwpmNetEventSubscriptionsGet FwpmNetEventSubscriptionsGet0

#define FwpmSystemPortsGet FwpmSystemPortsGet0

#define FwpmSystemPortsSubscribe FwpmSystemPortsSubscribe0

#define FwpmSystemPortsUnsubscribe FwpmSystemPortsUnsubscribe0

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN10_CO))

#define FwpmDynamicKeywordSubscribe FwpmDynamicKeywordSubscribe0

#define FwpmDynamicKeywordUnsubscribe FwpmDynamicUnsubscribe0

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define FwpmConnectionSubscribe FwpmConnectionSubscribe0

#define FwpmConnectionUnsubscribe FwpmConnectionUnsubscribe0

#define FwpmConnectionGetById FwpmConnectionGetById0

#define FwpmConnectionEnum FwpmConnectionEnum0

#define FwpmConnectionCreateEnumHandle FwpmConnectionCreateEnumHandle0

#define FwpmConnectionDestroyEnumHandle FwpmConnectionDestroyEnumHandle0

#define FwpmConnectionSubscriptionsGet FwpmConnectionSubscriptionsGet0

#define FwpmConnectionGetSecurityInfo FwpmConnectionGetSecurityInfo0

#define FwpmConnectionSetSecurityInfo FwpmConnectionSetSecurityInfo0

#define FwpmvSwitchEventSubscribe FwpmvSwitchEventSubscribe0

#define FwpmvSwitchEventUnsubscribe FwpmvSwitchEventUnsubscribe0

#define FwpmvSwitchEventsGetSecurityInfo FwpmvSwitchEventsGetSecurityInfo0

#define FwpmvSwitchEventsSetSecurityInfo FwpmvSwitchEventsSetSecurityInfo0

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN11_ZN))

#define FwpmConnectionPolicyAdd FwpmConnectionPolicyAdd0

#define FwpmConnectionPolicyDeleteByKey FwpmConnectionPolicyDeleteByKey0

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define FWPS_VSWITCH_EVENT_DISPATCH_TABLE FWPS_VSWITCH_EVENT_DISPATCH_TABLE0

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN10_RS3))

#define FwpsvSwitchEventsSubscribe FwpsvSwitchEventsSubscribe0

#define FwpsvSwitchEventsUnsubscribe FwpsvSwitchEventsUnsubscribe0

#define FwpsvSwitchNotifyComplete FwpsvSwitchNotifyComplete0

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN7))
#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define FwpsNetBufferListAssociateContext FwpsNetBufferListAssociateContext1

#define FwpsFlowAbort FwpsFlowAbort0

#endif
#if !((NTDDI_VERSION >= NTDDI_WIN8))

#define FwpsNetBufferListAssociateContext FwpsNetBufferListAssociateContext0

#endif

#define FwpsNetBufferListGetTagForContext FwpsNetBufferListGetTagForContext0

#define FwpsNetBufferListRetrieveContext FwpsNetBufferListRetrieveContext0

#define FwpsNetBufferListRemoveContext FwpsNetBufferListRemoveContext0

#define FwpsOpenToken FwpsOpenToken0

#define FwpsAleEndpointGetById FwpsAleEndpointGetById0

#define FwpsAleEndpointCreateEnumHandle FwpsAleEndpointCreateEnumHandle0

#define FwpsAleEndpointEnum FwpsAleEndpointEnum0

#define FwpsAleEndpointDestroyEnumHandle FwpsAleEndpointDestroyEnumHandle0

#define FwpsAleEndpointGetSecurityInfo FwpsAleEndpointGetSecurityInfo0

#define FwpsAleEndpointSetSecurityInfo FwpsAleEndpointSetSecurityInfo0

#endif

#define FwpsClassifyOptionSet FwpsClassifyOptionSet0

#define FwpsReassembleForwardFragmentGroup FwpsReassembleForwardFragmentGroup0

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define FwpsQueryConnectionSioFormatRedirectRecords FwpsQueryConnectionSioFormatRedirectRecords0

#endif

#define FwpsStreamContinue FwpsStreamContinue0

#endif

#ifdef __cplusplus
}
#endif
