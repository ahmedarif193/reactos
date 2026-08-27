/*
 * PROJECT:     ReactOS WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Software-emulated WDDM color-pointer plane
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include "softgpu.h"

/* PRIVATE FUNCTIONS *********************************************************/

static ULONG
SoftGpuPointerBlendPixel(
    _In_ ULONG Destination,
    _In_ ULONG Source)
{
    ULONG Alpha = Source >> 24;
    ULONG InverseAlpha;
    ULONG Red;
    ULONG Green;
    ULONG Blue;

    if (Alpha == 0)
        return Destination;
    if (Alpha == 0xFF)
        return 0xFF000000 | (Source & 0x00FFFFFF);

    InverseAlpha = 0xFF - Alpha;
    Red = ((((Source >> 16) & 0xFF) * Alpha) +
           (((Destination >> 16) & 0xFF) * InverseAlpha) + 0x7F) / 0xFF;
    Green = ((((Source >> 8) & 0xFF) * Alpha) +
             (((Destination >> 8) & 0xFF) * InverseAlpha) + 0x7F) / 0xFF;
    Blue = (((Source & 0xFF) * Alpha) +
            ((Destination & 0xFF) * InverseAlpha) + 0x7F) / 0xFF;
    return 0xFF000000 | (Red << 16) | (Green << 8) | Blue;
}

static BOOLEAN
SoftGpuPointerClip(
    _In_ PSOFTGPU_DEVICE Device,
    _Out_ PRECT Clipped,
    _Out_ PULONG SourceX,
    _Out_ PULONG SourceY)
{
    LONGLONG Left = (LONGLONG)Device->PointerX - Device->PointerHotX;
    LONGLONG Top = (LONGLONG)Device->PointerY - Device->PointerHotY;
    LONGLONG Right = Left + Device->PointerWidth;
    LONGLONG Bottom = Top + Device->PointerHeight;

    *SourceX = 0;
    *SourceY = 0;
    if (Left < 0)
    {
        *SourceX = (ULONG)-Left;
        Left = 0;
    }
    if (Top < 0)
    {
        *SourceY = (ULONG)-Top;
        Top = 0;
    }
    if (Right > (LONG)Device->Width)
        Right = (LONG)Device->Width;
    if (Bottom > (LONG)Device->Height)
        Bottom = (LONG)Device->Height;
    if (Left >= Right || Top >= Bottom)
        return FALSE;

    Clipped->left = (LONG)Left;
    Clipped->top = (LONG)Top;
    Clipped->right = (LONG)Right;
    Clipped->bottom = (LONG)Bottom;
    return TRUE;
}

/* PUBLIC FUNCTIONS **********************************************************/

VOID
SoftGpuPointerRestoreLocked(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    ULONG Width;
    ULONG Height;
    ULONG X;
    ULONG Y;

    if (!Device->PointerBackingValid)
        return;

    Width = (ULONG)(Device->PointerSavedRect.right - Device->PointerSavedRect.left);
    Height = (ULONG)(Device->PointerSavedRect.bottom - Device->PointerSavedRect.top);
    if (Device->Scanout != NULL && Width <= SOFTGPU_POINTER_MAX_WIDTH &&
        Height <= SOFTGPU_POINTER_MAX_HEIGHT)
    {
        for (Y = 0; Y < Height; ++Y)
        {
            PULONG Destination = (PULONG)((PUCHAR)Device->Scanout +
                ((SIZE_T)(Device->PointerSavedRect.top + (LONG)Y) * Device->ScanoutPitch) +
                ((SIZE_T)Device->PointerSavedRect.left * sizeof(ULONG)));

            for (X = 0; X < Width; ++X)
                Destination[X] = Device->PointerBacking[Y * SOFTGPU_POINTER_MAX_WIDTH + X];
        }
    }
    Device->PointerBackingValid = FALSE;
}

VOID
SoftGpuPointerDrawLocked(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    RECT Clipped;
    ULONG SourceX;
    ULONG SourceY;
    ULONG Width;
    ULONG Height;
    ULONG X;
    ULONG Y;

    if (Device->Scanout == NULL || !Device->ScanoutVisible ||
        !Device->TimingActive || !Device->PointerShapeValid ||
        !Device->PointerVisible ||
        Device->ScanoutPitch == 0 ||
        Device->ScanoutPitch < Device->Width * sizeof(ULONG) ||
        Device->Height > Device->ScanoutSize / Device->ScanoutPitch ||
        !SoftGpuPointerClip(Device, &Clipped, &SourceX, &SourceY))
    {
        return;
    }

    Width = (ULONG)(Clipped.right - Clipped.left);
    Height = (ULONG)(Clipped.bottom - Clipped.top);
    Device->PointerSavedRect = Clipped;
    for (Y = 0; Y < Height; ++Y)
    {
        PULONG Destination = (PULONG)((PUCHAR)Device->Scanout +
            ((SIZE_T)(Clipped.top + (LONG)Y) * Device->ScanoutPitch) +
            ((SIZE_T)Clipped.left * sizeof(ULONG)));
        PULONG Source = &Device->PointerPixels[(SourceY + Y) * SOFTGPU_POINTER_MAX_WIDTH + SourceX];

        for (X = 0; X < Width; ++X)
        {
            Device->PointerBacking[Y * SOFTGPU_POINTER_MAX_WIDTH + X] = Destination[X];
            Destination[X] = SoftGpuPointerBlendPixel(Destination[X], Source[X]);
        }
    }
    Device->PointerBackingValid = TRUE;
}

NTSTATUS
APIENTRY
SoftGpuDdiSetPointerPosition(
    _In_ PVOID MiniportDeviceContext,
    _In_ const DXGKARG_SETPOINTERPOSITION *SetPointerPosition)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    NTSTATUS Status;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        SetPointerPosition == NULL || SetPointerPosition->VidPnSourceId != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (SetPointerPosition->Flags.Procedural || SetPointerPosition->Flags.Reserved)
        return STATUS_INVALID_PARAMETER;
    if (!ExAcquireRundownProtection(&Device->ScanoutRundown))
        return STATUS_DELETE_PENDING;

    Status = KeWaitForSingleObject(&Device->ScanoutMutex, Executive, KernelMode, FALSE, NULL);
    if (!NT_SUCCESS(Status))
        goto CleanupRundown;
    if (InterlockedCompareExchange(&Device->Stopped, 0, 0) != 0)
    {
        Status = STATUS_DELETE_PENDING;
        goto CleanupMutex;
    }

    SoftGpuPointerRestoreLocked(Device);
    Device->PointerX = SetPointerPosition->X;
    Device->PointerY = SetPointerPosition->Y;
    Device->PointerVisible = SetPointerPosition->Flags.Visible ? TRUE : FALSE;
    SoftGpuPointerDrawLocked(Device);
    KeMemoryBarrier();
    Status = STATUS_SUCCESS;

CleanupMutex:
    KeReleaseMutex(&Device->ScanoutMutex, FALSE);
CleanupRundown:
    ExReleaseRundownProtection(&Device->ScanoutRundown);
    return Status;
}

NTSTATUS
APIENTRY
SoftGpuDdiSetPointerShape(
    _In_ PVOID MiniportDeviceContext,
    _In_ const DXGKARG_SETPOINTERSHAPE *SetPointerShape)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    const UCHAR *Pixels;
    ULONG Y;
    NTSTATUS Status;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        SetPointerShape == NULL || SetPointerShape->VidPnSourceId != 0 ||
        SetPointerShape->Flags.Value != 2 || SetPointerShape->pPixels == NULL ||
        SetPointerShape->Width == 0 || SetPointerShape->Height == 0 ||
        SetPointerShape->Width > SOFTGPU_POINTER_MAX_WIDTH ||
        SetPointerShape->Height > SOFTGPU_POINTER_MAX_HEIGHT ||
        SetPointerShape->Pitch < SetPointerShape->Width * sizeof(ULONG) ||
        SetPointerShape->XHot >= SetPointerShape->Width ||
        SetPointerShape->YHot >= SetPointerShape->Height)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Pixels = (const UCHAR *)SetPointerShape->pPixels;
    if (!ExAcquireRundownProtection(&Device->ScanoutRundown))
        return STATUS_DELETE_PENDING;

    Status = KeWaitForSingleObject(&Device->ScanoutMutex, Executive, KernelMode, FALSE, NULL);
    if (!NT_SUCCESS(Status))
        goto CleanupRundown;
    if (InterlockedCompareExchange(&Device->Stopped, 0, 0) != 0)
    {
        Status = STATUS_DELETE_PENDING;
        goto CleanupMutex;
    }

    SoftGpuPointerRestoreLocked(Device);
    RtlZeroMemory(Device->PointerPixels, sizeof(Device->PointerPixels));
    for (Y = 0; Y < SetPointerShape->Height; ++Y)
    {
        RtlCopyMemory(&Device->PointerPixels[Y * SOFTGPU_POINTER_MAX_WIDTH],
                      Pixels + ((SIZE_T)Y * SetPointerShape->Pitch),
                      SetPointerShape->Width * sizeof(ULONG));
    }
    Device->PointerWidth = SetPointerShape->Width;
    Device->PointerHeight = SetPointerShape->Height;
    Device->PointerHotX = SetPointerShape->XHot;
    Device->PointerHotY = SetPointerShape->YHot;
    Device->PointerShapeValid = TRUE;
    SoftGpuPointerDrawLocked(Device);
    KeMemoryBarrier();
    Status = STATUS_SUCCESS;

CleanupMutex:
    KeReleaseMutex(&Device->ScanoutMutex, FALSE);
CleanupRundown:
    ExReleaseRundownProtection(&Device->ScanoutRundown);
    return Status;
}
