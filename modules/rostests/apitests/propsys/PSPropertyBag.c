/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Torture tests for PSPropertyBag_WriteDWORD
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#define COBJMACROS
#define CONST_VTABLE

#include <apitest.h>
#include <objbase.h>
#include <oleauto.h>
#include <ocidl.h>
#include <string.h>
#include <wchar.h>
#include <pseh/pseh2.h>

typedef HRESULT (WINAPI *FN_PSPropertyBag_WriteDWORD)(IPropertyBag *, LPCWSTR, DWORD);

static FN_PSPropertyBag_WriteDWORD pPSPropertyBag_WriteDWORD;

typedef struct _TESTBAG
{
    IPropertyBag IPropertyBag_iface;
    LONG cRef;
    LONG cWrites;
    LONG cReads;
    WCHAR szLastName[260];
    VARTYPE vtLast;
    ULONG ulLast;
    HRESULT hrWrite;
    BOOL bClearedByCallee;
} TESTBAG;

static inline TESTBAG *impl_from_IPropertyBag(IPropertyBag *iface)
{
    return CONTAINING_RECORD(iface, TESTBAG, IPropertyBag_iface);
}

static HRESULT STDMETHODCALLTYPE
TestBag_QueryInterface(IPropertyBag *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyBag))
    {
        *ppv = iface;
        IPropertyBag_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
TestBag_AddRef(IPropertyBag *iface)
{
    return InterlockedIncrement(&impl_from_IPropertyBag(iface)->cRef);
}

static ULONG STDMETHODCALLTYPE
TestBag_Release(IPropertyBag *iface)
{
    return InterlockedDecrement(&impl_from_IPropertyBag(iface)->cRef);
}

static HRESULT STDMETHODCALLTYPE
TestBag_Read(IPropertyBag *iface, LPCOLESTR pszName, VARIANT *pVar, IErrorLog *pLog)
{
    InterlockedIncrement(&impl_from_IPropertyBag(iface)->cReads);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
TestBag_Write(IPropertyBag *iface, LPCOLESTR pszName, VARIANT *pVar)
{
    TESTBAG *This = impl_from_IPropertyBag(iface);

    InterlockedIncrement(&This->cWrites);
    This->vtLast = V_VT(pVar);
    This->ulLast = V_UI4(pVar);
    lstrcpynW(This->szLastName, pszName, ARRAYSIZE(This->szLastName));
    return This->hrWrite;
}

static const IPropertyBagVtbl TestBagVtbl =
{
    TestBag_QueryInterface,
    TestBag_AddRef,
    TestBag_Release,
    TestBag_Read,
    TestBag_Write
};

static void
InitBag(_Out_ TESTBAG *pBag)
{
    memset(pBag, 0, sizeof(*pBag));
    pBag->IPropertyBag_iface.lpVtbl = &TestBagVtbl;
    pBag->cRef = 1;
    pBag->hrWrite = S_OK;
}

static void
Test_BadArguments(void)
{
    TESTBAG bag;
    HRESULT hr;

    InitBag(&bag);

    hr = pPSPropertyBag_WriteDWORD(NULL, L"Value", 42);
    ok(hr == E_INVALIDARG, "NULL bag: expected E_INVALIDARG, got 0x%08lx\n", hr);

    hr = pPSPropertyBag_WriteDWORD(&bag.IPropertyBag_iface, NULL, 42);
    ok(hr == E_INVALIDARG, "NULL name: expected E_INVALIDARG, got 0x%08lx\n", hr);
    ok(bag.cWrites == 0, "the bag must not be touched on failure, %ld writes\n", bag.cWrites);

    hr = pPSPropertyBag_WriteDWORD(NULL, NULL, 42);
    ok(hr == E_INVALIDARG, "both NULL: expected E_INVALIDARG, got 0x%08lx\n", hr);

    hr = E_NOTIMPL;
    _SEH2_TRY
    {
        hr = pPSPropertyBag_WriteDWORD((IPropertyBag *)InvalidPointer, L"Value", 42);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("bogus bag raised 0x%08lx\n", _SEH2_GetExceptionCode());
        hr = E_FAIL;
    }
    _SEH2_END;
    ok(FAILED(hr), "a bogus bag must not succeed, got 0x%08lx\n", hr);

    InitBag(&bag);
    hr = E_NOTIMPL;
    _SEH2_TRY
    {
        hr = pPSPropertyBag_WriteDWORD(&bag.IPropertyBag_iface, (LPCWSTR)InvalidPointer, 42);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("bogus name raised 0x%08lx\n", _SEH2_GetExceptionCode());
        hr = E_FAIL;
    }
    _SEH2_END;
    ok(bag.cWrites == 1,
       "the name pointer is forwarded verbatim, so the bag must still be called once (%ld)\n",
       bag.cWrites);
}

static void
Test_Values(void)
{
    static const DWORD Values[] =
    {
        0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xDEADBEEF, 0x0000FFFF, 0xFFFF0000
    };
    TESTBAG bag;
    HRESULT hr;
    UINT i;

    for (i = 0; i < ARRAYSIZE(Values); i++)
    {
        InitBag(&bag);
        hr = pPSPropertyBag_WriteDWORD(&bag.IPropertyBag_iface, L"Value", Values[i]);
        ok(hr == S_OK, "value 0x%08lx: expected S_OK, got 0x%08lx\n", Values[i], hr);
        ok(bag.cWrites == 1, "value 0x%08lx: expected one write, got %ld\n",
           Values[i], bag.cWrites);
        ok(bag.vtLast == VT_UI4, "value 0x%08lx: expected VT_UI4, got %u\n",
           Values[i], bag.vtLast);
        ok(bag.ulLast == Values[i], "expected 0x%08lx, got 0x%08lx\n", Values[i], bag.ulLast);
        ok(bag.cRef == 1, "the helper must not keep a reference, cRef %ld\n", bag.cRef);
        ok(bag.cReads == 0, "the helper must not read, %ld reads\n", bag.cReads);
    }
}

static void
Test_Names(void)
{
    static const WCHAR szLong[] =
        L"0123456789012345678901234567890123456789012345678901234567890123"
        L"0123456789012345678901234567890123456789012345678901234567890123";
    static const WCHAR *Names[] = { L"", L"a", L"With Space", L"Uni\x00e9\x4e2d", NULL };
    TESTBAG bag;
    HRESULT hr;
    UINT i;

    for (i = 0; i < ARRAYSIZE(Names); i++)
    {
        const WCHAR *pszName = Names[i] ? Names[i] : szLong;

        InitBag(&bag);
        hr = pPSPropertyBag_WriteDWORD(&bag.IPropertyBag_iface, pszName, 7);
        ok(hr == S_OK, "name %ls: expected S_OK, got 0x%08lx\n", pszName, hr);
        ok(wcscmp(bag.szLastName, pszName) == 0,
           "name mismatch: wrote %ls, expected %ls\n", bag.szLastName, pszName);
    }
}

static void
Test_FailurePropagation(void)
{
    static const HRESULT Errors[] =
    {
        E_FAIL, E_OUTOFMEMORY, E_NOTIMPL, E_ACCESSDENIED, S_FALSE,
        (HRESULT)0x80070005, (HRESULT)0x8007000E
    };
    TESTBAG bag;
    HRESULT hr;
    UINT i;

    for (i = 0; i < ARRAYSIZE(Errors); i++)
    {
        InitBag(&bag);
        bag.hrWrite = Errors[i];
        hr = pPSPropertyBag_WriteDWORD(&bag.IPropertyBag_iface, L"Value", 1);
        ok(hr == Errors[i], "expected the bag's 0x%08lx to propagate, got 0x%08lx\n",
           Errors[i], hr);
        ok(bag.cWrites == 1, "the bag must still be called once, got %ld\n", bag.cWrites);
    }
}

static void
Test_Churn(void)
{
    TESTBAG bag;
    UINT i;
    LONG Bad = 0;

    InitBag(&bag);
    for (i = 0; i < 50000; i++)
    {
        if (pPSPropertyBag_WriteDWORD(&bag.IPropertyBag_iface, L"Value", i) != S_OK)
        {
            Bad++;
            break;
        }
        if (bag.ulLast != i || bag.vtLast != VT_UI4)
        {
            Bad++;
            break;
        }
    }
    ok(Bad == 0, "50000 writes stayed consistent (stopped at %u)\n", i);
    ok(bag.cWrites == (LONG)i, "expected %u writes, got %ld\n", i, bag.cWrites);
    ok(bag.cRef == 1, "no reference may leak, cRef %ld\n", bag.cRef);
}

static DWORD WINAPI
HammerThread(LPVOID pv)
{
    LONG *pFailures = (LONG *)pv;
    TESTBAG bag;
    UINT i;

    InitBag(&bag);
    for (i = 0; i < 20000; i++)
    {
        if (pPSPropertyBag_WriteDWORD(&bag.IPropertyBag_iface, L"Threaded", i) != S_OK)
        {
            InterlockedIncrement(pFailures);
            break;
        }
    }
    if (bag.cRef != 1)
        InterlockedIncrement(pFailures);
    return 0;
}

static void
Test_Threaded(void)
{
    HANDLE hThreads[4];
    LONG Failures = 0;
    UINT i;

    for (i = 0; i < ARRAYSIZE(hThreads); i++)
        hThreads[i] = CreateThread(NULL, 0, HammerThread, &Failures, 0, NULL);

    WaitForMultipleObjects(ARRAYSIZE(hThreads), hThreads, TRUE, 60000);
    for (i = 0; i < ARRAYSIZE(hThreads); i++)
    {
        if (hThreads[i])
            CloseHandle(hThreads[i]);
    }
    ok(Failures == 0, "%ld threaded iterations misbehaved\n", Failures);
}

static void
Test_WithoutCom(void)
{
    TESTBAG bag;
    HRESULT hr;

    InitBag(&bag);
    hr = pPSPropertyBag_WriteDWORD(&bag.IPropertyBag_iface, L"NoCom", 5);
    ok(hr == S_OK, "the helper must not need CoInitialize, got 0x%08lx\n", hr);
    ok(bag.vtLast == VT_UI4, "expected VT_UI4, got %u\n", bag.vtLast);
}

START_TEST(PSPropertyBag)
{
    HMODULE hPropsys;

    hPropsys = LoadLibraryW(L"propsys.dll");
    ok(hPropsys != NULL, "propsys.dll failed to load, error %lu\n", GetLastError());
    if (!hPropsys)
        return;

    pPSPropertyBag_WriteDWORD =
        (FN_PSPropertyBag_WriteDWORD)GetProcAddress(hPropsys, "PSPropertyBag_WriteDWORD");
    ok(pPSPropertyBag_WriteDWORD != NULL, "propsys!PSPropertyBag_WriteDWORD is missing\n");
    if (!pPSPropertyBag_WriteDWORD)
    {
        FreeLibrary(hPropsys);
        return;
    }

    Test_WithoutCom();

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    Test_BadArguments();
    Test_Values();
    Test_Names();
    Test_FailurePropagation();
    Test_Churn();
    Test_Threaded();
    CoUninitialize();

    FreeLibrary(hPropsys);
}
