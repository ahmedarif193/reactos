/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Extended-context API exports and processor-state buffer contracts.
 */

#include <windows.h>
#include <wine/test.h>

#ifdef __x86_64__
static void test_module(HMODULE module)
{
    BOOL (WINAPI *initialize)(void *, DWORD, CONTEXT **, DWORD *);
    BOOL (WINAPI *initialize2)(void *, DWORD, CONTEXT **, DWORD *, DWORD64);
    BOOL (WINAPI *get_mask)(CONTEXT *, DWORD64 *);
    BOOL (WINAPI *set_mask)(CONTEXT *, DWORD64);
    void *(WINAPI *locate)(CONTEXT *, DWORD, DWORD *);
    DWORD64 (WINAPI *get_enabled)(void);
    BYTE *buffer, *snapshot;
    CONTEXT *context;
    DWORD length, needed, flags, feature_length, error;
    DWORD64 mask, enabled;
    unsigned int i, j;
    BOOL ret;
    void *feature;

    initialize = (void *)GetProcAddress(module, "InitializeContext");
    initialize2 = (void *)GetProcAddress(module, "InitializeContext2");
    get_mask = (void *)GetProcAddress(module, "GetXStateFeaturesMask");
    set_mask = (void *)GetProcAddress(module, "SetXStateFeaturesMask");
    locate = (void *)GetProcAddress(module, "LocateXStateFeature");
    get_enabled = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetEnabledXStateFeatures");
    ok(initialize != NULL, "InitializeContext is missing\n");
    ok(initialize2 != NULL, "InitializeContext2 is missing\n");
    ok(get_mask != NULL, "GetXStateFeaturesMask is missing\n");
    ok(set_mask != NULL, "SetXStateFeaturesMask is missing\n");
    ok(locate != NULL, "LocateXStateFeature is missing\n");
    if (!initialize || !initialize2 || !get_mask || !set_mask || !locate || !get_enabled)
        return;

    enabled = get_enabled();
    trace("Enabled features %llx\n", (unsigned long long)enabled);
    buffer = VirtualAlloc(NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    snapshot = HeapAlloc(GetProcessHeap(), 0, 0x10000);
    ok(buffer != NULL && snapshot != NULL, "Could not allocate context buffers\n");
    if (!buffer || !snapshot) goto cleanup;

    length = 0xdeadbeef;
    context = (void *)(ULONG_PTR)0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = initialize(buffer, 0, &context, &length);
    ok(!ret && GetLastError() == ERROR_INVALID_PARAMETER,
       "Invalid flags: ret=%d error=%lu\n", ret, GetLastError());
    ok(context == (void *)(ULONG_PTR)0xdeadbeef && length == 0xdeadbeef,
       "Invalid flags changed outputs: context=%p length=%lu\n", context, length);

    for (i = 0; i < 2; ++i)
    {
        flags = CONTEXT_ALL | (i ? CONTEXT_XSTATE : 0);
        needed = 0;
        SetLastError(0xdeadbeef);
        ret = initialize(NULL, flags, NULL, &needed);
        error = GetLastError();
        ok(!ret && error == ERROR_INSUFFICIENT_BUFFER && needed >= sizeof(CONTEXT),
           "Size query: flags=%#lx ret=%d error=%lu needed=%lu\n", flags, ret, error, needed);
        if (ret || error != ERROR_INSUFFICIENT_BUFFER || needed > 0x8000) continue;

        memset(buffer, 0xcc, 0x10000);
        context = (void *)(ULONG_PTR)0xdeadbeef;
        length = needed - 1;
        ret = initialize(buffer, flags, &context, &length);
        ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER && length == needed,
           "Short buffer: ret=%d error=%lu length=%lu needed=%lu\n", ret, GetLastError(), length, needed);
        ok(context == (void *)(ULONG_PTR)0xdeadbeef && buffer[0] == 0xcc,
           "Short buffer changed context/output\n");

        for (j = 0; j < 16; ++j)
        {
            memset(buffer, 0xcc, 0x10000);
            length = needed;
            SetLastError(0xdeadbeef);
            ret = initialize(buffer + j, flags, &context, &length);
            ok(ret && GetLastError() == 0xdeadbeef,
               "Initialize offset %u flags %#lx: ret=%d error=%lu\n", j, flags, ret, GetLastError());
            if (!ret) continue;
            ok((BYTE *)context >= buffer + j && (BYTE *)(context + 1) <= buffer + j + needed,
               "Context %p outside supplied buffer\n", context);
            ok(!((ULONG_PTR)context & 15), "Unaligned context %p\n", context);
            ok(context->ContextFlags == flags || (i && context->ContextFlags == CONTEXT_ALL && !(enabled & ~3ull)),
               "Unexpected context flags %#lx, requested %#lx\n", context->ContextFlags, flags);
            ok(buffer[j + needed] == 0xcc, "Wrote beyond context buffer\n");

            memcpy(snapshot, buffer, 0x10000);
            mask = 0xdeadbeef;
            ret = get_mask(context, &mask);
            ok(ret && mask == 3, "Initial mask: ret=%d mask=%llx\n", ret, (unsigned long long)mask);
            ok(!memcmp(buffer, snapshot, 0x10000), "Reading mask changed the context\n");

            feature_length = 0xdeadbeef;
            feature = locate(context, 0, &feature_length);
            ok(feature == &context->FltSave && feature_length == 160,
               "Legacy feature: address=%p length=%lu\n", feature, feature_length);
            feature = locate(context, 1, &feature_length);
            ok(feature == context->FltSave.XmmRegisters && feature_length == 256,
               "SSE feature: address=%p length=%lu\n", feature, feature_length);

            context->ContextFlags &= ~8u;
            ret = get_mask(context, &mask);
            ok(ret && mask == 0, "Absent legacy state: ret=%d mask=%llx\n", ret, (unsigned long long)mask);
            ret = set_mask(context, 1);
            ok(ret, "Could not enable legacy state\n");
            ret = get_mask(context, &mask);
            ok(ret && mask == 3, "Legacy roundtrip: ret=%d mask=%llx\n", ret, (unsigned long long)mask);

            ret = set_mask(context, 4);
            if ((context->ContextFlags & CONTEXT_XSTATE) == CONTEXT_XSTATE)
            {
                ok(ret, "Could not set extended mask\n");
                ret = get_mask(context, &mask);
                ok(ret && mask == (3 | (enabled & 4)), "Extended roundtrip: ret=%d mask=%llx\n", ret, (unsigned long long)mask);
                if (enabled & 4)
                {
                    feature = locate(context, 2, &feature_length);
                    ok(feature != NULL && feature_length == 256 && !((ULONG_PTR)feature & 63),
                       "AVX feature: address=%p length=%lu\n", feature, feature_length);
                }
            }
            else
            {
                ok(!ret, "Set extended mask without an extended context succeeded\n");
                ok(locate(context, 2, NULL) == NULL, "Located AVX without extended context\n");
            }
            context->ContextFlags = 0;
            mask = 0xdeadbeef;
            SetLastError(0xdeadbeef);
            ret = get_mask(context, &mask);
            ok(!ret && mask == 0xdeadbeef && GetLastError() == ERROR_NOT_SUPPORTED,
               "Invalid architecture: ret=%d mask=%llx error=%lu\n", ret, (unsigned long long)mask, GetLastError());
        }
    }

    length = 0;
    ret = initialize2(NULL, CONTEXT_ALL, NULL, &length, 0);
    ok(!ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER, "InitializeContext2 size query failed\n");
    SetLastError(0xdeadbeef);
    ret = initialize2(buffer, CONTEXT_ALL, &context, &length, 0);
    ok(ret && context->ContextFlags == CONTEXT_ALL && GetLastError() == 0xdeadbeef,
       "InitializeContext2 legacy context failed\n");

cleanup:
    if (buffer) VirtualFree(buffer, 0, MEM_RELEASE);
    if (snapshot) HeapFree(GetProcessHeap(), 0, snapshot);
}
#endif

START_TEST(XState)
{
#ifdef __x86_64__
    HMODULE module = LoadLibraryA("kernelbase.dll");
    trace("kernel32 exports\n");
    test_module(GetModuleHandleA("kernel32.dll"));
    ok(module != NULL, "Could not load kernelbase.dll: %lu\n", GetLastError());
    if (module)
    {
        trace("kernelbase exports\n");
        test_module(module);
        FreeLibrary(module);
    }
#else
    skip("Native AMD64 context contracts\n");
#endif
}
