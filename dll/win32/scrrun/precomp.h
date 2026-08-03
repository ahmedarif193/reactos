
#ifndef _SCRRUN_PRECOMP_H_
#define _SCRRUN_PRECOMP_H_

#include <wine/config.h>

#include <stdarg.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#define COBJMACROS

#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winver.h>
#include <objbase.h>
#include <oleauto.h>
#include <dispex.h>
#include <scrrun.h>

#include <wine/debug.h>
#include <wine/port.h>

#ifdef __REACTOS__
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif
#endif

#include "scrrun_private.h"

#endif /* !_SCRRUN_PRECOMP_H_ */
