#include <windows.h>

BOOL WINAPI __reactos_null_entrypoint(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)hinst;
    (void)reason;
    (void)reserved;
    return TRUE;
}
