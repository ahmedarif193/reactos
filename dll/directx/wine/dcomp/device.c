/*
 * Copyright 2020 Nikolay Sivov for CodeWeavers
 * Copyright 2026 Ahmed Arif for ReactOS
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "objidl.h"
#include "dxgi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

#ifdef __REACTOS__

#include "dcomp_private.h"
#include "reactos/dxgi_dcomp.h"
#include "wine/list.h"

static const GUID IID_IDCompositionDevice_local =
    {0xc37ea93a, 0xe7aa, 0x450d, {0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3}};
static const GUID IID_IDCompositionDevice2_local =
    {0x75f6468d, 0x1b8e, 0x447c, {0x9b, 0xc6, 0x75, 0xfe, 0xa8, 0x0b, 0x5b, 0x25}};
static const GUID IID_IDCompositionDevice3_local =
    {0x0987cb06, 0xf916, 0x48bf, {0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1b, 0xd9}};
static const GUID IID_IDCompositionDesktopDevice_local =
    {0x5f4633fe, 0x1e08, 0x4cb8, {0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02}};
static const GUID IID_IDCompositionTarget_local =
    {0xeacdd04c, 0x117e, 0x4e17, {0x88, 0xf4, 0xd1, 0xb1, 0x2b, 0x0e, 0x3d, 0x89}};
static const GUID IID_IDCompositionVisual_local =
    {0x4d93059d, 0x097b, 0x4651, {0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3}};
static const GUID IID_IDCompositionVisual2_local =
    {0xe8de1639, 0x4331, 0x4b26, {0xbc, 0x5f, 0x6a, 0x32, 0x1d, 0x34, 0x7a, 0x85}};
static const GUID IID_IDCompositionVisualDebug_local =
    {0xfed2b808, 0x5eb4, 0x43a0, {0xae, 0xa3, 0x35, 0xf6, 0x52, 0x80, 0xf9, 0x1b}};
static const GUID IID_IDCompositionVisual3_local =
    {0x2775f462, 0xb6c1, 0x4015, {0xb0, 0xbe, 0xb3, 0xe7, 0xd6, 0xa4, 0x97, 0x6d}};

struct dcomp_device
{
    IDCompositionDevice IDCompositionDevice_iface;
    IDCompositionDevice3 IDCompositionDevice3_iface;
    IDCompositionDesktopDevice IDCompositionDesktopDevice_iface;
    LONG refcount;
    IUnknown *rendering_device;
    struct list targets;
};

struct dcomp_visual
{
    IDCompositionVisual IDCompositionVisual_iface;
    LONG refcount;
    IUnknown *content;
    struct dcomp_visual *parent;
    struct list entry;
    struct list children;
    float offset_x;
    float offset_y;
    float transform_x;
    float transform_y;
    struct
    {
        float left;
        float top;
        float right;
        float bottom;
    } clip;
    BOOL has_clip;
    UINT interpolation_mode;
    UINT border_mode;
    UINT composite_mode;
    UINT opacity_mode;
    UINT backface_visibility;
    UINT depth_mode;
    float opacity;
    BOOL visible;
};

struct dcomp_target
{
    IDCompositionTarget IDCompositionTarget_iface;
    LONG refcount;
    struct dcomp_device *device;
    struct list entry;
    IDCompositionVisual *root;
    HWND window;
    BOOL topmost;
};

static const IDCompositionDeviceVtbl dcomp_device_vtbl;
static const IDCompositionDevice3Vtbl dcomp_device3_vtbl;
static const IDCompositionDesktopDeviceVtbl dcomp_desktop_device_vtbl;
static const IDCompositionTargetVtbl dcomp_target_vtbl;
static const IDCompositionVisualVtbl dcomp_visual_vtbl;

static inline struct dcomp_device *impl_from_IDCompositionDevice(IDCompositionDevice *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_device, IDCompositionDevice_iface);
}

static inline struct dcomp_device *impl_from_IDCompositionDevice3(IDCompositionDevice3 *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_device, IDCompositionDevice3_iface);
}

static inline struct dcomp_device *impl_from_IDCompositionDesktopDevice(IDCompositionDesktopDevice *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_device, IDCompositionDesktopDevice_iface);
}

static inline struct dcomp_target *impl_from_IDCompositionTarget(IDCompositionTarget *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_target, IDCompositionTarget_iface);
}

static inline struct dcomp_visual *impl_from_IDCompositionVisual(IDCompositionVisual *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_visual, IDCompositionVisual_iface);
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_QueryInterface(IDCompositionVisual *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (!out)
        return E_POINTER;
    *out = NULL;

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionVisual_local)
            || IsEqualGUID(iid, &IID_IDCompositionVisual2_local)
            || IsEqualGUID(iid, &IID_IDCompositionVisualDebug_local)
            || IsEqualGUID(iid, &IID_IDCompositionVisual3_local))
    {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dcomp_visual_AddRef(IDCompositionVisual *iface)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);
    return InterlockedIncrement(&visual->refcount);
}

static ULONG STDMETHODCALLTYPE dcomp_visual_Release(IDCompositionVisual *iface)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);
    ULONG refcount = InterlockedDecrement(&visual->refcount);

    if (!refcount)
    {
        while (!list_empty(&visual->children))
        {
            struct dcomp_visual *child = LIST_ENTRY(visual->children.next,
                    struct dcomp_visual, entry);

            list_remove(&child->entry);
            child->parent = NULL;
            child->IDCompositionVisual_iface.lpVtbl->Release(
                    &child->IDCompositionVisual_iface);
        }
        if (visual->content)
            IUnknown_Release(visual->content);
        free(visual);
    }
    return refcount;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetOffsetXAnimation(IDCompositionVisual *iface,
        IDCompositionAnimation *animation)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetOffsetX(IDCompositionVisual *iface, float offset)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    visual->offset_x = offset;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetOffsetYAnimation(IDCompositionVisual *iface,
        IDCompositionAnimation *animation)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetOffsetY(IDCompositionVisual *iface, float offset)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    visual->offset_y = offset;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetTransformObject(IDCompositionVisual *iface,
        IDCompositionTransform *transform)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetTransform(IDCompositionVisual *iface,
        const void *matrix)
{
    const float *values = matrix;
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    if (!matrix)
    {
        visual->transform_x = 0.0f;
        visual->transform_y = 0.0f;
        return S_OK;
    }

    /* The fallback compositor applies the translation component. Scaling,
     * rotation, and skew require a raster transform and are rejected. */
    if (values[0] != 1.0f || values[1] != 0.0f || values[2] != 0.0f
            || values[3] != 1.0f)
        return E_NOTIMPL;
    visual->transform_x = values[4];
    visual->transform_y = values[5];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetTransformParent(IDCompositionVisual *iface,
        IDCompositionVisual *visual)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetEffect(IDCompositionVisual *iface,
        IDCompositionEffect *effect)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetBitmapInterpolationMode(IDCompositionVisual *iface,
        UINT mode)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    visual->interpolation_mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetBorderMode(IDCompositionVisual *iface, UINT mode)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    visual->border_mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetClipObject(IDCompositionVisual *iface,
        IDCompositionClip *clip)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetClip(IDCompositionVisual *iface, const void *rect)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    if (!rect)
    {
        visual->has_clip = FALSE;
        return S_OK;
    }

    memcpy(&visual->clip, rect, sizeof(visual->clip));
    if (visual->clip.right < visual->clip.left
            || visual->clip.bottom < visual->clip.top)
        return E_INVALIDARG;
    visual->has_clip = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetContent(IDCompositionVisual *iface, IUnknown *content)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    TRACE("iface %p, content %p.\n", iface, content);

    if (content)
        IUnknown_AddRef(content);
    if (visual->content)
        IUnknown_Release(visual->content);
    visual->content = content;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_AddVisual(IDCompositionVisual *iface,
        IDCompositionVisual *visual, BOOL insert_above, IDCompositionVisual *reference_visual)
{
    struct dcomp_visual *parent = impl_from_IDCompositionVisual(iface);
    struct dcomp_visual *child, *reference = NULL;

    if (!visual || visual->lpVtbl != &dcomp_visual_vtbl || visual == iface)
        return E_INVALIDARG;
    child = impl_from_IDCompositionVisual(visual);
    if (child->parent)
        return E_INVALIDARG;

    if (reference_visual)
    {
        if (reference_visual->lpVtbl != &dcomp_visual_vtbl)
            return E_INVALIDARG;
        reference = impl_from_IDCompositionVisual(reference_visual);
        if (reference->parent != parent)
            return E_INVALIDARG;
    }

    visual->lpVtbl->AddRef(visual);
    child->parent = parent;
    if (reference)
    {
        if (insert_above)
            list_add_after(&reference->entry, &child->entry);
        else
            list_add_before(&reference->entry, &child->entry);
    }
    else if (insert_above)
    {
        list_add_tail(&parent->children, &child->entry);
    }
    else
    {
        list_add_head(&parent->children, &child->entry);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_RemoveVisual(IDCompositionVisual *iface,
        IDCompositionVisual *visual)
{
    struct dcomp_visual *parent = impl_from_IDCompositionVisual(iface);
    struct dcomp_visual *child;

    if (!visual || visual->lpVtbl != &dcomp_visual_vtbl)
        return E_INVALIDARG;
    child = impl_from_IDCompositionVisual(visual);
    if (child->parent != parent)
        return E_INVALIDARG;

    list_remove(&child->entry);
    child->parent = NULL;
    visual->lpVtbl->Release(visual);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_RemoveAllVisuals(IDCompositionVisual *iface)
{
    struct dcomp_visual *parent = impl_from_IDCompositionVisual(iface);

    while (!list_empty(&parent->children))
    {
        struct dcomp_visual *child = LIST_ENTRY(parent->children.next,
                struct dcomp_visual, entry);

        list_remove(&child->entry);
        child->parent = NULL;
        child->IDCompositionVisual_iface.lpVtbl->Release(
                &child->IDCompositionVisual_iface);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetCompositeMode(IDCompositionVisual *iface, UINT mode)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    visual->composite_mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetOpacityMode(IDCompositionVisual *iface, UINT mode)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    visual->opacity_mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetBackFaceVisibility(IDCompositionVisual *iface,
        UINT visibility)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    visual->backface_visibility = visibility;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_EnableHeatMap(IDCompositionVisual *iface,
        const void *color)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_DisableHeatMap(IDCompositionVisual *iface)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_EnableRedrawRegions(IDCompositionVisual *iface)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_DisableRedrawRegions(IDCompositionVisual *iface)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetDepthMode(IDCompositionVisual *iface, UINT mode)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    visual->depth_mode = mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetOffsetZ(IDCompositionVisual *iface, float offset)
{
    return offset == 0.0f ? S_OK : E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetOffsetZAnimation(IDCompositionVisual *iface,
        IDCompositionAnimation *animation)
{
    return animation ? E_NOTIMPL : S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetOpacity(IDCompositionVisual *iface, float opacity)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    if (opacity < 0.0f || opacity > 1.0f)
        return E_INVALIDARG;
    visual->opacity = opacity;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetOpacityAnimation(IDCompositionVisual *iface,
        IDCompositionAnimation *animation)
{
    return animation ? E_NOTIMPL : S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetTransform4x4(IDCompositionVisual *iface,
        const void *matrix)
{
    const float *values = matrix;
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);
    unsigned int i;

    if (!matrix)
    {
        visual->transform_x = 0.0f;
        visual->transform_y = 0.0f;
        return S_OK;
    }

    for (i = 0; i < 16; ++i)
    {
        float expected = i == 0 || i == 5 || i == 10 || i == 15 ? 1.0f : 0.0f;

        if (i != 12 && i != 13 && values[i] != expected)
            return E_NOTIMPL;
    }
    visual->transform_x = values[12];
    visual->transform_y = values[13];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetTransform3DObject(IDCompositionVisual *iface,
        IDCompositionTransform3D *transform)
{
    return transform ? E_NOTIMPL : S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_visual_SetVisible(IDCompositionVisual *iface, BOOL visible)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);

    visual->visible = !!visible;
    return S_OK;
}

static const IDCompositionVisualVtbl dcomp_visual_vtbl =
{
    dcomp_visual_QueryInterface,
    dcomp_visual_AddRef,
    dcomp_visual_Release,
    dcomp_visual_SetOffsetXAnimation,
    dcomp_visual_SetOffsetX,
    dcomp_visual_SetOffsetYAnimation,
    dcomp_visual_SetOffsetY,
    dcomp_visual_SetTransformObject,
    dcomp_visual_SetTransform,
    dcomp_visual_SetTransformParent,
    dcomp_visual_SetEffect,
    dcomp_visual_SetBitmapInterpolationMode,
    dcomp_visual_SetBorderMode,
    dcomp_visual_SetClipObject,
    dcomp_visual_SetClip,
    dcomp_visual_SetContent,
    dcomp_visual_AddVisual,
    dcomp_visual_RemoveVisual,
    dcomp_visual_RemoveAllVisuals,
    dcomp_visual_SetCompositeMode,
    dcomp_visual_SetOpacityMode,
    dcomp_visual_SetBackFaceVisibility,
    dcomp_visual_EnableHeatMap,
    dcomp_visual_DisableHeatMap,
    dcomp_visual_EnableRedrawRegions,
    dcomp_visual_DisableRedrawRegions,
    dcomp_visual_SetDepthMode,
    dcomp_visual_SetOffsetZ,
    dcomp_visual_SetOffsetZAnimation,
    dcomp_visual_SetOpacity,
    dcomp_visual_SetOpacityAnimation,
    dcomp_visual_SetTransform4x4,
    dcomp_visual_SetTransform3DObject,
    dcomp_visual_SetVisible,
};

static HRESULT STDMETHODCALLTYPE dcomp_target_QueryInterface(IDCompositionTarget *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (!out)
        return E_POINTER;
    *out = NULL;

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionTarget_local))
    {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dcomp_target_AddRef(IDCompositionTarget *iface)
{
    struct dcomp_target *target = impl_from_IDCompositionTarget(iface);
    return InterlockedIncrement(&target->refcount);
}

static ULONG STDMETHODCALLTYPE dcomp_device_Release(IDCompositionDevice *iface);

static ULONG STDMETHODCALLTYPE dcomp_target_Release(IDCompositionTarget *iface)
{
    struct dcomp_target *target = impl_from_IDCompositionTarget(iface);
    ULONG refcount = InterlockedDecrement(&target->refcount);

    if (!refcount)
    {
        list_remove(&target->entry);
        if (target->root)
            target->root->lpVtbl->Release(target->root);
        dcomp_device_Release(&target->device->IDCompositionDevice_iface);
        free(target);
    }
    return refcount;
}

static HRESULT STDMETHODCALLTYPE dcomp_target_SetRoot(IDCompositionTarget *iface,
        IDCompositionVisual *root)
{
    struct dcomp_target *target = impl_from_IDCompositionTarget(iface);

    TRACE("target %p window %p root %p.\n", iface, target->window, root);

    if (root)
        root->lpVtbl->AddRef(root);
    if (target->root)
        target->root->lpVtbl->Release(target->root);
    target->root = root;
    return S_OK;
}

static const IDCompositionTargetVtbl dcomp_target_vtbl =
{
    dcomp_target_QueryInterface,
    dcomp_target_AddRef,
    dcomp_target_Release,
    dcomp_target_SetRoot,
};

static ULONG dcomp_device_addref(struct dcomp_device *device)
{
    return InterlockedIncrement(&device->refcount);
}

static ULONG dcomp_device_release(struct dcomp_device *device)
{
    ULONG refcount = InterlockedDecrement(&device->refcount);

    if (!refcount)
    {
        if (!list_empty(&device->targets))
            ERR("Destroying device %p with live composition targets.\n", device);
        if (device->rendering_device)
            IUnknown_Release(device->rendering_device);
        free(device);
    }
    return refcount;
}

static HRESULT dcomp_device_query_interface(struct dcomp_device *device, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionDevice2_local)
            || IsEqualGUID(iid, &IID_IDCompositionDevice3_local))
        *out = &device->IDCompositionDevice3_iface;
    else if (IsEqualGUID(iid, &IID_IDCompositionDesktopDevice_local))
        *out = &device->IDCompositionDesktopDevice_iface;
    else if (IsEqualGUID(iid, &IID_IDCompositionDevice_local))
        *out = &device->IDCompositionDevice_iface;
    else
        return E_NOINTERFACE;

    dcomp_device_addref(device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_device_QueryInterface(IDCompositionDevice *iface,
        REFIID iid, void **out)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return dcomp_device_query_interface(device, iid, out);
}

static ULONG STDMETHODCALLTYPE dcomp_device_AddRef(IDCompositionDevice *iface)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice(iface);
    return dcomp_device_addref(device);
}

static ULONG STDMETHODCALLTYPE dcomp_device_Release(IDCompositionDevice *iface)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice(iface);
    return dcomp_device_release(device);
}

static LONG dcomp_round_offset(float value)
{
    return value < 0.0f ? (LONG)(value - 0.5f) : (LONG)(value + 0.5f);
}

static HRESULT dcomp_commit_visual(struct dcomp_visual *visual, HWND window,
        LONG parent_x, LONG parent_y, const RECT *parent_clip)
{
    struct reactos_dxgi_composition_target composition_target;
    struct dcomp_visual *child;
    RECT local_clip;
    const RECT *clip = parent_clip;
    IDXGIObject *dxgi_object;
    HRESULT hr;

    if (!visual->visible)
        return S_OK;

    composition_target.window = window;
    composition_target.offset_x = parent_x + dcomp_round_offset(
            visual->offset_x + visual->transform_x);
    composition_target.offset_y = parent_y + dcomp_round_offset(
            visual->offset_y + visual->transform_y);

    if (visual->has_clip)
    {
        local_clip.left = composition_target.offset_x + dcomp_round_offset(visual->clip.left);
        local_clip.top = composition_target.offset_y + dcomp_round_offset(visual->clip.top);
        local_clip.right = composition_target.offset_x + dcomp_round_offset(visual->clip.right);
        local_clip.bottom = composition_target.offset_y + dcomp_round_offset(visual->clip.bottom);
        if (parent_clip)
        {
            if (!IntersectRect(&local_clip, &local_clip, parent_clip))
                SetRectEmpty(&local_clip);
        }
        clip = &local_clip;
    }

    composition_target.has_clip = !!clip;
    if (clip)
        composition_target.clip = *clip;
    else
        SetRectEmpty(&composition_target.clip);

    if (visual->content)
    {
        hr = IUnknown_QueryInterface(visual->content, &IID_IDXGIObject,
                (void **)&dxgi_object);
        if (FAILED(hr))
            return hr;

        hr = IDXGIObject_SetPrivateData(dxgi_object,
                &GUID_ReactOSDXGICompositionWindow, sizeof(composition_target),
                &composition_target);
        IDXGIObject_Release(dxgi_object);
        if (FAILED(hr))
            return hr;
    }

    LIST_FOR_EACH_ENTRY(child, &visual->children, struct dcomp_visual, entry)
    {
        if (FAILED(hr = dcomp_commit_visual(child, window,
                composition_target.offset_x, composition_target.offset_y, clip)))
            return hr;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_device_Commit(IDCompositionDevice *iface)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice(iface);
    struct dcomp_target *target;

    TRACE("iface %p.\n", iface);

    LIST_FOR_EACH_ENTRY(target, &device->targets, struct dcomp_target, entry)
    {
        HRESULT hr;

        if (!target->root || target->root->lpVtbl != &dcomp_visual_vtbl)
            continue;
        if (FAILED(hr = dcomp_commit_visual(impl_from_IDCompositionVisual(target->root),
                target->window, 0, 0, NULL)))
            return hr;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_device_WaitForCommitCompletion(IDCompositionDevice *iface)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_device_GetFrameStatistics(IDCompositionDevice *iface,
        void *statistics)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_device_CreateTargetForHwnd(IDCompositionDevice *iface,
        HWND window, BOOL topmost, IDCompositionTarget **out)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice(iface);
    struct dcomp_target *target;

    TRACE("iface %p, window %p, topmost %d, out %p.\n", iface, window, topmost, out);

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!IsWindow(window))
        return E_INVALIDARG;
    if (!(target = calloc(1, sizeof(*target))))
        return E_OUTOFMEMORY;

    target->IDCompositionTarget_iface.lpVtbl = &dcomp_target_vtbl;
    target->refcount = 1;
    target->device = device;
    target->window = window;
    target->topmost = topmost;
    dcomp_device_AddRef(iface);
    list_add_tail(&device->targets, &target->entry);

    *out = &target->IDCompositionTarget_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dcomp_device_CreateVisual(IDCompositionDevice *iface,
        IDCompositionVisual **out)
{
    struct dcomp_visual *visual;

    TRACE("iface %p, out %p.\n", iface, out);

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!(visual = calloc(1, sizeof(*visual))))
        return E_OUTOFMEMORY;

    visual->IDCompositionVisual_iface.lpVtbl = &dcomp_visual_vtbl;
    visual->refcount = 1;
    visual->opacity = 1.0f;
    visual->visible = TRUE;
    list_init(&visual->entry);
    list_init(&visual->children);
    *out = &visual->IDCompositionVisual_iface;
    return S_OK;
}

#define DCOMP_DEVICE_NOTIMPL(name, type) \
    static HRESULT STDMETHODCALLTYPE name(IDCompositionDevice *iface, type **out) \
    { \
        if (out) *out = NULL; \
        return E_NOTIMPL; \
    }

static HRESULT STDMETHODCALLTYPE dcomp_device_CreateSurface(IDCompositionDevice *iface,
        UINT width, UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionSurface **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_device_CreateVirtualSurface(IDCompositionDevice *iface,
        UINT width, UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionVirtualSurface **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_device_CreateSurfaceFromHandle(IDCompositionDevice *iface,
        HANDLE handle, IUnknown **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_device_CreateSurfaceFromHwnd(IDCompositionDevice *iface,
        HWND window, IUnknown **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateTranslateTransform, IDCompositionTranslateTransform)
DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateScaleTransform, IDCompositionScaleTransform)
DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateRotateTransform, IDCompositionRotateTransform)
DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateSkewTransform, IDCompositionSkewTransform)
DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateMatrixTransform, IDCompositionMatrixTransform)

static HRESULT STDMETHODCALLTYPE dcomp_device_CreateTransformGroup(IDCompositionDevice *iface,
        IDCompositionTransform **transforms, UINT count, IDCompositionTransform **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateTranslateTransform3D, IDCompositionTranslateTransform3D)
DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateScaleTransform3D, IDCompositionScaleTransform3D)
DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateRotateTransform3D, IDCompositionRotateTransform3D)
DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateMatrixTransform3D, IDCompositionMatrixTransform3D)

static HRESULT STDMETHODCALLTYPE dcomp_device_CreateTransform3DGroup(IDCompositionDevice *iface,
        IDCompositionTransform3D **transforms, UINT count, IDCompositionTransform3D **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateEffectGroup, IDCompositionEffectGroup)
DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateRectangleClip, IDCompositionRectangleClip)
DCOMP_DEVICE_NOTIMPL(dcomp_device_CreateAnimation, IDCompositionAnimation)

static HRESULT STDMETHODCALLTYPE dcomp_device_CheckDeviceState(IDCompositionDevice *iface,
        BOOL *valid)
{
    if (!valid)
        return E_POINTER;
    *valid = TRUE;
    return S_OK;
}

static const IDCompositionDeviceVtbl dcomp_device_vtbl =
{
    dcomp_device_QueryInterface,
    dcomp_device_AddRef,
    dcomp_device_Release,
    dcomp_device_Commit,
    dcomp_device_WaitForCommitCompletion,
    dcomp_device_GetFrameStatistics,
    dcomp_device_CreateTargetForHwnd,
    dcomp_device_CreateVisual,
    dcomp_device_CreateSurface,
    dcomp_device_CreateVirtualSurface,
    dcomp_device_CreateSurfaceFromHandle,
    dcomp_device_CreateSurfaceFromHwnd,
    dcomp_device_CreateTranslateTransform,
    dcomp_device_CreateScaleTransform,
    dcomp_device_CreateRotateTransform,
    dcomp_device_CreateSkewTransform,
    dcomp_device_CreateMatrixTransform,
    dcomp_device_CreateTransformGroup,
    dcomp_device_CreateTranslateTransform3D,
    dcomp_device_CreateScaleTransform3D,
    dcomp_device_CreateRotateTransform3D,
    dcomp_device_CreateMatrixTransform3D,
    dcomp_device_CreateTransform3DGroup,
    dcomp_device_CreateEffectGroup,
    dcomp_device_CreateRectangleClip,
    dcomp_device_CreateAnimation,
    dcomp_device_CheckDeviceState,
};

#define DEFINE_DCOMP_MODERN_DEVICE_METHODS(prefix, iface_type, impl_func) \
    static HRESULT STDMETHODCALLTYPE prefix##_QueryInterface(iface_type *iface, \
            REFIID iid, void **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out); \
        return dcomp_device_query_interface(device, iid, out); \
    } \
    static ULONG STDMETHODCALLTYPE prefix##_AddRef(iface_type *iface) \
    { \
        return dcomp_device_addref(impl_func(iface)); \
    } \
    static ULONG STDMETHODCALLTYPE prefix##_Release(iface_type *iface) \
    { \
        return dcomp_device_release(impl_func(iface)); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_Commit(iface_type *iface) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_Commit(&device->IDCompositionDevice_iface); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_WaitForCommitCompletion(iface_type *iface) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_WaitForCommitCompletion(&device->IDCompositionDevice_iface); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetFrameStatistics(iface_type *iface, void *statistics) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_GetFrameStatistics(&device->IDCompositionDevice_iface, statistics); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateVisual(iface_type *iface, IDCompositionVisual **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateVisual(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateSurfaceFactory(iface_type *iface, \
            IUnknown *rendering_device, IUnknown **out) \
    { \
        if (out) *out = NULL; \
        return E_NOTIMPL; \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateSurface(iface_type *iface, UINT width, \
            UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, IDCompositionSurface **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateSurface(&device->IDCompositionDevice_iface, width, height, \
                format, alpha_mode, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateVirtualSurface(iface_type *iface, UINT width, \
            UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, \
            IDCompositionVirtualSurface **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateVirtualSurface(&device->IDCompositionDevice_iface, width, height, \
                format, alpha_mode, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateTranslateTransform(iface_type *iface, \
            IDCompositionTranslateTransform **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateTranslateTransform(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateScaleTransform(iface_type *iface, \
            IDCompositionScaleTransform **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateScaleTransform(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateRotateTransform(iface_type *iface, \
            IDCompositionRotateTransform **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateRotateTransform(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateSkewTransform(iface_type *iface, \
            IDCompositionSkewTransform **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateSkewTransform(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateMatrixTransform(iface_type *iface, \
            IDCompositionMatrixTransform **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateMatrixTransform(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateTransformGroup(iface_type *iface, \
            IDCompositionTransform **transforms, UINT count, IDCompositionTransform **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateTransformGroup(&device->IDCompositionDevice_iface, transforms, \
                count, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateTranslateTransform3D(iface_type *iface, \
            IDCompositionTranslateTransform3D **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateTranslateTransform3D(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateScaleTransform3D(iface_type *iface, \
            IDCompositionScaleTransform3D **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateScaleTransform3D(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateRotateTransform3D(iface_type *iface, \
            IDCompositionRotateTransform3D **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateRotateTransform3D(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateMatrixTransform3D(iface_type *iface, \
            IDCompositionMatrixTransform3D **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateMatrixTransform3D(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateTransform3DGroup(iface_type *iface, \
            IDCompositionTransform3D **transforms, UINT count, IDCompositionTransform3D **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateTransform3DGroup(&device->IDCompositionDevice_iface, transforms, \
                count, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateEffectGroup(iface_type *iface, \
            IDCompositionEffectGroup **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateEffectGroup(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateRectangleClip(iface_type *iface, \
            IDCompositionRectangleClip **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateRectangleClip(&device->IDCompositionDevice_iface, out); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CreateAnimation(iface_type *iface, \
            IDCompositionAnimation **out) \
    { \
        struct dcomp_device *device = impl_func(iface); \
        return dcomp_device_CreateAnimation(&device->IDCompositionDevice_iface, out); \
    }

DEFINE_DCOMP_MODERN_DEVICE_METHODS(dcomp_device3, IDCompositionDevice3,
        impl_from_IDCompositionDevice3)
DEFINE_DCOMP_MODERN_DEVICE_METHODS(dcomp_desktop_device, IDCompositionDesktopDevice,
        impl_from_IDCompositionDesktopDevice)

static HRESULT STDMETHODCALLTYPE dcomp_device3_CreateEffect(IDCompositionDevice3 *iface, void **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dcomp_desktop_device_CreateTargetForHwnd(
        IDCompositionDesktopDevice *iface, HWND window, BOOL topmost, IDCompositionTarget **out)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);

    TRACE("iface %p, window %p, topmost %d, out %p.\n", iface, window, topmost, out);
    return dcomp_device_CreateTargetForHwnd(&device->IDCompositionDevice_iface,
            window, topmost, out);
}

static HRESULT STDMETHODCALLTYPE dcomp_desktop_device_CreateSurfaceFromHandle(
        IDCompositionDesktopDevice *iface, HANDLE handle, IUnknown **out)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_CreateSurfaceFromHandle(&device->IDCompositionDevice_iface, handle, out);
}

static HRESULT STDMETHODCALLTYPE dcomp_desktop_device_CreateSurfaceFromHwnd(
        IDCompositionDesktopDevice *iface, HWND window, IUnknown **out)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_CreateSurfaceFromHwnd(&device->IDCompositionDevice_iface, window, out);
}

static const IDCompositionDevice3Vtbl dcomp_device3_vtbl =
{
    dcomp_device3_QueryInterface,
    dcomp_device3_AddRef,
    dcomp_device3_Release,
    dcomp_device3_Commit,
    dcomp_device3_WaitForCommitCompletion,
    dcomp_device3_GetFrameStatistics,
    dcomp_device3_CreateVisual,
    dcomp_device3_CreateSurfaceFactory,
    dcomp_device3_CreateSurface,
    dcomp_device3_CreateVirtualSurface,
    dcomp_device3_CreateTranslateTransform,
    dcomp_device3_CreateScaleTransform,
    dcomp_device3_CreateRotateTransform,
    dcomp_device3_CreateSkewTransform,
    dcomp_device3_CreateMatrixTransform,
    dcomp_device3_CreateTransformGroup,
    dcomp_device3_CreateTranslateTransform3D,
    dcomp_device3_CreateScaleTransform3D,
    dcomp_device3_CreateRotateTransform3D,
    dcomp_device3_CreateMatrixTransform3D,
    dcomp_device3_CreateTransform3DGroup,
    dcomp_device3_CreateEffectGroup,
    dcomp_device3_CreateRectangleClip,
    dcomp_device3_CreateAnimation,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
    dcomp_device3_CreateEffect,
};

static const IDCompositionDesktopDeviceVtbl dcomp_desktop_device_vtbl =
{
    dcomp_desktop_device_QueryInterface,
    dcomp_desktop_device_AddRef,
    dcomp_desktop_device_Release,
    dcomp_desktop_device_Commit,
    dcomp_desktop_device_WaitForCommitCompletion,
    dcomp_desktop_device_GetFrameStatistics,
    dcomp_desktop_device_CreateVisual,
    dcomp_desktop_device_CreateSurfaceFactory,
    dcomp_desktop_device_CreateSurface,
    dcomp_desktop_device_CreateVirtualSurface,
    dcomp_desktop_device_CreateTranslateTransform,
    dcomp_desktop_device_CreateScaleTransform,
    dcomp_desktop_device_CreateRotateTransform,
    dcomp_desktop_device_CreateSkewTransform,
    dcomp_desktop_device_CreateMatrixTransform,
    dcomp_desktop_device_CreateTransformGroup,
    dcomp_desktop_device_CreateTranslateTransform3D,
    dcomp_desktop_device_CreateScaleTransform3D,
    dcomp_desktop_device_CreateRotateTransform3D,
    dcomp_desktop_device_CreateMatrixTransform3D,
    dcomp_desktop_device_CreateTransform3DGroup,
    dcomp_desktop_device_CreateEffectGroup,
    dcomp_desktop_device_CreateRectangleClip,
    dcomp_desktop_device_CreateAnimation,
    dcomp_desktop_device_CreateTargetForHwnd,
    dcomp_desktop_device_CreateSurfaceFromHandle,
    dcomp_desktop_device_CreateSurfaceFromHwnd,
};

#undef DEFINE_DCOMP_MODERN_DEVICE_METHODS

static HRESULT dcomp_create_device(IUnknown *rendering_device, REFIID iid, void **out)
{
    struct dcomp_device *device;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!(device = calloc(1, sizeof(*device))))
        return E_OUTOFMEMORY;

    device->IDCompositionDevice_iface.lpVtbl = &dcomp_device_vtbl;
    device->IDCompositionDevice3_iface.lpVtbl = &dcomp_device3_vtbl;
    device->IDCompositionDesktopDevice_iface.lpVtbl = &dcomp_desktop_device_vtbl;
    device->refcount = 1;
    if (rendering_device)
    {
        IUnknown_AddRef(rendering_device);
        device->rendering_device = rendering_device;
    }
    list_init(&device->targets);

    hr = dcomp_device_query_interface(device, iid, out);
    dcomp_device_release(device);
    return hr;
}

HRESULT WINAPI DCompositionCreateDevice(IDXGIDevice *dxgi_device, REFIID iid, void **device)
{
    TRACE("%p, %s, %p.\n", dxgi_device, debugstr_guid(iid), device);
    return dcomp_create_device((IUnknown *)dxgi_device, iid, device);
}

HRESULT WINAPI DCompositionCreateDevice2(IUnknown *rendering_device, REFIID iid, void **device)
{
    TRACE("rendering_device %p, iid %s, device %p.\n",
            rendering_device, debugstr_guid(iid), device);
    return dcomp_create_device(rendering_device, iid, device);
}

HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **device)
{
    TRACE("rendering_device %p, iid %s, device %p.\n",
            rendering_device, debugstr_guid(iid), device);
    return dcomp_create_device(rendering_device, iid, device);
}

#else /* __REACTOS__ */

HRESULT WINAPI DCompositionCreateDevice(IDXGIDevice *dxgi_device, REFIID iid, void **device)
{
    FIXME("%p, %s, %p.\n", dxgi_device, debugstr_guid(iid), device);
    return E_NOTIMPL;
}

HRESULT WINAPI DCompositionCreateDevice2(IUnknown *rendering_device, REFIID iid, void **device)
{
    FIXME("%p, %s, %p.\n", rendering_device, debugstr_guid(iid), device);
    return E_NOTIMPL;
}

HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **device)
{
    FIXME("%p, %s, %p.\n", rendering_device, debugstr_guid(iid), device);
    return E_NOTIMPL;
}

#endif /* __REACTOS__ */

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
