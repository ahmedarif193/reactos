/*
 * PROJECT:     ReactOS DirectComposition
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DirectComposition interface declarations used by the ReactOS backend
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#pragma once

#include <objbase.h>
#include <dxgi1_2.h>

typedef struct IDCompositionAnimation IDCompositionAnimation;
typedef struct IDCompositionClip IDCompositionClip;
typedef struct IDCompositionEffect IDCompositionEffect;
typedef struct IDCompositionSurface IDCompositionSurface;
typedef struct IDCompositionVirtualSurface IDCompositionVirtualSurface;
typedef struct IDCompositionTransform IDCompositionTransform;
typedef struct IDCompositionTransform3D IDCompositionTransform3D;
typedef struct IDCompositionTranslateTransform IDCompositionTranslateTransform;
typedef struct IDCompositionScaleTransform IDCompositionScaleTransform;
typedef struct IDCompositionRotateTransform IDCompositionRotateTransform;
typedef struct IDCompositionSkewTransform IDCompositionSkewTransform;
typedef struct IDCompositionMatrixTransform IDCompositionMatrixTransform;
typedef struct IDCompositionTranslateTransform3D IDCompositionTranslateTransform3D;
typedef struct IDCompositionScaleTransform3D IDCompositionScaleTransform3D;
typedef struct IDCompositionRotateTransform3D IDCompositionRotateTransform3D;
typedef struct IDCompositionMatrixTransform3D IDCompositionMatrixTransform3D;
typedef struct IDCompositionEffectGroup IDCompositionEffectGroup;
typedef struct IDCompositionRectangleClip IDCompositionRectangleClip;
typedef struct IDCompositionVisual IDCompositionVisual;
typedef struct IDCompositionTarget IDCompositionTarget;
typedef struct IDCompositionDevice IDCompositionDevice;
typedef struct IDCompositionDevice3 IDCompositionDevice3;
typedef struct IDCompositionDesktopDevice IDCompositionDesktopDevice;

/* Device2 is the ABI prefix of both Device3 and DesktopDevice. */
typedef IDCompositionDevice3 IDCompositionDevice2;

typedef struct IDCompositionVisualVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDCompositionVisual *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDCompositionVisual *);
    ULONG (STDMETHODCALLTYPE *Release)(IDCompositionVisual *);
    HRESULT (STDMETHODCALLTYPE *SetOffsetXAnimation)(IDCompositionVisual *, IDCompositionAnimation *);
    HRESULT (STDMETHODCALLTYPE *SetOffsetX)(IDCompositionVisual *, float);
    HRESULT (STDMETHODCALLTYPE *SetOffsetYAnimation)(IDCompositionVisual *, IDCompositionAnimation *);
    HRESULT (STDMETHODCALLTYPE *SetOffsetY)(IDCompositionVisual *, float);
    HRESULT (STDMETHODCALLTYPE *SetTransformObject)(IDCompositionVisual *, IDCompositionTransform *);
    HRESULT (STDMETHODCALLTYPE *SetTransform)(IDCompositionVisual *, const void *);
    HRESULT (STDMETHODCALLTYPE *SetTransformParent)(IDCompositionVisual *, IDCompositionVisual *);
    HRESULT (STDMETHODCALLTYPE *SetEffect)(IDCompositionVisual *, IDCompositionEffect *);
    HRESULT (STDMETHODCALLTYPE *SetBitmapInterpolationMode)(IDCompositionVisual *, UINT);
    HRESULT (STDMETHODCALLTYPE *SetBorderMode)(IDCompositionVisual *, UINT);
    HRESULT (STDMETHODCALLTYPE *SetClipObject)(IDCompositionVisual *, IDCompositionClip *);
    HRESULT (STDMETHODCALLTYPE *SetClip)(IDCompositionVisual *, const void *);
    HRESULT (STDMETHODCALLTYPE *SetContent)(IDCompositionVisual *, IUnknown *);
    HRESULT (STDMETHODCALLTYPE *AddVisual)(IDCompositionVisual *, IDCompositionVisual *, BOOL, IDCompositionVisual *);
    HRESULT (STDMETHODCALLTYPE *RemoveVisual)(IDCompositionVisual *, IDCompositionVisual *);
    HRESULT (STDMETHODCALLTYPE *RemoveAllVisuals)(IDCompositionVisual *);
    HRESULT (STDMETHODCALLTYPE *SetCompositeMode)(IDCompositionVisual *, UINT);
    /* IDCompositionVisual2 */
    HRESULT (STDMETHODCALLTYPE *SetOpacityMode)(IDCompositionVisual *, UINT);
    HRESULT (STDMETHODCALLTYPE *SetBackFaceVisibility)(IDCompositionVisual *, UINT);
    /* IDCompositionVisualDebug */
    HRESULT (STDMETHODCALLTYPE *EnableHeatMap)(IDCompositionVisual *, const void *);
    HRESULT (STDMETHODCALLTYPE *DisableHeatMap)(IDCompositionVisual *);
    HRESULT (STDMETHODCALLTYPE *EnableRedrawRegions)(IDCompositionVisual *);
    HRESULT (STDMETHODCALLTYPE *DisableRedrawRegions)(IDCompositionVisual *);
    /* IDCompositionVisual3 */
    HRESULT (STDMETHODCALLTYPE *SetDepthMode)(IDCompositionVisual *, UINT);
    HRESULT (STDMETHODCALLTYPE *SetOffsetZ)(IDCompositionVisual *, float);
    HRESULT (STDMETHODCALLTYPE *SetOffsetZAnimation)(IDCompositionVisual *, IDCompositionAnimation *);
    HRESULT (STDMETHODCALLTYPE *SetOpacity)(IDCompositionVisual *, float);
    HRESULT (STDMETHODCALLTYPE *SetOpacityAnimation)(IDCompositionVisual *, IDCompositionAnimation *);
    HRESULT (STDMETHODCALLTYPE *SetTransform4x4)(IDCompositionVisual *, const void *);
    HRESULT (STDMETHODCALLTYPE *SetTransform3DObject)(IDCompositionVisual *, IDCompositionTransform3D *);
    HRESULT (STDMETHODCALLTYPE *SetVisible)(IDCompositionVisual *, BOOL);
} IDCompositionVisualVtbl;

struct IDCompositionVisual
{
    const IDCompositionVisualVtbl *lpVtbl;
};

typedef struct IDCompositionTargetVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDCompositionTarget *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDCompositionTarget *);
    ULONG (STDMETHODCALLTYPE *Release)(IDCompositionTarget *);
    HRESULT (STDMETHODCALLTYPE *SetRoot)(IDCompositionTarget *, IDCompositionVisual *);
} IDCompositionTargetVtbl;

struct IDCompositionTarget
{
    const IDCompositionTargetVtbl *lpVtbl;
};

typedef struct IDCompositionDeviceVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDCompositionDevice *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDCompositionDevice *);
    ULONG (STDMETHODCALLTYPE *Release)(IDCompositionDevice *);
    HRESULT (STDMETHODCALLTYPE *Commit)(IDCompositionDevice *);
    HRESULT (STDMETHODCALLTYPE *WaitForCommitCompletion)(IDCompositionDevice *);
    HRESULT (STDMETHODCALLTYPE *GetFrameStatistics)(IDCompositionDevice *, void *);
    HRESULT (STDMETHODCALLTYPE *CreateTargetForHwnd)(IDCompositionDevice *, HWND, BOOL, IDCompositionTarget **);
    HRESULT (STDMETHODCALLTYPE *CreateVisual)(IDCompositionDevice *, IDCompositionVisual **);
    HRESULT (STDMETHODCALLTYPE *CreateSurface)(IDCompositionDevice *, UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, IDCompositionSurface **);
    HRESULT (STDMETHODCALLTYPE *CreateVirtualSurface)(IDCompositionDevice *, UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, IDCompositionVirtualSurface **);
    HRESULT (STDMETHODCALLTYPE *CreateSurfaceFromHandle)(IDCompositionDevice *, HANDLE, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *CreateSurfaceFromHwnd)(IDCompositionDevice *, HWND, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *CreateTranslateTransform)(IDCompositionDevice *, IDCompositionTranslateTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateScaleTransform)(IDCompositionDevice *, IDCompositionScaleTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateRotateTransform)(IDCompositionDevice *, IDCompositionRotateTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateSkewTransform)(IDCompositionDevice *, IDCompositionSkewTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateMatrixTransform)(IDCompositionDevice *, IDCompositionMatrixTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateTransformGroup)(IDCompositionDevice *, IDCompositionTransform **, UINT, IDCompositionTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateTranslateTransform3D)(IDCompositionDevice *, IDCompositionTranslateTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateScaleTransform3D)(IDCompositionDevice *, IDCompositionScaleTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateRotateTransform3D)(IDCompositionDevice *, IDCompositionRotateTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateMatrixTransform3D)(IDCompositionDevice *, IDCompositionMatrixTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateTransform3DGroup)(IDCompositionDevice *, IDCompositionTransform3D **, UINT, IDCompositionTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateEffectGroup)(IDCompositionDevice *, IDCompositionEffectGroup **);
    HRESULT (STDMETHODCALLTYPE *CreateRectangleClip)(IDCompositionDevice *, IDCompositionRectangleClip **);
    HRESULT (STDMETHODCALLTYPE *CreateAnimation)(IDCompositionDevice *, IDCompositionAnimation **);
    HRESULT (STDMETHODCALLTYPE *CheckDeviceState)(IDCompositionDevice *, BOOL *);
} IDCompositionDeviceVtbl;

struct IDCompositionDevice
{
    const IDCompositionDeviceVtbl *lpVtbl;
};

typedef struct IDCompositionDevice3Vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDCompositionDevice3 *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDCompositionDevice3 *);
    ULONG (STDMETHODCALLTYPE *Release)(IDCompositionDevice3 *);
    /* IDCompositionDevice2 */
    HRESULT (STDMETHODCALLTYPE *Commit)(IDCompositionDevice3 *);
    HRESULT (STDMETHODCALLTYPE *WaitForCommitCompletion)(IDCompositionDevice3 *);
    HRESULT (STDMETHODCALLTYPE *GetFrameStatistics)(IDCompositionDevice3 *, void *);
    HRESULT (STDMETHODCALLTYPE *CreateVisual)(IDCompositionDevice3 *, IDCompositionVisual **);
    HRESULT (STDMETHODCALLTYPE *CreateSurfaceFactory)(IDCompositionDevice3 *, IUnknown *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *CreateSurface)(IDCompositionDevice3 *, UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, IDCompositionSurface **);
    HRESULT (STDMETHODCALLTYPE *CreateVirtualSurface)(IDCompositionDevice3 *, UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, IDCompositionVirtualSurface **);
    HRESULT (STDMETHODCALLTYPE *CreateTranslateTransform)(IDCompositionDevice3 *, IDCompositionTranslateTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateScaleTransform)(IDCompositionDevice3 *, IDCompositionScaleTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateRotateTransform)(IDCompositionDevice3 *, IDCompositionRotateTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateSkewTransform)(IDCompositionDevice3 *, IDCompositionSkewTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateMatrixTransform)(IDCompositionDevice3 *, IDCompositionMatrixTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateTransformGroup)(IDCompositionDevice3 *, IDCompositionTransform **, UINT, IDCompositionTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateTranslateTransform3D)(IDCompositionDevice3 *, IDCompositionTranslateTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateScaleTransform3D)(IDCompositionDevice3 *, IDCompositionScaleTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateRotateTransform3D)(IDCompositionDevice3 *, IDCompositionRotateTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateMatrixTransform3D)(IDCompositionDevice3 *, IDCompositionMatrixTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateTransform3DGroup)(IDCompositionDevice3 *, IDCompositionTransform3D **, UINT, IDCompositionTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateEffectGroup)(IDCompositionDevice3 *, IDCompositionEffectGroup **);
    HRESULT (STDMETHODCALLTYPE *CreateRectangleClip)(IDCompositionDevice3 *, IDCompositionRectangleClip **);
    HRESULT (STDMETHODCALLTYPE *CreateAnimation)(IDCompositionDevice3 *, IDCompositionAnimation **);
    /* IDCompositionDevice3 */
    HRESULT (STDMETHODCALLTYPE *CreateGaussianBlurEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateBrightnessEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateColorMatrixEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateShadowEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateHueRotationEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateSaturationEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateTurbulenceEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateLinearTransferEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateTableTransferEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateCompositeEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateBlendEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateArithmeticCompositeEffect)(IDCompositionDevice3 *, void **);
    HRESULT (STDMETHODCALLTYPE *CreateAffineTransform2DEffect)(IDCompositionDevice3 *, void **);
} IDCompositionDevice3Vtbl;

struct IDCompositionDevice3
{
    const IDCompositionDevice3Vtbl *lpVtbl;
};

typedef struct IDCompositionDesktopDeviceVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDCompositionDesktopDevice *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDCompositionDesktopDevice *);
    ULONG (STDMETHODCALLTYPE *Release)(IDCompositionDesktopDevice *);
    /* IDCompositionDevice2 */
    HRESULT (STDMETHODCALLTYPE *Commit)(IDCompositionDesktopDevice *);
    HRESULT (STDMETHODCALLTYPE *WaitForCommitCompletion)(IDCompositionDesktopDevice *);
    HRESULT (STDMETHODCALLTYPE *GetFrameStatistics)(IDCompositionDesktopDevice *, void *);
    HRESULT (STDMETHODCALLTYPE *CreateVisual)(IDCompositionDesktopDevice *, IDCompositionVisual **);
    HRESULT (STDMETHODCALLTYPE *CreateSurfaceFactory)(IDCompositionDesktopDevice *, IUnknown *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *CreateSurface)(IDCompositionDesktopDevice *, UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, IDCompositionSurface **);
    HRESULT (STDMETHODCALLTYPE *CreateVirtualSurface)(IDCompositionDesktopDevice *, UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, IDCompositionVirtualSurface **);
    HRESULT (STDMETHODCALLTYPE *CreateTranslateTransform)(IDCompositionDesktopDevice *, IDCompositionTranslateTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateScaleTransform)(IDCompositionDesktopDevice *, IDCompositionScaleTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateRotateTransform)(IDCompositionDesktopDevice *, IDCompositionRotateTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateSkewTransform)(IDCompositionDesktopDevice *, IDCompositionSkewTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateMatrixTransform)(IDCompositionDesktopDevice *, IDCompositionMatrixTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateTransformGroup)(IDCompositionDesktopDevice *, IDCompositionTransform **, UINT, IDCompositionTransform **);
    HRESULT (STDMETHODCALLTYPE *CreateTranslateTransform3D)(IDCompositionDesktopDevice *, IDCompositionTranslateTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateScaleTransform3D)(IDCompositionDesktopDevice *, IDCompositionScaleTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateRotateTransform3D)(IDCompositionDesktopDevice *, IDCompositionRotateTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateMatrixTransform3D)(IDCompositionDesktopDevice *, IDCompositionMatrixTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateTransform3DGroup)(IDCompositionDesktopDevice *, IDCompositionTransform3D **, UINT, IDCompositionTransform3D **);
    HRESULT (STDMETHODCALLTYPE *CreateEffectGroup)(IDCompositionDesktopDevice *, IDCompositionEffectGroup **);
    HRESULT (STDMETHODCALLTYPE *CreateRectangleClip)(IDCompositionDesktopDevice *, IDCompositionRectangleClip **);
    HRESULT (STDMETHODCALLTYPE *CreateAnimation)(IDCompositionDesktopDevice *, IDCompositionAnimation **);
    /* IDCompositionDesktopDevice */
    HRESULT (STDMETHODCALLTYPE *CreateTargetForHwnd)(IDCompositionDesktopDevice *, HWND, BOOL, IDCompositionTarget **);
    HRESULT (STDMETHODCALLTYPE *CreateSurfaceFromHandle)(IDCompositionDesktopDevice *, HANDLE, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *CreateSurfaceFromHwnd)(IDCompositionDesktopDevice *, HWND, IUnknown **);
} IDCompositionDesktopDeviceVtbl;

struct IDCompositionDesktopDevice
{
    const IDCompositionDesktopDeviceVtbl *lpVtbl;
};
