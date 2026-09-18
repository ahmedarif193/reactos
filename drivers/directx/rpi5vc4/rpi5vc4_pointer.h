/*
 * PROJECT:     ReactOS Raspberry Pi 5 WDDM miniport
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Cursor composition for the fixed firmware scanout
 */

#pragma once

#define RPI5VC4_SOFTWARE_POINTER_SIZE 64

/* All access is serialized with fixed-framebuffer publication by HvsMutex. */
typedef struct _RPI5VC4_SOFTWARE_POINTER
{
    PUCHAR Scanout;
    ULONG Pitch;
    ULONG ScreenWidth;
    ULONG ScreenHeight;
    BOOLEAN Rotate90;
    LONG X;
    LONG Y;
    ULONG Width;
    ULONG Height;
    RECT SavedRect;
    BOOLEAN BackingValid;
    ULONG Pixels[RPI5VC4_SOFTWARE_POINTER_SIZE * RPI5VC4_SOFTWARE_POINTER_SIZE];
    ULONG Backing[RPI5VC4_SOFTWARE_POINTER_SIZE * RPI5VC4_SOFTWARE_POINTER_SIZE];
} RPI5VC4_SOFTWARE_POINTER, *PRPI5VC4_SOFTWARE_POINTER;

static __inline PULONG
Rpi5Vc4PointerPixel(PRPI5VC4_SOFTWARE_POINTER Pointer, LONG X, LONG Y)
{
    if (Pointer->Rotate90)
        return (PULONG)(Pointer->Scanout + (SIZE_T)X * Pointer->Pitch) +
               Pointer->ScreenHeight - 1 - Y;

    return (PULONG)(Pointer->Scanout + (SIZE_T)Y * Pointer->Pitch) + X;
}

static __inline ULONG
Rpi5Vc4PointerBlend(ULONG Background, ULONG Foreground)
{
    ULONG Alpha = Foreground >> 24;
    ULONG Inverse = 255 - Alpha;
    ULONG Red, Green, Blue;

    if (Alpha == 0)
        return Background;
    if (Alpha == 255)
        return Foreground;

    /* The color-pointer DDI supplies premultiplied ARGB, as for HVS. */
    Red = ((Foreground >> 16) & 255) + (((Background >> 16) & 255) * Inverse + 127) / 255;
    Green = ((Foreground >> 8) & 255) + (((Background >> 8) & 255) * Inverse + 127) / 255;
    Blue = (Foreground & 255) + ((Background & 255) * Inverse + 127) / 255;
    return 0xff000000 | (min(Red, 255) << 16) | (min(Green, 255) << 8) | min(Blue, 255);
}

static __inline VOID
Rpi5Vc4PointerRestore(PRPI5VC4_SOFTWARE_POINTER Pointer)
{
    LONG X, Y;

    if (Pointer == NULL || !Pointer->BackingValid)
        return;

    for (Y = Pointer->SavedRect.top; Y < Pointer->SavedRect.bottom; ++Y)
    {
        for (X = Pointer->SavedRect.left; X < Pointer->SavedRect.right; ++X)
        {
            *Rpi5Vc4PointerPixel(Pointer, X, Y) = Pointer->Backing[
                (Y - Pointer->SavedRect.top) * RPI5VC4_SOFTWARE_POINTER_SIZE +
                X - Pointer->SavedRect.left];
        }
    }
    Pointer->BackingValid = FALSE;
}

/* Restore before changing the shape, position or visibility and drawing again. */
static __inline VOID
Rpi5Vc4PointerDraw(PRPI5VC4_SOFTWARE_POINTER Pointer)
{
    LONG X, Y;
    RECT Rect;

    Rect.left = max(Pointer->X, 0);
    Rect.top = max(Pointer->Y, 0);
    Rect.right = (LONG)min((LONGLONG)Pointer->X + Pointer->Width, Pointer->ScreenWidth);
    Rect.bottom = (LONG)min((LONGLONG)Pointer->Y + Pointer->Height, Pointer->ScreenHeight);
    if (Rect.left >= Rect.right || Rect.top >= Rect.bottom)
        return;

    Pointer->SavedRect = Rect;
    for (Y = Rect.top; Y < Rect.bottom; ++Y)
    {
        for (X = Rect.left; X < Rect.right; ++X)
        {
            PULONG Pixel = Rpi5Vc4PointerPixel(Pointer, X, Y);
            ULONG Background = *Pixel;
            ULONG Foreground = Pointer->Pixels[
                (Y - Pointer->Y) * RPI5VC4_SOFTWARE_POINTER_SIZE + X - Pointer->X];

            Pointer->Backing[(Y - Rect.top) * RPI5VC4_SOFTWARE_POINTER_SIZE + X - Rect.left] = Background;
            *Pixel = Rpi5Vc4PointerBlend(Background, Foreground);
        }
    }
    Pointer->BackingValid = TRUE;
}

/* Compose while staging the new frame, retaining its cursor-free background. */
static __inline VOID
Rpi5Vc4PointerCopyRow(
    PRPI5VC4_SOFTWARE_POINTER Pointer,
    PULONG Destination,
    const ULONG *Source,
    LONG Y,
    LONG Left,
    LONG Right)
{
    LONG Start, End, X;

    if (Pointer == NULL || !Pointer->BackingValid ||
        Y < Pointer->SavedRect.top || Y >= Pointer->SavedRect.bottom ||
        Right <= Pointer->SavedRect.left || Left >= Pointer->SavedRect.right)
    {
        if (Destination != Source)
            RtlCopyMemory(Destination, Source, (SIZE_T)(Right - Left) * sizeof(ULONG));
        return;
    }

    Start = max(Left, Pointer->SavedRect.left);
    End = min(Right, Pointer->SavedRect.right);
    if (Destination != Source)
    {
        RtlCopyMemory(Destination, Source, (SIZE_T)(Start - Left) * sizeof(ULONG));
        RtlCopyMemory(Destination + End - Left, Source + End - Left,
                      (SIZE_T)(Right - End) * sizeof(ULONG));
    }
    for (X = Start; X < End; ++X)
    {
        ULONG Background = Source[X - Left];
        ULONG Foreground = Pointer->Pixels[
            (Y - Pointer->Y) * RPI5VC4_SOFTWARE_POINTER_SIZE + X - Pointer->X];

        Pointer->Backing[(Y - Pointer->SavedRect.top) * RPI5VC4_SOFTWARE_POINTER_SIZE +
                         X - Pointer->SavedRect.left] = Background;
        Destination[X - Left] = Rpi5Vc4PointerBlend(Background, Foreground);
    }
}
