
#ifndef _JSCRIPT_PRECOMP_H
#define _JSCRIPT_PRECOMP_H

#ifdef __REACTOS__
#include <wine/config.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <wine/port.h>
#ifndef _swprintf_l
#define _swprintf_l _snwprintf_l
#endif
#endif

#include <assert.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include "jscript.h"

#include <objsafe.h>

#include <wine/debug.h>

#include "engine.h"
#include "regexp.h"

#endif /* !_JSCRIPT_PRECOMP_H */
