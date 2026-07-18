/* Generate the Direct3D 9 GUID definitions missing from ReactOS' dxguid library. */

#include <stdarg.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <initguid.h>
#include <d3d9.h>
