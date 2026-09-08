/* SPDX-License-Identifier: GPL-2.0-or-later
 * Black-box tests for native runtime imports used by RPi5FanControl.
 */
#define SECURITY_WIN32
#include <windows.h>
#include <sspi.h>
#include <stdio.h>
#include <pseh/pseh2.h>

typedef BOOL (WINAPI *GETIDEAL)(HANDLE, PPROCESSOR_NUMBER);
typedef BOOL (WINAPI *SETIDEAL)(HANDLE, PPROCESSOR_NUMBER, PPROCESSOR_NUMBER);
typedef BOOL (WINAPI *GETNODE)(PPROCESSOR_NUMBER, PUSHORT);
typedef SECURITY_STATUS (WINAPI *SECATTR)(PSecHandle, ULONG, PVOID, ULONG);
static GETIDEAL GetIdeal;
static SETIDEAL SetIdeal;
static GETNODE GetNode;
static volatile LONG Failures;
static DWORD CpuCount;
#define CHECK(c) do { if (!(c)) { InterlockedIncrement(&Failures); printf("FAIL line=%u error=%lu\n", __LINE__, GetLastError()); } } while (0)

static DWORD WINAPI Stress(PVOID Arg)
{
    PROCESSOR_NUMBER original, input, previous, current;
    ULONG i;
    if (!GetIdeal(GetCurrentThread(), &original)) { CHECK(FALSE); return 1; }
    for (i = 0; i < 5000; ++i)
    {
        input.Group = 0; input.Number = (UCHAR)((i + (ULONG_PTR)Arg) % CpuCount); input.Reserved = 0;
        CHECK(SetIdeal(GetCurrentThread(), &input, &previous));
        CHECK(GetIdeal(GetCurrentThread(), &current));
        CHECK(current.Group == 0 && current.Number == input.Number && current.Reserved == 0);
        /* Input and previous-output aliasing is explicitly supported. */
        CHECK(SetIdeal(GetCurrentThread(), &input, &input));
        CHECK(input.Group == current.Group && input.Number == current.Number && input.Reserved == 0);
    }
    CHECK(SetIdeal(GetCurrentThread(), &original, NULL));
    return 0;
}

static void ProcessorProbes(void)
{
    PROCESSOR_NUMBER original, input, previous, output;
    DWORD nums[] = {0, 1, 3, 4, 63, 64, 254, 255};
    WORD groups[] = {0, 1, 0xffff};
    HANDLE handles[] = { (HANDLE)-2, NULL, (HANDLE)-1, (HANDLE)0x1234 };
    unsigned g, i, r, h;
    BOOL result;
    DWORD error, exception;
    USHORT node;
    HANDLE workers[8];
    CHECK(GetIdeal(GetCurrentThread(), &original));
    printf("ORIGINAL group=%u number=%u reserved=%u cpus=%lu\n", original.Group, original.Number, original.Reserved, CpuCount);
    for (g = 0; g < 3; ++g) for (i = 0; i < 8; ++i) for (r = 0; r < 2; ++r)
    {
        input.Group = groups[g]; input.Number = (BYTE)nums[i]; input.Reserved = (BYTE)r;
        memset(&previous, 0xcc, sizeof(previous)); SetLastError(0xdeadbeef);
        result = SetIdeal(GetCurrentThread(), &input, &previous); error = GetLastError();
        CHECK(result == (groups[g] == 0 && nums[i] < CpuCount && r == 0));
        if (!result) CHECK(error == ERROR_INVALID_PARAMETER && previous.Group == 0xcccc && previous.Number == 0xcc && previous.Reserved == 0xcc);
        else CHECK(error == 0xdeadbeef);
        CHECK(input.Group == groups[g] && input.Number == nums[i] && input.Reserved == r);
        printf("SET g=%u n=%u r=%u ret=%u err=%lu prev=%04x/%02x/%02x input=%04x/%02x/%02x\n", groups[g], (unsigned)nums[i], r, result, error, previous.Group, previous.Number, previous.Reserved, input.Group, input.Number, input.Reserved);
        node = 0xcccc; SetLastError(0xdeadbeef);
        result = GetNode(&input, &node); error = GetLastError();
        CHECK(result == (groups[g] == 0 && nums[i] < CpuCount && r == 0));
        if (!result) CHECK(error == ERROR_INVALID_PARAMETER && node == 0xffff);
        else CHECK(node == 0 && error == 0xdeadbeef);
        printf("NUMA g=%u n=%u r=%u ret=%u err=%lu node=%04x\n", groups[g], (unsigned)nums[i], r, result, error, node);
    }
    for (h = 0; h < 4; ++h)
    {
        memset(&output, 0xcc, sizeof(output)); SetLastError(0xdeadbeef);
        result = GetIdeal(handles[h], &output); error = GetLastError();
        printf("GET handle=%u ret=%u err=%lu out=%04x/%02x/%02x\n", h, result, error, output.Group, output.Number, output.Reserved);
    }
    for (i = 0; i < 5; ++i)
    {
        input = original; exception = 0; result = FALSE; SetLastError(0xdeadbeef);
        _SEH2_TRY
        {
            if (i == 0) result = GetIdeal(GetCurrentThread(), NULL);
            if (i == 1) result = SetIdeal(GetCurrentThread(), NULL, NULL);
            if (i == 2) result = GetNode(NULL, &node);
            if (i == 3) result = GetNode(&input, NULL);
            if (i == 4) result = SetIdeal(GetCurrentThread(), &input, (PPROCESSOR_NUMBER)1);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { exception = _SEH2_GetExceptionCode(); }
        _SEH2_END;
        CHECK(exception == EXCEPTION_ACCESS_VIOLATION);
        printf("POINTER case=%u ret=%u err=%lu exception=%08lx\n", i, result, GetLastError(), exception);
    }
    CHECK(SetIdeal(GetCurrentThread(), &original, NULL));
    for (i = 0; i < 8; ++i) { workers[i] = CreateThread(NULL, 0, Stress, (PVOID)(ULONG_PTR)i, 0, NULL); CHECK(workers[i] != NULL); }
    for (i = 0; i < 8; ++i) if (workers[i]) { CHECK(WaitForSingleObject(workers[i], 120000) == WAIT_OBJECT_0); CloseHandle(workers[i]); }
    printf("PROCESSOR_STRESS_DONE iterations=40000 failures=%ld\n", Failures);
}

static void AccessProbes(void)
{
    DWORD masks[] = {SYNCHRONIZE, THREAD_QUERY_INFORMATION, THREAD_QUERY_LIMITED_INFORMATION, THREAD_SET_INFORMATION, 0x0400 /* THREAD_SET_LIMITED_INFORMATION */};
    PROCESSOR_NUMBER original, output, previous;
    unsigned i;
    CHECK(GetIdeal(GetCurrentThread(), &original));
    for (i = 0; i < sizeof(masks)/sizeof(masks[0]); ++i)
    {
        HANDLE thread = OpenThread(masks[i], FALSE, GetCurrentThreadId());
        BOOL result;
        CHECK(thread != NULL);
        SetLastError(0xdeadbeef);
        result = GetIdeal(thread, &output);
        CHECK(result == (masks[i] == THREAD_QUERY_INFORMATION || masks[i] == THREAD_QUERY_LIMITED_INFORMATION));
        if (!result) CHECK(GetLastError() == ERROR_ACCESS_DENIED);
        printf("ACCESS_GET mask=%lx ret=%u error=%lu\n", masks[i], result, GetLastError());
        SetLastError(0xdeadbeef);
        result = SetIdeal(thread, &original, &previous);
        CHECK(result == (masks[i] == THREAD_SET_INFORMATION));
        if (!result) CHECK(GetLastError() == ERROR_ACCESS_DENIED);
        printf("ACCESS_SET mask=%lx ret=%u error=%lu\n", masks[i], result, GetLastError());
        CloseHandle(thread);
    }
}

static void SecurityProbes(void)
{
    HMODULE sec = LoadLibraryW(L"secur32.dll"), cli = LoadLibraryW(L"sspicli.dll");
    SECATTR funcs[2];
    SecHandle empty = {0, 0};
    ULONG attrs[] = {0, 1, 2, 4, 5, 6, 9, 0x53, 0x59, 0x60, 0x64, 0x80000000};
    ULONG sizes[] = {0, 1, 4, 8, 16, 64, 256};
    BYTE buffer[256];
    unsigned f, a, s, nul;
    funcs[0] = (SECATTR)GetProcAddress(sec, "SetCredentialsAttributesW");
    funcs[1] = (SECATTR)GetProcAddress(cli, "QueryContextAttributesExW");
    for (f = 0; f < 2; ++f)
    {
        CHECK(funcs[f] != NULL);
        if (!funcs[f]) continue;
        for (a = 0; a < sizeof(attrs)/sizeof(attrs[0]); ++a) for (s = 0; s < 7; ++s) for (nul = 0; nul < 2; ++nul)
        {
            SECURITY_STATUS status = 0xdeadbeef;
            DWORD exception = 0;
            memset(buffer, 0xcc, sizeof(buffer)); SetLastError(0xdeadbeef);
            _SEH2_TRY { status = funcs[f](nul ? NULL : &empty, attrs[a], buffer, sizes[s]); }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { exception = _SEH2_GetExceptionCode(); }
            _SEH2_END;
            CHECK(status == SEC_E_INVALID_HANDLE && exception == 0 && buffer[0] == 0xcc);
            printf("SEC f=%u a=%lx size=%lu null=%u status=%08lx err=%lu exception=%08lx first=%02x\n", f, attrs[a], sizes[s], nul, status, GetLastError(), exception, buffer[0]);
        }
    }
}

static void ValidSecurityProbes(void)
{
    HMODULE sec = LoadLibraryW(L"secur32.dll");
    INIT_SECURITY_INTERFACE_W init = (INIT_SECURITY_INTERFACE_W)GetProcAddress(sec, "InitSecurityInterfaceW");
    PSecurityFunctionTableW table = init();
    SECATTR set = (SECATTR)GetProcAddress(sec, "SetCredentialsAttributesW");
    SECATTR query = (SECATTR)GetProcAddress(LoadLibraryW(L"sspicli.dll"), "QueryContextAttributesExW");
    CredHandle cred;
    CtxtHandle context, server;
    CredHandle inbound;
    SecBuffer challenge = {0, SECBUFFER_TOKEN, NULL};
    SecBufferDesc challengeDesc = {SECBUFFER_VERSION, 1, &challenge};
    TimeStamp expiry;
    SecBuffer token = {0, SECBUFFER_TOKEN, NULL};
    SecBufferDesc desc = {SECBUFFER_VERSION, 1, &token};
    ULONG flags = 0, a, size;
    ULONG attrs[] = {0, 2, 4, 14, 0x5a, 0x80000000};
    BYTE data[128];
    SECURITY_STATUS status;

    CHECK(set && query);
    if (!set || !query) return;
    status = table->AcquireCredentialsHandleW(NULL, L"NTLM", SECPKG_CRED_OUTBOUND, NULL, NULL, NULL, NULL, &cred, &expiry);
    printf("SEC_ACQUIRE status=%08lx\n", status);
    CHECK(status == SEC_E_OK);
    if (status != SEC_E_OK) return;
    for (a = 0; a < 6; ++a)
    {
        DWORD exception = 0;
        memset(data, 0, sizeof(data));
        _SEH2_TRY { status = set(&cred, attrs[a], data, sizeof(data)); }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { exception = _SEH2_GetExceptionCode(); }
        _SEH2_END;
        printf("SEC_VALID_SET a=%lx status=%08lx exception=%08lx\n", attrs[a], status, exception);
    }
    status = table->InitializeSecurityContextW(&cred, NULL, L"localhost", ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONNECTION, 0, SECURITY_NATIVE_DREP, NULL, 0, &context, &desc, &flags, &expiry);
    printf("SEC_INITIALIZE status=%08lx\n", status);
    CHECK(status == SEC_I_CONTINUE_NEEDED || status == SEC_E_OK);
    if (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_OK)
    {
        SECURITY_STATUS hs;
        printf("SEC_OLD_QUERY status=%08lx\n", table->QueryContextAttributesW(&context, SECPKG_ATTR_SIZES, data));
        hs = table->AcquireCredentialsHandleW(NULL, L"NTLM", SECPKG_CRED_INBOUND, NULL, NULL, NULL, NULL, &inbound, &expiry);
        printf("SEC_INBOUND status=%08lx\n", hs);
        if (hs == SEC_E_OK)
        {
            hs = table->AcceptSecurityContext(&inbound, NULL, &desc, ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_CONNECTION, SECURITY_NATIVE_DREP, &server, &challengeDesc, &flags, &expiry);
            printf("SEC_ACCEPT status=%08lx\n", hs);
            if (hs == SEC_I_CONTINUE_NEEDED || hs == SEC_E_OK)
            {
                table->FreeContextBuffer(token.pvBuffer);
                token.pvBuffer = NULL; token.cbBuffer = 0;
                hs = table->InitializeSecurityContextW(&cred, &context, L"localhost", ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONNECTION, 0, SECURITY_NATIVE_DREP, &challengeDesc, 0, &context, &desc, &flags, &expiry);
                printf("SEC_COMPLETE status=%08lx\n", hs);
                CHECK(hs == SEC_E_OK);
                table->DeleteSecurityContext(&server);
            }
            if (challenge.pvBuffer) table->FreeContextBuffer(challenge.pvBuffer);
            table->FreeCredentialsHandle(&inbound);
        }
        for (a = 0; a < 6; ++a) for (size = 0; size <= 64; ++size)
        {
            BYTE expected[128];
            SECURITY_STATUS oldStatus;
            memset(data, 0xcc, sizeof(data));
            memset(expected, 0xcc, sizeof(expected));
            oldStatus = table->QueryContextAttributesW(&context, attrs[a], expected);
            DWORD exception = 0;
            _SEH2_TRY { status = query(&context, attrs[a], data, size); }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { exception = _SEH2_GetExceptionCode(); }
            _SEH2_END;
            CHECK(exception == 0 && status == oldStatus && !memcmp(data, expected, sizeof(data)));
            printf("SEC_VALID_QUERY a=%lx size=%lu status=%08lx exception=%08lx first=%02x guard=%02x\n", attrs[a], size, status, exception, data[0], data[size]);
        }
        table->DeleteSecurityContext(&context);
    }
    if (token.pvBuffer) table->FreeContextBuffer(token.pvBuffer);
    table->FreeCredentialsHandle(&cred);
}

int main(void)
{
    SYSTEM_INFO info;
    OSVERSIONINFOW version = {sizeof(version)};
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    typedef LONG (WINAPI *RTLGETVERSION)(OSVERSIONINFOW*);
    RTLGETVERSION rtlversion = (RTLGETVERSION)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    setvbuf(stdout, NULL, _IONBF, 0);
    rtlversion(&version); GetSystemInfo(&info); CpuCount = info.dwNumberOfProcessors;
    printf("RUNTIME_EXPORTS_BEGIN os=%lu.%lu.%lu arch=%u cpus=%lu\n", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber, info.wProcessorArchitecture, CpuCount);
    GetIdeal = (GETIDEAL)GetProcAddress(kernel, "GetThreadIdealProcessorEx");
    SetIdeal = (SETIDEAL)GetProcAddress(kernel, "SetThreadIdealProcessorEx");
    GetNode = (GETNODE)GetProcAddress(kernel, "GetNumaProcessorNodeEx");
    CHECK(GetIdeal && SetIdeal && GetNode);
    if (GetIdeal && SetIdeal && GetNode) { ProcessorProbes(); AccessProbes(); }
    SecurityProbes();
    ValidSecurityProbes();
    printf("RUNTIME_EXPORTS_DONE failures=%ld\n", Failures);
    return Failures ? 1 : 0;
}
