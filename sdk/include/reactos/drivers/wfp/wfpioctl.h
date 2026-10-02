/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private management interface between fwpuclnt.dll and the WFP filter engine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _WFPIOCTL_H_
#define _WFPIOCTL_H_

#define WFP_ENGINE_DEVICE_NAME     L"\\Device\\WfpEngine"
#define WFP_ENGINE_SYMLINK_NAME    L"\\DosDevices\\WfpEngine"
#define WFP_ENGINE_WIN32_NAME      L"\\\\.\\WfpEngine"

#define WFP_IOCTL(Function) CTL_CODE(FILE_DEVICE_NETWORK, 0x900 + (Function), METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_WFP_OPEN                WFP_IOCTL(0)
#define IOCTL_WFP_TRANSACTION_BEGIN   WFP_IOCTL(1)
#define IOCTL_WFP_TRANSACTION_COMMIT  WFP_IOCTL(2)
#define IOCTL_WFP_TRANSACTION_ABORT   WFP_IOCTL(3)
#define IOCTL_WFP_PROVIDER_ADD        WFP_IOCTL(4)
#define IOCTL_WFP_PROVIDER_DELETE     WFP_IOCTL(5)
#define IOCTL_WFP_SUBLAYER_ADD        WFP_IOCTL(6)
#define IOCTL_WFP_SUBLAYER_DELETE     WFP_IOCTL(7)
#define IOCTL_WFP_CALLOUT_ADD         WFP_IOCTL(8)
#define IOCTL_WFP_CALLOUT_DELETE_KEY  WFP_IOCTL(9)
#define IOCTL_WFP_CALLOUT_DELETE_ID   WFP_IOCTL(10)
#define IOCTL_WFP_FILTER_ADD          WFP_IOCTL(11)
#define IOCTL_WFP_FILTER_DELETE_KEY   WFP_IOCTL(12)
#define IOCTL_WFP_FILTER_DELETE_ID    WFP_IOCTL(13)

typedef struct _WFP_IOCTL_OPEN
{
    ULONG Flags;
    ULONG TransactionTimeout;
} WFP_IOCTL_OPEN, *PWFP_IOCTL_OPEN;

typedef struct _WFP_IOCTL_OBJECT
{
    GUID Key;
    GUID ProviderKey;
    GUID LayerKey;
    ULONG Flags;
    USHORT Weight;
    BOOLEAN HasProvider;
} WFP_IOCTL_OBJECT, *PWFP_IOCTL_OBJECT;

typedef struct _WFP_IOCTL_VALUE
{
    ULONG Type;
    ULONG Size;
    ULONG Offset;
    ULONG64 Number;
} WFP_IOCTL_VALUE, *PWFP_IOCTL_VALUE;

typedef struct _WFP_IOCTL_CONDITION
{
    GUID FieldKey;
    ULONG MatchType;
    WFP_IOCTL_VALUE Value;
    WFP_IOCTL_VALUE High;
} WFP_IOCTL_CONDITION, *PWFP_IOCTL_CONDITION;

typedef struct _WFP_IOCTL_FILTER
{
    GUID FilterKey;
    GUID ProviderKey;
    GUID LayerKey;
    GUID SubLayerKey;
    GUID ActionKey;
    ULONG Flags;
    ULONG ActionType;
    ULONG WeightType;
    ULONG ConditionCount;
    ULONG64 Weight;
    ULONG64 RawContext;
    BOOLEAN HasProvider;
    WFP_IOCTL_CONDITION Conditions[ANYSIZE_ARRAY];
} WFP_IOCTL_FILTER, *PWFP_IOCTL_FILTER;

#endif
