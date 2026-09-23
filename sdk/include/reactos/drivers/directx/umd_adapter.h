/*
 * PROJECT:     ReactOS Direct3D runtime
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Adapter callbacks for modern user-mode display drivers
 */

#ifndef _ROS_UMD_ADAPTER_H_
#define _ROS_UMD_ADAPTER_H_

#include <d3dumddi.h>

/* Adapter discovery precedes device-DDI negotiation. Modern drivers need the
 * WDDM 2.4 query callback even when the runtime negotiates an older device DDI.
 * Keep this complete adapter table separate from the version-selected device
 * callbacks populated by D3DUmdRtCreateDeviceCallbacksEx. */
typedef struct _ROS_UMD_ADAPTER_CALLBACKS
{
    PFND3DDDI_QUERYADAPTERINFOCB pfnQueryAdapterInfoCb;
    PFND3DDDI_GETMULTISAMPLEMETHODLISTCB pfnGetMultisampleMethodListCb;
    PFND3DDDI_QUERYADAPTERINFOCB2 pfnQueryAdapterInfoCb2;
} ROS_UMD_ADAPTER_CALLBACKS;

C_ASSERT(FIELD_OFFSET(ROS_UMD_ADAPTER_CALLBACKS, pfnQueryAdapterInfoCb2) == 2 * sizeof(void *));
C_ASSERT(FIELD_OFFSET(D3DDDI_ADAPTERCALLBACKS, pfnGetMultisampleMethodListCb) == sizeof(void *));
#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(D3DDDICB_QUERYADAPTERINFO2, pPrivateDriverData) == 8);
C_ASSERT(FIELD_OFFSET(D3DDDICB_QUERYADAPTERINFO2, PrivateDriverDataSize) == 16);
C_ASSERT(sizeof(D3DDDICB_QUERYADAPTERINFO2) == 24);
#else
C_ASSERT(FIELD_OFFSET(D3DDDICB_QUERYADAPTERINFO2, pPrivateDriverData) == 4);
C_ASSERT(FIELD_OFFSET(D3DDDICB_QUERYADAPTERINFO2, PrivateDriverDataSize) == 8);
C_ASSERT(sizeof(D3DDDICB_QUERYADAPTERINFO2) == 12);
#endif

static __inline HRESULT
RosUmdQueryAdapterInfo2(
    HANDLE Adapter,
    const D3DDDICB_QUERYADAPTERINFO2 *Info,
    PFND3DKMT_QUERYADAPTERINFO QueryAdapterInfo)
{
    D3DKMT_QUERYADAPTERINFO Query = {0};
    NTSTATUS Status;

    if (!Adapter || (ULONG_PTR)Adapter > MAXDWORD || !Info ||
        !Info->pPrivateDriverData || !Info->PrivateDriverDataSize || !QueryAdapterInfo)
        return E_INVALIDARG;

    switch (Info->QueryType)
    {
        case D3DDDI_QUERYADAPTERTYPE_DRIVERPRIVATE:
            Query.Type = KMTQAITYPE_UMDRIVERPRIVATE;
            break;
        case D3DDDI_QUERYADAPTERTYPE_QUERYREGISTRY:
            Query.Type = KMTQAITYPE_QUERYREGISTRY;
            break;
        default:
            return E_INVALIDARG;
    }
    Query.hAdapter = (D3DKMT_HANDLE)(ULONG_PTR)Adapter;
    Query.pPrivateDriverData = Info->pPrivateDriverData;
    Query.PrivateDriverDataSize = Info->PrivateDriverDataSize;
    Status = QueryAdapterInfo(&Query);
    return Status >= 0 ? S_OK : HRESULT_FROM_NT(Status);
}

#endif
