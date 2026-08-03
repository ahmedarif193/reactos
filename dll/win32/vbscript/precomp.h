
#ifndef _VBSCRIPT_PRECOMP_H
#define _VBSCRIPT_PRECOMP_H

#ifdef __REACTOS__
#include <wine/config.h>
#include <math.h>
#include <wine/port.h>
double __cdecl trunc(double);
#endif

#include <assert.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include "vbscript.h"

#ifdef __REACTOS__
#include <wingdi.h>
#endif
#include <winnls.h>
#include <objsafe.h>

#include <wine/debug.h>

#include "parse.h"
#include "regexp.h"
#include "vbscript_defs.h"

#endif /* !_VBSCRIPT_PRECOMP_H */
