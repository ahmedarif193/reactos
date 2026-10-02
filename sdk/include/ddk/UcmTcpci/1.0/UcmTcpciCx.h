/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB Type-C port controller interface class extension client interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _UCMTCPCICX_H_
#define _UCMTCPCICX_H_

#ifndef WDF_EXTERN_C
  #ifdef __cplusplus
    #define WDF_EXTERN_C       extern "C"
    #define WDF_EXTERN_C_START extern "C" {
    #define WDF_EXTERN_C_END   }
  #else
    #define WDF_EXTERN_C
    #define WDF_EXTERN_C_START
    #define WDF_EXTERN_C_END
  #endif
#endif

WDF_EXTERN_C_START

typedef VOID (*UCMTCPCIFUNC) (VOID);
extern UCMTCPCIFUNC UcmtcpciFunctions[];

#include "UcmTcpciGlobals.h"
#include "UcmTcpciFuncEnum.h"
#include "UcmTcpciSpec.h"
#include "UcmTcpciDevice.h"
#include "UcmTcpciPortController.h"
#include "UcmTcpciPortControllerRequests.h"

WDF_EXTERN_C_END

#endif
