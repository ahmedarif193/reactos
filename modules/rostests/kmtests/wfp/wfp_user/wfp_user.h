/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform user-mode management test declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _KMTEST_WFP_USER_H_
#define _KMTEST_WFP_USER_H_

#define IOCTL_WFPUSER_REGISTER   1
#define IOCTL_WFPUSER_QUERY      2
#define IOCTL_WFPUSER_UNREGISTER 3

#define WFPUSER_SERVER_PORT 47351

#define WFPUSER_CONTEXT_OWN    11
#define WFPUSER_CONTEXT_OTHER  12
#define WFPUSER_CONTEXT_STREAM 13

#define WFPUSER_FLOW   0
#define WFPUSER_STREAM 1

DEFINE_GUID(WFPUSER_SUBLAYER, 0x3e1b9a52, 0x6c07, 0x4f1d, 0xa8, 0x24, 0x9b, 0x15, 0x70, 0xd3, 0x6e, 0x21);
DEFINE_GUID(WFPUSER_FLOW_CALLOUT, 0x3e1b9a52, 0x6c07, 0x4f1d, 0xa8, 0x24, 0x9b, 0x15, 0x70, 0xd3, 0x6e, 0x22);
DEFINE_GUID(WFPUSER_STREAM_CALLOUT, 0x3e1b9a52, 0x6c07, 0x4f1d, 0xa8, 0x24, 0x9b, 0x15, 0x70, 0xd3, 0x6e, 0x23);
DEFINE_GUID(WFPUSER_UNKNOWN_CALLOUT, 0x3e1b9a52, 0x6c07, 0x4f1d, 0xa8, 0x24, 0x9b, 0x15, 0x70, 0xd3, 0x6e, 0x24);
DEFINE_GUID(WFPUSER_PROVIDER, 0x3e1b9a52, 0x6c07, 0x4f1d, 0xa8, 0x24, 0x9b, 0x15, 0x70, 0xd3, 0x6e, 0x25);

typedef struct _WFPUSER_STATE
{
    LONG Status[2];
    UINT32 CalloutId[2];
    LONG FlowClassify[3];
    LONG Associated;
    LONG FlowDelete;
    LONG StreamClassify;
    LONG StreamBytes[2];
    LONG NotifyAdd[2];
    LONG NotifyDelete[2];
    ULONG NotifyAction[2];
    LONG NotifyContext[2];
    ULONG PathSize;
    WCHAR Path[512];
} WFPUSER_STATE, *PWFPUSER_STATE;

#endif
