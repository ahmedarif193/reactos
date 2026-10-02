/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Audio class extension 1.1 interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _ACX_H_
#define _ACX_H_

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

typedef VOID (*ACXFUNC) (VOID);
extern ACXFUNC AcxFunctions[];

#include "acxtypes.h"
#include "acxglobals.h"
#include "acxfuncenum.h"
#include "acxmisc.h"
#include "acxdriver.h"
#include "acxdevice.h"
#include "acxrequest.h"
#include "acxevents.h"
#include "acxpin.h"
#include "acxcircuit.h"
#include "acxelements.h"
#include "acxdataformat.h"
#include "acxstreams.h"
#include "acxtargets.h"
#include "acxmanager.h"

WDF_EXTERN_C_END

#endif
