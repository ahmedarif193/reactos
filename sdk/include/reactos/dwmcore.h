/*
 * PROJECT:     ReactOS Desktop Window Manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private DWM process/module contracts
 */

#pragma once

#include <windef.h>
#include <winerror.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HMIL_CONNECTION__ HMIL_CONNECTION;
typedef struct IDwmChannelProvider IDwmChannelProvider;
typedef struct IDwmChannelPrivate IDwmChannelPrivate;
typedef struct IDwmCursorController IDwmCursorController;
typedef struct IRenderDataBuilder IRenderDataBuilder;
typedef struct IDwmSettingsManager IDwmSettingsManager;

/*
 * Private Win11 render-data stream builder.  These signatures and their
 * vtable order come from the public dwmcore PDB and the matching ARM64 image.
 * D2D value types remain opaque here so this private contract does not pull
 * Direct2D headers into every DWM consumer.
 */
typedef struct IRenderDataBuilderVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IRenderDataBuilder *, REFIID,
                                                void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IRenderDataBuilder *);
    ULONG (STDMETHODCALLTYPE *Release)(IRenderDataBuilder *);
    HRESULT (STDMETHODCALLTYPE *DrawBitmap)(IRenderDataBuilder *, UINT);
    HRESULT (STDMETHODCALLTYPE *DrawGeometry)(IRenderDataBuilder *, UINT,
                                              UINT);
    HRESULT (STDMETHODCALLTYPE *DrawImage)(IRenderDataBuilder *, const void *,
                                           UINT);
    HRESULT (STDMETHODCALLTYPE *DrawMesh2D)(IRenderDataBuilder *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *DrawRectangle)(IRenderDataBuilder *,
                                               const void *, UINT);
    HRESULT (STDMETHODCALLTYPE *DrawTileImage)(IRenderDataBuilder *, UINT,
                                               const void *, float,
                                               const void *);
    HRESULT (STDMETHODCALLTYPE *DrawVisual)(IRenderDataBuilder *, UINT);
    HRESULT (STDMETHODCALLTYPE *Pop)(IRenderDataBuilder *);
    HRESULT (STDMETHODCALLTYPE *PushTransform)(IRenderDataBuilder *, UINT);
    HRESULT (STDMETHODCALLTYPE *DrawSolidRectangle)(IRenderDataBuilder *,
                                                    const void *,
                                                    const void *);
} IRenderDataBuilderVtbl;

struct IRenderDataBuilder
{
    const IRenderDataBuilderVtbl *lpVtbl;
};

/*
 * Native Win11 IDwmChannelPrivate vtable.  The order and signatures below
 * are taken from the public dwmcore PDB and the corresponding ARM64 vtable.
 * Pointer-only private structures stay opaque on purpose.
 */
typedef struct IDwmChannelPrivateVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDwmChannelPrivate *, REFIID,
                                                void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDwmChannelPrivate *);
    ULONG (STDMETHODCALLTYPE *Release)(IDwmChannelPrivate *);
    HRESULT (STDMETHODCALLTYPE *Commit)(IDwmChannelPrivate *);
    HRESULT (STDMETHODCALLTYPE *SynchronizedCommit)(IDwmChannelPrivate *,
                                                    void *);
    BOOL (STDMETHODCALLTYPE *PeekNextMessage)(IDwmChannelPrivate *, void *);
    HRESULT (STDMETHODCALLTYPE *SyncFlush)(IDwmChannelPrivate *);
    HRESULT (STDMETHODCALLTYPE *WaitForNextMessage)(IDwmChannelPrivate *,
                                                    void *, UINT,
                                                    const HANDLE *, DWORD,
                                                    DWORD *);
    HRESULT (STDMETHODCALLTYPE *AddRefResource)(IDwmChannelPrivate *, UINT);
    HRESULT (STDMETHODCALLTYPE *CreateResource)(IDwmChannelPrivate *, UINT,
                                                UINT *);
    HRESULT (STDMETHODCALLTYPE *CreateSharedResource)(IDwmChannelPrivate *,
                                                      UINT, UINT *, HANDLE *);
    HRESULT (STDMETHODCALLTYPE *DuplicateSharedResource)(IDwmChannelPrivate *,
                                                         HANDLE, UINT, BOOL,
                                                         UINT *);
    HRESULT (STDMETHODCALLTYPE *ReleaseResource)(IDwmChannelPrivate *, UINT);
    HRESULT (STDMETHODCALLTYPE *CreateRenderDataBuilder)(IDwmChannelPrivate *,
                                                        IRenderDataBuilder **);
    HRESULT (STDMETHODCALLTYPE *QueryResourceInterface)(IDwmChannelPrivate *,
                                                       UINT, REFIID, void **);
    HRESULT (STDMETHODCALLTYPE *RoundTripRequest)(IDwmChannelPrivate *, UINT);
    HRESULT (STDMETHODCALLTYPE *AsyncFlush)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *PartitionRegisterForNotifications)(IDwmChannelPrivate *, BOOL);
    HRESULT (STDMETHODCALLTYPE *PartitionSetCurrentMmTask)(IDwmChannelPrivate *, const void *);
    HRESULT (STDMETHODCALLTYPE *PartitionSwitchRemotingMode)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *PartitionSetCursor)(IDwmChannelPrivate *, UINT, BOOL);
    HRESULT (STDMETHODCALLTYPE *PartitionSetMagnifier)(IDwmChannelPrivate *, UINT, BOOL);
    HRESULT (STDMETHODCALLTYPE *PartitionSetExcludeFromDDA)(IDwmChannelPrivate *, UINT, BOOL);
    HRESULT (STDMETHODCALLTYPE *PartitionToggleHolographicSuspension)(IDwmChannelPrivate *, BOOL);
    HRESULT (STDMETHODCALLTYPE *BitmapSource)(IDwmChannelPrivate *, UINT, void *);
    HRESULT (STDMETHODCALLTYPE *DoubleResourceUpdate)(IDwmChannelPrivate *, UINT, double);
    HRESULT (STDMETHODCALLTYPE *RectResourceUpdate)(IDwmChannelPrivate *, UINT, const void *);
    HRESULT (STDMETHODCALLTYPE *SizeResourceUpdate)(IDwmChannelPrivate *, UINT, const void *);
    HRESULT (STDMETHODCALLTYPE *ColorTransformResourceUpdate)(IDwmChannelPrivate *, UINT, const void *);
    HRESULT (STDMETHODCALLTYPE *RedirectVisualSetRedirectedVisual)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *RenderDataUpdate)(IDwmChannelPrivate *, UINT,
                                                  IRenderDataBuilder *);
    HRESULT (STDMETHODCALLTYPE *SyncLegacyVisualCaptureRenderTargetCaptureBits)(IDwmChannelPrivate *, UINT, UINT, float, INT, INT, INT, INT, ULONGLONG, UINT *, void **);
    HRESULT (STDMETHODCALLTYPE *VisualSetBlurredWallpaperSurface)(IDwmChannelPrivate *, UINT, UINT, const RECT *);
    HRESULT (STDMETHODCALLTYPE *VisualSetTouchTargetRect)(IDwmChannelPrivate *, UINT, const RECT *);
    HRESULT (STDMETHODCALLTYPE *VisualSetOptions)(IDwmChannelPrivate *, UINT, BOOL, BOOL, BOOL);
    HRESULT (STDMETHODCALLTYPE *VisualSetContent)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *VisualSetColorTransform)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *VisualTopLevelNode)(IDwmChannelPrivate *, UINT, HWND, BOOL);
    HRESULT (STDMETHODCALLTYPE *VisualSetPassiveUpdateMode)(IDwmChannelPrivate *, UINT, BOOL);
    HRESULT (STDMETHODCALLTYPE *VisualSetExcludeSubtree)(IDwmChannelPrivate *, UINT, BOOL);
    HRESULT (STDMETHODCALLTYPE *VisualTargetSetRoot)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *WindowNodeInitialize)(IDwmChannelPrivate *, UINT, HWND, HANDLE, ULONG, ULONGLONG);
    HRESULT (STDMETHODCALLTYPE *WindowNodeSetIsComposeOnce)(IDwmChannelPrivate *, UINT, BOOL);
    HRESULT (STDMETHODCALLTYPE *VisualGroupUpdate)(IDwmChannelPrivate *, UINT, const UINT *, UINT);
    HRESULT (STDMETHODCALLTYPE *RectangleGeometrySetRectangle)(IDwmChannelPrivate *, UINT, float, float, float, float, float, float, float, float, float, float, float, float, BOOL);
    HRESULT (STDMETHODCALLTYPE *RenderTargetSetRoot)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *SyncDesktopCaptureBits)(IDwmChannelPrivate *, ULONGLONG, INT, INT, UINT, UINT, UINT, ULONGLONG, HANDLE);
    HRESULT (STDMETHODCALLTYPE *SyncMagnifierRenderTargetCaptureBits)(IDwmChannelPrivate *, UINT, UINT, UINT, ULONGLONG, UINT, const void *);
    HRESULT (STDMETHODCALLTYPE *MagnifierRenderTargetCreate)(IDwmChannelPrivate *, UINT, ULONGLONG, const void *);
    HRESULT (STDMETHODCALLTYPE *MagnifierRenderTargetSetTransform)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *MagnifierRenderTargetSetColorTransform)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *MagnifierRenderTargetUpdate)(IDwmChannelPrivate *, UINT, const void *);
    HRESULT (STDMETHODCALLTYPE *MagnifierRenderTargetSetFilterList)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *SyncIndirectSwapchainRenderTargetCreate)(IDwmChannelPrivate *, UINT, HANDLE, ULONGLONG, UINT);
    HRESULT (STDMETHODCALLTYPE *IndirectSwapchainRenderTargetUpdateTargetBounds)(IDwmChannelPrivate *, UINT, UINT, UINT, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *IndirectSwapchainRenderTargetUnregister)(IDwmChannelPrivate *, UINT);
    HRESULT (STDMETHODCALLTYPE *BaseAnimationAddBinding)(IDwmChannelPrivate *, UINT, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *BaseAnimationRemoveBinding)(IDwmChannelPrivate *, UINT, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *AnimationUpdateBeginTime)(IDwmChannelPrivate *, UINT, ULONGLONG, ULONGLONG);
    HRESULT (STDMETHODCALLTYPE *AnimationUpdatePrimitives)(IDwmChannelPrivate *, UINT, const void *, UINT);
    HRESULT (STDMETHODCALLTYPE *AnimationSetTrigger)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *EffectGroupUpdate)(IDwmChannelPrivate *, UINT, double, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *CachedVisualImageUpdate)(IDwmChannelPrivate *, UINT, const void *, const void *, UINT, UINT, UINT, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *CachedVisualImageFreeze)(IDwmChannelPrivate *, UINT);
    HRESULT (STDMETHODCALLTYPE *CachedVisualImageSnapshot)(IDwmChannelPrivate *, UINT, const RECT *);
    HRESULT (STDMETHODCALLTYPE *AnimationTriggerTrigger)(IDwmChannelPrivate *, UINT, ULONGLONG);
    HRESULT (STDMETHODCALLTYPE *MeshGeometry2DUpdate)(IDwmChannelPrivate *, UINT, INT, const void *, const void *, UINT, const UINT *, UINT);
    HRESULT (STDMETHODCALLTYPE *Geometry2DGroupUpdate)(IDwmChannelPrivate *, UINT, const UINT *, UINT);
    HRESULT (STDMETHODCALLTYPE *AtlasedRectsMeshUpdate)(IDwmChannelPrivate *, UINT, BOOL, INT, const void *, const void *, UINT);
    HRESULT (STDMETHODCALLTYPE *AtlasedRectsMeshSetOpacity)(IDwmChannelPrivate *, UINT, INT);
    HRESULT (STDMETHODCALLTYPE *AtlasedRectsGroupUpdate)(IDwmChannelPrivate *, UINT, UINT, const UINT *, UINT);
    HRESULT (STDMETHODCALLTYPE *GaussianBlurEffectUpdate)(IDwmChannelPrivate *, UINT, float, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *MatrixTransform3DUpdate)(IDwmChannelPrivate *, UINT, const void *);
    HRESULT (STDMETHODCALLTYPE *Transform3DGroupUpdate)(IDwmChannelPrivate *, UINT, const UINT *, UINT);
    HRESULT (STDMETHODCALLTYPE *TransformGroupUpdate)(IDwmChannelPrivate *, UINT, const UINT *, UINT);
    HRESULT (STDMETHODCALLTYPE *TranslateTransformUpdate)(IDwmChannelPrivate *, UINT, double, double);
    HRESULT (STDMETHODCALLTYPE *ScaleTransformUpdate)(IDwmChannelPrivate *, UINT, double, double, double, double);
    HRESULT (STDMETHODCALLTYPE *RotateTransformUpdate)(IDwmChannelPrivate *, UINT, double, double, double);
    HRESULT (STDMETHODCALLTYPE *MatrixTransformUpdate)(IDwmChannelPrivate *, UINT, const void *);
    HRESULT (STDMETHODCALLTYPE *CombinedGeometryUpdate)(IDwmChannelPrivate *, UINT, UINT, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *RgnGeometryUpdate)(IDwmChannelPrivate *, UINT, const RECT *, UINT, INT, INT);
    HRESULT (STDMETHODCALLTYPE *SolidColorLegacyMilBrushUpdate)(IDwmChannelPrivate *, UINT, double, const void *);
    HRESULT (STDMETHODCALLTYPE *LinearGradientLegacyMilBrushUpdate)(IDwmChannelPrivate *, UINT, double, const void *, const void *, UINT, UINT, UINT, const void *, UINT);
    HRESULT (STDMETHODCALLTYPE *ImageLegacyMilBrushUpdate)(IDwmChannelPrivate *, UINT, double, const void *, const void *, UINT, UINT, UINT, UINT, UINT, UINT, UINT, UINT, UINT, UINT, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *HolographicInteropTextureSetRoot)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *VisualSetResampleMode)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *MagnifierRenderTargetSetResampleMode)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *CaptureControllerSetRootVisual)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *CaptureControllerSetCaptureState)(IDwmChannelPrivate *, UINT, BOOL);
    HRESULT (STDMETHODCALLTYPE *CaptureControllerSetContentSize)(IDwmChannelPrivate *, UINT, double, double);
    HRESULT (STDMETHODCALLTYPE *CaptureControllerSetTransform)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *CaptureControllerSetDefaultSDRBoost)(IDwmChannelPrivate *, UINT, float);
    HRESULT (STDMETHODCALLTYPE *CaptureControllerSetReferenceVisual)(IDwmChannelPrivate *, UINT, UINT);
    HRESULT (STDMETHODCALLTYPE *CaptureControllerSetSuspendOnScreenOff)(IDwmChannelPrivate *, UINT, BOOL);
    HRESULT (STDMETHODCALLTYPE *CursorVisualSetCursorId)(IDwmChannelPrivate *, UINT, ULONGLONG);
    HRESULT (STDMETHODCALLTYPE *CursorVisualSetIsHardwareCursorEnabled)(IDwmChannelPrivate *, UINT, BOOL);
    HRESULT (STDMETHODCALLTYPE *CursorVisualSetIsSynchronized)(IDwmChannelPrivate *, UINT, BOOL);
    HRESULT (STDMETHODCALLTYPE *CursorVisualSetPosition)(IDwmChannelPrivate *, UINT, INT, INT);
    HRESULT (STDMETHODCALLTYPE *CaptureControllerSetWindowInfos)(IDwmChannelPrivate *, UINT, const void *, UINT);
    HRESULT (STDMETHODCALLTYPE *CaptureControllerSetContentOffset)(IDwmChannelPrivate *, UINT, INT, INT);
    void (STDMETHODCALLTYPE *GetCommandBatch)(IDwmChannelPrivate *,
                                             void **, BOOL *);
    void (STDMETHODCALLTYPE *ReleaseCommandBatch)(IDwmChannelPrivate *);
    BOOL (STDMETHODCALLTYPE *IsRemoteTreeEnabled)(IDwmChannelPrivate *);
} IDwmChannelPrivateVtbl;

struct IDwmChannelPrivate
{
    const IDwmChannelPrivateVtbl *lpVtbl;
};

/* Native IDwmSettingsManager has no IUnknown prefix. */
typedef struct IDwmSettingsManagerVtbl
{
    HRESULT (STDMETHODCALLTYPE *GetPolicyDword)(IDwmSettingsManager *,
                                               const WCHAR *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *GetPreferenceDword)(IDwmSettingsManager *,
                                                   const WCHAR *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *SetPreferenceDword)(IDwmSettingsManager *,
                                                   const WCHAR *, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetThemesPersonalizeDword)(IDwmSettingsManager *,
                                                          const WCHAR *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *GetPreferenceFloat)(IDwmSettingsManager *,
                                                   const WCHAR *, float *);
    HRESULT (STDMETHODCALLTYPE *ReadRegistryDwords)(IDwmSettingsManager *,
                                                   UINT, void *, UINT);
    HRESULT (STDMETHODCALLTYPE *GetPreferenceString)(IDwmSettingsManager *,
                                                    const WCHAR *, UINT,
                                                    WCHAR *);
    BOOL (STDMETHODCALLTYPE *ReadOnlyMode)(IDwmSettingsManager *, UINT);
    BOOL (STDMETHODCALLTYPE *CheckPolicy)(IDwmSettingsManager *, DWORD);
    BOOL (STDMETHODCALLTYPE *CheckPreference)(IDwmSettingsManager *, DWORD);
} IDwmSettingsManagerVtbl;

struct IDwmSettingsManager
{
    const IDwmSettingsManagerVtbl *lpVtbl;
};

HRESULT CDECL MilCompositionEngine_CreateChannel(
    IDwmChannelProvider *Provider,
    IDwmChannelPrivate **Channel);
HRESULT CDECL MilCompositionEngine_CreateCursorController(
    ULONGLONG Identifier,
    IDwmCursorController **Controller);
HRESULT CDECL MilCompositionEngine_GetComposedEventId(UINT *EventId);
HRESULT CDECL MilCompositionEngine_Initialize(
    INT Flags,
    HMIL_CONNECTION **Connection);
HRESULT CDECL MilCompositionEngine_Uninitialize(HMIL_CONNECTION *Connection);

/* uDWM ordinal 101, imported by dwm.exe through the Window Manager API set. */
struct IUnknown;
HRESULT CDECL DwmClientStartup(struct IUnknown *ApplicationHost);

#ifdef __cplusplus
}
#endif
