#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <directmanipulation.h>
#include <wine/test.h>

START_TEST(manipulation)
{
    IDirectManipulationManager *manager;
    IDirectManipulationViewport *viewport;
    IDirectManipulationContent *content;
    IDirectManipulationUpdateManager *update;
    HWND window;
    RECT rect = {0,0,640,480}, result;
    float matrix[6];
    HRESULT hr;
    CoInitialize(NULL);
    window = CreateWindowW(L"STATIC", L"manual viewport", WS_POPUP, 0, 0, 640, 480, NULL, NULL, NULL, NULL);
    hr = CoCreateInstance(&CLSID_DirectManipulationManager, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IDirectManipulationManager, (void **)&manager);
    ok(hr == S_OK, "manager activation: %lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IDirectManipulationManager_Activate(manager, window);
    ok(hr == S_OK, "activate window: %lx\n", hr);
    hr = IDirectManipulationManager_GetUpdateManager(manager, &IID_IDirectManipulationUpdateManager, (void **)&update);
    ok(hr == S_OK, "update manager: %lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IDirectManipulationUpdateManager_Update(update, NULL);
        ok(hr == S_OK, "update: %lx\n", hr);
        IDirectManipulationUpdateManager_Release(update);
    }
    hr = IDirectManipulationManager_CreateViewport(manager, NULL, window, &IID_IDirectManipulationViewport, (void **)&viewport);
    ok(hr == S_OK, "viewport: %lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IDirectManipulationViewport_SetViewportRect(viewport, &rect);
        ok(hr == S_OK, "set rect: %lx\n", hr);
        hr = IDirectManipulationViewport_GetViewportRect(viewport, &result);
        ok(hr == S_OK && EqualRect(&rect, &result), "rect round trip: %lx\n", hr);
        hr = IDirectManipulationViewport_GetPrimaryContent(viewport, &IID_IDirectManipulationContent, (void **)&content);
        ok(hr == S_OK, "primary content: %lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = IDirectManipulationViewport_ZoomToRect(viewport, 0, 0, 320, 240, FALSE);
            ok(hr == S_OK, "zoom: %lx\n", hr);
            hr = IDirectManipulationContent_GetContentTransform(content, matrix, 6);
            ok(hr == S_OK && matrix[0] == 2 && matrix[3] == 2, "transform: %lx/%f/%f\n", hr, matrix[0], matrix[3]);
            IDirectManipulationContent_Release(content);
        }
        hr = IDirectManipulationViewport_Abandon(viewport);
        ok(hr == S_OK, "abandon: %lx\n", hr);
        IDirectManipulationViewport_Release(viewport);
    }
    IDirectManipulationManager_Deactivate(manager, window);
    IDirectManipulationManager_Release(manager);
done:
    DestroyWindow(window);
    CoUninitialize();
}
