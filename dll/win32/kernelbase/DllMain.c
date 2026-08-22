
#include <windows.h>

void init_locale( HMODULE module );
void WerCleanupRuntimeExceptionModules(void);

const WCHAR windows_dir[] = L"C:\\windows";
const WCHAR system_dir[] = L"C:\\windows\\system32";

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            init_locale(hinstDLL);
            break;

        case DLL_THREAD_ATTACH:
            break;

        case DLL_THREAD_DETACH:
            break;

        case DLL_PROCESS_DETACH:
            if (!lpvReserved) WerCleanupRuntimeExceptionModules();
            break;
    }

    return TRUE;
}
