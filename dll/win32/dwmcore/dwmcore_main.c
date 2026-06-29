/*
 * PROJECT:     ReactOS DWM Core Composition Engine
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     MIL (Media Integration Layer) composition engine compatibility
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * dwmcore.dll is the core composition engine of the Desktop Window Manager.
 * It implements the MIL command protocol: transport/channel management,
 * resource lifecycle, visual tree composition, and the composition message
 * loop.  dwm.exe calls into these functions to drive the composition pipeline.
 *
 * This is a compatibility implementation, not a hardware composition engine.
 * It keeps enough typed MIL object lifetime for callers to exercise startup and
 * teardown paths without handing out fixed fake handles.
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(dwmcore);

/* ========================================================================
 * Internal types
 *
 * Opaque handle types for the MIL object hierarchy.
 * ====================================================================== */

typedef void *HMILCHANNEL;
typedef void *HMILRESOURCE;
typedef void *HMILTRANSPORT;
typedef void *HMILCONNECTION;
typedef void *HMILPLAYER;
typedef void *HMILVISUALTARGET;

#define MIL_OBJECT_MAGIC 0x4d494c4f /* "MILO" */

typedef enum _MIL_OBJECT_TYPE
{
    MilObjectTransport = 1,
    MilObjectConnection,
    MilObjectChannel,
    MilObjectResource,
    MilObjectParameters,
    MilObjectSurfaceManager,
    MilObjectPlayer,
    MilObjectVisualTarget,
} MIL_OBJECT_TYPE;

typedef struct _MIL_OBJECT
{
    DWORD Magic;
    MIL_OBJECT_TYPE Type;
    LONG RefCount;
    DWORD Flags;
    DWORD ResourceType;
    HWND NotificationWindow;
    UINT NotificationMessage;
    BOOL ReceiveBroadcasts;
    struct _MIL_OBJECT *Next;
    struct _MIL_OBJECT *Prev;
} MIL_OBJECT;

static BOOL g_PartitionInitialized = FALSE;
static HANDLE g_CompositionEvent = NULL;
static CRITICAL_SECTION g_MilObjectLock;
static BOOL g_MilObjectLockInitialized;
static MIL_OBJECT *g_MilObjectList;

static MIL_OBJECT *
MilObjectFindLocked(void *Handle, MIL_OBJECT_TYPE Type)
{
    MIL_OBJECT *Object;

    if (!Handle)
        return NULL;

    for (Object = g_MilObjectList; Object; Object = Object->Next)
    {
        if (Object == Handle && Object->Magic == MIL_OBJECT_MAGIC &&
            Object->Type == Type)
            return Object;
    }

    return NULL;
}

static MIL_OBJECT *
MilObjectFromHandle(void *Handle, MIL_OBJECT_TYPE Type)
{
    MIL_OBJECT *Object;

    if (!g_MilObjectLockInitialized)
        return NULL;

    EnterCriticalSection(&g_MilObjectLock);
    Object = MilObjectFindLocked(Handle, Type);
    LeaveCriticalSection(&g_MilObjectLock);

    return Object;
}

static HRESULT
MilObjectCreate(MIL_OBJECT_TYPE Type, void **Handle)
{
    MIL_OBJECT *Object;

    if (!Handle)
        return E_INVALIDARG;

    *Handle = NULL;
    Object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Object));
    if (!Object)
        return E_OUTOFMEMORY;

    Object->Magic = MIL_OBJECT_MAGIC;
    Object->Type = Type;
    Object->RefCount = 1;

    EnterCriticalSection(&g_MilObjectLock);
    Object->Next = g_MilObjectList;
    if (g_MilObjectList)
        g_MilObjectList->Prev = Object;
    g_MilObjectList = Object;
    LeaveCriticalSection(&g_MilObjectLock);

    *Handle = Object;
    return S_OK;
}

static ULONG
MilObjectAddRef(void *Handle, MIL_OBJECT_TYPE Type)
{
    MIL_OBJECT *Object;
    ULONG RefCount;

    if (!g_MilObjectLockInitialized)
        return 0;

    EnterCriticalSection(&g_MilObjectLock);
    Object = MilObjectFindLocked(Handle, Type);
    if (!Object)
    {
        LeaveCriticalSection(&g_MilObjectLock);
        return 0;
    }

    RefCount = InterlockedIncrement(&Object->RefCount);
    LeaveCriticalSection(&g_MilObjectLock);
    return RefCount;
}

static ULONG
MilObjectRelease(void *Handle, MIL_OBJECT_TYPE Type)
{
    MIL_OBJECT *Object;
    ULONG RefCount;

    if (!g_MilObjectLockInitialized)
        return 0;

    EnterCriticalSection(&g_MilObjectLock);
    Object = MilObjectFindLocked(Handle, Type);
    if (!Object)
    {
        LeaveCriticalSection(&g_MilObjectLock);
        return 0;
    }

    RefCount = InterlockedDecrement(&Object->RefCount);
    if (!RefCount)
    {
        if (Object->Prev)
            Object->Prev->Next = Object->Next;
        else
            g_MilObjectList = Object->Next;

        if (Object->Next)
            Object->Next->Prev = Object->Prev;

        Object->Magic = 0;
    }
    LeaveCriticalSection(&g_MilObjectLock);

    if (!RefCount)
        HeapFree(GetProcessHeap(), 0, Object);

    return RefCount;
}

static void
MilObjectDeleteAll(void)
{
    MIL_OBJECT *Object;
    MIL_OBJECT *Next;

    if (!g_MilObjectLockInitialized)
        return;

    EnterCriticalSection(&g_MilObjectLock);
    Object = g_MilObjectList;
    g_MilObjectList = NULL;
    LeaveCriticalSection(&g_MilObjectLock);

    while (Object)
    {
        Next = Object->Next;
        Object->Magic = 0;
        HeapFree(GetProcessHeap(), 0, Object);
        Object = Next;
    }
}

/* ========================================================================
 * DLL entry point
 * ====================================================================== */

BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hInstDLL);
        InitializeCriticalSection(&g_MilObjectLock);
        g_MilObjectLockInitialized = TRUE;
        break;
    case DLL_PROCESS_DETACH:
        if (g_CompositionEvent)
        {
            CloseHandle(g_CompositionEvent);
            g_CompositionEvent = NULL;
        }
        MilObjectDeleteAll();
        if (g_MilObjectLockInitialized)
        {
            DeleteCriticalSection(&g_MilObjectLock);
            g_MilObjectLockInitialized = FALSE;
        }
        break;
    }
    return TRUE;
}

/* ========================================================================
 * 1. Composition Engine Lifecycle (ordinals 14-18)
 * ====================================================================== */

/*
 * MilCompositionEngine_InitializePartitionManager (ordinal 17)
 *
 * Master initialization of the composition engine.  Creates the partition
 * manager which owns all rendering state.  Called once at DWM startup.
 */
HRESULT WINAPI MilCompositionEngine_InitializePartitionManager(void *pInitData)
{
    TRACE("(%p)\n", pInitData);

    if (g_PartitionInitialized)
        return S_OK;

    /* Create the event that WaitForNextMessage will wait on */
    g_CompositionEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!g_CompositionEvent)
    {
        ERR("Failed to create composition event\n");
        return E_FAIL;
    }

    g_PartitionInitialized = TRUE;
    return S_OK;
}

/*
 * MilCompositionEngine_DeinitializePartitionManager (ordinal 14)
 *
 * Tears down the composition engine.
 */
HRESULT WINAPI MilCompositionEngine_DeinitializePartitionManager(void)
{
    TRACE("()\n");

    if (g_CompositionEvent)
    {
        CloseHandle(g_CompositionEvent);
        g_CompositionEvent = NULL;
    }

    g_PartitionInitialized = FALSE;
    return S_OK;
}

/*
 * MilCompositionEngine_UpdateSchedulerSettings (ordinal 18)
 *
 * Adjusts the composition scheduler timing (VSync alignment, frame rate caps).
 */
HRESULT WINAPI MilCompositionEngine_UpdateSchedulerSettings(void *pSettings)
{
    TRACE("(%p)\n", pSettings);
    return S_OK;
}

/*
 * MilCompositionEngine_GetComposedEventId (ordinal 15)
 *
 * Returns an event identifier for the most recently composed frame.
 */
HRESULT WINAPI MilCompositionEngine_GetComposedEventId(void *pEventId)
{
    TRACE("(%p)\n", pEventId);

    if (!pEventId)
        return E_INVALIDARG;

    /* Return 0 as the event ID -- no frames composed yet */
    *(DWORD *)pEventId = 0;
    return S_OK;
}

/*
 * MilCompositionEngine_GetFeedbackReader (ordinal 16)
 *
 * Obtains a reader for composition feedback/statistics.
 */
HRESULT WINAPI MilCompositionEngine_GetFeedbackReader(void *ppReader)
{
    TRACE("(%p)\n", ppReader);

    if (!ppReader)
        return E_INVALIDARG;

    /* Return NULL reader -- no feedback available */
    *(void **)ppReader = NULL;
    return S_OK;
}

/* ========================================================================
 * 2. Transport Layer (ordinals 37-48, 29)
 * ====================================================================== */

/*
 * MilTransport_Create (ordinal 39)
 *
 * Creates a new transport instance.
 */
HRESULT WINAPI MilTransport_Create(void *pParams, HMILTRANSPORT *ppTransport)
{
    TRACE("(%p, %p)\n", pParams, ppTransport);

    UNREFERENCED_PARAMETER(pParams);

    return MilObjectCreate(MilObjectTransport, (void **)ppTransport);
}

/*
 * MilTransport_CreateFromPacketTransport (ordinal 40)
 *
 * Creates a transport wrapping an existing packet transport.
 */
HRESULT WINAPI MilTransport_CreateFromPacketTransport(void *pPacketTransport,
                                                       void *pParams,
                                                       HMILTRANSPORT *ppTransport)
{
    TRACE("(%p, %p, %p)\n", pPacketTransport, pParams, ppTransport);

    UNREFERENCED_PARAMETER(pPacketTransport);
    UNREFERENCED_PARAMETER(pParams);

    return MilObjectCreate(MilObjectTransport, (void **)ppTransport);
}

/*
 * MilTransport_CreateTransportParameters (ordinal 42)
 *
 * Allocates and initializes transport configuration parameters.
 */
HRESULT WINAPI MilTransport_CreateTransportParameters(void *pInput, void *ppParams)
{
    TRACE("(%p, %p)\n", pInput, ppParams);

    UNREFERENCED_PARAMETER(pInput);
    return MilObjectCreate(MilObjectParameters, (void **)ppParams);
}

/*
 * MilTransport_CreateSurfaceManager (ordinal 41)
 *
 * Creates the surface manager associated with a transport.
 */
HRESULT WINAPI MilTransport_CreateSurfaceManager(HMILTRANSPORT hTransport,
                                                   void *ppSurfMgr)
{
    TRACE("(%p, %p)\n", hTransport, ppSurfMgr);

    if (!MilObjectFromHandle(hTransport, MilObjectTransport))
        return E_HANDLE;

    return MilObjectCreate(MilObjectSurfaceManager, (void **)ppSurfMgr);
}

/*
 * MilTransport_InitializeConnectionManager (ordinal 44)
 *
 * Initializes the connection manager that coordinates multiple transports.
 */
HRESULT WINAPI MilTransport_InitializeConnectionManager(void *pTransportTable,
                                                          void *ppConnMgr)
{
    TRACE("(%p, %p)\n", pTransportTable, ppConnMgr);

    UNREFERENCED_PARAMETER(pTransportTable);
    return MilObjectCreate(MilObjectConnection, (void **)ppConnMgr);
}

/*
 * MilTransport_ShutDownConnectionManager (ordinal 48)
 *
 * Shuts down the connection manager, disconnecting all clients.
 */
HRESULT WINAPI MilTransport_ShutDownConnectionManager(void *pConnMgr)
{
    TRACE("(%p)\n", pConnMgr);

    if (!MilObjectFromHandle(pConnMgr, MilObjectConnection))
        return E_HANDLE;

    MilObjectRelease(pConnMgr, MilObjectConnection);
    return S_OK;
}

/*
 * MilTransport_Open (ordinal 45)
 *
 * Opens an established transport for communication.
 */
HRESULT WINAPI MilTransport_Open(HMILTRANSPORT hTransport)
{
    MIL_OBJECT *Transport;

    TRACE("(%p)\n", hTransport);

    Transport = MilObjectFromHandle(hTransport, MilObjectTransport);
    if (!Transport)
        return E_HANDLE;

    Transport->Flags |= 1;
    return S_OK;
}

/*
 * MilTransport_Close (ordinal 38)
 *
 * Closes a transport, flushing pending commands.
 */
HRESULT WINAPI MilTransport_Close(HMILTRANSPORT hTransport)
{
    MIL_OBJECT *Transport;

    TRACE("(%p)\n", hTransport);

    Transport = MilObjectFromHandle(hTransport, MilObjectTransport);
    if (!Transport)
        return E_HANDLE;

    Transport->Flags &= ~1u;
    return S_OK;
}

/*
 * MilTransport_PostPacket (ordinal 46)
 *
 * Posts a command packet to the transport for async delivery.
 */
HRESULT WINAPI MilTransport_PostPacket(HMILTRANSPORT hTransport,
                                        void *pPacket,
                                        DWORD cbPacket)
{
    TRACE("(%p, %p, %lu)\n", hTransport, pPacket, cbPacket);

    if (!MilObjectFromHandle(hTransport, MilObjectTransport))
        return E_HANDLE;
    if (cbPacket && !pPacket)
        return E_INVALIDARG;

    if (g_CompositionEvent)
        SetEvent(g_CompositionEvent);

    return S_OK;
}

/*
 * MilTransport_DisconnectTransport (ordinal 43)
 *
 * Disconnects a specific transport.
 */
HRESULT WINAPI MilTransport_DisconnectTransport(HMILTRANSPORT hTransport)
{
    return MilTransport_Close(hTransport);
}

/*
 * MilTransport_AddRef (ordinal 37)
 *
 * Increments the transport reference count.
 */
ULONG WINAPI MilTransport_AddRef(HMILTRANSPORT hTransport)
{
    TRACE("(%p)\n", hTransport);
    return MilObjectAddRef(hTransport, MilObjectTransport);
}

/*
 * MilTransport_Release (ordinal 47)
 *
 * Decrements the transport reference count.
 */
ULONG WINAPI MilTransport_Release(HMILTRANSPORT hTransport)
{
    TRACE("(%p)\n", hTransport);
    return MilObjectRelease(hTransport, MilObjectTransport);
}

/*
 * MilCrossThreadPacketTransport_Create (ordinal 29)
 *
 * Creates a cross-thread packet transport for in-process communication.
 */
HRESULT WINAPI MilCrossThreadPacketTransport_Create(void *pParams, void *ppTransport)
{
    TRACE("(%p, %p)\n", pParams, ppTransport);

    UNREFERENCED_PARAMETER(pParams);

    return MilObjectCreate(MilObjectTransport, (void **)ppTransport);
}

/* ========================================================================
 * 3. Command Transport (ordinals 12-13)
 * ====================================================================== */

/*
 * MilCommandTransport_AddRef (ordinal 12)
 */
ULONG WINAPI MilCommandTransport_AddRef(void *pCmdTransport)
{
    TRACE("(%p)\n", pCmdTransport);
    return MilObjectAddRef(pCmdTransport, MilObjectTransport);
}

/*
 * MilCommandTransport_Release (ordinal 13)
 */
ULONG WINAPI MilCommandTransport_Release(void *pCmdTransport)
{
    TRACE("(%p)\n", pCmdTransport);
    return MilObjectRelease(pCmdTransport, MilObjectTransport);
}

/* ========================================================================
 * 4. Connection Management (ordinals 22-27)
 * ====================================================================== */

/*
 * MilConnection_CreateChannel (ordinal 24)
 *
 * Creates a new command channel on a connection.
 */
HRESULT WINAPI MilConnection_CreateChannel(HMILCONNECTION hConnection,
                                            void *pParams,
                                            HMILCHANNEL *ppChannel)
{
    TRACE("(%p, %p, %p)\n", hConnection, pParams, ppChannel);

    UNREFERENCED_PARAMETER(pParams);

    if (hConnection && !MilObjectFromHandle(hConnection, MilObjectConnection))
        return E_HANDLE;

    return MilObjectCreate(MilObjectChannel, (void **)ppChannel);
}

/*
 * MilConnection_DestroyChannel (ordinal 25)
 *
 * Destroys a channel, releasing all resources owned by that channel.
 */
HRESULT WINAPI MilConnection_DestroyChannel(HMILCHANNEL hChannel)
{
    TRACE("(%p)\n", hChannel);

    if (!MilObjectFromHandle(hChannel, MilObjectChannel))
        return E_HANDLE;

    MilObjectRelease(hChannel, MilObjectChannel);
    return S_OK;
}

/*
 * MilConnection_RecordUCE (ordinal 27)
 *
 * Records a UCE (Unified Composition Engine) event on the connection.
 */
HRESULT WINAPI MilConnection_RecordUCE(HMILCONNECTION hConnection, DWORD dwEvent)
{
    TRACE("(%p, %lu)\n", hConnection, dwEvent);
    if (hConnection && !MilObjectFromHandle(hConnection, MilObjectConnection))
        return E_HANDLE;
    return S_OK;
}

/*
 * MilConnection_HandleSfmEventOnPartition (ordinal 26)
 *
 * Handles a Surface Flipping Model event on the composition partition.
 */
HRESULT WINAPI MilConnection_HandleSfmEventOnPartition(HMILCONNECTION hConnection,
                                                         void *pSfmEvent)
{
    TRACE("(%p, %p)\n", hConnection, pSfmEvent);
    if (hConnection && !MilObjectFromHandle(hConnection, MilObjectConnection))
        return E_HANDLE;
    return S_OK;
}

/*
 * MilConnection_ClearSfmEventOnPartition (ordinal 23)
 *
 * Clears a pending SFM event on the partition after processing.
 */
HRESULT WINAPI MilConnection_ClearSfmEventOnPartition(HMILCONNECTION hConnection,
                                                        void *pSfmEvent)
{
    TRACE("(%p, %p)\n", hConnection, pSfmEvent);
    if (hConnection && !MilObjectFromHandle(hConnection, MilObjectConnection))
        return E_HANDLE;
    return S_OK;
}

/*
 * MilConnectionManager_NotifyHostEvent (ordinal 22)
 *
 * Notifies the connection manager of a host-level event.
 */
HRESULT WINAPI MilConnectionManager_NotifyHostEvent(void *pEvent)
{
    TRACE("(%p)\n", pEvent);
    return S_OK;
}

/* ========================================================================
 * 5. Channel Operations (ordinals 3-11)
 * ====================================================================== */

/*
 * MilChannel_BeginCommand (ordinal 4)
 *
 * Begins a new command on the channel.
 */
HRESULT WINAPI MilChannel_BeginCommand(HMILCHANNEL hChannel,
                                         DWORD dwCommandType,
                                         DWORD cbPayload)
{
    TRACE("(%p, %lu, %lu)\n", hChannel, dwCommandType, cbPayload);
    if (!MilObjectFromHandle(hChannel, MilObjectChannel))
        return E_HANDLE;
    return S_OK;
}

/*
 * MilChannel_AppendCommandData (ordinal 3)
 *
 * Appends data to the current command being constructed.
 */
HRESULT WINAPI MilChannel_AppendCommandData(HMILCHANNEL hChannel,
                                              const void *pData,
                                              DWORD cbData)
{
    TRACE("(%p, %p, %lu)\n", hChannel, pData, cbData);
    if (!MilObjectFromHandle(hChannel, MilObjectChannel))
        return E_HANDLE;
    if (cbData && !pData)
        return E_INVALIDARG;
    return S_OK;
}

/*
 * MilChannel_EndCommand (ordinal 6)
 *
 * Finalizes the current command.
 */
HRESULT WINAPI MilChannel_EndCommand(HMILCHANNEL hChannel)
{
    TRACE("(%p)\n", hChannel);
    if (!MilObjectFromHandle(hChannel, MilObjectChannel))
        return E_HANDLE;
    return S_OK;
}

/*
 * MilChannel_CommitChannel (ordinal 5)
 *
 * Commits all queued commands on the channel for processing.
 */
HRESULT WINAPI MilChannel_CommitChannel(HMILCHANNEL hChannel)
{
    TRACE("(%p)\n", hChannel);

    if (!MilObjectFromHandle(hChannel, MilObjectChannel))
        return E_HANDLE;

    if (g_CompositionEvent)
        SetEvent(g_CompositionEvent);

    return S_OK;
}

/*
 * MilChannel_SendSyncCommand (ordinal 9)
 *
 * Sends a command synchronously and waits for the result.
 */
HRESULT WINAPI MilChannel_SendSyncCommand(HMILCHANNEL hChannel,
                                            const void *pCommand,
                                            DWORD cbCommand,
                                            void *pReply,
                                            void *ppReplyData,
                                            DWORD cbMaxReply)
{
    TRACE("(%p, %p, %lu, %p, %p, %lu)\n",
          hChannel, pCommand, cbCommand, pReply, ppReplyData, cbMaxReply);

    if (!MilObjectFromHandle(hChannel, MilObjectChannel))
        return E_HANDLE;
    if (cbCommand && !pCommand)
        return E_INVALIDARG;

    return S_OK;
}

/*
 * MilChannel_FreeSyncCommandReplay (ordinal 7)
 *
 * Frees the replay buffer allocated for a synchronous command's response.
 */
HRESULT WINAPI MilChannel_FreeSyncCommandReplay(HMILCHANNEL hChannel,
                                                  void *pReplayData)
{
    TRACE("(%p, %p)\n", hChannel, pReplayData);
    if (!MilObjectFromHandle(hChannel, MilObjectChannel))
        return E_HANDLE;
    return S_OK;
}

/*
 * MilChannel_GetMarshalType (ordinal 8)
 *
 * Returns the marshaling type for the channel.
 */
HRESULT WINAPI MilChannel_GetMarshalType(HMILCHANNEL hChannel, DWORD *pMarshalType)
{
    TRACE("(%p, %p)\n", hChannel, pMarshalType);

    if (!MilObjectFromHandle(hChannel, MilObjectChannel))
        return E_HANDLE;
    if (!pMarshalType)
        return E_INVALIDARG;

    /* 0 = cross-thread (in-process) marshaling */
    *pMarshalType = 0;
    return S_OK;
}

/*
 * MilChannel_SetNotificationWindow (ordinal 10)
 *
 * Sets the HWND that receives WM_* notification messages.
 */
HRESULT WINAPI MilChannel_SetNotificationWindow(HMILCHANNEL hChannel,
                                                  HWND hWnd,
                                                  UINT uMsg)
{
    MIL_OBJECT *Channel;

    TRACE("(%p, %p, %u)\n", hChannel, hWnd, uMsg);

    Channel = MilObjectFromHandle(hChannel, MilObjectChannel);
    if (!Channel)
        return E_HANDLE;

    Channel->NotificationWindow = hWnd;
    Channel->NotificationMessage = uMsg;
    return S_OK;
}

/*
 * MilChannel_SetReceiveBroadcastMessages (ordinal 11)
 *
 * Configures whether this channel receives broadcast messages.
 */
HRESULT WINAPI MilChannel_SetReceiveBroadcastMessages(HMILCHANNEL hChannel,
                                                        BOOL bReceive)
{
    MIL_OBJECT *Channel;

    TRACE("(%p, %d)\n", hChannel, bReceive);

    Channel = MilObjectFromHandle(hChannel, MilObjectChannel);
    if (!Channel)
        return E_HANDLE;

    Channel->ReceiveBroadcasts = !!bReceive;
    return S_OK;
}

/* ========================================================================
 * 6. Resource Management (ordinals 32-36)
 * ====================================================================== */

/*
 * MilResource_CreateOrAddRefOnChannel (ordinal 32)
 *
 * Creates a new resource on the channel, or increments the refcount.
 */
HRESULT WINAPI MilResource_CreateOrAddRefOnChannel(HMILCHANNEL hChannel,
                                                     DWORD dwResourceType,
                                                     HMILRESOURCE *phResource)
{
    MIL_OBJECT *Resource;
    HRESULT hr;

    TRACE("(%p, %lu, %p)\n", hChannel, dwResourceType, phResource);

    if (!MilObjectFromHandle(hChannel, MilObjectChannel))
        return E_HANDLE;

    hr = MilObjectCreate(MilObjectResource, (void **)phResource);
    if (hr != S_OK)
        return hr;

    Resource = MilObjectFromHandle(*phResource, MilObjectResource);
    Resource->ResourceType = dwResourceType;
    return hr;
}

/*
 * MilResource_ReleaseOnChannel (ordinal 34)
 *
 * Releases a resource on the specified channel.
 */
HRESULT WINAPI MilResource_ReleaseOnChannel(HMILCHANNEL hChannel,
                                              HMILRESOURCE hResource)
{
    TRACE("(%p, %p)\n", hChannel, hResource);

    if (!MilObjectFromHandle(hChannel, MilObjectChannel))
        return E_HANDLE;
    if (!MilObjectFromHandle(hResource, MilObjectResource))
        return E_HANDLE;

    MilObjectRelease(hResource, MilObjectResource);
    return S_OK;
}

/*
 * MilResource_SendCommand (ordinal 35)
 *
 * Sends a command to a specific resource.
 */
HRESULT WINAPI MilResource_SendCommand(HMILRESOURCE hResource,
                                         const void *pCommand,
                                         DWORD cbCommand,
                                         DWORD dwFlags)
{
    TRACE("(%p, %p, %lu, %lu)\n", hResource, pCommand, cbCommand, dwFlags);
    if (!MilObjectFromHandle(hResource, MilObjectResource))
        return E_HANDLE;
    if (cbCommand && !pCommand)
        return E_INVALIDARG;
    return S_OK;
}

/*
 * MilResource_SendCommandBitmapSource (ordinal 36)
 *
 * Sends bitmap source data to a resource.
 */
HRESULT WINAPI MilResource_SendCommandBitmapSource(HMILRESOURCE hResource,
                                                     const void *pBitmapData,
                                                     DWORD cbBitmapData)
{
    TRACE("(%p, %p, %lu)\n", hResource, pBitmapData, cbBitmapData);
    if (!MilObjectFromHandle(hResource, MilObjectResource))
        return E_HANDLE;
    if (cbBitmapData && !pBitmapData)
        return E_INVALIDARG;
    return S_OK;
}

/*
 * MilResource_DuplicateHandle (ordinal 33)
 *
 * Duplicates a resource handle across channels or transports.
 */
HRESULT WINAPI MilResource_DuplicateHandle(HMILCHANNEL hChannel,
                                             HMILRESOURCE hResource,
                                             HMILRESOURCE *phDuplicate)
{
    TRACE("(%p, %p, %p)\n", hChannel, hResource, phDuplicate);

    if (!MilObjectFromHandle(hChannel, MilObjectChannel))
        return E_HANDLE;
    if (!MilObjectFromHandle(hResource, MilObjectResource))
        return E_HANDLE;
    if (!phDuplicate)
        return E_INVALIDARG;

    *phDuplicate = hResource;
    MilObjectAddRef(hResource, MilObjectResource);
    return S_OK;
}

/* ========================================================================
 * 7. Composition Message Loop (ordinals 19-21)
 * ====================================================================== */

/*
 * MilComposition_WaitForNextMessage (ordinal 21)
 *
 * Blocks the calling thread until the composition engine has a message
 * to process.  This is the main wait point in the DWM render loop.
 */
HRESULT WINAPI MilComposition_WaitForNextMessage(void *pPartition,
                                                   DWORD dwTimeout)
{
    DWORD dwResult;

    TRACE("(%p, %lu)\n", pPartition, dwTimeout);

    UNREFERENCED_PARAMETER(pPartition);

    if (!g_CompositionEvent)
        return E_FAIL;

    /* Wait on the composition event with the specified timeout.
     * CommitChannel signals this event when commands are ready. */
    dwResult = WaitForSingleObject(g_CompositionEvent, dwTimeout);

    if (dwResult == WAIT_OBJECT_0)
        return S_OK;

    if (dwResult == WAIT_TIMEOUT)
        return S_OK;  /* Timeout is not an error -- DWM uses it for idle frames */

    return E_FAIL;
}

/*
 * MilComposition_PeekNextMessage (ordinal 19)
 *
 * Non-blocking check for pending composition messages.
 */
HRESULT WINAPI MilComposition_PeekNextMessage(void *pPartition)
{
    TRACE("(%p)\n", pPartition);

    UNREFERENCED_PARAMETER(pPartition);

    /* S_FALSE = no message available */
    return S_FALSE;
}

/*
 * MilComposition_SyncFlush (ordinal 20)
 *
 * Flushes all pending commands synchronously.
 */
HRESULT WINAPI MilComposition_SyncFlush(void *pPartition)
{
    TRACE("(%p)\n", pPartition);

    UNREFERENCED_PARAMETER(pPartition);

    return S_OK;
}

/* ========================================================================
 * 8. Visual Target (ordinals 51-52)
 * ====================================================================== */

/*
 * MilVisualTarget_AttachToHwnd (ordinal 51)
 *
 * Attaches a visual target (render target) to a specific HWND.
 */
HRESULT WINAPI MilVisualTarget_AttachToHwnd(HMILVISUALTARGET hTarget, HWND hWnd)
{
    TRACE("(%p, %p)\n", hTarget, hWnd);
    if (!hWnd)
        return E_INVALIDARG;
    if (!MilObjectFromHandle(hTarget, MilObjectVisualTarget))
        return E_HANDLE;
    return S_OK;
}

/*
 * MilVisualTarget_DetachFromHwnd (ordinal 52)
 *
 * Detaches a visual target from its HWND.
 */
HRESULT WINAPI MilVisualTarget_DetachFromHwnd(HMILVISUALTARGET hTarget)
{
    TRACE("(%p)\n", hTarget);
    if (!MilObjectFromHandle(hTarget, MilObjectVisualTarget))
        return E_HANDLE;
    return S_OK;
}

/* ========================================================================
 * 9. Player (ordinals 30-31)
 * ====================================================================== */

/*
 * MilPlayer_Create (ordinal 30)
 *
 * Creates a media player instance within the composition engine.
 */
HRESULT WINAPI MilPlayer_Create(void *pParams, HMILPLAYER *ppPlayer)
{
    FIXME("(%p, %p) stub\n", pParams, ppPlayer);

    if (!ppPlayer)
        return E_INVALIDARG;

    *ppPlayer = NULL;
    return E_NOTIMPL;
}

/*
 * MilPlayer_Process (ordinal 31)
 *
 * Processes the next frame or event for a media player instance.
 */
HRESULT WINAPI MilPlayer_Process(HMILPLAYER hPlayer)
{
    FIXME("(%p) stub\n", hPlayer);

    if (!MilObjectFromHandle(hPlayer, MilObjectPlayer))
        return E_HANDLE;

    return E_NOTIMPL;
}

/* ========================================================================
 * 10. Utility and Miscellaneous (ordinals 1-2, 28, 49-50, 53-54)
 * ====================================================================== */

/*
 * MILCreateFactory (ordinal 54)
 *
 * Creates the top-level MIL factory object.  Entry point for creating
 * the composition engine's object hierarchy.
 */
HRESULT WINAPI MILCreateFactory(void **ppFactory, DWORD dwVersion)
{
    FIXME("(%p, %lu) stub\n", ppFactory, dwVersion);

    if (!ppFactory)
        return E_INVALIDARG;

    /* Return NULL factory -- caller must handle gracefully */
    *ppFactory = NULL;
    return E_NOTIMPL;
}

/*
 * MilVersionCheck (ordinal 50)
 *
 * Validates version compatibility between the caller and dwmcore.dll.
 */
HRESULT WINAPI MilVersionCheck(DWORD dwVersion)
{
    TRACE("(%lu)\n", dwVersion);

    if (!dwVersion)
        return E_INVALIDARG;

    return S_OK;
}

/*
 * MilCoreClientIsDwm (ordinal 28)
 *
 * Registers or checks whether the current client is the DWM process.
 */
HRESULT WINAPI MilCoreClientIsDwm(void)
{
    WCHAR Path[MAX_PATH];
    WCHAR *Name;

    TRACE("()\n");

    if (!GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path)))
        return E_FAIL;

    Name = Path + lstrlenW(Path);
    while (Name > Path && Name[-1] != L'\\' && Name[-1] != L'/')
        Name--;

    return lstrcmpiW(Name, L"dwm.exe") == 0 ? S_OK : S_FALSE;
}

/*
 * MIL3DCalcBrushToIdealSampleSpace (ordinal 1)
 *
 * Calculates the transformation from a 3D brush's texture space
 * to the ideal sampling space.
 */
HRESULT WINAPI MIL3DCalcBrushToIdealSampleSpace(void *pBrush,
                                                   void *pGeometry,
                                                   void *pTransform,
                                                   void *pResult)
{
    FIXME("(%p, %p, %p, %p) stub\n", pBrush, pGeometry, pTransform, pResult);
    return E_NOTIMPL;
}

/*
 * MIL3DCalcProjected2DBounds (ordinal 2)
 *
 * Calculates the 2D bounding rectangle of a 3D object after projection.
 */
HRESULT WINAPI MIL3DCalcProjected2DBounds(void *pGeometry,
                                            void *pTransform,
                                            void *pBounds)
{
    FIXME("(%p, %p, %p) stub\n", pGeometry, pTransform, pBounds);
    return E_NOTIMPL;
}

/*
 * MilUtility_GetTileBrushMapping (ordinal 49)
 *
 * Computes the texture coordinate mapping for tiled brush fill patterns.
 */
HRESULT WINAPI MilUtility_GetTileBrushMapping(void *pBrush,
                                                void *pBounds,
                                                void *pViewport,
                                                void *pMapping)
{
    FIXME("(%p, %p, %p, %p) stub\n", pBrush, pBounds, pViewport, pMapping);
    return E_NOTIMPL;
}

/*
 * SetMilPerfInstrumentationFlags (ordinal 53)
 *
 * Configures performance instrumentation flags.
 */
void WINAPI SetMilPerfInstrumentationFlags(DWORD dwFlags)
{
    FIXME("(%lu) stub\n", dwFlags);
}
