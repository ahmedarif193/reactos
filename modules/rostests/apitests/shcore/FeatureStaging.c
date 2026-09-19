/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Feature-staging contract loading and default feature behavior
 */
#include <apitest.h>
#include <featurestagingapi.h>

static LONG callbacks;

static void WINAPI feature_changed(void *context)
{
    InterlockedIncrement(&callbacks);
}

START_TEST(FeatureStaging)
{
    static const WCHAR *names[] =
    {
        L"shcore.dll",
        L"api-ms-win-core-featurestaging-l1-1-0.dll",
        L"api-ms-win-core-featurestaging-l1-1-1.dll"
    };
    unsigned int i, change;

    for (i = 0; i < ARRAYSIZE(names); ++i)
    {
        HMODULE module = LoadLibraryW(names[i]);
        FEATURE_ENABLED_STATE (WINAPI *get_state)(UINT32, FEATURE_CHANGE_TIME);
        UINT32 (WINAPI *get_variant)(UINT32, FEATURE_CHANGE_TIME, UINT32*, BOOL*);
        void (WINAPI *subscribe)(FEATURE_STATE_CHANGE_SUBSCRIPTION*, PFEATURE_STATE_CHANGE_CALLBACK, void*);
        void (WINAPI *unsubscribe)(FEATURE_STATE_CHANGE_SUBSCRIPTION);
        void (WINAPI *record_error)(UINT32, const FEATURE_ERROR*);
        void (WINAPI *record_usage)(UINT32, UINT32, UINT32, PCSTR);
        FEATURE_STATE_CHANGE_SUBSCRIPTION subscription = (void *)(ULONG_PTR)0xdeadbeef;
        FEATURE_ERROR error = {0};

        winetest_push_context("%ls", names[i]);
        ok(module != NULL, "LoadLibrary failed: %lu\n", GetLastError());
        if (!module)
        {
            winetest_pop_context();
            continue;
        }
#define RESOLVE(variable, name) do { \
        variable = (void *)GetProcAddress(module, name); \
        ok(variable != NULL, "Missing %s\n", name); \
    } while (0)
        RESOLVE(get_state, "GetFeatureEnabledState");
        RESOLVE(subscribe, "SubscribeFeatureStateChangeNotification");
        RESOLVE(unsubscribe, "UnsubscribeFeatureStateChangeNotification");
        RESOLVE(record_error, "RecordFeatureError");
        RESOLVE(record_usage, "RecordFeatureUsage");
        /* Variant was added by the l1-1-1 contract. */
        get_variant = (void *)GetProcAddress(module, "GetFeatureVariant");
        if (i != 1) ok(get_variant != NULL, "Missing GetFeatureVariant\n");
#undef RESOLVE
        for (change = FEATURE_CHANGE_TIME_READ; change <= FEATURE_CHANGE_TIME_REBOOT; ++change)
        {
            /* No rollout configuration exists for this reserved feature ID. */
            if (get_state)
                ok(get_state(0xffffffff, change) == FEATURE_ENABLED_STATE_DEFAULT,
                   "Unknown feature must use the caller's default\n");
            if (get_variant)
            {
                UINT32 payload = 0xdeadbeef, variant;
                BOOL notification = 0x55;
                variant = get_variant(0xffffffff, change, &payload, &notification);
                ok(variant == 0, "variant %u\n", variant);
                ok(payload == 0, "payload %u\n", payload);
                ok(notification == FALSE, "notification %d\n", notification);
            }
        }
        if (subscribe && unsubscribe)
        {
            subscribe(&subscription, feature_changed, NULL);
            ok(subscription != (void *)(ULONG_PTR)0xdeadbeef, "Subscription output untouched\n");
            unsubscribe(subscription);
        }
        error.hr = E_FAIL;
        error.file = "FeatureStaging.c";
        error.message = "API contract test";
        if (record_error) record_error(0xffffffff, &error);
        if (record_usage) record_usage(0xffffffff, 0, 1, "apitest");
        ok(callbacks == 0, "Unexpected feature-change notification\n");
        FreeLibrary(module);
        winetest_pop_context();
    }
}
