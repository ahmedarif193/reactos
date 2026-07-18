#include <windef.h>
#include <winbase.h>
#include <winerror.h>

HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
