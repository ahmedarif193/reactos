# dwmcore.dll - DWM Core Composition Engine (MIL)
# Ordinal base: 1, 54 exports
#
# Implements the Media Integration Layer (MIL) command protocol used by
# dwm.exe for composition: channel/transport management, resource lifecycle,
# composition message loop, and visual target binding.

# --- 10. Utility and Miscellaneous ---
1 stdcall MIL3DCalcBrushToIdealSampleSpace(ptr ptr ptr ptr)
2 stdcall MIL3DCalcProjected2DBounds(ptr ptr ptr)

# --- 5. Channel Operations ---
3 stdcall MilChannel_AppendCommandData(ptr ptr long)
4 stdcall MilChannel_BeginCommand(ptr long long)
5 stdcall MilChannel_CommitChannel(ptr)
6 stdcall MilChannel_EndCommand(ptr)
7 stdcall MilChannel_FreeSyncCommandReplay(ptr ptr)
8 stdcall MilChannel_GetMarshalType(ptr ptr)
9 stdcall MilChannel_SendSyncCommand(ptr ptr long ptr ptr long)
10 stdcall MilChannel_SetNotificationWindow(ptr ptr long)
11 stdcall MilChannel_SetReceiveBroadcastMessages(ptr long)

# --- 3. Command Transport ---
12 stdcall MilCommandTransport_AddRef(ptr)
13 stdcall MilCommandTransport_Release(ptr)

# --- 1. Composition Engine Lifecycle ---
14 stdcall MilCompositionEngine_DeinitializePartitionManager()
15 stdcall MilCompositionEngine_GetComposedEventId(ptr)
16 stdcall MilCompositionEngine_GetFeedbackReader(ptr)
17 stdcall MilCompositionEngine_InitializePartitionManager(ptr)
18 stdcall MilCompositionEngine_UpdateSchedulerSettings(ptr)

# --- 7. Composition Message Loop ---
19 stdcall MilComposition_PeekNextMessage(ptr)
20 stdcall MilComposition_SyncFlush(ptr)
21 stdcall MilComposition_WaitForNextMessage(ptr long)

# --- 4. Connection Management ---
22 stdcall MilConnectionManager_NotifyHostEvent(ptr)
23 stdcall MilConnection_ClearSfmEventOnPartition(ptr ptr)
24 stdcall MilConnection_CreateChannel(ptr ptr ptr)
25 stdcall MilConnection_DestroyChannel(ptr)
26 stdcall MilConnection_HandleSfmEventOnPartition(ptr ptr)
27 stdcall MilConnection_RecordUCE(ptr long)

# --- 10. Utility (cont.) ---
28 stdcall MilCoreClientIsDwm()

# --- 2. Transport Layer (cross-thread) ---
29 stdcall MilCrossThreadPacketTransport_Create(ptr ptr)

# --- 9. Player ---
30 stdcall MilPlayer_Create(ptr ptr)
31 stdcall MilPlayer_Process(ptr)

# --- 6. Resource Management ---
32 stdcall MilResource_CreateOrAddRefOnChannel(ptr long ptr)
33 stdcall MilResource_DuplicateHandle(ptr long ptr)
34 stdcall MilResource_ReleaseOnChannel(ptr long)
35 stdcall MilResource_SendCommand(ptr ptr long long)
36 stdcall MilResource_SendCommandBitmapSource(ptr ptr long)

# --- 2. Transport Layer ---
37 stdcall MilTransport_AddRef(ptr)
38 stdcall MilTransport_Close(ptr)
39 stdcall MilTransport_Create(ptr ptr)
40 stdcall MilTransport_CreateFromPacketTransport(ptr ptr ptr)
41 stdcall MilTransport_CreateSurfaceManager(ptr ptr)
42 stdcall MilTransport_CreateTransportParameters(ptr ptr)
43 stdcall MilTransport_DisconnectTransport(ptr)
44 stdcall MilTransport_InitializeConnectionManager(ptr ptr)
45 stdcall MilTransport_Open(ptr)
46 stdcall MilTransport_PostPacket(ptr ptr long)
47 stdcall MilTransport_Release(ptr)
48 stdcall MilTransport_ShutDownConnectionManager(ptr)

# --- 10. Utility (cont.) ---
49 stdcall MilUtility_GetTileBrushMapping(ptr ptr ptr ptr)
50 stdcall MilVersionCheck(long)

# --- 8. Visual Target ---
51 stdcall MilVisualTarget_AttachToHwnd(ptr ptr)
52 stdcall MilVisualTarget_DetachFromHwnd(ptr)

# --- 10. Utility (cont.) ---
53 stdcall SetMilPerfInstrumentationFlags(long)
54 stdcall MILCreateFactory(ptr long)
