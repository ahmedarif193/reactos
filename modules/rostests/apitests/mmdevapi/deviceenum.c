/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Concurrent MMDevice endpoint discovery parity tests
 */

#include <apitest.h>

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>

#define ENUM_THREADS     8
#define ENUM_ITERATIONS  32

typedef struct _ENUM_CONTEXT
{
    LONG Completed;
    LONG Failures;
} ENUM_CONTEXT;

static DWORD WINAPI EnumWorker(void *Parameter)
{
    ENUM_CONTEXT *Context = Parameter;
    IMMDeviceEnumerator *Enumerator;
    IMMDeviceCollection *Collection;
    HRESULT hr;
    UINT Count, Iteration;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        InterlockedIncrement(&Context->Failures);
        return 1;
    }

    for (Iteration = 0; Iteration < ENUM_ITERATIONS; ++Iteration)
    {
        Enumerator = NULL;
        hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                              &IID_IMMDeviceEnumerator, (void **)&Enumerator);
        if (FAILED(hr))
        {
            InterlockedIncrement(&Context->Failures);
            continue;
        }

        Collection = NULL;
        hr = IMMDeviceEnumerator_EnumAudioEndpoints(Enumerator, eAll,
                                                     DEVICE_STATEMASK_ALL, &Collection);
        if (FAILED(hr) || !Collection)
            InterlockedIncrement(&Context->Failures);
        else
        {
            Count = ~0u;
            hr = IMMDeviceCollection_GetCount(Collection, &Count);
            if (FAILED(hr) || Count == ~0u)
                InterlockedIncrement(&Context->Failures);
            IMMDeviceCollection_Release(Collection);
        }
        IMMDeviceEnumerator_Release(Enumerator);
    }

    CoUninitialize();
    InterlockedIncrement(&Context->Completed);
    return 0;
}

START_TEST(deviceenum)
{
    ENUM_CONTEXT Context = {0};
    HANDLE Threads[ENUM_THREADS] = {0};
    DWORD Wait;
    UINT Created = 0, i;

    for (i = 0; i < ARRAYSIZE(Threads); ++i)
    {
        Threads[i] = CreateThread(NULL, 0, EnumWorker, &Context, 0, NULL);
        ok(Threads[i] != NULL, "CreateThread %u failed: %lu\n", i, GetLastError());
        if (!Threads[i])
            break;
        ++Created;
    }

    if (Created)
    {
        Wait = WaitForMultipleObjects(Created, Threads, TRUE, 30000);
        ok_eq_ulong(Wait, (ULONG)WAIT_OBJECT_0);
    }
    for (i = 0; i < Created; ++i)
        CloseHandle(Threads[i]);

    ok_eq_long(Context.Completed, Created);
    ok_eq_long(Context.Failures, 0);
    trace("Completed %u concurrent endpoint enumerations\n",
          Created * ENUM_ITERATIONS);
}
