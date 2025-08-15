/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS kernel
 * FILE:        include/reactos/services/services.h
 * PURPOSE:     Private interface between SERVICES.EXE and ADVAPI32.DLL
 * PROGRAMMER:  Eric Kohl
 */

#ifndef __SERVICES_SERVICES_H__
#define __SERVICES_SERVICES_H__

/*
 * Internal control codes.
 * Neither in the range of public control codes (1-10 and 11-14 or 16 or 32 or 64)
 * nor in the range of user-defined control codes (128-255).
 */
/* Start a service that shares a process with other services */
#define SERVICE_CONTROL_START_SHARE 80
/* Start a service that runs in its own process */
#define SERVICE_CONTROL_START_OWN   81

/*
 * Start event name used by OpenSCManager
 * to know whether the SCM is initialized.
 */
#define SCM_START_EVENT L"SvcctrlStartEvent_A3752DX"
#define SCM_GLOBAL_PREFIX L"Global\\"

/* Full event name with namespace for cross-process access */
#ifdef _M_AMD64
    /* On AMD64, concatenate Global prefix with event name */
    #define SCM_START_EVENT_FULL SCM_GLOBAL_PREFIX SCM_START_EVENT
#else
    /* On i386, use event name without prefix */
    #define SCM_START_EVENT_FULL SCM_START_EVENT
#endif

typedef struct _SCM_CONTROL_PACKET
{
    DWORD dwSize;
    DWORD dwControl;
    DWORD dwArgumentsCount;
    DWORD dwServiceTag;
    SERVICE_STATUS_HANDLE hServiceStatus;
    DWORD dwServiceNameOffset;
    DWORD dwArgumentsOffset;
} SCM_CONTROL_PACKET, *PSCM_CONTROL_PACKET;

typedef struct _SCM_REPLY_PACKET
{
    DWORD dwError;
} SCM_REPLY_PACKET, *PSCM_REPLY_PACKET;

#endif /* __SERVICES_SERVICES_H__ */

/* EOF */
