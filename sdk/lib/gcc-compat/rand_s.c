#include <errno.h>
#include <windows.h>

typedef BOOLEAN (WINAPI *PFN_RtlGenRandom)(PVOID, ULONG);

/* Optional BCrypt fallback */
typedef NTSTATUS (WINAPI *PFN_BCryptGenRandom)(PVOID /*BCRYPT_ALG_HANDLE*/,
                                               PUCHAR, ULONG, ULONG);
#ifndef BCRYPT_USE_SYSTEM_PREFERRED_RNG
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002
#endif

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

static PFN_RtlGenRandom pRtlGenRandom;
static PFN_BCryptGenRandom pBCryptGenRandom;

static void ensure_resolved(void)
{
    /* Resolve advapi32!SystemFunction036 (RtlGenRandom) */
    if (!pRtlGenRandom) {
        HMODULE advapi = GetModuleHandleW(L"advapi32.dll");
        if (!advapi)
            advapi = LoadLibraryExW(L"advapi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (advapi)
            pRtlGenRandom = (PFN_RtlGenRandom)GetProcAddress(advapi, "SystemFunction036");
    }

    /* Resolve bcrypt!BCryptGenRandom as a fallback */
    if (!pBCryptGenRandom) {
        HMODULE bcrypt = GetModuleHandleW(L"bcrypt.dll");
        if (!bcrypt)
            bcrypt = LoadLibraryExW(L"bcrypt.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (bcrypt)
            pBCryptGenRandom = (PFN_BCryptGenRandom)GetProcAddress(bcrypt, "BCryptGenRandom");
    }
}

errno_t rand_s(unsigned int *value)
{
    if (!value) {
        errno = EINVAL;
        return EINVAL;
    }

    ensure_resolved();

    if (pRtlGenRandom && pRtlGenRandom(value, sizeof(*value)))
        return 0;

    if (pBCryptGenRandom &&
        pBCryptGenRandom(NULL, (PUCHAR)value, sizeof(*value), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0)
        return 0;

    errno = EINVAL;
    return EINVAL;
}

#if defined(__GNUC__) && !defined(__clang__)
#  ifdef _WIN64
__attribute__((used))
errno_t (__cdecl *__imp_rand_s)(unsigned int *) = rand_s;
#  else
__attribute__((used))
errno_t (__cdecl *__imp__rand_s)(unsigned int *) = rand_s;
__attribute__((used))
errno_t (__cdecl *_imp__rand_s)(unsigned int *) = rand_s;
#  endif
#endif
