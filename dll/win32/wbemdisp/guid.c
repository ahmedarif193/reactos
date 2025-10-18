/* DO NOT USE THE PRECOMPILED HEADER FOR THIS FILE! */

#include <stdarg.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <initguid.h>
#include <wbemdisp_classes.h>
#include <wbemdisp.h>
#include <wbemcli.h>
#include <wmiutils.h>

DEFINE_GUID(IID_IWbemRefresher, 0x49353c99, 0x516b, 0x11d1, 0xae,0xa6, 0x00,0xc0,0x4f,0xb6,0x88,0x20);
DEFINE_GUID(IID_IWbemConfigureRefresher, 0x49353c92, 0x516b, 0x11d1, 0xae,0xa6, 0x00,0xc0,0x4f,0xb6,0x88,0x20);
DEFINE_GUID(CLSID_WbemRefresher, 0xc71566f2, 0x561e, 0x11d1, 0xad,0x87, 0x00,0xc0,0x4f,0xd8,0xfd,0xff);

/* NO CODE HERE, THIS IS JUST REQUIRED FOR THE GUID DEFINITIONS */
