/*
 * PROJECT:     ReactOS Desktop Window Manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Composition-engine module boundary
 */

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <dxgiformat.h>
#include <wincodec.h>
#include <reactos/dwmcore.h>

#define DWM_CONNECTION_MAGIC 0x434C494Du /* 'MILC' */
#define DWM_E_ENGINE_NOT_INITIALIZED ((HRESULT)0x8000000EL)
#define DWM_E_CHANNEL_UNAVAILABLE    ((HRESULT)0x88980416L)
#define DWM_RESOURCE_TYPE_COUNT      44
#define DWM_MIL_MESSAGE_SIZE         28

struct HMIL_CONNECTION__
{
    ULONG Magic;
    INT Flags;
    HANDLE StopEvent;
    HANDLE StartEvent;
    HANDLE Thread;
    UINT ComposedEventId;
};

static CRITICAL_SECTION g_EngineLock;
static HMIL_CONNECTION *g_Connection;
static IDwmChannelPrivateVtbl g_DwmChannelVtbl;
static const IRenderDataBuilderVtbl g_RenderDataBuilderVtbl;

/* {92CA7DFF-2AC8-4C49-9927-FAB209DB648E} */
static const GUID IID_IRenderDataBuilderPrivate =
    {0x92ca7dff, 0x2ac8, 0x4c49,
     {0x99, 0x27, 0xfa, 0xb2, 0x09, 0xdb, 0x64, 0x8e}};

typedef struct _DWM_CHANNEL
{
    IDwmChannelPrivate IDwmChannelPrivate_iface;
    LONG References;
    HMIL_CONNECTION *Connection;
    IDwmChannelProvider *Provider;
    ULONG CommitSequence;
    CRITICAL_SECTION Lock;
    struct _DWM_RESOURCE_ENTRY *Resources;
    UINT ResourceCapacity;
    LIST_ENTRY Messages;
    HANDLE MessageEvent;
    BYTE *CommandData;
    SIZE_T CommandBytes;
    SIZE_T CommandCapacity;
    struct _DWM_COMMAND_BATCH *BorrowedBatch;
} DWM_CHANNEL;

typedef struct _DWM_RESOURCE_ENTRY
{
    ULONG References;
    UINT Type;
    IUnknown *Object;
    HANDLE SharedHandle;
} DWM_RESOURCE_ENTRY;

typedef struct _DWM_CHANNEL_MESSAGE
{
    LIST_ENTRY Entry;
    BYTE Data[DWM_MIL_MESSAGE_SIZE];
} DWM_CHANNEL_MESSAGE;

typedef struct _DWM_COMMAND_BATCH
{
    SIZE_T ByteCount;
    BYTE Data[ANYSIZE_ARRAY];
} DWM_COMMAND_BATCH;

typedef struct _DWM_RENDER_DATA_BUILDER
{
    IRenderDataBuilder IRenderDataBuilder_iface;
    UINT StackDepth;
    LONG References;
    BYTE *Data;
    UINT ByteCount;
    UINT Capacity;
} DWM_RENDER_DATA_BUILDER;

typedef struct _DWM_RENDER_RECORD_HEADER
{
    UINT ByteCount;
    UINT Operation;
} DWM_RENDER_RECORD_HEADER;

typedef struct _DWM_RENDER_RECORD_RESOURCE
{
    UINT ByteCount;
    UINT Operation;
    UINT Resource;
} DWM_RENDER_RECORD_RESOURCE;

typedef struct _DWM_RENDER_RECORD_PAIR
{
    UINT ByteCount;
    UINT Operation;
    UINT First;
    UINT Second;
} DWM_RENDER_RECORD_PAIR;

typedef struct _DWM_RENDER_RECORD_RECT_RESOURCE
{
    UINT ByteCount;
    UINT Operation;
    UINT Resource;
    BYTE Rectangle[16];
} DWM_RENDER_RECORD_RECT_RESOURCE;

typedef struct _DWM_RENDER_RECORD_TILE_IMAGE
{
    UINT ByteCount;
    UINT Operation;
    UINT Resource;
    BYTE Rectangle[16];
    float Opacity;
    BYTE Offset[8];
} DWM_RENDER_RECORD_TILE_IMAGE;

typedef struct _DWM_RENDER_RECORD_SOLID_RECTANGLE
{
    UINT ByteCount;
    UINT Operation;
    BYTE Rectangle[16];
    BYTE Color[16];
} DWM_RENDER_RECORD_SOLID_RECTANGLE;

typedef struct _DWM_COMMAND_PARTITION_BOOL
{
    UINT Id;
    BYTE Value;
    BYTE Reserved[3];
} DWM_COMMAND_PARTITION_BOOL;

typedef struct _DWM_COMMAND_RESOURCE_BOOL
{
    UINT Id;
    UINT Resource;
    BYTE Value;
    BYTE Reserved[3];
} DWM_COMMAND_RESOURCE_BOOL;

typedef struct _DWM_COMMAND_RESOURCE_UINT
{
    UINT Id;
    UINT Resource;
    UINT Value;
} DWM_COMMAND_RESOURCE_UINT;

typedef struct _DWM_COMMAND_RESOURCE_QWORD
{
    UINT Id;
    UINT Resource;
    ULONGLONG Value;
} DWM_COMMAND_RESOURCE_QWORD;

typedef struct _DWM_COMMAND_RESOURCE_POINT
{
    UINT Id;
    UINT Resource;
    INT X;
    INT Y;
} DWM_COMMAND_RESOURCE_POINT;

typedef struct _DWM_COMMAND_VISUAL_OPTIONS
{
    UINT Id;
    UINT Resource;
    BYTE First;
    BYTE Second;
    BYTE Reserved;
    BYTE Third;
} DWM_COMMAND_VISUAL_OPTIONS;

typedef struct _DWM_COMMAND_RESOURCE_WINDOW_BOOL
{
    UINT Id;
    UINT Resource;
    ULONGLONG WindowValue;
    BYTE Value;
    BYTE Reserved[7];
} DWM_COMMAND_RESOURCE_WINDOW_BOOL;

typedef struct _DWM_COMMAND_PARTITION_TASK
{
    UINT Id;
    UINT Reserved;
    BYTE Task[144];
} DWM_COMMAND_PARTITION_TASK;

typedef struct _DWM_COMMAND_RESOURCE_RECT
{
    UINT Id;
    UINT Resource;
    RECT Rectangle;
} DWM_COMMAND_RESOURCE_RECT;

typedef struct _DWM_COMMAND_WINDOW_NODE_STATE
{
    UINT Id;
    UINT Resource;
    ULONG Flags;
    ULONG Reserved;
    ULONGLONG Sequence;
} DWM_COMMAND_WINDOW_NODE_STATE;

C_ASSERT(sizeof(DWM_COMMAND_PARTITION_BOOL) == 8);
C_ASSERT(sizeof(DWM_COMMAND_RESOURCE_BOOL) == 12);
C_ASSERT(sizeof(DWM_COMMAND_RESOURCE_UINT) == 12);
C_ASSERT(sizeof(DWM_COMMAND_RESOURCE_QWORD) == 16);
C_ASSERT(sizeof(DWM_COMMAND_RESOURCE_POINT) == 16);
C_ASSERT(sizeof(DWM_COMMAND_VISUAL_OPTIONS) == 12);
C_ASSERT(sizeof(DWM_COMMAND_RESOURCE_WINDOW_BOOL) == 24);
C_ASSERT(sizeof(DWM_COMMAND_PARTITION_TASK) == 152);
C_ASSERT(sizeof(DWM_COMMAND_RESOURCE_RECT) == 24);
C_ASSERT(sizeof(DWM_COMMAND_WINDOW_NODE_STATE) == 24);
C_ASSERT(sizeof(IRenderDataBuilderVtbl) == 13 * sizeof(PVOID));
C_ASSERT(sizeof(DWM_RENDER_RECORD_HEADER) == 8);
C_ASSERT(sizeof(DWM_RENDER_RECORD_RESOURCE) == 12);
C_ASSERT(sizeof(DWM_RENDER_RECORD_PAIR) == 16);
C_ASSERT(sizeof(DWM_RENDER_RECORD_RECT_RESOURCE) == 28);
C_ASSERT(sizeof(DWM_RENDER_RECORD_TILE_IMAGE) == 40);
C_ASSERT(sizeof(DWM_RENDER_RECORD_SOLID_RECTANGLE) == 40);

static void
DwmListInitialize(PLIST_ENTRY Head)
{
    Head->Flink = Head;
    Head->Blink = Head;
}

static BOOL
DwmListIsEmpty(const LIST_ENTRY *Head)
{
    return Head->Flink == Head;
}

static PLIST_ENTRY
DwmListRemoveHead(PLIST_ENTRY Head)
{
    PLIST_ENTRY Entry = Head->Flink;

    Head->Flink = Entry->Flink;
    Entry->Flink->Blink = Head;
    return Entry;
}

C_ASSERT(sizeof(IDwmChannelPrivateVtbl) == 103 * sizeof(PVOID));
C_ASSERT(FIELD_OFFSET(IDwmChannelPrivateVtbl, WaitForNextMessage) ==
         7 * sizeof(PVOID));
C_ASSERT(FIELD_OFFSET(IDwmChannelPrivateVtbl, AddRefResource) ==
         8 * sizeof(PVOID));
C_ASSERT(FIELD_OFFSET(IDwmChannelPrivateVtbl, AsyncFlush) ==
         16 * sizeof(PVOID));
C_ASSERT(FIELD_OFFSET(IDwmChannelPrivateVtbl, GetCommandBatch) ==
         100 * sizeof(PVOID));

typedef struct _DWM_CURSOR_CONTROLLER
{
    IUnknown IUnknown_iface;
    LONG References;
    ULONGLONG Identifier;
} DWM_CURSOR_CONTROLLER;

static HRESULT STDMETHODCALLTYPE
DwmCursorController_QueryInterface(IUnknown *Interface,
                                   REFIID InterfaceId,
                                   PVOID *Object)
{
    if (Object == NULL)
        return E_POINTER;
    *Object = NULL;
    if (!IsEqualIID(InterfaceId, &IID_IUnknown))
        return E_NOINTERFACE;
    *Object = Interface;
    IUnknown_AddRef(Interface);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE
DwmCursorController_AddRef(IUnknown *Interface)
{
    DWM_CURSOR_CONTROLLER *Controller =
        CONTAINING_RECORD(Interface, DWM_CURSOR_CONTROLLER, IUnknown_iface);
    return (ULONG)InterlockedIncrement(&Controller->References);
}

static ULONG STDMETHODCALLTYPE
DwmCursorController_Release(IUnknown *Interface)
{
    DWM_CURSOR_CONTROLLER *Controller =
        CONTAINING_RECORD(Interface, DWM_CURSOR_CONTROLLER, IUnknown_iface);
    LONG References = InterlockedDecrement(&Controller->References);

    if (References == 0)
        HeapFree(GetProcessHeap(), 0, Controller);
    return (ULONG)References;
}

static IUnknownVtbl g_DwmCursorControllerVtbl =
{
    DwmCursorController_QueryInterface,
    DwmCursorController_AddRef,
    DwmCursorController_Release
};

DWORD WINAPI DwmCoreCompositorThread(LPVOID Parameter);

static DWM_CHANNEL *
DwmChannelFromInterface(IDwmChannelPrivate *Interface)
{
    return CONTAINING_RECORD(Interface, DWM_CHANNEL,
                             IDwmChannelPrivate_iface);
}

static HRESULT
DwmChannelValidate(DWM_CHANNEL *Channel)
{
    HRESULT Result;

    EnterCriticalSection(&g_EngineLock);
    Result = (g_Connection != NULL &&
              Channel->Connection == g_Connection &&
              g_Connection->Magic == DWM_CONNECTION_MAGIC) ?
             S_OK : DWM_E_ENGINE_NOT_INITIALIZED;
    LeaveCriticalSection(&g_EngineLock);
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_QueryInterface(IDwmChannelPrivate *Interface, REFIID InterfaceId,
                          void **Object)
{
    if (Object == NULL)
        return E_POINTER;
    *Object = NULL;
    if (!IsEqualIID(InterfaceId, &IID_IUnknown))
        return E_NOINTERFACE;
    *Object = Interface;
    Interface->lpVtbl->AddRef(Interface);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE
DwmChannel_AddRef(IDwmChannelPrivate *Interface)
{
    return (ULONG)InterlockedIncrement(
        &DwmChannelFromInterface(Interface)->References);
}

static ULONG STDMETHODCALLTYPE
DwmChannel_Release(IDwmChannelPrivate *Interface)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    LONG References = InterlockedDecrement(&Channel->References);

    if (References == 0)
    {
        UINT Index;
        PLIST_ENTRY Entry;

        for (Index = 0; Index < Channel->ResourceCapacity; ++Index)
        {
            if (Channel->Resources[Index].Object != NULL)
                IUnknown_Release(Channel->Resources[Index].Object);
            if (Channel->Resources[Index].SharedHandle != NULL)
                CloseHandle(Channel->Resources[Index].SharedHandle);
        }
        while (!DwmListIsEmpty(&Channel->Messages))
        {
            Entry = DwmListRemoveHead(&Channel->Messages);
            HeapFree(GetProcessHeap(), 0,
                     CONTAINING_RECORD(Entry, DWM_CHANNEL_MESSAGE, Entry));
        }
        if (Channel->MessageEvent != NULL)
            CloseHandle(Channel->MessageEvent);
        HeapFree(GetProcessHeap(), 0, Channel->BorrowedBatch);
        HeapFree(GetProcessHeap(), 0, Channel->CommandData);
        HeapFree(GetProcessHeap(), 0, Channel->Resources);
        DeleteCriticalSection(&Channel->Lock);
        if (Channel->Provider != NULL)
            IUnknown_Release((IUnknown *)Channel->Provider);
        HeapFree(GetProcessHeap(), 0, Channel);
    }
    return (ULONG)References;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_Commit(IDwmChannelPrivate *Interface)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;

    EnterCriticalSection(&g_EngineLock);
    if (g_Connection == Channel->Connection)
    {
        ++Channel->CommitSequence;
        SetEvent(Channel->Connection->StartEvent);
    }
    else
    {
        Result = DWM_E_ENGINE_NOT_INITIALIZED;
    }
    LeaveCriticalSection(&g_EngineLock);
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_SynchronizedCommit(IDwmChannelPrivate *Interface, void *Context)
{
    UNREFERENCED_PARAMETER(Context);
    return DwmChannel_Commit(Interface);
}

static BOOL STDMETHODCALLTYPE
DwmChannel_PeekNextMessage(IDwmChannelPrivate *Interface, void *Message)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_CHANNEL_MESSAGE *Queued;
    PLIST_ENTRY Entry;

    if (Message == NULL)
        return FALSE;

    EnterCriticalSection(&Channel->Lock);
    if (DwmListIsEmpty(&Channel->Messages))
    {
        ZeroMemory(Message, DWM_MIL_MESSAGE_SIZE);
        LeaveCriticalSection(&Channel->Lock);
        return FALSE;
    }

    Entry = DwmListRemoveHead(&Channel->Messages);
    Queued = CONTAINING_RECORD(Entry, DWM_CHANNEL_MESSAGE, Entry);
    CopyMemory(Message, Queued->Data, sizeof(Queued->Data));
    if (DwmListIsEmpty(&Channel->Messages))
        ResetEvent(Channel->MessageEvent);
    LeaveCriticalSection(&Channel->Lock);
    HeapFree(GetProcessHeap(), 0, Queued);
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_SyncFlush(IDwmChannelPrivate *Interface)
{
    HRESULT Result = DwmChannel_Commit(Interface);
    if (SUCCEEDED(Result))
        GdiFlush();
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_WaitForNextMessage(IDwmChannelPrivate *Interface,
                              void *MessageLoopExtensions,
                              UINT HandleCount,
                              const HANDLE *Handles,
                              DWORD Timeout,
                              DWORD *WaitIndex)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    HANDLE WaitHandles[MAXIMUM_WAIT_OBJECTS];
    DWORD Result;

    UNREFERENCED_PARAMETER(MessageLoopExtensions);

    if (WaitIndex == NULL || HandleCount >= MAXIMUM_WAIT_OBJECTS ||
        (HandleCount != 0 && Handles == NULL))
        return E_INVALIDARG;
    if (FAILED(DwmChannelValidate(Channel)))
        return DWM_E_ENGINE_NOT_INITIALIZED;

    EnterCriticalSection(&Channel->Lock);
    if (!DwmListIsEmpty(&Channel->Messages))
    {
        *WaitIndex = HandleCount;
        LeaveCriticalSection(&Channel->Lock);
        return S_OK;
    }
    LeaveCriticalSection(&Channel->Lock);

    if (HandleCount != 0)
        CopyMemory(WaitHandles, Handles, HandleCount * sizeof(HANDLE));
    WaitHandles[HandleCount] = Channel->MessageEvent;
    Result = WaitForMultipleObjects(HandleCount + 1, WaitHandles, FALSE,
                                    Timeout);
    if (Result >= WAIT_OBJECT_0 && Result < WAIT_OBJECT_0 + HandleCount + 1)
    {
        *WaitIndex = Result - WAIT_OBJECT_0;
        return S_OK;
    }
    if (Result == WAIT_TIMEOUT)
        return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    return HRESULT_FROM_WIN32(GetLastError());
}

static DWM_RESOURCE_ENTRY *
DwmChannelFindResource(DWM_CHANNEL *Channel, UINT Handle)
{
    if (Handle == 0 || Handle > Channel->ResourceCapacity ||
        Channel->Resources[Handle - 1].References == 0)
        return NULL;
    return &Channel->Resources[Handle - 1];
}

static HRESULT
DwmChannelCreateResourceEntry(DWM_CHANNEL *Channel, UINT Type,
                              HANDLE SharedHandle, UINT *Handle)
{
    DWM_RESOURCE_ENTRY *Resources;
    UINT Index, NewCapacity;

    if (Type >= DWM_RESOURCE_TYPE_COUNT || Handle == NULL)
        return E_INVALIDARG;

    EnterCriticalSection(&Channel->Lock);
    for (Index = 0; Index < Channel->ResourceCapacity; ++Index)
    {
        if (Channel->Resources[Index].References == 0)
            break;
    }
    if (Index == Channel->ResourceCapacity)
    {
        NewCapacity = Channel->ResourceCapacity != 0 ?
                      Channel->ResourceCapacity * 2 : 16;
        if (NewCapacity < Channel->ResourceCapacity ||
            NewCapacity > MAXUINT / sizeof(*Resources))
        {
            LeaveCriticalSection(&Channel->Lock);
            return E_OUTOFMEMORY;
        }
        if (Channel->Resources == NULL)
            Resources = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                  NewCapacity * sizeof(*Resources));
        else
            Resources = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                    Channel->Resources,
                                    NewCapacity * sizeof(*Resources));
        if (Resources == NULL)
        {
            LeaveCriticalSection(&Channel->Lock);
            return E_OUTOFMEMORY;
        }
        Channel->Resources = Resources;
        Channel->ResourceCapacity = NewCapacity;
    }

    Channel->Resources[Index].References = 1;
    Channel->Resources[Index].Type = Type;
    Channel->Resources[Index].Object = NULL;
    Channel->Resources[Index].SharedHandle = SharedHandle;
    *Handle = Index + 1;
    LeaveCriticalSection(&Channel->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_AddRefResource(IDwmChannelPrivate *Interface, UINT Handle)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_RESOURCE_ENTRY *Resource;
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    EnterCriticalSection(&Channel->Lock);
    Resource = DwmChannelFindResource(Channel, Handle);
    if (Resource == NULL || Resource->References == (ULONG)-1)
        Result = E_INVALIDARG;
    else
        ++Resource->References;
    LeaveCriticalSection(&Channel->Lock);
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CreateResource(IDwmChannelPrivate *Interface, UINT Type,
                          UINT *Handle)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    HRESULT Result = DwmChannelValidate(Channel);

    if (Handle != NULL)
        *Handle = 0;
    if (FAILED(Result))
        return Result;
    return DwmChannelCreateResourceEntry(Channel, Type, NULL, Handle);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CreateSharedResource(IDwmChannelPrivate *Interface, UINT Type,
                                UINT *Handle, HANDLE *SharedHandle)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    HANDLE NewHandle;
    HRESULT Result;

    if (Handle != NULL)
        *Handle = 0;
    if (SharedHandle != NULL)
        *SharedHandle = NULL;
    if (SharedHandle == NULL)
        return E_INVALIDARG;
    Result = DwmChannelValidate(Channel);
    if (FAILED(Result))
        return Result;

    NewHandle = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (NewHandle == NULL)
        return HRESULT_FROM_WIN32(GetLastError());
    Result = DwmChannelCreateResourceEntry(Channel, Type, NewHandle, Handle);
    if (FAILED(Result))
    {
        CloseHandle(NewHandle);
        return Result;
    }
    if (!DuplicateHandle(GetCurrentProcess(), NewHandle, GetCurrentProcess(),
                         SharedHandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        Interface->lpVtbl->ReleaseResource(Interface, *Handle);
        *Handle = 0;
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_DuplicateSharedResource(IDwmChannelPrivate *Interface,
                                   HANDLE SharedHandle, UINT Type,
                                   BOOL ReadOnly, UINT *Handle)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    HANDLE Duplicate = NULL;
    HRESULT Result;

    UNREFERENCED_PARAMETER(ReadOnly);
    if (Handle != NULL)
        *Handle = 0;
    if (SharedHandle == NULL || Handle == NULL)
        return E_INVALIDARG;
    Result = DwmChannelValidate(Channel);
    if (FAILED(Result))
        return Result;
    if (!DuplicateHandle(GetCurrentProcess(), SharedHandle,
                         GetCurrentProcess(), &Duplicate, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
        return HRESULT_FROM_WIN32(GetLastError());
    Result = DwmChannelCreateResourceEntry(Channel, Type, Duplicate, Handle);
    if (FAILED(Result))
        CloseHandle(Duplicate);
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_ReleaseResource(IDwmChannelPrivate *Interface, UINT Handle)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_RESOURCE_ENTRY *Resource;
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    EnterCriticalSection(&Channel->Lock);
    Resource = DwmChannelFindResource(Channel, Handle);
    if (Resource == NULL)
    {
        Result = E_INVALIDARG;
    }
    else if (--Resource->References == 0)
    {
        if (Resource->Object != NULL)
            IUnknown_Release(Resource->Object);
        if (Resource->SharedHandle != NULL)
            CloseHandle(Resource->SharedHandle);
        ZeroMemory(Resource, sizeof(*Resource));
    }
    LeaveCriticalSection(&Channel->Lock);
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmRenderDataBuilder_QueryInterface(IRenderDataBuilder *Interface,
                                    REFIID InterfaceId, void **Object)
{
    if (Object == NULL)
        return E_INVALIDARG;
    *Object = NULL;
    if (InterfaceId == NULL)
        return E_INVALIDARG;
    if (!IsEqualIID(InterfaceId, &IID_IUnknown) &&
        !IsEqualIID(InterfaceId, &IID_IRenderDataBuilderPrivate))
        return E_NOINTERFACE;
    *Object = Interface;
    Interface->lpVtbl->AddRef(Interface);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE
DwmRenderDataBuilder_AddRef(IRenderDataBuilder *Interface)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    return (ULONG)InterlockedIncrement(&Builder->References);
}

static ULONG STDMETHODCALLTYPE
DwmRenderDataBuilder_Release(IRenderDataBuilder *Interface)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    LONG References = InterlockedDecrement(&Builder->References);

    if (References == 0)
    {
        HeapFree(GetProcessHeap(), 0, Builder->Data);
        HeapFree(GetProcessHeap(), 0, Builder);
    }
    return (ULONG)References;
}

static HRESULT
DwmRenderDataBuilderAppend(DWM_RENDER_DATA_BUILDER *Builder,
                           const void *Record, UINT ByteCount)
{
    BYTE *Data;
    UINT Required, Capacity;

    if (Record == NULL || ByteCount < sizeof(DWM_RENDER_RECORD_HEADER) ||
        (ByteCount & 3) != 0)
        return E_INVALIDARG;
    if (Builder->ByteCount > MAXUINT - ByteCount)
        return E_OUTOFMEMORY;

    Required = Builder->ByteCount + ByteCount;
    if (Required > Builder->Capacity)
    {
        Capacity = Builder->Capacity != 0 ? Builder->Capacity : 64;
        while (Capacity < Required)
        {
            if (Capacity > MAXUINT / 2)
            {
                Capacity = Required;
                break;
            }
            Capacity *= 2;
        }
        if (Builder->Data == NULL)
            Data = HeapAlloc(GetProcessHeap(), 0, Capacity);
        else
            Data = HeapReAlloc(GetProcessHeap(), 0, Builder->Data, Capacity);
        if (Data == NULL)
            return E_OUTOFMEMORY;
        Builder->Data = Data;
        Builder->Capacity = Capacity;
    }

    CopyMemory(Builder->Data + Builder->ByteCount, Record, ByteCount);
    Builder->ByteCount = Required;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
DwmRenderDataBuilder_DrawBitmap(IRenderDataBuilder *Interface, UINT Bitmap)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    const DWM_RENDER_RECORD_PAIR Record = {16, 0x1a1, Bitmap, 0};
    return DwmRenderDataBuilderAppend(Builder, &Record, sizeof(Record));
}

static HRESULT STDMETHODCALLTYPE
DwmRenderDataBuilder_DrawGeometry(IRenderDataBuilder *Interface,
                                  UINT Geometry, UINT Brush)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    const DWM_RENDER_RECORD_PAIR Record = {16, 0x1a2, Geometry, Brush};
    return DwmRenderDataBuilderAppend(Builder, &Record, sizeof(Record));
}

static HRESULT STDMETHODCALLTYPE
DwmRenderDataBuilder_DrawImage(IRenderDataBuilder *Interface,
                               const void *Rectangle, UINT Image)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    DWM_RENDER_RECORD_RECT_RESOURCE Record = {28, 0x1a4, Image, {0}};

    if (Rectangle == NULL)
        return E_INVALIDARG;
    CopyMemory(Record.Rectangle, Rectangle, sizeof(Record.Rectangle));
    return DwmRenderDataBuilderAppend(Builder, &Record, sizeof(Record));
}

static HRESULT STDMETHODCALLTYPE
DwmRenderDataBuilder_DrawMesh2D(IRenderDataBuilder *Interface,
                                UINT Mesh, UINT Brush)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    const DWM_RENDER_RECORD_PAIR Record = {16, 0x1a5, Mesh, Brush};
    return DwmRenderDataBuilderAppend(Builder, &Record, sizeof(Record));
}

static HRESULT STDMETHODCALLTYPE
DwmRenderDataBuilder_DrawRectangle(IRenderDataBuilder *Interface,
                                   const void *Rectangle, UINT Brush)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    DWM_RENDER_RECORD_RECT_RESOURCE Record = {28, 0x1a6, Brush, {0}};

    if (Rectangle == NULL)
        return E_INVALIDARG;
    CopyMemory(Record.Rectangle, Rectangle, sizeof(Record.Rectangle));
    return DwmRenderDataBuilderAppend(Builder, &Record, sizeof(Record));
}

static HRESULT STDMETHODCALLTYPE
DwmRenderDataBuilder_DrawTileImage(IRenderDataBuilder *Interface, UINT Image,
                                   const void *Rectangle, float Opacity,
                                   const void *Offset)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    DWM_RENDER_RECORD_TILE_IMAGE Record =
        {40, 0x1a8, Image, {0}, Opacity, {0}};

    if (Rectangle == NULL || Offset == NULL)
        return E_INVALIDARG;
    CopyMemory(Record.Rectangle, Rectangle, sizeof(Record.Rectangle));
    CopyMemory(Record.Offset, Offset, sizeof(Record.Offset));
    return DwmRenderDataBuilderAppend(Builder, &Record, sizeof(Record));
}

static HRESULT STDMETHODCALLTYPE
DwmRenderDataBuilder_DrawVisual(IRenderDataBuilder *Interface, UINT Visual)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    const DWM_RENDER_RECORD_RESOURCE Record = {12, 0x1a9, Visual};
    return DwmRenderDataBuilderAppend(Builder, &Record, sizeof(Record));
}

static HRESULT STDMETHODCALLTYPE
DwmRenderDataBuilder_Pop(IRenderDataBuilder *Interface)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    const DWM_RENDER_RECORD_HEADER Record = {8, 0x1aa};
    HRESULT Result = DwmRenderDataBuilderAppend(Builder, &Record,
                                                sizeof(Record));
    if (SUCCEEDED(Result))
        --Builder->StackDepth;
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmRenderDataBuilder_PushTransform(IRenderDataBuilder *Interface,
                                   UINT Transform)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    const DWM_RENDER_RECORD_RESOURCE Record = {12, 0x1ab, Transform};
    HRESULT Result = DwmRenderDataBuilderAppend(Builder, &Record,
                                                sizeof(Record));
    if (SUCCEEDED(Result))
        ++Builder->StackDepth;
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmRenderDataBuilder_DrawSolidRectangle(IRenderDataBuilder *Interface,
                                        const void *Rectangle,
                                        const void *Color)
{
    DWM_RENDER_DATA_BUILDER *Builder =
        CONTAINING_RECORD(Interface, DWM_RENDER_DATA_BUILDER,
                          IRenderDataBuilder_iface);
    DWM_RENDER_RECORD_SOLID_RECTANGLE Record = {40, 0x1a7, {0}, {0}};

    if (Rectangle == NULL || Color == NULL)
        return E_INVALIDARG;
    CopyMemory(Record.Rectangle, Rectangle, sizeof(Record.Rectangle));
    CopyMemory(Record.Color, Color, sizeof(Record.Color));
    return DwmRenderDataBuilderAppend(Builder, &Record, sizeof(Record));
}

static const IRenderDataBuilderVtbl g_RenderDataBuilderVtbl =
{
    DwmRenderDataBuilder_QueryInterface,
    DwmRenderDataBuilder_AddRef,
    DwmRenderDataBuilder_Release,
    DwmRenderDataBuilder_DrawBitmap,
    DwmRenderDataBuilder_DrawGeometry,
    DwmRenderDataBuilder_DrawImage,
    DwmRenderDataBuilder_DrawMesh2D,
    DwmRenderDataBuilder_DrawRectangle,
    DwmRenderDataBuilder_DrawTileImage,
    DwmRenderDataBuilder_DrawVisual,
    DwmRenderDataBuilder_Pop,
    DwmRenderDataBuilder_PushTransform,
    DwmRenderDataBuilder_DrawSolidRectangle
};

static HRESULT STDMETHODCALLTYPE
DwmChannel_CreateRenderDataBuilder(IDwmChannelPrivate *Interface,
                                   IRenderDataBuilder **Builder)
{
    DWM_RENDER_DATA_BUILDER *NewBuilder;
    HRESULT Result = DwmChannelValidate(DwmChannelFromInterface(Interface));

    if (Builder == NULL)
        return E_INVALIDARG;
    *Builder = NULL;
    if (FAILED(Result))
        return Result;

    NewBuilder = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           sizeof(*NewBuilder));
    if (NewBuilder == NULL)
        return E_OUTOFMEMORY;
    NewBuilder->IRenderDataBuilder_iface.lpVtbl = &g_RenderDataBuilderVtbl;
    NewBuilder->References = 1;
    *Builder = &NewBuilder->IRenderDataBuilder_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_QueryResourceInterface(IDwmChannelPrivate *Interface, UINT Handle,
                                  REFIID InterfaceId, void **Object)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_RESOURCE_ENTRY *Resource;
    HRESULT Result = DwmChannelValidate(Channel);

    if (Object == NULL || InterfaceId == NULL)
        return E_INVALIDARG;
    *Object = NULL;
    if (FAILED(Result))
        return Result;
    EnterCriticalSection(&Channel->Lock);
    Resource = DwmChannelFindResource(Channel, Handle);
    if (Resource == NULL)
        Result = E_INVALIDARG;
    else if (Resource->Object == NULL)
        Result = E_NOINTERFACE;
    else
        Result = IUnknown_QueryInterface(Resource->Object, InterfaceId, Object);
    LeaveCriticalSection(&Channel->Lock);
    return Result;
}

static HRESULT
DwmChannelAppendCommand(DWM_CHANNEL *Channel, const void *Command,
                        SIZE_T ByteCount)
{
    BYTE *Data;
    SIZE_T Required, Capacity;

    if (Command == NULL || ByteCount < sizeof(UINT) ||
        ByteCount > MAXUINT || (ByteCount & 3) != 0)
        return E_INVALIDARG;

    EnterCriticalSection(&Channel->Lock);
    if (Channel->CommandBytes > (SIZE_T)-1 - ByteCount)
    {
        LeaveCriticalSection(&Channel->Lock);
        return E_OUTOFMEMORY;
    }
    Required = Channel->CommandBytes + ByteCount;
    if (Required > Channel->CommandCapacity)
    {
        Capacity = Channel->CommandCapacity != 0 ?
                   Channel->CommandCapacity : 4096;
        while (Capacity < Required)
        {
            if (Capacity > (SIZE_T)-1 / 2)
            {
                LeaveCriticalSection(&Channel->Lock);
                return E_OUTOFMEMORY;
            }
            Capacity *= 2;
        }
        if (Channel->CommandData == NULL)
            Data = HeapAlloc(GetProcessHeap(), 0, Capacity);
        else
            Data = HeapReAlloc(GetProcessHeap(), 0, Channel->CommandData,
                               Capacity);
        if (Data == NULL)
        {
            LeaveCriticalSection(&Channel->Lock);
            return E_OUTOFMEMORY;
        }
        Channel->CommandData = Data;
        Channel->CommandCapacity = Capacity;
    }
    CopyMemory(Channel->CommandData + Channel->CommandBytes,
               Command, ByteCount);
    Channel->CommandBytes = Required;
    LeaveCriticalSection(&Channel->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_RoundTripRequest(IDwmChannelPrivate *Interface, UINT Request)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    UINT Command[2] = {0x27, Request};
    HRESULT Result = DwmChannelValidate(Channel);
    return FAILED(Result) ? Result :
           DwmChannelAppendCommand(Channel, Command, sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_AsyncFlush(IDwmChannelPrivate *Interface, UINT First, UINT Second)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    UINT Command[3] = {0x22, First, Second};
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    return DwmChannelAppendCommand(Channel, Command, sizeof(Command));
}

static HRESULT
DwmChannelCheckResource(DWM_CHANNEL *Channel, UINT Handle, BOOL Optional)
{
    HRESULT Result = S_OK;

    if (Handle == 0)
        return Optional ? S_OK : E_INVALIDARG;

    EnterCriticalSection(&Channel->Lock);
    if (DwmChannelFindResource(Channel, Handle) == NULL)
        Result = E_INVALIDARG;
    LeaveCriticalSection(&Channel->Lock);
    return Result;
}

static HRESULT
DwmChannelAppendCheckedCommand(DWM_CHANNEL *Channel, UINT Resource,
                               BOOL OptionalResource, const void *Command,
                               SIZE_T ByteCount)
{
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, Resource, OptionalResource);
    if (FAILED(Result))
        return Result;
    return DwmChannelAppendCommand(Channel, Command, ByteCount);
}

static HRESULT
DwmChannelAppendResourcePair(IDwmChannelPrivate *Interface, UINT CommandId,
                             UINT Resource, UINT RelatedResource);

static HRESULT
DwmChannelAppendResourcePayload(IDwmChannelPrivate *Interface, UINT CommandId,
                                UINT Resource, const void *Payload,
                                SIZE_T PayloadBytes)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    BYTE *Command;
    SIZE_T ByteCount;
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    if ((PayloadBytes != 0 && Payload == NULL) ||
        PayloadBytes > MAXUINT - 8 || (PayloadBytes & 3) != 0)
        return E_INVALIDARG;
    Result = DwmChannelCheckResource(Channel, Resource, FALSE);
    if (FAILED(Result))
        return Result;

    ByteCount = 8 + PayloadBytes;
    Command = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ByteCount);
    if (Command == NULL)
        return E_OUTOFMEMORY;
    ((UINT *)Command)[0] = CommandId;
    ((UINT *)Command)[1] = Resource;
    if (PayloadBytes != 0)
        CopyMemory(Command + 8, Payload, PayloadBytes);
    Result = DwmChannelAppendCommand(Channel, Command, ByteCount);
    HeapFree(GetProcessHeap(), 0, Command);
    return Result;
}

static HRESULT
DwmChannelAppendResourceArray(IDwmChannelPrivate *Interface, UINT CommandId,
                              UINT Resource, const void *Elements,
                              UINT Count, UINT ElementBytes)
{
    BYTE *Payload;
    SIZE_T DataBytes;
    HRESULT Result;

    if (ElementBytes == 0 || Count > (MAXUINT - sizeof(UINT)) / ElementBytes)
        return E_OUTOFMEMORY;
    DataBytes = (SIZE_T)Count * ElementBytes;
    if (DataBytes != 0 && Elements == NULL)
        return E_INVALIDARG;
    Payload = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                        sizeof(UINT) + DataBytes);
    if (Payload == NULL)
        return E_OUTOFMEMORY;
    *(UINT *)Payload = (UINT)DataBytes;
    if (DataBytes != 0)
        CopyMemory(Payload + sizeof(UINT), Elements, DataBytes);
    Result = DwmChannelAppendResourcePayload(
        Interface, CommandId, Resource, Payload, sizeof(UINT) + DataBytes);
    HeapFree(GetProcessHeap(), 0, Payload);
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_RenderDataUpdate(IDwmChannelPrivate *Interface, UINT Resource,
                            IRenderDataBuilder *RenderData)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_RENDER_DATA_BUILDER *Builder = NULL;
    BYTE *Command;
    UINT DataBytes = 0, CommandBytes;
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, Resource, FALSE);
    if (FAILED(Result))
        return Result;
    if (RenderData != NULL)
    {
        if (RenderData->lpVtbl != &g_RenderDataBuilderVtbl)
            return E_INVALIDARG;
        Builder = CONTAINING_RECORD(RenderData, DWM_RENDER_DATA_BUILDER,
                                    IRenderDataBuilder_iface);
        DataBytes = Builder->ByteCount;
    }
    if (DataBytes > MAXUINT - 12)
        return E_OUTOFMEMORY;

    CommandBytes = 12 + DataBytes;
    Command = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, CommandBytes);
    if (Command == NULL)
        return E_OUTOFMEMORY;
    ((UINT *)Command)[0] = 0x144;
    ((UINT *)Command)[1] = Resource;
    ((UINT *)Command)[2] = DataBytes;
    if (DataBytes != 0)
        CopyMemory(Command + 12, Builder->Data, DataBytes);
    Result = DwmChannelAppendCommand(Channel, Command, CommandBytes);
    HeapFree(GetProcessHeap(), 0, Command);
    return Result;
}

static HRESULT
DwmCaptureScreenBits(INT X, INT Y, UINT Width, UINT Height, UINT Format,
                     ULONGLONG BufferBytes, void *Buffer)
{
    BITMAPINFO BitmapInfo;
    HGDIOBJ PreviousBitmap;
    HBITMAP Bitmap = NULL;
    HDC ScreenDc = NULL, MemoryDc = NULL;
    BYTE *Bits = NULL;
    SIZE_T Required, PixelCount, Index;
    HRESULT Result = S_OK;

    if (Width == 0 || Height == 0 || Buffer == NULL)
        return E_INVALIDARG;
    if (Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
        Format != DXGI_FORMAT_B8G8R8X8_UNORM)
        return E_INVALIDARG;
    if (Width > MAXUINT / 4 || Height > (SIZE_T)-1 / ((SIZE_T)Width * 4))
        return E_OUTOFMEMORY;
    Required = (SIZE_T)Width * Height * 4;
    if (Required > BufferBytes)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    ScreenDc = GetDC(NULL);
    if (ScreenDc == NULL)
        return HRESULT_FROM_WIN32(GetLastError());
    MemoryDc = CreateCompatibleDC(ScreenDc);
    if (MemoryDc == NULL)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }

    ZeroMemory(&BitmapInfo, sizeof(BitmapInfo));
    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth = (LONG)Width;
    BitmapInfo.bmiHeader.biHeight = -(LONG)Height;
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;
    Bitmap = CreateDIBSection(ScreenDc, &BitmapInfo, DIB_RGB_COLORS,
                              (void **)&Bits, NULL, 0);
    if (Bitmap == NULL || Bits == NULL)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    PreviousBitmap = SelectObject(MemoryDc, Bitmap);
    if (PreviousBitmap == NULL || PreviousBitmap == HGDI_ERROR)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    if (!BitBlt(MemoryDc, 0, 0, Width, Height, ScreenDc, X, Y,
                SRCCOPY | CAPTUREBLT))
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        SelectObject(MemoryDc, PreviousBitmap);
        goto Cleanup;
    }
    GdiFlush();
    CopyMemory(Buffer, Bits, Required);
    SelectObject(MemoryDc, PreviousBitmap);

    if (Format == DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        PixelCount = (SIZE_T)Width * Height;
        for (Index = 0; Index < PixelCount; ++Index)
            ((BYTE *)Buffer)[Index * 4 + 3] = 0xff;
    }

Cleanup:
    if (Bitmap != NULL)
        DeleteObject(Bitmap);
    if (MemoryDc != NULL)
        DeleteDC(MemoryDc);
    if (ScreenDc != NULL)
        ReleaseDC(NULL, ScreenDc);
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_SyncDesktopCaptureBits(IDwmChannelPrivate *Interface,
                                  ULONGLONG AdapterLuid, INT X, INT Y,
                                  UINT Width, UINT Height, UINT Format,
                                  ULONGLONG BufferBytes, HANDLE BufferSection)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    BYTE Command[56];
    HANDLE Event = NULL, CommandEvent = NULL, CommandSection = NULL;
    void *Buffer = NULL;
    DWORD WaitResult;
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    if (BufferSection == NULL || BufferBytes == 0 ||
        BufferBytes > (SIZE_T)-1)
        return E_INVALIDARG;

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (Event == NULL)
        return HRESULT_FROM_WIN32(GetLastError());
    if (!DuplicateHandle(GetCurrentProcess(), Event, GetCurrentProcess(),
                         &CommandEvent, 0, FALSE, DUPLICATE_SAME_ACCESS) ||
        !DuplicateHandle(GetCurrentProcess(), BufferSection,
                         GetCurrentProcess(), &CommandSection, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }

    ZeroMemory(Command, sizeof(Command));
    *(UINT *)(Command + 0) = 0xfc;
    CopyMemory(Command + 4, &AdapterLuid, sizeof(AdapterLuid));
    *(INT *)(Command + 12) = X;
    *(INT *)(Command + 16) = Y;
    *(UINT *)(Command + 20) = Width;
    *(UINT *)(Command + 24) = Height;
    *(UINT *)(Command + 28) = Format;
    CopyMemory(Command + 32, &BufferBytes, sizeof(BufferBytes));
    CopyMemory(Command + 40, &CommandEvent, sizeof(CommandEvent));
    CopyMemory(Command + 48, &CommandSection, sizeof(CommandSection));
    Result = DwmChannelAppendCommand(Channel, Command, sizeof(Command));
    if (FAILED(Result))
        goto Cleanup;
    Result = DwmChannel_Commit(Interface);
    if (FAILED(Result))
        goto Cleanup;

    Buffer = MapViewOfFile(BufferSection, FILE_MAP_WRITE, 0, 0,
                           (SIZE_T)BufferBytes);
    if (Buffer == NULL)
    {
        Result = HRESULT_FROM_WIN32(GetLastError());
        goto Cleanup;
    }
    Result = DwmCaptureScreenBits(X, Y, Width, Height, Format, BufferBytes,
                                  Buffer);
    if (FAILED(Result))
        goto Cleanup;
    SetEvent(Event);
    WaitResult = WaitForSingleObject(Event, 5000);
    if (WaitResult == WAIT_TIMEOUT)
        Result = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    else if (WaitResult != WAIT_OBJECT_0)
        Result = HRESULT_FROM_WIN32(GetLastError());

Cleanup:
    if (Buffer != NULL)
        UnmapViewOfFile(Buffer);
    if (CommandSection != NULL)
        CloseHandle(CommandSection);
    if (CommandEvent != NULL)
        CloseHandle(CommandEvent);
    if (Event != NULL)
        CloseHandle(Event);
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_SyncLegacyVisualCaptureRenderTargetCaptureBits(
    IDwmChannelPrivate *Interface, UINT Resource, UINT RenderTarget,
    float Scale, INT X, INT Y, INT Width, INT Height,
    ULONGLONG BufferBytes, UINT *Format, void **Bits)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    BYTE Command[48];
    void *CaptureBits;
    SIZE_T Required;
    UINT CaptureFormat;
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    if (Format == NULL || Bits == NULL || Width <= 0 || Height <= 0)
        return E_INVALIDARG;
    *Bits = NULL;
    Result = DwmChannelCheckResource(Channel, Resource, FALSE);
    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, RenderTarget, FALSE);
    if (FAILED(Result))
        return Result;

    CaptureFormat = *Format;
    ZeroMemory(Command, sizeof(Command));
    *(UINT *)(Command + 0) = 0xd9;
    *(UINT *)(Command + 4) = Resource;
    *(UINT *)(Command + 8) = RenderTarget;
    CopyMemory(Command + 12, &Scale, sizeof(Scale));
    *(INT *)(Command + 16) = X;
    *(INT *)(Command + 20) = Y;
    *(INT *)(Command + 24) = Width;
    *(INT *)(Command + 28) = Height;
    CopyMemory(Command + 32, &BufferBytes, sizeof(BufferBytes));
    *(UINT *)(Command + 40) = CaptureFormat;
    Result = DwmChannelAppendCommand(Channel, Command, sizeof(Command));
    if (FAILED(Result))
        return Result;
    Result = DwmChannel_Commit(Interface);
    if (FAILED(Result))
        return Result;

    if ((UINT)Width > MAXUINT / 4 ||
        (UINT)Height > (SIZE_T)-1 / ((SIZE_T)(UINT)Width * 4))
        return E_OUTOFMEMORY;
    Required = (SIZE_T)(UINT)Width * (UINT)Height * 4;
    if (Required > BufferBytes)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    CaptureBits = VirtualAlloc(NULL, Required, MEM_COMMIT | MEM_RESERVE,
                               PAGE_READWRITE);
    if (CaptureBits == NULL)
        return HRESULT_FROM_WIN32(GetLastError());
    Result = DwmCaptureScreenBits(X, Y, (UINT)Width, (UINT)Height,
                                  CaptureFormat, BufferBytes, CaptureBits);
    if (FAILED(Result))
    {
        VirtualFree(CaptureBits, 0, MEM_RELEASE);
        return Result;
    }
    *Format = CaptureFormat;
    *Bits = CaptureBits;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_SyncMagnifierRenderTargetCaptureBits(
    IDwmChannelPrivate *Interface, UINT Resource, UINT First, UINT Second,
    ULONGLONG Identifier, UINT Flags, const void *Parameters)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    BYTE Command[72];
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    if (Parameters == NULL)
        return E_INVALIDARG;
    Result = DwmChannelCheckResource(Channel, Resource, FALSE);
    if (FAILED(Result))
        return Result;
    ZeroMemory(Command, sizeof(Command));
    *(UINT *)(Command + 0) = 0xde;
    *(UINT *)(Command + 4) = Resource;
    *(UINT *)(Command + 8) = First;
    *(UINT *)(Command + 12) = Second;
    CopyMemory(Command + 16, &Identifier, sizeof(Identifier));
    *(UINT *)(Command + 24) = Flags;
    CopyMemory(Command + 32, Parameters, 40);
    Result = DwmChannelAppendCommand(Channel, Command, sizeof(Command));
    return FAILED(Result) ? Result : DwmChannel_Commit(Interface);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_SyncIndirectSwapchainRenderTargetCreate(
    IDwmChannelPrivate *Interface, UINT Resource, HANDLE Swapchain,
    ULONGLONG AdapterLuid, UINT RootVisual)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    BYTE Command[32];
    ULONGLONG SwapchainValue = (ULONGLONG)(ULONG_PTR)Swapchain;
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, Resource, FALSE);
    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, RootVisual, FALSE);
    if (FAILED(Result))
        return Result;
    ZeroMemory(Command, sizeof(Command));
    *(UINT *)(Command + 0) = 0xa0;
    *(UINT *)(Command + 4) = Resource;
    CopyMemory(Command + 8, &SwapchainValue, sizeof(SwapchainValue));
    CopyMemory(Command + 16, &AdapterLuid, sizeof(AdapterLuid));
    *(UINT *)(Command + 24) = RootVisual;
    Result = DwmChannelAppendCommand(Channel, Command, sizeof(Command));
    return FAILED(Result) ? Result : DwmChannel_Commit(Interface);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_BitmapSource(IDwmChannelPrivate *Interface, UINT Resource,
                        void *Source)
{
    ULONGLONG Value = (ULONGLONG)(ULONG_PTR)Source;

    return DwmChannelAppendResourcePayload(Interface, 0x14, Resource,
                                           &Value, sizeof(Value));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_DoubleResourceUpdate(IDwmChannelPrivate *Interface, UINT Resource,
                                double Value)
{
    float SerializedValue = (float)Value;

    return DwmChannelAppendResourcePayload(Interface, 0x80, Resource,
                                           &SerializedValue,
                                           sizeof(SerializedValue));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_RectResourceUpdate(IDwmChannelPrivate *Interface, UINT Resource,
                              const void *Rectangle)
{
    return DwmChannelAppendResourcePayload(Interface, 0x138, Resource,
                                           Rectangle, sizeof(RECT));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_SizeResourceUpdate(IDwmChannelPrivate *Interface, UINT Resource,
                              const void *Size)
{
    return DwmChannelAppendResourcePayload(Interface, 0x157, Resource, Size,
                                           2 * sizeof(float));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_RedirectVisualSetRedirectedVisual(
    IDwmChannelPrivate *Interface, UINT Resource, UINT RedirectedVisual)
{
    return DwmChannelAppendResourcePair(Interface, 0x139, Resource,
                                        RedirectedVisual);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_ColorTransformResourceUpdate(IDwmChannelPrivate *Interface,
                                        UINT Resource, const void *Transform)
{
    return DwmChannelAppendResourcePayload(Interface, 0x32, Resource,
                                           Transform, 100);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_PartitionRegisterForNotifications(IDwmChannelPrivate *Interface,
                                              BOOL Enabled)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_PARTITION_BOOL Command = {0x102, !!Enabled, {0}};
    HRESULT Result = DwmChannelValidate(Channel);

    return FAILED(Result) ? Result :
           DwmChannelAppendCommand(Channel, &Command, sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_PartitionSetCurrentMmTask(IDwmChannelPrivate *Interface,
                                     const void *Task)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_PARTITION_TASK Command;
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    if (Task == NULL)
        return E_INVALIDARG;
    ZeroMemory(&Command, sizeof(Command));
    Command.Id = 0x104;
    CopyMemory(Command.Task, Task, sizeof(Command.Task));
    return DwmChannelAppendCommand(Channel, &Command, sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_PartitionSwitchRemotingMode(IDwmChannelPrivate *Interface,
                                       UINT Mode, UINT Value)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    UINT Command[3] = {0x109, Mode, Value};
    HRESULT Result = DwmChannelValidate(Channel);

    return FAILED(Result) ? Result :
           DwmChannelAppendCommand(Channel, Command, sizeof(Command));
}

static HRESULT
DwmChannelAppendResourceBool(IDwmChannelPrivate *Interface, UINT CommandId,
                             UINT Resource, BOOL Value,
                             BOOL OptionalResource)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_BOOL Command =
        {CommandId, Resource, !!Value, {0}};

    return DwmChannelAppendCheckedCommand(Channel, Resource,
                                          OptionalResource, &Command,
                                          sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_PartitionSetCursor(IDwmChannelPrivate *Interface, UINT Resource,
                              BOOL Enabled)
{
    return DwmChannelAppendResourceBool(Interface, 0x105, Resource, Enabled,
                                        FALSE);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_PartitionSetMagnifier(IDwmChannelPrivate *Interface,
                                 UINT Resource, BOOL Enabled)
{
    return DwmChannelAppendResourceBool(Interface, 0x107, Resource, Enabled,
                                        TRUE);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_PartitionSetExcludeFromDDA(IDwmChannelPrivate *Interface,
                                      UINT Resource, BOOL Enabled)
{
    return DwmChannelAppendResourceBool(Interface, 0x106, Resource, Enabled,
                                        FALSE);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_PartitionToggleHolographicSuspension(
    IDwmChannelPrivate *Interface, BOOL Enabled)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_PARTITION_BOOL Command = {0x10b, !!Enabled, {0}};
    HRESULT Result = DwmChannelValidate(Channel);

    return FAILED(Result) ? Result :
           DwmChannelAppendCommand(Channel, &Command, sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_VisualSetTouchTargetRect(IDwmChannelPrivate *Interface,
                                    UINT Resource, const RECT *Rectangle)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_RECT Command;

    if (Rectangle == NULL)
        return E_INVALIDARG;
    Command.Id = 0x194;
    Command.Resource = Resource;
    Command.Rectangle = *Rectangle;
    return DwmChannelAppendCheckedCommand(Channel, Resource, FALSE, &Command,
                                          sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_VisualSetOptions(IDwmChannelPrivate *Interface, UINT Resource,
                            BOOL First, BOOL Second, BOOL Third)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_VISUAL_OPTIONS Command =
        {0x18c, Resource, !!First, !!Second, 0, !!Third};

    return DwmChannelAppendCheckedCommand(Channel, Resource, FALSE, &Command,
                                          sizeof(Command));
}

static HRESULT
DwmChannelAppendResourcePair(IDwmChannelPrivate *Interface, UINT CommandId,
                             UINT Resource, UINT RelatedResource)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_UINT Command =
        {CommandId, Resource, RelatedResource};
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, Resource, FALSE);
    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, RelatedResource, TRUE);
    if (FAILED(Result))
        return Result;
    return DwmChannelAppendCommand(Channel, &Command, sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_VisualSetContent(IDwmChannelPrivate *Interface, UINT Resource,
                            UINT Content)
{
    return DwmChannelAppendResourcePair(Interface, 0x183, Resource, Content);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_VisualSetColorTransform(IDwmChannelPrivate *Interface,
                                   UINT Resource, UINT Transform)
{
    return DwmChannelAppendResourcePair(Interface, 0x182, Resource,
                                        Transform);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_VisualTopLevelNode(IDwmChannelPrivate *Interface, UINT Resource,
                              HWND Window, BOOL Enabled)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_WINDOW_BOOL Command;

    ZeroMemory(&Command, sizeof(Command));
    Command.Id = 0x199;
    Command.Resource = Resource;
    Command.WindowValue = (ULONGLONG)(ULONG_PTR)Window;
    Command.Value = !!Enabled;
    return DwmChannelAppendCheckedCommand(Channel, Resource, FALSE, &Command,
                                          sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_VisualSetPassiveUpdateMode(IDwmChannelPrivate *Interface,
                                      UINT Resource, BOOL Enabled)
{
    return DwmChannelAppendResourceBool(Interface, 0x18d, Resource, Enabled,
                                        FALSE);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_VisualSetExcludeSubtree(IDwmChannelPrivate *Interface,
                                   UINT Resource, BOOL Enabled)
{
    return DwmChannelAppendResourceBool(Interface, 0x187, Resource, Enabled,
                                        FALSE);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_VisualTargetSetRoot(IDwmChannelPrivate *Interface, UINT Target,
                               UINT Root)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, Target, FALSE);
    if (FAILED(Result))
        return Result;
    return DwmChannelCheckResource(Channel, Root, TRUE);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_WindowNodeInitialize(IDwmChannelPrivate *Interface, UINT Resource,
                                HWND Window, HANDLE Sprite, ULONG Flags,
                                ULONGLONG Sequence)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_QWORD WindowCommand =
        {0x297, Resource, (ULONGLONG)(ULONG_PTR)Window};
    DWM_COMMAND_RESOURCE_QWORD SpriteCommand =
        {0x296, Resource, (ULONGLONG)(ULONG_PTR)Sprite};
    DWM_COMMAND_WINDOW_NODE_STATE StateCommand =
        {0x292, Resource, Flags, 0, Sequence};
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, Resource, FALSE);
    if (FAILED(Result))
        return Result;
    Result = DwmChannelAppendCommand(Channel, &WindowCommand,
                                     sizeof(WindowCommand));
    if (SUCCEEDED(Result))
        Result = DwmChannelAppendCommand(Channel, &SpriteCommand,
                                         sizeof(SpriteCommand));
    if (SUCCEEDED(Result))
        Result = DwmChannelAppendCommand(Channel, &StateCommand,
                                         sizeof(StateCommand));
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_WindowNodeSetIsComposeOnce(IDwmChannelPrivate *Interface,
                                      UINT Resource, BOOL Enabled)
{
    return DwmChannelAppendResourceBool(Interface, 0x28f, Resource, Enabled,
                                        FALSE);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_VisualGroupUpdate(IDwmChannelPrivate *Interface, UINT Resource,
                             const UINT *Children, UINT Count)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    UINT Index;
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    if (Count != 0 && Children == NULL)
        return E_INVALIDARG;
    if (Count > (MAXUINT - 12) / sizeof(UINT))
        return E_OUTOFMEMORY;
    Result = DwmChannelCheckResource(Channel, Resource, FALSE);
    if (FAILED(Result))
        return Result;
    for (Index = 0; Index < Count; ++Index)
    {
        Result = DwmChannelCheckResource(Channel, Children[Index], FALSE);
        if (FAILED(Result))
            return Result;
    }
    return DwmChannelAppendResourceArray(Interface, 0x285, Resource, Children,
                                         Count, sizeof(UINT));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_RenderTargetSetRoot(IDwmChannelPrivate *Interface, UINT Target,
                               UINT Root)
{
    return DwmChannelAppendResourcePair(Interface, 0x145, Target, Root);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_VisualSetBlurredWallpaperSurface(
    IDwmChannelPrivate *Interface, UINT Resource, UINT Surface,
    const RECT *Rectangle)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_UINT SurfaceCommand = {0x17f, Resource, Surface};
    DWM_COMMAND_RESOURCE_RECT RectangleCommand;
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, Resource, FALSE);
    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, Surface, TRUE);
    if (FAILED(Result))
        return Result;
    Result = DwmChannelAppendCommand(Channel, &SurfaceCommand,
                                     sizeof(SurfaceCommand));
    if (FAILED(Result))
        return Result;
    ZeroMemory(&RectangleCommand, sizeof(RectangleCommand));
    RectangleCommand.Id = 0x180;
    RectangleCommand.Resource = Resource;
    if (Rectangle != NULL)
        RectangleCommand.Rectangle = *Rectangle;
    return DwmChannelAppendCommand(Channel, &RectangleCommand,
                                   sizeof(RectangleCommand));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_VisualSetResampleMode(IDwmChannelPrivate *Interface, UINT Resource,
                                 UINT Mode)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_UINT Command = {0x17e, Resource, Mode};

    return DwmChannelAppendCheckedCommand(Channel, Resource, FALSE, &Command,
                                          sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CursorVisualSetCursorId(IDwmChannelPrivate *Interface,
                                   UINT Resource, ULONGLONG CursorId)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_QWORD Command = {0x1ef, Resource, CursorId};

    return DwmChannelAppendCheckedCommand(Channel, Resource, FALSE, &Command,
                                          sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CursorVisualSetIsHardwareCursorEnabled(
    IDwmChannelPrivate *Interface, UINT Resource, BOOL Enabled)
{
    return DwmChannelAppendResourceBool(Interface, 0x1f1, Resource, Enabled,
                                        FALSE);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CursorVisualSetIsSynchronized(IDwmChannelPrivate *Interface,
                                         UINT Resource, BOOL Enabled)
{
    return DwmChannelAppendResourceBool(Interface, 0x1f2, Resource, Enabled,
                                        FALSE);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CursorVisualSetPosition(IDwmChannelPrivate *Interface,
                                   UINT Resource, INT X, INT Y)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_POINT Command = {0x1f0, Resource, X, Y};

    return DwmChannelAppendCheckedCommand(Channel, Resource, FALSE, &Command,
                                          sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_IndirectSwapchainRenderTargetUpdateTargetBounds(
    IDwmChannelPrivate *Interface, UINT Resource, UINT Left, UINT Top,
    UINT Right, UINT Bottom)
{
    UINT Bounds[4] = {Left, Top, Right, Bottom};

    return DwmChannelAppendResourcePayload(Interface, 0xa2, Resource, Bounds,
                                           sizeof(Bounds));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_IndirectSwapchainRenderTargetUnregister(
    IDwmChannelPrivate *Interface, UINT Resource)
{
    return DwmChannelAppendResourcePayload(Interface, 0xa1, Resource, NULL,
                                           0);
}

static HRESULT
DwmChannel_BaseAnimationBinding(IDwmChannelPrivate *Interface, UINT CommandId,
                                UINT Resource, UINT Source, UINT Property)
{
    UINT Payload[2] = {Source, Property};

    return DwmChannelAppendResourcePayload(Interface, CommandId, Resource,
                                           Payload, sizeof(Payload));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_BaseAnimationAddBinding(IDwmChannelPrivate *Interface,
                                   UINT Resource, UINT Source, UINT Property)
{
    return DwmChannel_BaseAnimationBinding(Interface, 0xc, Resource, Source,
                                           Property);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_BaseAnimationRemoveBinding(IDwmChannelPrivate *Interface,
                                      UINT Resource, UINT Source,
                                      UINT Property)
{
    return DwmChannel_BaseAnimationBinding(Interface, 0xd, Resource, Source,
                                           Property);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_AnimationUpdateBeginTime(IDwmChannelPrivate *Interface,
                                    UINT Resource, ULONGLONG BeginTime,
                                    ULONGLONG Duration)
{
    ULONGLONG Payload[2] = {BeginTime, Duration};

    return DwmChannelAppendResourcePayload(Interface, 0xb, Resource, Payload,
                                           sizeof(Payload));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_AnimationSetTrigger(IDwmChannelPrivate *Interface, UINT Resource,
                               UINT Trigger)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_UINT Command = {0x7, Resource, Trigger};

    return DwmChannelAppendCheckedCommand(Channel, Resource, FALSE, &Command,
                                          sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_EffectGroupUpdate(IDwmChannelPrivate *Interface, UINT Resource,
                             double Opacity, UINT Transform, UINT Clip)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    float SerializedOpacity = (float)Opacity;
    DWM_COMMAND_RESOURCE_UINT TransformCommand =
        {0x1f8, Resource, Transform};
    DWM_COMMAND_RESOURCE_UINT ClipCommand = {0x1f9, Resource, Clip};
    HRESULT Result;

    Result = DwmChannelAppendResourcePayload(Interface, 0x1f7, Resource,
                                             &SerializedOpacity,
                                             sizeof(SerializedOpacity));
    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, Transform, TRUE);
    if (FAILED(Result))
        return Result;
    Result = DwmChannelAppendCommand(Channel, &TransformCommand,
                                     sizeof(TransformCommand));
    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, Clip, TRUE);
    if (FAILED(Result))
        return Result;
    return DwmChannelAppendCommand(Channel, &ClipCommand,
                                   sizeof(ClipCommand));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CachedVisualImageFreeze(IDwmChannelPrivate *Interface,
                                   UINT Resource)
{
    return DwmChannelAppendResourcePayload(Interface, 0x15, Resource, NULL,
                                           0);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CachedVisualImageSnapshot(IDwmChannelPrivate *Interface,
                                     UINT Resource, const RECT *Rectangle)
{
    RECT Empty = {0};

    return DwmChannelAppendResourcePayload(Interface, 0x16, Resource,
                                           Rectangle != NULL ? Rectangle :
                                                               &Empty,
                                           sizeof(Empty));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_AnimationTriggerTrigger(IDwmChannelPrivate *Interface,
                                   UINT Resource, ULONGLONG Value)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_QWORD Command = {0xd4, Resource, Value};

    return DwmChannelAppendCheckedCommand(Channel, Resource, FALSE, &Command,
                                          sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_GaussianBlurEffectUpdate(IDwmChannelPrivate *Interface,
                                    UINT Resource, float Amount, UINT Border,
                                    UINT Optimization)
{
    struct
    {
        float Amount;
        UINT Border;
        UINT Optimization;
    } Payload = {Amount, Border, Optimization};

    return DwmChannelAppendResourcePayload(Interface, 0x1bd, Resource,
                                           &Payload, sizeof(Payload));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_MatrixTransform3DUpdate(IDwmChannelPrivate *Interface,
                                   UINT Resource, const void *Matrix)
{
    return DwmChannelAppendResourcePayload(Interface, 0x1c3, Resource,
                                           Matrix, 16 * sizeof(float));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_TranslateTransformUpdate(IDwmChannelPrivate *Interface,
                                    UINT Resource, double X, double Y)
{
    float Value = (float)X;
    HRESULT Result = DwmChannelAppendResourcePayload(
        Interface, 0x27f, Resource, &Value, sizeof(Value));

    if (FAILED(Result))
        return Result;
    Value = (float)Y;
    return DwmChannelAppendResourcePayload(Interface, 0x280, Resource, &Value,
                                           sizeof(Value));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_ScaleTransformUpdate(IDwmChannelPrivate *Interface, UINT Resource,
                                double ScaleX, double ScaleY, double CenterX,
                                double CenterY)
{
    static const UINT CommandIds[] = {0x232, 0x233, 0x234, 0x235};
    const double Values[] = {ScaleX, ScaleY, CenterX, CenterY};
    UINT Index;
    HRESULT Result = S_OK;

    for (Index = 0; Index < ARRAYSIZE(CommandIds); ++Index)
    {
        float Value = (float)Values[Index];
        Result = DwmChannelAppendResourcePayload(
            Interface, CommandIds[Index], Resource, &Value, sizeof(Value));
        if (FAILED(Result))
            break;
    }
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_RotateTransformUpdate(IDwmChannelPrivate *Interface,
                                 UINT Resource, double Angle, double CenterX,
                                 double CenterY)
{
    static const UINT CommandIds[] = {0x228, 0x229, 0x22a};
    const double Values[] = {Angle, CenterX, CenterY};
    UINT Index;
    HRESULT Result = S_OK;

    for (Index = 0; Index < ARRAYSIZE(CommandIds); ++Index)
    {
        float Value = (float)Values[Index];
        Result = DwmChannelAppendResourcePayload(
            Interface, CommandIds[Index], Resource, &Value, sizeof(Value));
        if (FAILED(Result))
            break;
    }
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_MatrixTransformUpdate(IDwmChannelPrivate *Interface,
                                 UINT Resource, const void *Matrix)
{
    return DwmChannelAppendResourcePayload(Interface, 0x1c2, Resource,
                                           Matrix, 6 * sizeof(float));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CombinedGeometryUpdate(IDwmChannelPrivate *Interface,
                                  UINT Resource, UINT Mode, UINT First,
                                  UINT Second)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    UINT Payload[3] = {Mode, First, Second};
    HRESULT Result = DwmChannelValidate(Channel);

    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, First, TRUE);
    if (FAILED(Result))
        return Result;
    Result = DwmChannelCheckResource(Channel, Second, TRUE);
    if (FAILED(Result))
        return Result;
    return DwmChannelAppendResourcePayload(Interface, 0x1b9, Resource,
                                           Payload, sizeof(Payload));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_HolographicInteropTextureSetRoot(
    IDwmChannelPrivate *Interface, UINT Resource, UINT Root)
{
    return DwmChannelAppendResourcePair(Interface, 0x9c, Resource, Root);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_MagnifierRenderTargetSetTransform(
    IDwmChannelPrivate *Interface, UINT Resource, UINT Transform)
{
    return DwmChannelAppendResourcePair(Interface, 0xe3, Resource, Transform);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_MagnifierRenderTargetSetColorTransform(
    IDwmChannelPrivate *Interface, UINT Resource, UINT Transform)
{
    return DwmChannelAppendResourcePair(Interface, 0xe0, Resource, Transform);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_MagnifierRenderTargetSetFilterList(
    IDwmChannelPrivate *Interface, UINT Resource, UINT FilterList)
{
    return DwmChannelAppendResourcePair(Interface, 0xe1, Resource,
                                        FilterList);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_MagnifierRenderTargetSetResampleMode(
    IDwmChannelPrivate *Interface, UINT Resource, UINT Mode)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_UINT Command = {0xe2, Resource, Mode};

    return DwmChannelAppendCheckedCommand(Channel, Resource, FALSE, &Command,
                                          sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CaptureControllerSetRootVisual(IDwmChannelPrivate *Interface,
                                          UINT Resource, UINT Visual)
{
    return DwmChannelAppendResourcePair(Interface, 0x1df, Resource, Visual);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CaptureControllerSetCaptureState(IDwmChannelPrivate *Interface,
                                            UINT Resource, BOOL Enabled)
{
    return DwmChannelAppendResourceBool(Interface, 0x1e0, Resource, Enabled,
                                        FALSE);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CaptureControllerSetContentSize(IDwmChannelPrivate *Interface,
                                           UINT Resource, double Width,
                                           double Height)
{
    float Size[2] = {(float)Width, (float)Height};

    return DwmChannelAppendResourcePayload(Interface, 0x1e1, Resource, Size,
                                           sizeof(Size));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CaptureControllerSetTransform(IDwmChannelPrivate *Interface,
                                         UINT Resource, UINT Transform)
{
    return DwmChannelAppendResourcePair(Interface, 0x1e2, Resource,
                                        Transform);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CaptureControllerSetDefaultSDRBoost(
    IDwmChannelPrivate *Interface, UINT Resource, float Boost)
{
    return DwmChannelAppendResourcePayload(Interface, 0x1e4, Resource, &Boost,
                                           sizeof(Boost));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CaptureControllerSetReferenceVisual(
    IDwmChannelPrivate *Interface, UINT Resource, UINT Visual)
{
    return DwmChannelAppendResourcePair(Interface, 0x1e5, Resource, Visual);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CaptureControllerSetSuspendOnScreenOff(
    IDwmChannelPrivate *Interface, UINT Resource, BOOL Enabled)
{
    return DwmChannelAppendResourceBool(Interface, 0x1e9, Resource, Enabled,
                                        FALSE);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CaptureControllerSetContentOffset(
    IDwmChannelPrivate *Interface, UINT Resource, INT X, INT Y)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_POINT Command = {0x1e8, Resource, X, Y};

    return DwmChannelAppendCheckedCommand(Channel, Resource, FALSE, &Command,
                                          sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_AnimationUpdatePrimitives(IDwmChannelPrivate *Interface,
                                     UINT Resource, const void *Primitives,
                                     UINT Count)
{
    return DwmChannelAppendResourceArray(Interface, 0x2, Resource,
                                         Primitives, Count, 32);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_Geometry2DGroupUpdate(IDwmChannelPrivate *Interface,
                                 UINT Resource, const UINT *Children,
                                 UINT Count)
{
    return DwmChannelAppendResourceArray(Interface, 0x200, Resource, Children,
                                         Count, sizeof(UINT));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_Transform3DGroupUpdate(IDwmChannelPrivate *Interface,
                                  UINT Resource, const UINT *Children,
                                  UINT Count)
{
    return DwmChannelAppendResourceArray(Interface, 0x27b, Resource, Children,
                                         Count, sizeof(UINT));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_TransformGroupUpdate(IDwmChannelPrivate *Interface, UINT Resource,
                                const UINT *Children, UINT Count)
{
    return DwmChannelAppendResourceArray(Interface, 0x27d, Resource, Children,
                                         Count, sizeof(UINT));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_RgnGeometryUpdate(IDwmChannelPrivate *Interface, UINT Resource,
                             const RECT *Rectangles, UINT Count,
                             INT OffsetX, INT OffsetY)
{
    RECT *Adjusted = NULL;
    const RECT *Data = Rectangles;
    UINT Index;
    HRESULT Result;

    if (Count != 0 && Rectangles == NULL)
        return E_INVALIDARG;
    if (Count != 0 && (OffsetX != 0 || OffsetY != 0))
    {
        if (Count > MAXUINT / sizeof(*Adjusted))
            return E_OUTOFMEMORY;
        Adjusted = HeapAlloc(GetProcessHeap(), 0,
                             Count * sizeof(*Adjusted));
        if (Adjusted == NULL)
            return E_OUTOFMEMORY;
        for (Index = 0; Index < Count; ++Index)
        {
            Adjusted[Index] = Rectangles[Index];
            OffsetRect(&Adjusted[Index], OffsetX, OffsetY);
        }
        Data = Adjusted;
    }
    Result = DwmChannelAppendResourceArray(Interface, 0x226, Resource, Data,
                                           Count, sizeof(RECT));
    HeapFree(GetProcessHeap(), 0, Adjusted);
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CaptureControllerSetWindowInfos(
    IDwmChannelPrivate *Interface, UINT Resource, const void *WindowInfos,
    UINT Count)
{
    return DwmChannelAppendResourceArray(Interface, 0x1e6, Resource,
                                         WindowInfos, Count, 32);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_MagnifierRenderTargetCreate(IDwmChannelPrivate *Interface,
                                       UINT Resource, ULONGLONG Identifier,
                                       const void *AdapterTextures)
{
    BYTE Payload[216];

    if (AdapterTextures == NULL)
        return E_INVALIDARG;
    ZeroMemory(Payload, sizeof(Payload));
    CopyMemory(Payload, &Identifier, sizeof(Identifier));
    CopyMemory(Payload + sizeof(Identifier), AdapterTextures, 208);
    return DwmChannelAppendResourcePayload(Interface, 0xdf, Resource, Payload,
                                           sizeof(Payload));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_MagnifierRenderTargetUpdate(IDwmChannelPrivate *Interface,
                                       UINT Resource,
                                       const void *AdapterTextures)
{
    return DwmChannelAppendResourcePayload(Interface, 0xe4, Resource,
                                           AdapterTextures, 208);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_CachedVisualImageUpdate(IDwmChannelPrivate *Interface,
                                   UINT Resource, const void *Destination,
                                   const void *Source, UINT First,
                                   UINT Second, UINT Third, UINT Fourth,
                                   UINT Fifth)
{
    BYTE Payload[56];

    if (Destination == NULL || Source == NULL)
        return E_INVALIDARG;
    ZeroMemory(Payload, sizeof(Payload));
    CopyMemory(Payload, Destination, 16);
    CopyMemory(Payload + 16, Source, sizeof(ULONGLONG));
    ((UINT *)(Payload + 24))[0] = First;
    ((UINT *)(Payload + 24))[1] = Second;
    ((UINT *)(Payload + 24))[2] = Third;
    ((UINT *)(Payload + 24))[3] = Fourth;
    ((UINT *)(Payload + 24))[7] = Fifth;
    return DwmChannelAppendResourcePayload(Interface, 0x17, Resource, Payload,
                                           sizeof(Payload));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_RectangleGeometrySetRectangle(
    IDwmChannelPrivate *Interface, UINT Resource,
    float First, float Second, float Third, float Fourth,
    float Fifth, float Sixth, float Seventh, float Eighth,
    float Ninth, float Tenth, float Eleventh, float Twelfth, BOOL Enabled)
{
    const float Values[] =
        {First, Second, Third, Fourth, Fifth, Sixth, Seventh, Eighth,
         Ninth, Tenth, Eleventh, Twelfth};
    static const UINT PropertyCommands[] = {0x132, 0x137, 0x135, 0x131};
    BYTE Payload[36];
    UINT Index;
    HRESULT Result;

    ZeroMemory(Payload, sizeof(Payload));
    CopyMemory(Payload, Values, 8 * sizeof(float));
    Payload[32] = !!Enabled;
    Result = DwmChannelAppendResourcePayload(Interface, 0x100, Resource,
                                             Payload, sizeof(Payload));
    for (Index = 0; SUCCEEDED(Result) &&
                    Index < ARRAYSIZE(PropertyCommands); ++Index)
    {
        Result = DwmChannelAppendResourcePayload(
            Interface, PropertyCommands[Index], Resource, &Values[8 + Index],
            sizeof(float));
    }
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_SolidColorLegacyMilBrushUpdate(IDwmChannelPrivate *Interface,
                                          UINT Resource, double Opacity,
                                          const void *Color)
{
    float SerializedOpacity = (float)Opacity;
    HRESULT Result;

    if (Color == NULL)
        return E_INVALIDARG;
    Result = DwmChannelAppendResourcePayload(Interface, 0x219, Resource,
                                             &SerializedOpacity,
                                             sizeof(SerializedOpacity));
    if (FAILED(Result))
        return Result;
    return DwmChannelAppendResourcePayload(Interface, 0x264, Resource, Color,
                                           4 * sizeof(float));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_LinearGradientLegacyMilBrushUpdate(
    IDwmChannelPrivate *Interface, UINT Resource, double Opacity,
    const void *StartPoint, const void *EndPoint, UINT InterpolationMode,
    UINT MappingMode, UINT SpreadMethod, const void *Stops, UINT StopCount)
{
    float SerializedOpacity = (float)Opacity;
    const UINT Values[] = {InterpolationMode, MappingMode, SpreadMethod};
    static const UINT ValueCommands[] = {0x206, 0x207, 0x208};
    UINT Index;
    HRESULT Result;

    if (StartPoint == NULL || EndPoint == NULL)
        return E_INVALIDARG;
    Result = DwmChannelAppendResourcePayload(Interface, 0x219, Resource,
                                             &SerializedOpacity,
                                             sizeof(SerializedOpacity));
    if (SUCCEEDED(Result))
        Result = DwmChannelAppendResourcePayload(Interface, 0x21d, Resource,
                                                 StartPoint, 2 * sizeof(float));
    if (SUCCEEDED(Result))
        Result = DwmChannelAppendResourcePayload(Interface, 0x21e, Resource,
                                                 EndPoint, 2 * sizeof(float));
    for (Index = 0; SUCCEEDED(Result) &&
                    Index < ARRAYSIZE(ValueCommands); ++Index)
    {
        Result = DwmChannelAppendResourcePayload(
            Interface, ValueCommands[Index], Resource, &Values[Index],
            sizeof(UINT));
    }
    if (FAILED(Result))
        return Result;
    return DwmChannelAppendResourceArray(Interface, 0x209, Resource, Stops,
                                         StopCount, 24);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_ImageLegacyMilBrushUpdate(
    IDwmChannelPrivate *Interface, UINT Resource, double Opacity,
    const void *SourceRectangle, const void *DestinationRectangle,
    UINT First, UINT Second, UINT Third, UINT FirstMapping,
    UINT SecondMapping, UINT Fourth, UINT Fifth, UINT Stretch, UINT TileMode,
    UINT HorizontalAlignment, UINT VerticalAlignment, UINT Bitmap)
{
    static const UINT CommandIds[] =
        {0x21a, 0x21b, 0x21c, 0x20b, 0x20c, 0x20e,
         0x210, 0x211, 0x212, 0x213, 0x214, 0x218};
    const UINT Values[] =
        {First, Second, Third, FirstMapping, SecondMapping, Fourth,
         Fifth, Stretch, TileMode, HorizontalAlignment, VerticalAlignment,
         Bitmap};
    float SerializedOpacity = (float)Opacity;
    UINT Index;
    HRESULT Result;

    if (SourceRectangle == NULL || DestinationRectangle == NULL)
        return E_INVALIDARG;
    Result = DwmChannelAppendResourcePayload(Interface, 0x219, Resource,
                                             &SerializedOpacity,
                                             sizeof(SerializedOpacity));
    if (SUCCEEDED(Result))
        Result = DwmChannelAppendResourcePayload(
            Interface, 0x20d, Resource, SourceRectangle, 4 * sizeof(float));
    if (SUCCEEDED(Result))
        Result = DwmChannelAppendResourcePayload(
            Interface, 0x20f, Resource, DestinationRectangle,
            4 * sizeof(float));
    for (Index = 0; SUCCEEDED(Result) && Index < ARRAYSIZE(CommandIds); ++Index)
    {
        Result = DwmChannelAppendResourcePayload(
            Interface, CommandIds[Index], Resource, &Values[Index],
            sizeof(UINT));
    }
    return Result;
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_AtlasedRectsMeshSetOpacity(IDwmChannelPrivate *Interface,
                                      UINT Resource, INT Opacity)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_UINT Command = {0x1da, Resource, (UINT)Opacity};

    return DwmChannelAppendCheckedCommand(Channel, Resource, FALSE, &Command,
                                          sizeof(Command));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_AtlasedRectsGroupUpdate(IDwmChannelPrivate *Interface,
                                   UINT Resource, UINT Atlas,
                                   const UINT *Rectangles, UINT Count)
{
    HRESULT Result = DwmChannelAppendResourcePair(Interface, 0x1d6, Resource,
                                                  Atlas);

    if (FAILED(Result))
        return Result;
    return DwmChannelAppendResourceArray(Interface, 0x1d7, Resource,
                                         Rectangles, Count, sizeof(UINT));
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_AtlasedRectsMeshUpdate(IDwmChannelPrivate *Interface,
                                  UINT Resource, BOOL Enabled, INT Opacity,
                                  const void *FirstRects,
                                  const void *SecondRects, UINT Count)
{
    HRESULT Result = DwmChannelAppendResourceBool(
        Interface, 0x1d9, Resource, Enabled, FALSE);

    if (FAILED(Result))
        return Result;
    Result = DwmChannel_AtlasedRectsMeshSetOpacity(Interface, Resource,
                                                   Opacity);
    if (FAILED(Result))
        return Result;
    Result = DwmChannelAppendResourceArray(Interface, 0x1db, Resource,
                                           FirstRects, Count, 16);
    if (FAILED(Result))
        return Result;
    return DwmChannelAppendResourceArray(Interface, 0x1dd, Resource,
                                         SecondRects, Count, 16);
}

static HRESULT STDMETHODCALLTYPE
DwmChannel_MeshGeometry2DUpdate(IDwmChannelPrivate *Interface, UINT Resource,
                                INT Mode, const void *Positions,
                                const void *TextureCoordinates,
                                UINT VertexCount, const UINT *Indices,
                                UINT IndexCount)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_RESOURCE_UINT ModeCommand =
        {0x21f, Resource, (UINT)Mode};
    HRESULT Result;

    Result = DwmChannelAppendCheckedCommand(Channel, Resource, FALSE,
                                            &ModeCommand,
                                            sizeof(ModeCommand));
    if (FAILED(Result))
        return Result;
    Result = DwmChannelAppendResourceArray(Interface, 0x220, Resource,
                                           Positions, VertexCount, 12);
    if (FAILED(Result))
        return Result;
    Result = DwmChannelAppendResourceArray(Interface, 0x222, Resource,
                                           TextureCoordinates, VertexCount,
                                           8);
    if (FAILED(Result))
        return Result;
    return DwmChannelAppendResourceArray(Interface, 0x224, Resource, Indices,
                                         IndexCount, sizeof(UINT));
}

static void STDMETHODCALLTYPE
DwmChannel_GetCommandBatch(IDwmChannelPrivate *Interface, void **Batch,
                           BOOL *MoreCommands)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_BATCH *NewBatch = NULL;

    if (Batch != NULL)
        *Batch = NULL;
    if (MoreCommands != NULL)
        *MoreCommands = FALSE;
    if (Batch == NULL || MoreCommands == NULL)
        return;

    EnterCriticalSection(&Channel->Lock);
    if (Channel->BorrowedBatch == NULL && Channel->CommandBytes != 0 &&
        Channel->CommandBytes <= (SIZE_T)-1 - FIELD_OFFSET(DWM_COMMAND_BATCH, Data))
    {
        NewBatch = HeapAlloc(GetProcessHeap(), 0,
                             FIELD_OFFSET(DWM_COMMAND_BATCH, Data) +
                             Channel->CommandBytes);
        if (NewBatch != NULL)
        {
            NewBatch->ByteCount = Channel->CommandBytes;
            CopyMemory(NewBatch->Data, Channel->CommandData,
                       Channel->CommandBytes);
            Channel->CommandBytes = 0;
            Channel->BorrowedBatch = NewBatch;
            *Batch = NewBatch;
        }
    }
    LeaveCriticalSection(&Channel->Lock);
}

static void STDMETHODCALLTYPE
DwmChannel_ReleaseCommandBatch(IDwmChannelPrivate *Interface)
{
    DWM_CHANNEL *Channel = DwmChannelFromInterface(Interface);
    DWM_COMMAND_BATCH *Batch;

    EnterCriticalSection(&Channel->Lock);
    Batch = Channel->BorrowedBatch;
    Channel->BorrowedBatch = NULL;
    LeaveCriticalSection(&Channel->Lock);
    HeapFree(GetProcessHeap(), 0, Batch);
}

static BOOL STDMETHODCALLTYPE
DwmChannel_IsRemoteTreeEnabled(IDwmChannelPrivate *Interface)
{
    UNREFERENCED_PARAMETER(Interface);
    return FALSE;
}

static void
DwmInitializeChannelVtable(void)
{
    ZeroMemory(&g_DwmChannelVtbl, sizeof(g_DwmChannelVtbl));
    g_DwmChannelVtbl.QueryInterface = DwmChannel_QueryInterface;
    g_DwmChannelVtbl.AddRef = DwmChannel_AddRef;
    g_DwmChannelVtbl.Release = DwmChannel_Release;
    g_DwmChannelVtbl.Commit = DwmChannel_Commit;
    g_DwmChannelVtbl.SynchronizedCommit = DwmChannel_SynchronizedCommit;
    g_DwmChannelVtbl.PeekNextMessage = DwmChannel_PeekNextMessage;
    g_DwmChannelVtbl.SyncFlush = DwmChannel_SyncFlush;
    g_DwmChannelVtbl.WaitForNextMessage = DwmChannel_WaitForNextMessage;
    g_DwmChannelVtbl.AddRefResource = DwmChannel_AddRefResource;
    g_DwmChannelVtbl.CreateResource = DwmChannel_CreateResource;
    g_DwmChannelVtbl.CreateSharedResource = DwmChannel_CreateSharedResource;
    g_DwmChannelVtbl.DuplicateSharedResource = DwmChannel_DuplicateSharedResource;
    g_DwmChannelVtbl.ReleaseResource = DwmChannel_ReleaseResource;
    g_DwmChannelVtbl.CreateRenderDataBuilder = DwmChannel_CreateRenderDataBuilder;
    g_DwmChannelVtbl.QueryResourceInterface = DwmChannel_QueryResourceInterface;
    g_DwmChannelVtbl.RoundTripRequest = DwmChannel_RoundTripRequest;
    g_DwmChannelVtbl.AsyncFlush = DwmChannel_AsyncFlush;
    g_DwmChannelVtbl.PartitionRegisterForNotifications =
        DwmChannel_PartitionRegisterForNotifications;
    g_DwmChannelVtbl.PartitionSetCurrentMmTask =
        DwmChannel_PartitionSetCurrentMmTask;
    g_DwmChannelVtbl.PartitionSwitchRemotingMode =
        DwmChannel_PartitionSwitchRemotingMode;
    g_DwmChannelVtbl.PartitionSetCursor = DwmChannel_PartitionSetCursor;
    g_DwmChannelVtbl.PartitionSetMagnifier = DwmChannel_PartitionSetMagnifier;
    g_DwmChannelVtbl.PartitionSetExcludeFromDDA =
        DwmChannel_PartitionSetExcludeFromDDA;
    g_DwmChannelVtbl.PartitionToggleHolographicSuspension =
        DwmChannel_PartitionToggleHolographicSuspension;
    g_DwmChannelVtbl.BitmapSource = DwmChannel_BitmapSource;
    g_DwmChannelVtbl.DoubleResourceUpdate = DwmChannel_DoubleResourceUpdate;
    g_DwmChannelVtbl.RectResourceUpdate = DwmChannel_RectResourceUpdate;
    g_DwmChannelVtbl.SizeResourceUpdate = DwmChannel_SizeResourceUpdate;
    g_DwmChannelVtbl.ColorTransformResourceUpdate =
        DwmChannel_ColorTransformResourceUpdate;
    g_DwmChannelVtbl.RedirectVisualSetRedirectedVisual =
        DwmChannel_RedirectVisualSetRedirectedVisual;
    g_DwmChannelVtbl.RenderDataUpdate = DwmChannel_RenderDataUpdate;
    g_DwmChannelVtbl.SyncLegacyVisualCaptureRenderTargetCaptureBits =
        DwmChannel_SyncLegacyVisualCaptureRenderTargetCaptureBits;
    g_DwmChannelVtbl.RectangleGeometrySetRectangle =
        DwmChannel_RectangleGeometrySetRectangle;
    g_DwmChannelVtbl.VisualSetBlurredWallpaperSurface =
        DwmChannel_VisualSetBlurredWallpaperSurface;
    g_DwmChannelVtbl.VisualSetTouchTargetRect =
        DwmChannel_VisualSetTouchTargetRect;
    g_DwmChannelVtbl.VisualSetOptions = DwmChannel_VisualSetOptions;
    g_DwmChannelVtbl.VisualSetContent = DwmChannel_VisualSetContent;
    g_DwmChannelVtbl.VisualSetColorTransform =
        DwmChannel_VisualSetColorTransform;
    g_DwmChannelVtbl.VisualTopLevelNode = DwmChannel_VisualTopLevelNode;
    g_DwmChannelVtbl.VisualSetPassiveUpdateMode =
        DwmChannel_VisualSetPassiveUpdateMode;
    g_DwmChannelVtbl.VisualSetExcludeSubtree =
        DwmChannel_VisualSetExcludeSubtree;
    g_DwmChannelVtbl.VisualTargetSetRoot = DwmChannel_VisualTargetSetRoot;
    g_DwmChannelVtbl.WindowNodeInitialize = DwmChannel_WindowNodeInitialize;
    g_DwmChannelVtbl.WindowNodeSetIsComposeOnce =
        DwmChannel_WindowNodeSetIsComposeOnce;
    g_DwmChannelVtbl.VisualGroupUpdate = DwmChannel_VisualGroupUpdate;
    g_DwmChannelVtbl.RenderTargetSetRoot = DwmChannel_RenderTargetSetRoot;
    g_DwmChannelVtbl.SyncDesktopCaptureBits =
        DwmChannel_SyncDesktopCaptureBits;
    g_DwmChannelVtbl.SyncMagnifierRenderTargetCaptureBits =
        DwmChannel_SyncMagnifierRenderTargetCaptureBits;
    g_DwmChannelVtbl.IndirectSwapchainRenderTargetUpdateTargetBounds =
        DwmChannel_IndirectSwapchainRenderTargetUpdateTargetBounds;
    g_DwmChannelVtbl.IndirectSwapchainRenderTargetUnregister =
        DwmChannel_IndirectSwapchainRenderTargetUnregister;
    g_DwmChannelVtbl.BaseAnimationAddBinding =
        DwmChannel_BaseAnimationAddBinding;
    g_DwmChannelVtbl.BaseAnimationRemoveBinding =
        DwmChannel_BaseAnimationRemoveBinding;
    g_DwmChannelVtbl.AnimationUpdateBeginTime =
        DwmChannel_AnimationUpdateBeginTime;
    g_DwmChannelVtbl.AnimationUpdatePrimitives =
        DwmChannel_AnimationUpdatePrimitives;
    g_DwmChannelVtbl.AnimationSetTrigger = DwmChannel_AnimationSetTrigger;
    g_DwmChannelVtbl.EffectGroupUpdate = DwmChannel_EffectGroupUpdate;
    g_DwmChannelVtbl.CachedVisualImageFreeze =
        DwmChannel_CachedVisualImageFreeze;
    g_DwmChannelVtbl.CachedVisualImageUpdate =
        DwmChannel_CachedVisualImageUpdate;
    g_DwmChannelVtbl.CachedVisualImageSnapshot =
        DwmChannel_CachedVisualImageSnapshot;
    g_DwmChannelVtbl.AnimationTriggerTrigger =
        DwmChannel_AnimationTriggerTrigger;
    g_DwmChannelVtbl.MeshGeometry2DUpdate =
        DwmChannel_MeshGeometry2DUpdate;
    g_DwmChannelVtbl.Geometry2DGroupUpdate =
        DwmChannel_Geometry2DGroupUpdate;
    g_DwmChannelVtbl.AtlasedRectsMeshUpdate =
        DwmChannel_AtlasedRectsMeshUpdate;
    g_DwmChannelVtbl.AtlasedRectsMeshSetOpacity =
        DwmChannel_AtlasedRectsMeshSetOpacity;
    g_DwmChannelVtbl.AtlasedRectsGroupUpdate =
        DwmChannel_AtlasedRectsGroupUpdate;
    g_DwmChannelVtbl.GaussianBlurEffectUpdate =
        DwmChannel_GaussianBlurEffectUpdate;
    g_DwmChannelVtbl.MatrixTransform3DUpdate =
        DwmChannel_MatrixTransform3DUpdate;
    g_DwmChannelVtbl.Transform3DGroupUpdate =
        DwmChannel_Transform3DGroupUpdate;
    g_DwmChannelVtbl.TransformGroupUpdate =
        DwmChannel_TransformGroupUpdate;
    g_DwmChannelVtbl.TranslateTransformUpdate =
        DwmChannel_TranslateTransformUpdate;
    g_DwmChannelVtbl.ScaleTransformUpdate = DwmChannel_ScaleTransformUpdate;
    g_DwmChannelVtbl.RotateTransformUpdate = DwmChannel_RotateTransformUpdate;
    g_DwmChannelVtbl.MatrixTransformUpdate =
        DwmChannel_MatrixTransformUpdate;
    g_DwmChannelVtbl.CombinedGeometryUpdate =
        DwmChannel_CombinedGeometryUpdate;
    g_DwmChannelVtbl.RgnGeometryUpdate = DwmChannel_RgnGeometryUpdate;
    g_DwmChannelVtbl.HolographicInteropTextureSetRoot =
        DwmChannel_HolographicInteropTextureSetRoot;
    g_DwmChannelVtbl.SolidColorLegacyMilBrushUpdate =
        DwmChannel_SolidColorLegacyMilBrushUpdate;
    g_DwmChannelVtbl.LinearGradientLegacyMilBrushUpdate =
        DwmChannel_LinearGradientLegacyMilBrushUpdate;
    g_DwmChannelVtbl.ImageLegacyMilBrushUpdate =
        DwmChannel_ImageLegacyMilBrushUpdate;
    g_DwmChannelVtbl.VisualSetResampleMode =
        DwmChannel_VisualSetResampleMode;
    g_DwmChannelVtbl.MagnifierRenderTargetSetTransform =
        DwmChannel_MagnifierRenderTargetSetTransform;
    g_DwmChannelVtbl.MagnifierRenderTargetCreate =
        DwmChannel_MagnifierRenderTargetCreate;
    g_DwmChannelVtbl.MagnifierRenderTargetSetColorTransform =
        DwmChannel_MagnifierRenderTargetSetColorTransform;
    g_DwmChannelVtbl.MagnifierRenderTargetSetFilterList =
        DwmChannel_MagnifierRenderTargetSetFilterList;
    g_DwmChannelVtbl.MagnifierRenderTargetUpdate =
        DwmChannel_MagnifierRenderTargetUpdate;
    g_DwmChannelVtbl.SyncIndirectSwapchainRenderTargetCreate =
        DwmChannel_SyncIndirectSwapchainRenderTargetCreate;
    g_DwmChannelVtbl.MagnifierRenderTargetSetResampleMode =
        DwmChannel_MagnifierRenderTargetSetResampleMode;
    g_DwmChannelVtbl.CaptureControllerSetRootVisual =
        DwmChannel_CaptureControllerSetRootVisual;
    g_DwmChannelVtbl.CaptureControllerSetCaptureState =
        DwmChannel_CaptureControllerSetCaptureState;
    g_DwmChannelVtbl.CaptureControllerSetContentSize =
        DwmChannel_CaptureControllerSetContentSize;
    g_DwmChannelVtbl.CaptureControllerSetTransform =
        DwmChannel_CaptureControllerSetTransform;
    g_DwmChannelVtbl.CaptureControllerSetDefaultSDRBoost =
        DwmChannel_CaptureControllerSetDefaultSDRBoost;
    g_DwmChannelVtbl.CaptureControllerSetReferenceVisual =
        DwmChannel_CaptureControllerSetReferenceVisual;
    g_DwmChannelVtbl.CaptureControllerSetSuspendOnScreenOff =
        DwmChannel_CaptureControllerSetSuspendOnScreenOff;
    g_DwmChannelVtbl.CursorVisualSetCursorId =
        DwmChannel_CursorVisualSetCursorId;
    g_DwmChannelVtbl.CursorVisualSetIsHardwareCursorEnabled =
        DwmChannel_CursorVisualSetIsHardwareCursorEnabled;
    g_DwmChannelVtbl.CursorVisualSetIsSynchronized =
        DwmChannel_CursorVisualSetIsSynchronized;
    g_DwmChannelVtbl.CursorVisualSetPosition =
        DwmChannel_CursorVisualSetPosition;
    g_DwmChannelVtbl.CaptureControllerSetContentOffset =
        DwmChannel_CaptureControllerSetContentOffset;
    g_DwmChannelVtbl.CaptureControllerSetWindowInfos =
        DwmChannel_CaptureControllerSetWindowInfos;
    g_DwmChannelVtbl.GetCommandBatch = DwmChannel_GetCommandBatch;
    g_DwmChannelVtbl.ReleaseCommandBatch = DwmChannel_ReleaseCommandBatch;
    g_DwmChannelVtbl.IsRemoteTreeEnabled = DwmChannel_IsRemoteTreeEnabled;
}

static DWORD WINAPI
DwmEngineThread(LPVOID Parameter)
{
    HMIL_CONNECTION *Connection = Parameter;
    HANDLE Events[2] = {Connection->StopEvent, Connection->StartEvent};

    if (WaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, INFINITE) ==
        WAIT_OBJECT_0 + 1)
        DwmCoreCompositorThread(Connection->StopEvent);
    return 0;
}

BOOL WINAPI
DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);

    if (Reason == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&g_EngineLock);
        DwmInitializeChannelVtable();
        DisableThreadLibraryCalls(Instance);
    }
    else if (Reason == DLL_PROCESS_DETACH)
    {
        if (Reserved == NULL && g_Connection != NULL)
            SetEvent(g_Connection->StopEvent);
        DeleteCriticalSection(&g_EngineLock);
    }
    return TRUE;
}

HRESULT CDECL
CreateCompressedSourceBitmap(IWICImagingFactory *ImagingFactory,
                             const BYTE *CompressedBytes,
                             UINT ByteCount,
                             double DpiX,
                             double DpiY,
                             IWICBitmap **Bitmap)
{
    IWICStream *Stream = NULL;
    IWICBitmapDecoder *Decoder = NULL;
    IWICBitmapFrameDecode *Frame = NULL;
    HRESULT Result;

    if (Bitmap != NULL)
        *Bitmap = NULL;

    if (ImagingFactory == NULL || CompressedBytes == NULL ||
        ByteCount == 0 || Bitmap == NULL)
        return E_INVALIDARG;

    Result = IWICImagingFactory_CreateStream(ImagingFactory, &Stream);
    if (SUCCEEDED(Result))
        Result = IWICStream_InitializeFromMemory(
            Stream, (BYTE *)CompressedBytes, ByteCount);
    if (SUCCEEDED(Result))
        Result = IWICImagingFactory_CreateDecoderFromStream(
            ImagingFactory, (IStream *)Stream, NULL,
            WICDecodeMetadataCacheOnLoad, &Decoder);
    if (SUCCEEDED(Result))
        Result = IWICBitmapDecoder_GetFrame(Decoder, 0, &Frame);
    if (SUCCEEDED(Result))
        Result = IWICImagingFactory_CreateBitmapFromSource(
            ImagingFactory, (IWICBitmapSource *)Frame,
            WICBitmapCacheOnLoad, Bitmap);
    if (SUCCEEDED(Result))
        Result = IWICBitmap_SetResolution(*Bitmap, DpiX, DpiY);

    if (FAILED(Result) && *Bitmap != NULL)
    {
        IWICBitmap_Release(*Bitmap);
        *Bitmap = NULL;
    }
    if (Frame != NULL)
        IWICBitmapFrameDecode_Release(Frame);
    if (Decoder != NULL)
        IWICBitmapDecoder_Release(Decoder);
    if (Stream != NULL)
        IWICStream_Release(Stream);
    return Result;
}

VOID CDECL
SignalStartNowEvent(VOID)
{
    EnterCriticalSection(&g_EngineLock);
    if (g_Connection != NULL)
        SetEvent(g_Connection->StartEvent);
    LeaveCriticalSection(&g_EngineLock);
}

HRESULT CDECL
MilCompositionEngine_CreateChannel(IDwmChannelProvider *Provider,
                                   IDwmChannelPrivate **Channel)
{
    DWM_CHANNEL *NewChannel;
    HMIL_CONNECTION *Connection;

    EnterCriticalSection(&g_EngineLock);
    if (g_Connection == NULL)
    {
        LeaveCriticalSection(&g_EngineLock);
        return DWM_E_CHANNEL_UNAVAILABLE;
    }
    Connection = g_Connection;
    LeaveCriticalSection(&g_EngineLock);

    if (Provider == NULL || Channel == NULL)
        return E_INVALIDARG;

    NewChannel = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           sizeof(*NewChannel));
    if (NewChannel == NULL)
        return E_OUTOFMEMORY;
    NewChannel->IDwmChannelPrivate_iface.lpVtbl = &g_DwmChannelVtbl;
    NewChannel->References = 1;
    NewChannel->Connection = Connection;
    NewChannel->Provider = Provider;
    InitializeCriticalSection(&NewChannel->Lock);
    DwmListInitialize(&NewChannel->Messages);
    NewChannel->MessageEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (NewChannel->MessageEvent == NULL)
    {
        DeleteCriticalSection(&NewChannel->Lock);
        HeapFree(GetProcessHeap(), 0, NewChannel);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    IUnknown_AddRef((IUnknown *)Provider);
    *Channel = &NewChannel->IDwmChannelPrivate_iface;
    return S_OK;
}

HRESULT CDECL
MilCompositionEngine_CreateCursorController(
    ULONGLONG Identifier,
    IDwmCursorController **Controller)
{
    DWM_CURSOR_CONTROLLER *NewController;

    EnterCriticalSection(&g_EngineLock);
    if (g_Connection == NULL)
    {
        LeaveCriticalSection(&g_EngineLock);
        return DWM_E_ENGINE_NOT_INITIALIZED;
    }
    LeaveCriticalSection(&g_EngineLock);

    if (Controller == NULL)
        return E_INVALIDARG;

    NewController = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              sizeof(*NewController));
    if (NewController == NULL)
        return E_OUTOFMEMORY;
    NewController->IUnknown_iface.lpVtbl = &g_DwmCursorControllerVtbl;
    NewController->References = 1;
    NewController->Identifier = Identifier;
    *Controller = (IDwmCursorController *)&NewController->IUnknown_iface;
    return S_OK;
}

HRESULT CDECL
MilCompositionEngine_GetComposedEventId(UINT *EventId)
{
    if (EventId == NULL)
        return E_INVALIDARG;

    EnterCriticalSection(&g_EngineLock);
    if (g_Connection == NULL)
    {
        LeaveCriticalSection(&g_EngineLock);
        return DWM_E_ENGINE_NOT_INITIALIZED;
    }
    *EventId = g_Connection->ComposedEventId;
    LeaveCriticalSection(&g_EngineLock);
    return S_OK;
}

HRESULT CDECL
MilCompositionEngine_Initialize(INT Flags, HMIL_CONNECTION **Connection)
{
    HMIL_CONNECTION *NewConnection;

    if (Connection == NULL)
        return E_INVALIDARG;

    EnterCriticalSection(&g_EngineLock);
    if (g_Connection != NULL)
    {
        LeaveCriticalSection(&g_EngineLock);
        return DWM_E_ENGINE_NOT_INITIALIZED;
    }

    NewConnection = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              sizeof(*NewConnection));
    if (NewConnection == NULL)
    {
        LeaveCriticalSection(&g_EngineLock);
        return E_OUTOFMEMORY;
    }

    NewConnection->StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    NewConnection->StartEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (NewConnection->StopEvent == NULL || NewConnection->StartEvent == NULL)
    {
        if (NewConnection->StopEvent != NULL)
            CloseHandle(NewConnection->StopEvent);
        if (NewConnection->StartEvent != NULL)
            CloseHandle(NewConnection->StartEvent);
        HeapFree(GetProcessHeap(), 0, NewConnection);
        LeaveCriticalSection(&g_EngineLock);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    NewConnection->Magic = DWM_CONNECTION_MAGIC;
    NewConnection->Flags = Flags;
    NewConnection->ComposedEventId =
        RegisterWindowMessageW(L"ReactOS.Dwm.Composed");
    if (NewConnection->ComposedEventId == 0)
        NewConnection->ComposedEventId = WM_APP + 0x3D;

    NewConnection->Thread =
        CreateThread(NULL, 0, DwmEngineThread,
                     NewConnection, CREATE_SUSPENDED, NULL);
    if (NewConnection->Thread == NULL)
    {
        HRESULT Result = HRESULT_FROM_WIN32(GetLastError());

        CloseHandle(NewConnection->StartEvent);
        CloseHandle(NewConnection->StopEvent);
        HeapFree(GetProcessHeap(), 0, NewConnection);
        LeaveCriticalSection(&g_EngineLock);
        return Result;
    }

    g_Connection = NewConnection;
    *Connection = NewConnection;
    ResumeThread(NewConnection->Thread);
    LeaveCriticalSection(&g_EngineLock);
    return S_OK;
}

HRESULT CDECL
MilCompositionEngine_Uninitialize(HMIL_CONNECTION *Connection)
{
    EnterCriticalSection(&g_EngineLock);
    if (Connection == NULL || Connection != g_Connection ||
        Connection->Magic != DWM_CONNECTION_MAGIC)
    {
        LeaveCriticalSection(&g_EngineLock);
        return E_INVALIDARG;
    }

    g_Connection = NULL;
    Connection->Magic = 0;
    SetEvent(Connection->StopEvent);
    LeaveCriticalSection(&g_EngineLock);

    WaitForSingleObject(Connection->Thread, INFINITE);
    CloseHandle(Connection->Thread);
    CloseHandle(Connection->StartEvent);
    CloseHandle(Connection->StopEvent);
    HeapFree(GetProcessHeap(), 0, Connection);
    return S_OK;
}
