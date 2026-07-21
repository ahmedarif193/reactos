/* ReactOS compatibility definitions missing from sdk/include/wine/ddk/d3dkmthk.h. */

#ifndef __WOW64WIN_D3DKMTHK_COMPAT_H
#define __WOW64WIN_D3DKMTHK_COMPAT_H

typedef struct _D3DKMT_ACQUIREKEYEDMUTEX
{
    D3DKMT_HANDLE hKeyedMutex;
    UINT64 Key;
    LARGE_INTEGER *pTimeout;
    UINT64 FenceValue;
} D3DKMT_ACQUIREKEYEDMUTEX;

typedef struct _D3DKMT_RELEASEKEYEDMUTEX
{
    D3DKMT_HANDLE hKeyedMutex;
    UINT64 Key;
    UINT64 FenceValue;
} D3DKMT_RELEASEKEYEDMUTEX;

typedef struct _D3DKMT_ACQUIREKEYEDMUTEX2
{
    D3DKMT_HANDLE hKeyedMutex;
    UINT64 Key;
    LARGE_INTEGER *pTimeout;
    UINT64 FenceValue;
    void *pPrivateRuntimeData;
    UINT PrivateRuntimeDataSize;
} D3DKMT_ACQUIREKEYEDMUTEX2;

typedef struct _D3DKMT_RELEASEKEYEDMUTEX2
{
    D3DKMT_HANDLE hKeyedMutex;
    UINT64 Key;
    UINT64 FenceValue;
    void *pPrivateRuntimeData;
    UINT PrivateRuntimeDataSize;
} D3DKMT_RELEASEKEYEDMUTEX2;

typedef struct _D3DKMT_OPENNTHANDLEFROMNAME
{
    DWORD dwDesiredAccess;
    OBJECT_ATTRIBUTES *pObjAttrib;
    HANDLE hNtHandle;
} D3DKMT_OPENNTHANDLEFROMNAME;

typedef struct _D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU
{
    D3DKMT_HANDLE hDevice;
    UINT ObjectCount;
    const D3DKMT_HANDLE *ObjectHandleArray;
    const UINT64 *FenceValueArray;
    D3DDDICB_SIGNALFLAGS Flags;
} D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU;

typedef struct _D3DDDI_WAITFORSYNCHRONIZATIONOBJECTFROMCPU_FLAGS
{
    union
    {
        struct
        {
            UINT WaitAny : 1;
            UINT Reserved : 31;
        };
        UINT Value;
    };
} D3DDDI_WAITFORSYNCHRONIZATIONOBJECTFROMCPU_FLAGS;

typedef struct _D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU
{
    D3DKMT_HANDLE hDevice;
    UINT ObjectCount;
    const D3DKMT_HANDLE *ObjectHandleArray;
    const UINT64 *FenceValueArray;
    HANDLE hAsyncEvent;
    D3DDDI_WAITFORSYNCHRONIZATIONOBJECTFROMCPU_FLAGS Flags;
} D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU;

#endif /* __WOW64WIN_D3DKMTHK_COMPAT_H */
