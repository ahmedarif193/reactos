/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <apitest.h>
#include <windows.h>
#include <reactos/dwmframe.h>
#include "../../../../base/system/dwm/gpugeometry.h"
#include "../../../../base/system/dwm/gpublurdamage.h"
#include "../../../../base/system/dwm/gpuscenecache.h"

static DWM_GPU_SCENE_CACHE Cache;

START_TEST(scene_cache)
{
    DWM_GPU_SCENE_SPACE Space = {0, 0, 1280, 720, 10, {24, 12, 24, 32}};
    DWM_WIN Old = {0}, Current, Windows[2];
    RECT ShadowCapture = {0, 662, 1280, 720};
    RECT ClientCapture = {100, 100, 200, 200};
    RECT EdgeCapture = {100, 644, 200, 650};
    BOOL Same[2];

    Old.x = Old.y = 12;
    Old.cx = 808;
    Old.cy = 636;
    Old.SurfaceId = 1;
    Old.Generation = 1;
    Old.LayerFlags = DWM_WINDOW_NC_SHADOW;
    Old.Alpha = 255;
    Old.DxGlobalShare = 1;
    Old.DxGeneration = 1;
    Old.DxUpdateId = 10;
    Old.DxClientX = 4;
    Old.DxClientY = 32;
    Old.DxWidth = 800;
    Old.DxHeight = 600;
    Old.BaseUpdateId = 3;
    Current = Old;
    ++Current.DxUpdateId;

#define SAME(a, b) DwmGpuSceneWindowSameForCapture(&Current, &Old, NULL, 0, NULL, 0, &Space, a, b)
    ok(SAME(&ShadowCapture, &ShadowCapture), "Client update invalidated an unchanged shadow\n");
    ok(!SAME(&ClientCapture, &ShadowCapture), "Current capture reads changed client pixels\n");
    ok(!SAME(&ShadowCapture, &ClientCapture), "Previous capture reads changed client pixels\n");
    ok(!SAME(&EdgeCapture, &EdgeCapture), "Bilinear sampling guard was lost\n");

    Current.DxUpdateId += 5;
    ok(SAME(&ShadowCapture, &ShadowCapture), "Skipped client publications are safe outside the entire client\n");
    Current.DxGlobalShare = 2;
    ok(SAME(&ShadowCapture, &ShadowCapture), "A retained frame in another buffer changed the shadow\n");
    ok(!SAME(&ClientCapture, &ShadowCapture), "A retained frame in another buffer kept stale client pixels\n");
    Current = Old; ++Current.DxUpdateId; ++Current.DxGeneration;
    ok(SAME(&ShadowCapture, &ShadowCapture), "A new buffer generation changed the shadow\n");
    Current = Old; ++Current.DxUpdateId; ++Current.x;
    ok(!SAME(&ShadowCapture, &ShadowCapture), "Moving the shadow must invalidate\n");
    Current = Old; ++Current.DxUpdateId; Current.Alpha = 128;
    ok(!SAME(&ShadowCapture, &ShadowCapture), "Changing opacity must invalidate\n");
    Current = Old; ++Current.DxUpdateId; Current.AnimFlags = 1;
    ok(!SAME(&ShadowCapture, &ShadowCapture), "Animated geometry must invalidate\n");
    Current = Old; ++Current.DxUpdateId; Current.DxWidth = 0;
    ok(!SAME(&ShadowCapture, &ShadowCapture), "Invalid client geometry must invalidate\n");
    Current = Old; Current.DxUpdateId = 0;
    ok(!SAME(&ShadowCapture, &ShadowCapture), "Removing the client publication must invalidate\n");

    Current = Old; ++Current.DxUpdateId;
    ++Current.BaseUpdateId;
    Current.BasePreviousUpdateId = Old.BaseUpdateId;
    SetRect((RECT *)&Current.BaseDirtyRect, 0, 0, 100, 10);
    ok(SAME(&ShadowCapture, &ShadowCapture), "Independent out-of-capture GDI and client updates\n");
    Current.BasePreviousUpdateId = 1;
    ok(!SAME(&ShadowCapture, &ShadowCapture), "Missing GDI damage history must invalidate\n");
    Current.BasePreviousUpdateId = Old.BaseUpdateId;
    SetRect((RECT *)&Current.BaseDirtyRect, 0, 610, 100, 636);
    ok(!SAME(&ClientCapture, &ClientCapture), "Client change cannot be hidden by unrelated GDI damage\n");

    ZeroMemory(Windows, sizeof(Windows));
    Windows[0] = Old;
    Windows[1].x = 0; Windows[1].y = 672;
    Windows[1].cx = 1280; Windows[1].cy = 48;
    Windows[1].SurfaceId = 2; Windows[1].Generation = 1;
    Windows[1].Alpha = 255;
    Windows[1].BlurFlags = DWM_BLUR_ENABLE | DWM_BLUR_REGION_ENTIRE_WINDOW;
    ZeroMemory(&Cache, sizeof(Cache));
    DwmGpuCacheScene(&Cache, Windows, 2, NULL, 0, &Space, TRUE, Same);
    Cache.Valid = TRUE;
    ++Windows[0].DxUpdateId;
    DwmGpuCacheScene(&Cache, Windows, 2, NULL, 0, &Space, FALSE, Same);
    ok(Same[1], "Taskbar blur should reuse its unchanged shadow backdrop\n");
    Windows[0].y += 30;
    DwmGpuCacheScene(&Cache, Windows, 2, NULL, 0, &Space, FALSE, Same);
    ok(!Same[1], "Moved client entering taskbar capture must invalidate\n");
    ++Windows[0].DxUpdateId;
    DwmGpuCacheScene(&Cache, Windows, 2, NULL, 0, &Space, FALSE, Same);
    ok(!Same[1], "Overlapping client updates must refresh taskbar blur\n");
#undef SAME
}
