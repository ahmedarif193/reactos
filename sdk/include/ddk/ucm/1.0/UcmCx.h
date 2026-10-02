/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB connector manager class extension client interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _UCMCX_H_
#define _UCMCX_H_

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

typedef VOID (*UCMFUNC) (VOID);
extern UCMFUNC UcmFunctions[];

#include "UcmTypes.h"
#include "UcmGlobals.h"
#include "UcmFuncEnum.h"
#include "UcmManager.h"

WDF_EXTERN_C_END

#endif
