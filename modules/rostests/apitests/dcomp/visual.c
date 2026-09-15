#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <guiddef.h>
#include <objbase.h>

typedef HRESULT (WINAPI *dcomposition_create_device2_fn)(IUnknown *, REFIID, void **);
typedef HRESULT (WINAPI *create_visual_fn)(void *, void **);
typedef HRESULT (WINAPI *set_object_fn)(void *, void *);
typedef ULONG (WINAPI *release_fn)(void *);

static const GUID iid_dcomposition_device2 =
    {0x75f6468d, 0x1b8e, 0x447c, {0x9b, 0xc6, 0x75, 0xfe, 0xa8, 0x0b, 0x5b, 0x25}};

START_TEST(visual_reset)
{
    dcomposition_create_device2_fn create_device;
    HMODULE dcomp;
    void **device_vtbl, **visual_vtbl;
    void *device = NULL, *visual = NULL;
    HRESULT hr;

    dcomp = LoadLibraryW(L"dcomp.dll");
    ok(dcomp != NULL, "LoadLibraryW failed, error %lu\n", GetLastError());
    if (!dcomp)
        return;

    create_device = (dcomposition_create_device2_fn)GetProcAddress(dcomp,
            "DCompositionCreateDevice2");
    ok(create_device != NULL, "DCompositionCreateDevice2 is unavailable\n");
    if (!create_device)
    {
        FreeLibrary(dcomp);
        return;
    }

    hr = create_device(NULL, &iid_dcomposition_device2, &device);
    ok(hr == S_OK, "DCompositionCreateDevice2 returned %#lx\n", hr);
    ok(device != NULL, "DCompositionCreateDevice2 returned a NULL device\n");
    if (!device)
    {
        FreeLibrary(dcomp);
        return;
    }

    device_vtbl = *(void ***)device;
    hr = ((create_visual_fn)device_vtbl[6])(device, &visual);
    ok(hr == S_OK, "CreateVisual returned %#lx\n", hr);
    ok(visual != NULL, "CreateVisual returned a NULL visual\n");
    if (visual)
    {
        visual_vtbl = *(void ***)visual;
        hr = ((set_object_fn)visual_vtbl[7])(visual, NULL);
        ok(hr == S_OK, "SetTransform(NULL) returned %#lx\n", hr);
        hr = ((set_object_fn)visual_vtbl[13])(visual, NULL);
        ok(hr == S_OK, "SetClip(NULL) returned %#lx\n", hr);
        ((release_fn)visual_vtbl[2])(visual);
    }

    ((release_fn)device_vtbl[2])(device);
    FreeLibrary(dcomp);
}
