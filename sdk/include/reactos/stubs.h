
#ifndef _WINDEF_
typedef unsigned short wchar_t;
typedef unsigned long DWORD, ULONG;
typedef void* PVOID;
#define __int64 long long
#ifndef LONG
typedef long LONG;
#endif
#ifdef _WIN64
typedef unsigned long long ULONG_PTR;
#else
typedef unsigned long ULONG_PTR;
#endif

#define EXCEPTION_MAXIMUM_PARAMETERS 15
typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD* ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, * PEXCEPTION_RECORD;

#define EXCEPTION_NONCONTINUABLE  0x01

#endif

#ifndef PRIx64
#define PRIx64 "I64x"
#endif

#define EXCEPTION_WINE_STUB     0x80000100
#define EH_NONCONTINUABLE       0x01

#ifndef STATUS_DLL_INIT_FAILED
#define STATUS_DLL_INIT_FAILED  ((LONG)0xC0000142)
#endif

/* __int128 is not supported on x86, so use a custom type */
typedef struct
{
    __int64 lower;
    __int64 upper;
} MyInt128;

void
__stdcall
RtlRaiseException(
    PEXCEPTION_RECORD ExceptionRecord
);

#ifdef _M_AMD64
void __stdcall RtlRaiseStatus(long Status);
#endif

ULONG
__cdecl
DbgPrint(
    const char* Format,
    ...
);

#ifdef _M_AMD64
static __inline void __wine_raise_unimplemented_stub(const char* module, const char* function)
{
    static volatile LONG __wine_stub_guard;

    if (__wine_stub_guard)
    {
        DbgPrint("__wine_stub_guard triggered for %s.%s, raising STATUS_DLL_INIT_FAILED\n", module, function);
        EXCEPTION_RECORD ExceptionRecord = {0};
        ExceptionRecord.ExceptionCode = STATUS_DLL_INIT_FAILED;
        ExceptionRecord.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
        RtlRaiseException(&ExceptionRecord);
    }

    __wine_stub_guard = 1;

    DbgPrint("__wine_spec_unimplemented_stub: %s.%s\n", module, function);

    EXCEPTION_RECORD ExceptionRecord = {0};
    ExceptionRecord.ExceptionRecord = 0;
    ExceptionRecord.ExceptionCode = EXCEPTION_WINE_STUB;
    ExceptionRecord.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    ExceptionRecord.ExceptionInformation[0] = (ULONG_PTR)module;
    ExceptionRecord.ExceptionInformation[1] = (ULONG_PTR)function;
    ExceptionRecord.NumberParameters = 2;
    RtlRaiseException(&ExceptionRecord);
}

#define __wine_spec_unimplemented_stub(module, function) \
    __wine_raise_unimplemented_stub(module, function)

#else

#define __wine_spec_unimplemented_stub(module, function) \
{ \
    EXCEPTION_RECORD ExceptionRecord = {0}; \
    ExceptionRecord.ExceptionRecord = 0; \
    ExceptionRecord.ExceptionCode = EXCEPTION_WINE_STUB; \
    ExceptionRecord.ExceptionFlags = EXCEPTION_NONCONTINUABLE; \
    ExceptionRecord.ExceptionInformation[0] = (ULONG_PTR)module; \
    ExceptionRecord.ExceptionInformation[1] = (ULONG_PTR)function; \
    ExceptionRecord.NumberParameters = 2; \
    RtlRaiseException(&ExceptionRecord); \
}

#endif
