//
//    Copyright (C) Microsoft.  All rights reserved.
//
#ifndef _FXCXDEVICEINFO_H_
#define _FXCXDEVICEINFO_H_

#include "fxdevicecallbacks.hpp"

struct FxCxDeviceInfo : public FxStump {
    FxCxDeviceInfo(PFX_DRIVER_GLOBALS FxDriverGlobals) :
        Driver(NULL),
        IoInCallerContextCallback(FxDriverGlobals),
        Index(0)
    {
        InitializeListHead(&ListEntry);
        RtlZeroMemory(&RequestAttributes, sizeof(RequestAttributes));
        RtlZeroMemory(&PnpPowerCallbacks, sizeof(PnpPowerCallbacks));
        RtlZeroMemory(&PowerPolicyCallbacks, sizeof(PowerPolicyCallbacks));
    }

    ~FxCxDeviceInfo()
    {
        ASSERT(IsListEmpty(&ListEntry));
    }

    LIST_ENTRY                  ListEntry;
    FxDriver*                   Driver;
    FxIoInCallerContext         IoInCallerContextCallback;
    WDF_OBJECT_ATTRIBUTES       RequestAttributes;
    WDFCX_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
    WDFCX_POWER_POLICY_EVENT_CALLBACKS PowerPolicyCallbacks;
    CCHAR                       Index;
};

#endif // _FXCXDEVICEINFO_H_
