/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* GPU backdrop dependencies. These compare metadata, never window pixels. */
#pragma once

typedef struct _DWM_GPU_SCENE_SPACE
{
    LONG OriginX, OriginY, Width, Height;
    ULONG BlurRadius;
    RECT ShadowMargins;
} DWM_GPU_SCENE_SPACE;

typedef struct _DWM_GPU_SCENE_CACHE
{
    DWM_WIN Windows[DWM_MAX_WINDOWS];
    RECTL BlurRects[DWM_MAX_BLUR_RECTS];
    ULONG Count, BlurRectCount;
    DWM_GPU_SCENE_SPACE Space;
    RECT AnimationDamage;
    BOOL Valid;
} DWM_GPU_SCENE_CACHE;

static BOOL
DwmGpuSceneHasSurface(const DWM_WIN *Windows, ULONG Count,
                      ULONG SurfaceId, BOOL Client)
{
    ULONG Index;

    /* The wallpaper is synthesized by Begin, outside the window snapshot. */
    if (SurfaceId == (ULONG)-1 && !Client)
        return TRUE;
    for (Index = 0; Index < Count; ++Index)
    {
        if (Windows[Index].SurfaceId == SurfaceId &&
            (!Client || Windows[Index].DxGlobalShare != 0))
            return TRUE;
    }
    return FALSE;
}

static DWM_WIN
DwmGpuCacheBlurOwner(const DWM_WIN *Window)
{
    DWM_WIN Key = *Window;

    /* Own pixels and a packed region-array offset do not change the lower
     * scene. The filter key separately checks the actual capture rectangle.
     * A retained client publishes each frame in another buffer, so only
     * whether it has a client layer is kept of that buffer's identity. */
    Key.Damaged = 0;
    Key.BaseUpdateId = 0;
    Key.BasePreviousUpdateId = 0;
    memset(&Key.BaseDirtyRect, 0, sizeof(Key.BaseDirtyRect));
    Key.DxGlobalShare = Key.DxGlobalShare != 0;
    Key.DxGeneration = 0;
    Key.DxUpdateId = 0;
    Key.BlurRectBase = 0;
    return Key;
}

static BOOL
DwmGpuSceneRegionsSame(const DWM_WIN *A, const DWM_WIN *B,
                       const RECTL *ARects, ULONG ACount,
                       const RECTL *BRects, ULONG BCount)
{
    if (A->BlurRectBase > ACount || A->BlurRectCount > ACount - A->BlurRectBase ||
        B->BlurRectBase > BCount || B->BlurRectCount > BCount - B->BlurRectBase)
        return FALSE;
    return A->BlurRectCount == B->BlurRectCount &&
           (A->BlurRectCount == 0 ||
            memcmp(ARects + A->BlurRectBase, BRects + B->BlurRectBase,
                   A->BlurRectCount * sizeof(*ARects)) == 0);
}

static BOOL
DwmGpuSceneWindowSame(const DWM_WIN *A, const DWM_WIN *B,
                      const RECTL *ARects, ULONG ACount,
                      const RECTL *BRects, ULONG BCount)
{
    DWM_WIN Left = *A, Right = *B;

    Left.Damaged = Right.Damaged = 0;
    Left.BlurRectBase = Right.BlurRectBase = 0;
    return memcmp(&Left, &Right, sizeof(Left)) == 0 &&
           DwmGpuSceneRegionsSame(A, B, ARects, ACount, BRects, BCount);
}

static BOOL
DwmGpuSceneWindowSameForCapture(const DWM_WIN *Current, const DWM_WIN *Old,
                                const RECTL *CurrentRects, ULONG CurrentRectCount,
                                const RECTL *OldRects, ULONG OldRectCount,
                                const DWM_GPU_SCENE_SPACE *Space,
                                const RECT *CurrentInterest, const RECT *OldInterest)
{
    DWM_WIN CurrentKey, OldKey;
    RECT Changed;
    const RECTL *Dirty = &Current->BaseDirtyRect;

    if (DwmGpuSceneWindowSame(Current, Old, CurrentRects, CurrentRectCount,
                              OldRects, OldRectCount))
        return TRUE;

    if (Current->AnimFlags != 0 || Old->AnimFlags != 0)
        return FALSE;

    CurrentKey = *Current;
    OldKey = *Old;
    if (Current->DxUpdateId != Old->DxUpdateId)
    {
        DWM_GPU_WINDOW_GEOMETRY Owner, Client;

        /* A client publication changes client pixels, not the surrounding
         * shadow. A capture can intersect that shadow while lying entirely
         * outside the client. Include a sampling guard and keep comparing
         * every other field below, so geometry, resource and visual changes
         * still invalidate the cached backdrop. */
        if (Current->DxGlobalShare == 0 || Old->DxGlobalShare == 0 ||
            Old->DxUpdateId == 0 || Current->DxUpdateId <= Old->DxUpdateId ||
            !DwmGpuWindowGeometry(Current, Space->OriginX, Space->OriginY, &Owner) ||
            !DwmGpuClientGeometry(Current, &Owner, &Client))
            return FALSE;

        if (DwmGpuDamageBounds(&Changed, Space->Width, Space->Height,
                               Client.Left - 1, Client.Top - 1,
                               Client.Left + Client.Width + 1,
                               Client.Top + Client.Height + 1) &&
            (DwmGpuDamageIntersects(&Changed, CurrentInterest) ||
             DwmGpuDamageIntersects(&Changed, OldInterest)))
            return FALSE;

        /* A retained client presents each frame from another buffer. */
        CurrentKey.DxGlobalShare = OldKey.DxGlobalShare;
        CurrentKey.DxGeneration = OldKey.DxGeneration;
        CurrentKey.DxUpdateId = OldKey.DxUpdateId;
        if (DwmGpuSceneWindowSame(&CurrentKey, &OldKey, CurrentRects, CurrentRectCount,
                                  OldRects, OldRectCount))
            return TRUE;
    }

    /* One completed GDI FRONT publication supplies its exact dirty bounds.
     * A lower window can change elsewhere without changing this capture.
     * The previous ID must match the snapshot: otherwise an intervening
     * publication could have touched the capture outside the latest rect. */
    if (Old->BaseUpdateId == 0 ||
        Current->BaseUpdateId <= Old->BaseUpdateId ||
        Current->BasePreviousUpdateId != Old->BaseUpdateId ||
        Dirty->left < 0 || Dirty->top < 0 ||
        Dirty->right > Current->cx || Dirty->bottom > Current->cy ||
        Dirty->left >= Dirty->right || Dirty->top >= Dirty->bottom)
        return FALSE;

    if (DwmGpuDamageBounds(&Changed, Space->Width, Space->Height,
                           (LONGLONG)Current->x - Space->OriginX + Dirty->left - 1,
                           (LONGLONG)Current->y - Space->OriginY + Dirty->top - 1,
                           (LONGLONG)Current->x - Space->OriginX + Dirty->right + 1,
                           (LONGLONG)Current->y - Space->OriginY + Dirty->bottom + 1) &&
        (DwmGpuDamageIntersects(&Changed, CurrentInterest) ||
         DwmGpuDamageIntersects(&Changed, OldInterest)))
        return FALSE;

    CurrentKey.BaseUpdateId = OldKey.BaseUpdateId;
    CurrentKey.BasePreviousUpdateId = OldKey.BasePreviousUpdateId;
    CurrentKey.BaseDirtyRect = OldKey.BaseDirtyRect;
    return DwmGpuSceneWindowSame(&CurrentKey, &OldKey,
                                  CurrentRects, CurrentRectCount,
                                  OldRects, OldRectCount);
}

static BOOL
DwmGpuSceneWindowBounds(const DWM_WIN *Window, const DWM_GPU_SCENE_SPACE *Space,
                        BOOL Capture, RECT *Bounds)
{
    BOOL Glass = Window->BackdropType == DWM_BACKDROP_TRANSIENT &&
                 Window->BackdropRegion != 0 && Window->BackdropOpacity < 255;
    BOOL Blur = (Window->BlurFlags & DWM_BLUR_ENABLE) &&
                ((Window->BlurFlags & DWM_BLUR_REGION_ENTIRE_WINDOW) ||
                 Window->BlurRectCount != 0);
    DWM_GPU_WINDOW_GEOMETRY Geometry, Client;
    LONGLONG Left, Top, Right, Bottom;
    ULONG Radius = max(Glass ? DwmGpuMaterialBlurRadius(Window) : 0,
                       Blur ? Space->BlurRadius : 0);

    if (!DwmGpuWindowGeometry(Window, Space->OriginX, Space->OriginY, &Geometry) ||
        ((Window->LayerFlags & DWM_LWA_ALPHA) && Window->Alpha == 0) ||
        (Capture && !Glass && !Blur))
        return FALSE;
    Left = Geometry.Left; Top = Geometry.Top;
    Right = Left + Geometry.Width; Bottom = Top + Geometry.Height;
    if (Capture)
    {
        Left -= Radius; Top -= Radius;
        Right += Radius; Bottom += Radius;
    }
    else if (Window->LayerFlags & DWM_WINDOW_NC_SHADOW)
    {
        Left -= Space->ShadowMargins.left; Top -= Space->ShadowMargins.top;
        Right += Space->ShadowMargins.right; Bottom += Space->ShadowMargins.bottom;
    }
    /* Client DX surfaces can extend beyond the base window bounds. A whole
     * glass material also captures their lower scene independently. */
    if (Window->DxGlobalShare != 0 && Window->DxUpdateId != 0 &&
        DwmGpuClientGeometry(Window, &Geometry, &Client) &&
        (!Capture || (Glass && Window->BackdropRegion == DWM_BACKDROP_REGION_WINDOW)))
    {
        ULONG Margin = Capture ? DwmGpuMaterialBlurRadius(Window) : 0;

        Left = min(Left, Client.Left - Margin); Top = min(Top, Client.Top - Margin);
        Right = max(Right, Client.Left + Client.Width + Margin);
        Bottom = max(Bottom, Client.Top + Client.Height + Margin);
    }
    return DwmGpuDamageBounds(Bounds, Space->Width, Space->Height, Left, Top, Right, Bottom);
}

static inline void
DwmGpuSceneOcclusion(const DWM_WIN *Windows, ULONG Count,
                      const DWM_GPU_SCENE_SPACE *Space, RECT *Occlusion)
{
    RECT Cover = {0};

    /* One opaque rectangle per layer bounds the amount of clipping work.
     * A backdrop capture between that layer and its cover needs the lower
     * pixels even where the cover will eventually hide them. */
    while (Count != 0)
    {
        const DWM_WIN *Window = &Windows[--Count];
        DWM_GPU_WINDOW_GEOMETRY Geometry;
        RECT Capture, Bounds;
        BOOL Captures = DwmGpuSceneWindowBounds(Window, Space, TRUE, &Capture);
        LONG Inset;

        Occlusion[Count + 1] = Cover;
        if (Captures && DwmGpuDamageIntersects(&Cover, &Capture))
            SetRectEmpty(&Cover);
        if (Captures || Window->AnimFlags != 0 ||
            (Window->LayerFlags & DWM_LWA_COLORKEY) ||
            (Window->LayerFlags & DWM_WINDOW_PREMULTIPLIED_ALPHA) ||
            (Window->BlurFlags & DWM_BLUR_ENABLE) ||
            ((Window->LayerFlags & DWM_LWA_ALPHA) && Window->Alpha < 255) ||
            !DwmGpuWindowGeometry(Window, Space->OriginX, Space->OriginY, &Geometry))
            continue;

        /* Keep rounded edges and their antialiasing outside the cover. */
        Inset = Window->CornerRadius != 0 ?
            min(Window->CornerRadius, (ULONG)min(Geometry.Width / 2, Geometry.Height / 2)) + 1 : 0;
        if (!DwmGpuDamageBounds(&Bounds, Space->Width, Space->Height,
                Geometry.Left + Inset, Geometry.Top + Inset,
                Geometry.Left + Geometry.Width - Inset,
                Geometry.Top + Geometry.Height - Inset))
            continue;
        if ((ULONGLONG)(Bounds.right - Bounds.left) * (Bounds.bottom - Bounds.top) >
            (ULONGLONG)(Cover.right - Cover.left) * (Cover.bottom - Cover.top))
            Cover = Bounds;
    }
    Occlusion[0] = Cover; /* Wallpaper precedes every window. */
}

static void
DwmGpuSceneIncludeWindow(RECT *Damage, const DWM_WIN *Window,
                         const DWM_GPU_SCENE_SPACE *Space)
{
    RECT Bounds;

    if (DwmGpuSceneWindowBounds(Window, Space, FALSE, &Bounds))
        DwmGpuDamageUnion(Damage, &Bounds);
}

static RECT
DwmGpuSceneAnimationDamage(const DWM_GPU_SCENE_CACHE *Cache,
                           const DWM_WIN *Windows, ULONG Count,
                           const DWM_GPU_SCENE_SPACE *Space)
{
    RECT Damage = {0};
    ULONG Index, Other;

    /* Retain the last presented footprint as well as the new destination.
     * Include client surfaces and shadows outside the kernel's base-window
     * damage, including animation start, cancellation and disappearance.
     * A replaced surface needs both footprints too: matching its slot here
     * only unions damage, and never permits resource or blur-cache reuse. */
    for (Index = 0; Index < Count; ++Index)
    {
        if (Windows[Index].AnimFlags == 0)
            continue;
        DwmGpuSceneIncludeWindow(&Damage, &Windows[Index], Space);
        for (Other = 0; Cache->Valid && Other < Cache->Count; ++Other)
        {
            if (Windows[Index].SurfaceId == Cache->Windows[Other].SurfaceId)
            {
                DwmGpuSceneIncludeWindow(&Damage, &Cache->Windows[Other], &Cache->Space);
                break;
            }
        }
    }
    for (Index = 0; Cache->Valid && Index < Cache->Count; ++Index)
    {
        if (Cache->Windows[Index].AnimFlags == 0)
            continue;
        DwmGpuSceneIncludeWindow(&Damage, &Cache->Windows[Index], &Cache->Space);
        for (Other = 0; Other < Count; ++Other)
        {
            if (Cache->Windows[Index].SurfaceId == Windows[Other].SurfaceId)
            {
                DwmGpuSceneIncludeWindow(&Damage, &Windows[Other], Space);
                break;
            }
        }
    }
    return Damage;
}

static BOOL
DwmGpuSceneDependencies(const DWM_WIN *Windows, ULONG Count,
                        const DWM_GPU_SCENE_SPACE *Space, RECT Interest,
                        BOOL *Relevant, RECT *Required)
{
    /* Walk downward: another glass layer can bring more lower pixels into
     * the capture, but windows above that layer cannot affect its backdrop. */
    while (Count != 0)
    {
        const DWM_WIN *Window = &Windows[--Count];
        RECT Bounds, Capture;

        /* A lower window must preserve every pixel sampled by intervening
         * glass layers, including samples outside the owner's own capture. */
        Required[Count] = Interest;
        Relevant[Count] = DwmGpuSceneWindowBounds(Window, Space, FALSE, &Bounds) &&
                          DwmGpuDamageIntersects(&Bounds, &Interest);
        if (Relevant[Count] && DwmGpuSceneWindowBounds(Window, Space, TRUE, &Capture))
        {
            Interest.left = min(Interest.left, Capture.left);
            Interest.top = min(Interest.top, Capture.top);
            Interest.right = max(Interest.right, Capture.right);
            Interest.bottom = max(Interest.bottom, Capture.bottom);
        }
    }
    return TRUE;
}

static BOOL
DwmGpuSceneLowerSame(const DWM_GPU_SCENE_CACHE *Cache,
                     const DWM_WIN *Windows, ULONG OwnerIndex,
                     const RECTL *BlurRects, ULONG BlurRectCount)
{
    DWM_WIN Owner = DwmGpuCacheBlurOwner(&Windows[OwnerIndex]), Previous;
    BOOL CurrentRelevant[DWM_MAX_WINDOWS], PreviousRelevant[DWM_MAX_WINDOWS];
    RECT CurrentRequired[DWM_MAX_WINDOWS], PreviousRequired[DWM_MAX_WINDOWS];
    ULONG OldOwner, CurrentIndex = 0, PreviousIndex = 0;
    RECT Capture;

    if (Owner.AnimFlags != 0 || !DwmGpuSceneWindowBounds(&Owner, &Cache->Space, TRUE, &Capture))
        return FALSE;
    for (OldOwner = 0; OldOwner < Cache->Count; ++OldOwner)
        if (Cache->Windows[OldOwner].SurfaceId == Owner.SurfaceId &&
            Cache->Windows[OldOwner].Generation == Owner.Generation)
            break;
    if (OldOwner == Cache->Count)
        return FALSE;
    Previous = DwmGpuCacheBlurOwner(&Cache->Windows[OldOwner]);
    if (memcmp(&Owner, &Previous, sizeof(Owner)) != 0 ||
        !DwmGpuSceneRegionsSame(&Windows[OwnerIndex], &Cache->Windows[OldOwner],
                                 BlurRects, BlurRectCount, Cache->BlurRects, Cache->BlurRectCount) ||
        !DwmGpuSceneDependencies(Windows, OwnerIndex, &Cache->Space, Capture,
                                 CurrentRelevant, CurrentRequired) ||
        !DwmGpuSceneDependencies(Cache->Windows, OldOwner, &Cache->Space, Capture,
                                 PreviousRelevant, PreviousRequired))
        return FALSE;
    for (;;)
    {
        while (CurrentIndex < OwnerIndex && !CurrentRelevant[CurrentIndex]) ++CurrentIndex;
        while (PreviousIndex < OldOwner && !PreviousRelevant[PreviousIndex]) ++PreviousIndex;
        if (CurrentIndex == OwnerIndex || PreviousIndex == OldOwner)
            return CurrentIndex == OwnerIndex && PreviousIndex == OldOwner;
        if (!DwmGpuSceneWindowSameForCapture(&Windows[CurrentIndex],
                                              &Cache->Windows[PreviousIndex],
                                              BlurRects, BlurRectCount,
                                              Cache->BlurRects, Cache->BlurRectCount,
                                              &Cache->Space,
                                              &CurrentRequired[CurrentIndex],
                                              &PreviousRequired[PreviousIndex]))
            return FALSE;
        ++CurrentIndex;
        ++PreviousIndex;
    }
}

static void
DwmGpuCacheScene(DWM_GPU_SCENE_CACHE *Cache, const DWM_WIN *Windows,
                 ULONG Count, const RECTL *BlurRects, ULONG BlurRectCount,
                 const DWM_GPU_SCENE_SPACE *Space, BOOL RefreshBackdrop,
                 BOOL *LowerUnchanged)
{
    ULONG Index;
    BOOL Valid = Cache->Valid && !RefreshBackdrop &&
                 memcmp(&Cache->Space, Space, sizeof(*Space)) == 0;
    BOOL Same = Valid;
    RECT AnimationDamage = DwmGpuSceneAnimationDamage(Cache, Windows, Count, Space);

    for (Index = 0; Index < Count; ++Index)
    {
        DWM_WIN Current = Windows[Index];

        /* Damage is an edge notification; the publication IDs identify the
         * pixels. Keep every visual property and both publication IDs in the
         * lower-scene comparison, including order and surface generation. */
        Current.Damaged = 0;
        /* The same owner must terminate the unchanged prefix. Its regions
         * also determine whether an explicit blur precedes its own shadow
         * and material, so they must preserve the first capture's meaning. */
        LowerUnchanged[Index] = Same && Index < Cache->Count &&
                Cache->Windows[Index].SurfaceId == Current.SurfaceId &&
                Cache->Windows[Index].Generation == Current.Generation &&
                DwmGpuSceneRegionsSame(&Cache->Windows[Index], &Current,
                                        Cache->BlurRects, Cache->BlurRectCount, BlurRects, BlurRectCount);
        if (Valid && !LowerUnchanged[Index])
            LowerUnchanged[Index] = DwmGpuSceneLowerSame(Cache, Windows, Index, BlurRects, BlurRectCount);
        Same = Same && Index < Cache->Count && Current.AnimFlags == 0 &&
                DwmGpuSceneWindowSame(&Cache->Windows[Index], &Current,
                                      Cache->BlurRects, Cache->BlurRectCount, BlurRects, BlurRectCount);
    }
    /* Preserve the complete old stack until every dependency comparison is done. */
    for (Index = 0; Index < Count; ++Index)
    {
        Cache->Windows[Index] = Windows[Index];
        Cache->Windows[Index].Damaged = 0;
    }
    if (BlurRectCount != 0)
        memcpy(Cache->BlurRects, BlurRects, BlurRectCount * sizeof(*BlurRects));
    Cache->Count = Count;
    Cache->BlurRectCount = BlurRectCount;
    Cache->Space = *Space;
    Cache->AnimationDamage = AnimationDamage;
    /* The caller commits this snapshot only after a successful GPU present. */
    Cache->Valid = FALSE;
}
