#include <windef.h>
#include <winbase.h>
#include <ntsecapi.h>

BOOL WINAPI ProcessPrng(PBYTE pbData, SIZE_T cbData)
{
    return RtlGenRandom(pbData, (ULONG)cbData);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    return TRUE;
}
