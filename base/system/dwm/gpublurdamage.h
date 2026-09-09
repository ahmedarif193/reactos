/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* Reconstruct a blur's complete lower scene before sampling a damaged buffer. */
#pragma once

#define DWM_GPU_MATERIAL_BLUR_RADIUS 24

static ULONG
DwmGpuMaterialBlurRadius(const DWM_WIN *Window)
{
    return (Window->BlurFlags & DWM_BLUR_DISABLE_FILTER) ? 0 : DWM_GPU_MATERIAL_BLUR_RADIUS;
}

static BOOL
DwmGpuDamageIntersects(const RECT *A, const RECT *B)
{
    return A->left < B->right && B->left < A->right &&
           A->top < B->bottom && B->top < A->bottom;
}

static BOOL
DwmGpuDamageBounds(RECT *Bounds, LONG Width, LONG Height,
                    LONGLONG Left, LONGLONG Top, LONGLONG Right, LONGLONG Bottom)
{
    Bounds->left = (LONG)max(0, min(Width, Left));
    Bounds->top = (LONG)max(0, min(Height, Top));
    Bounds->right = (LONG)max(0, min(Width, Right));
    Bounds->bottom = (LONG)max(0, min(Height, Bottom));
    return Bounds->left < Bounds->right && Bounds->top < Bounds->bottom;
}

static BOOL
DwmGpuDamageIncludeCapture(RECT *Damage, const RECT *Capture)
{
    RECT Before = *Damage;

    if (!DwmGpuDamageIntersects(Damage, Capture))
        return FALSE;
    Damage->left = min(Damage->left, Capture->left);
    Damage->top = min(Damage->top, Capture->top);
    Damage->right = max(Damage->right, Capture->right);
    Damage->bottom = max(Damage->bottom, Capture->bottom);
    return Before.left != Damage->left || Before.top != Damage->top ||
           Before.right != Damage->right || Before.bottom != Damage->bottom;
}

static void
DwmGpuDamageUnion(RECT *Damage, const RECT *Bounds)
{
    if (Bounds->left >= Bounds->right || Bounds->top >= Bounds->bottom)
        return;
    if (Damage->left >= Damage->right || Damage->top >= Damage->bottom)
        *Damage = *Bounds;
    else
    {
        Damage->left = min(Damage->left, Bounds->left);
        Damage->top = min(Damage->top, Bounds->top);
        Damage->right = max(Damage->right, Bounds->right);
        Damage->bottom = max(Damage->bottom, Bounds->bottom);
    }
}

static void
DwmGpuDamageExpandBlur(RECT *Damage, LONG Width, LONG Height,
                       const DWM_WIN *Windows, ULONG Count,
                       LONG OriginX, LONG OriginY, ULONG BlurRadius,
                       const BOOL *CachedCapture)
{
    BOOL Changed;
    ULONG Index;

    /* A captured rectangle must contain only the current lower scene, not
     * preserved pixels from upper windows in the previous composed frame.
     * Include complete captures, then close over overlapping captures. Each
     * expansion adds a bounded rectangle; the union reaches a fixed point. */
    do
    {
        Changed = FALSE;
        for (Index = 0; Index < Count; ++Index)
        {
            const DWM_WIN *Window = &Windows[Index];
            BOOL Glass = Window->BackdropType == DWM_BACKDROP_TRANSIENT &&
                         Window->BackdropRegion != 0 && Window->BackdropOpacity < 255;
            BOOL Blur = (Window->BlurFlags & DWM_BLUR_ENABLE) &&
                        ((Window->BlurFlags & DWM_BLUR_REGION_ENTIRE_WINDOW) ||
                         Window->BlurRectCount != 0);
            LONG MaterialRadius = Glass ? (LONG)DwmGpuMaterialBlurRadius(Window) : 0;
            LONG Radius = max(MaterialRadius,
                              Blur ? (LONG)BlurRadius : 0);
            DWM_GPU_WINDOW_GEOMETRY Geometry, Client;
            RECT Capture;

            /* The caller retains this exact filtered result for the whole
             * frame, so drawing it does not sample the composition buffer. */
            if (CachedCapture != NULL && CachedCapture[Index])
                continue;
            if ((!Glass && !Blur) || !DwmGpuWindowGeometry(Window, OriginX, OriginY, &Geometry) ||
                ((Window->LayerFlags & DWM_LWA_ALPHA) && Window->Alpha == 0))
                continue;
            /* Explicit regions are conservatively bounded by their window.
             * Material captures already use these whole-window bounds. */
            if (DwmGpuDamageBounds(&Capture, Width, Height,
                    Geometry.Left - Radius, Geometry.Top - Radius,
                    Geometry.Left + Geometry.Width + Radius, Geometry.Top + Geometry.Height + Radius))
                Changed |= DwmGpuDamageIncludeCapture(Damage, &Capture);
            if (Glass && Window->BackdropRegion == DWM_BACKDROP_REGION_WINDOW &&
                Window->DxGlobalShare != 0 && Window->DxUpdateId != 0 &&
                DwmGpuClientGeometry(Window, &Geometry, &Client) &&
                DwmGpuDamageBounds(&Capture, Width, Height,
                    Client.Left - MaterialRadius,
                    Client.Top - MaterialRadius,
                    Client.Left + Client.Width + MaterialRadius,
                    Client.Top + Client.Height + MaterialRadius))
                Changed |= DwmGpuDamageIncludeCapture(Damage, &Capture);
        }
    } while (Changed);
}
