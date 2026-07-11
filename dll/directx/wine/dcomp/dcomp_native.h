/*
 * IDCompositionDevice interface surface, matching the public dcomp.h ABI.
 *
 * Interface out-params and effect/transform objects are declared as void*
 * (they are only ever forwarded or nulled by the current stubs); enums are
 * passed as UINT.  This keeps every vtable slot at the correct offset and
 * argument width while the real DirectComposition backing (a visual tree fed
 * to the WDDM compositor) is brought up.
 */

#ifndef DCOMP_NATIVE_H
#define DCOMP_NATIVE_H

#include <windows.h>
#include <objbase.h>

typedef struct IDCompositionDevice IDCompositionDevice;

typedef struct IDCompositionDeviceVtbl {
    BEGIN_INTERFACE
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDCompositionDevice *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDCompositionDevice *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IDCompositionDevice *This);
    HRESULT (STDMETHODCALLTYPE *Commit)(IDCompositionDevice *This);
    HRESULT (STDMETHODCALLTYPE *WaitForCommitCompletion)(IDCompositionDevice *This);
    HRESULT (STDMETHODCALLTYPE *GetFrameStatistics)(IDCompositionDevice *This, void *stats);
    HRESULT (STDMETHODCALLTYPE *CreateTargetForHwnd)(IDCompositionDevice *This, HWND hwnd, BOOL topmost, void **target);
    HRESULT (STDMETHODCALLTYPE *CreateVisual)(IDCompositionDevice *This, void **visual);
    HRESULT (STDMETHODCALLTYPE *CreateSurface)(IDCompositionDevice *This, UINT w, UINT h, UINT fmt, UINT alpha, void **surface);
    HRESULT (STDMETHODCALLTYPE *CreateVirtualSurface)(IDCompositionDevice *This, UINT w, UINT h, UINT fmt, UINT alpha, void **surface);
    HRESULT (STDMETHODCALLTYPE *CreateSurfaceFromHandle)(IDCompositionDevice *This, HANDLE h, IUnknown **surface);
    HRESULT (STDMETHODCALLTYPE *CreateSurfaceFromHwnd)(IDCompositionDevice *This, HWND hwnd, IUnknown **surface);
    HRESULT (STDMETHODCALLTYPE *CreateTranslateTransform)(IDCompositionDevice *This, void **transform);
    HRESULT (STDMETHODCALLTYPE *CreateScaleTransform)(IDCompositionDevice *This, void **transform);
    HRESULT (STDMETHODCALLTYPE *CreateRotateTransform)(IDCompositionDevice *This, void **transform);
    HRESULT (STDMETHODCALLTYPE *CreateSkewTransform)(IDCompositionDevice *This, void **transform);
    HRESULT (STDMETHODCALLTYPE *CreateMatrixTransform)(IDCompositionDevice *This, void **transform);
    HRESULT (STDMETHODCALLTYPE *CreateTransformGroup)(IDCompositionDevice *This, void **transforms, UINT count, void **group);
    HRESULT (STDMETHODCALLTYPE *CreateTranslateTransform3D)(IDCompositionDevice *This, void **transform);
    HRESULT (STDMETHODCALLTYPE *CreateScaleTransform3D)(IDCompositionDevice *This, void **transform);
    HRESULT (STDMETHODCALLTYPE *CreateRotateTransform3D)(IDCompositionDevice *This, void **transform);
    HRESULT (STDMETHODCALLTYPE *CreateMatrixTransform3D)(IDCompositionDevice *This, void **transform);
    HRESULT (STDMETHODCALLTYPE *CreateTransform3DGroup)(IDCompositionDevice *This, void **transforms, UINT count, void **group);
    HRESULT (STDMETHODCALLTYPE *CreateEffectGroup)(IDCompositionDevice *This, void **group);
    HRESULT (STDMETHODCALLTYPE *CreateRectangleClip)(IDCompositionDevice *This, void **clip);
    HRESULT (STDMETHODCALLTYPE *CreateAnimation)(IDCompositionDevice *This, void **animation);
    HRESULT (STDMETHODCALLTYPE *CheckDeviceState)(IDCompositionDevice *This, BOOL *valid);
    END_INTERFACE
} IDCompositionDeviceVtbl;

struct IDCompositionDevice { const IDCompositionDeviceVtbl *lpVtbl; };

#endif /* DCOMP_NATIVE_H */
