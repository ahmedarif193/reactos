/*
 * PROJECT: ReactOS host-side USBPORT tests
 * LICENSE: GPL-2.0-or-later
 * PURPOSE: Test the real DMA mapper, including repeated physical pages.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define IN
#define NTAPI
#define TRUE 1
#define PAGE_SIZE 4096
#define NormalPagePriority 0
#define USBPORT_TRANSFER_TYPE_BULK 2
#define USBPORT_DMA_DIRECTION_TO_DEVICE 1
#define TRANSFER_FLAG_BOUNCE 1
#define TRANSFER_FLAG_HIGH_SPEED 2
#define TRANSFER_FLAG_DMA_MAPPED 4
#define TRANSFER_FLAG_ISO 8
#define UsbHighSpeed 2
#define UsbSuperSpeed 3
#define USBPORT_MAP_CALLBACK_DONE 2
#define USBPORT_MAP_CALLBACK_PENDING 1
#define INVALIDATE_ENDPOINT_WORKER_THREAD 1
#define USBD_STATUS_SUCCESS 0
#define USBD_STATUS_INTERNAL_HC_ERROR 0x80000800u
#define DeallocateObjectKeepRegisters 2
#define DPRINT(...)
#define DPRINT1(...)
#define DPRINT_CORE(...)
#define ASSERT assert
#define CONTAINING_RECORD(p, t, f) ((t *)((char *)(p) - offsetof(t, f)))
typedef uint32_t ULONG, USBD_STATUS;
typedef int32_t LONG;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef int BOOLEAN, IO_ALLOCATION_ACTION;
typedef void *PVOID, *PIRP;
typedef union {
    struct
    {
        uint32_t LowPart;
        int32_t HighPart;
    };
    int64_t QuadPart;
} PHYSICAL_ADDRESS;
typedef struct _LIST_ENTRY
{
    struct _LIST_ENTRY *Flink, *Blink;
} LIST_ENTRY;
static void InitializeListHead(LIST_ENTRY *l)
{
    l->Flink = l->Blink = l;
}
static int IsListEmpty(LIST_ENTRY *l)
{
    return l->Flink == l;
}
static void RemoveHeadList(LIST_ENTRY *l)
{
    assert(0);
}
static void InsertTailList(LIST_ENTRY *l, LIST_ENTRY *e)
{
    l->Flink = e;
}
typedef struct
{
    uintptr_t Va;
    ULONG Bytes;
} MDL, *PMDL;
typedef struct
{
    PHYSICAL_ADDRESS SgPhysicalAddress;
    SIZE_T SgTransferLength, SgOffset;
} SG_ELEMENT;
typedef struct
{
    ULONG Flags;
    uintptr_t CurrentVa;
    PVOID MappedSystemVa;
    ULONG SgElementCount;
    SG_ELEMENT SgElement[256];
} SG_LIST, *PUSBPORT_SCATTER_GATHER_LIST;
typedef struct
{
    LONG DeviceHandleLock;
} DEVICE_HANDLE, *PUSBPORT_DEVICE_HANDLE;
typedef struct
{
    struct
    {
        PMDL TransferBufferMDL;
    } UrbControlTransfer;
    struct
    {
        PVOID UsbdDeviceHandle;
    } UrbHeader;
    int UrbIsochronousTransfer;
} URB, *PURB;
typedef struct
{
    struct
    {
        int TransferType, DeviceSpeed;
    } EndpointProperties;
    int EndpointSpinLock, EndpointOldIrql;
    LIST_ENTRY TransferList;
} ENDPOINT, *PUSBPORT_ENDPOINT;
typedef struct
{
    URB *Urb;
    ENDPOINT *Endpoint;
    struct
    {
        ULONG TransferBufferLength, TransferFlags;
    } TransferParameters;
    ULONG Flags;
    int Direction;
    PVOID MapRegisterBase;
    SG_LIST SgList;
    LIST_ENTRY TransferLink;
} TRANSFER, USBPORT_TRANSFER, *PUSBPORT_TRANSFER;
struct _DMA_ADAPTER;
typedef struct
{
    PHYSICAL_ADDRESS (*MapTransfer)(struct _DMA_ADAPTER *, PMDL, PVOID, PVOID, ULONG *, BOOLEAN);
} DMA_OPS, *PDMA_OPERATIONS;
typedef struct _DMA_ADAPTER
{
    DMA_OPS *DmaOperations;
} DMA_ADAPTER, *PDMA_ADAPTER;
typedef struct
{
    DMA_ADAPTER *DmaAdapter;
    LONG MapTransferCallbackState;
    int MapTransferDpc;
} EXT, *PUSBPORT_DEVICE_EXTENSION;
typedef struct
{
    EXT *DeviceExtension;
} DEV, *PDEVICE_OBJECT;
static int split_calls, done_calls, map_calls, mode, hal_chunk, dpcs;
static ULONG done_status, advanced, total;
static uintptr_t initial;
static PVOID MmGetMdlVirtualAddress(PMDL m)
{
    return (PVOID)m->Va;
}
static PVOID MmGetSystemAddressForMdlSafe(PMDL m, int priority)
{
    return (PVOID)0xabc;
}
static void KeAcquireSpinLock(int *a, int *b)
{
}
static void KeReleaseSpinLock(int *a, int b)
{
}
static void USBPORT_SplitTransfer(PDEVICE_OBJECT d, PUSBPORT_ENDPOINT e, PUSBPORT_TRANSFER t,
                                  LIST_ENTRY *l)
{
    split_calls++;
    InitializeListHead(l);
}
static void USBPORT_QueueDoneTransfer(PUSBPORT_TRANSFER t, USBD_STATUS status, int mapped)
{
    assert(t->Flags & TRANSFER_FLAG_DMA_MAPPED);
    done_calls++;
    done_status = status;
}
static USBD_STATUS USBPORT_InitializeIsoTransfer(PDEVICE_OBJECT d, int *u, PUSBPORT_TRANSFER t)
{
    assert(0);
    return 0;
}
static int USBPORT_EndpointWorker(PUSBPORT_ENDPOINT e, int x)
{
    return 0;
}
static void USBPORT_InvalidateEndpointHandler(PDEVICE_OBJECT d, PUSBPORT_ENDPOINT e, int type)
{
    assert(0);
}
static LONG InterlockedDecrement(LONG *p)
{
    return --*p;
}
static LONG InterlockedExchange(LONG *p, LONG v)
{
    LONG old = *p;
    *p = v;
    return old;
}
static void KeInsertQueueDpc(int *d, PVOID a, PVOID b)
{
    dpcs++;
}
static PHYSICAL_ADDRESS map(PDMA_ADAPTER a, PMDL m, PVOID registers, PVOID va, ULONG *length,
                            BOOLEAN out)
{
    assert(++map_calls <= 256);
    assert(registers == (PVOID)0x42);
    assert((uintptr_t)va == initial + advanced);
    assert(*length == total - advanced);
    assert(out);
    PHYSICAL_ADDRESS pa;
    ULONG offset = (uintptr_t)va & (PAGE_SIZE - 1), amount = hal_chunk - offset;
    if (amount > *length)
        amount = *length;
    pa.QuadPart = 0x200000 + offset;
    if (mode == 1)
        pa.QuadPart += advanced - offset; /* contiguous */
    if (mode == 2)
        pa.QuadPart += ((advanced / PAGE_SIZE) * 7 % 11) * PAGE_SIZE; /* non-monotonic */
    if (mode == 3 || (mode == 5 && map_calls == 2))
    {
        *length = 0;
        return pa;
    }
    if (mode == 4)
    {
        ++*length;
        return pa;
    }
    *length = amount;
    advanced += amount;
    return pa;
}
#include "map_transfer.h"
static void run(ULONG bytes, ULONG offset, int mapping, int chunk, int bounce, int pending)
{
    static TRANSFER t;
    MDL mdl = {0x100000 + offset, bytes};
    DEVICE_HANDLE device = {1};
    URB urb = {0};
    ENDPOINT ep = {0};
    DMA_OPS ops = {map};
    DMA_ADAPTER adapter = {&ops};
    EXT ext = {&adapter, pending ? 1 : 0, 0};
    DEV fdo = {&ext};
    memset(&t, 0, sizeof(t));
    memset(&t.SgList, 0xa5, sizeof(t.SgList));
    InitializeListHead(&ep.TransferList);
    urb.UrbControlTransfer.TransferBufferMDL = &mdl;
    urb.UrbHeader.UsbdDeviceHandle = &device;
    ep.EndpointProperties.TransferType = USBPORT_TRANSFER_TYPE_BULK;
    ep.EndpointProperties.DeviceSpeed = UsbSuperSpeed;
    t.Urb = &urb;
    t.Endpoint = &ep;
    t.Direction = USBPORT_DMA_DIRECTION_TO_DEVICE;
    t.TransferParameters.TransferBufferLength = bytes;
    t.Flags = bounce ? TRANSFER_FLAG_BOUNCE : 0;
    initial = mdl.Va;
    total = bytes;
    advanced = 0;
    mode = mapping;
    hal_chunk = chunk;
    map_calls = split_calls = done_calls = dpcs = 0;
    done_status = 0;
    assert(USBPORT_MapTransfer(&fdo, NULL, (PVOID)0x42, &t) == DeallocateObjectKeepRegisters);
    assert(device.DeviceHandleLock == 0 &&
           ext.MapTransferCallbackState == USBPORT_MAP_CALLBACK_DONE && dpcs == pending);
    assert(t.MapRegisterBase == (PVOID)0x42 && (t.Flags & TRANSFER_FLAG_DMA_MAPPED));
    if (mapping >= 3)
    {
        assert(done_calls == 1 && done_status == USBD_STATUS_INTERNAL_HC_ERROR && split_calls == 0);
        return;
    }
    assert(done_calls == 0 && split_calls == 1 && advanced == bytes);
    SIZE_T sum = 0;
    for (ULONG i = 0; i < t.SgList.SgElementCount; i++)
    {
        SG_ELEMENT *e = &t.SgList.SgElement[i];
        assert(e->SgOffset == sum && e->SgTransferLength > 0);
        if (!bounce)
            assert((e->SgPhysicalAddress.QuadPart & (PAGE_SIZE - 1)) + e->SgTransferLength <=
                   PAGE_SIZE);
        sum += e->SgTransferLength;
    }
    assert(sum == bytes); /* Old code truncates aliased mappings at 8 KiB. */
    assert(t.SgList.SgElement[t.SgList.SgElementCount].SgPhysicalAddress.QuadPart ==
           (int64_t)0xa5a5a5a5a5a5a5a5ull);
}
int main(void)
{
    int cases = 0;
    ULONG sizes[] = {253952, 0, 1, 4095, 4096, 4097};
    ULONG offsets[] = {0, 337};
    for (unsigned n = 0; n < sizeof(sizes) / sizeof(*sizes); n++)
        for (unsigned o = 0; o < 2; o++)
            for (int pattern = 0; pattern < 3; pattern++)
                for (int bounce = 0; bounce < 2; bounce++)
                    for (int pending = 0; pending < 2; pending++)
                    {
                        run(sizes[n], offsets[o], pattern, 4096, bounce, pending);
                        cases++;
                    }
    for (int pattern = 0; pattern < 3; pattern++)
        for (int bounce = 0; bounce < 2; bounce++)
        {
            run(253952, 337, pattern, 16384, bounce, 0);
            cases++;
        }
    for (int bad = 3; bad <= 5; bad++)
        for (int bounce = 0; bounce < 2; bounce++)
        {
            run(253952, 337, bad, 4096, bounce, 1);
            cases++;
        }
    printf(
        "%d production DMA-mapper cases passed: repeated/contiguous/non-monotonic physical pages, "
        "offsets, bounce mode, empty buffers, invalid HAL lengths and callback completion\n",
        cases);
}
