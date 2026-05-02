/*
 * PROJECT:     ReactOS Kernel ARM64
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Bootvid stubs until bootvid is built for arm64
 */

#include <ntoskrnl.h>

BOOLEAN NTAPI VidInitialize(BOOLEAN SetMode) { (VOID)SetMode; return FALSE; }
VOID NTAPI VidCleanUp(VOID) { }
VOID NTAPI VidSolidColorFill(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom, UCHAR Color) { (VOID)Left; (VOID)Top; (VOID)Right; (VOID)Bottom; (VOID)Color; }
VOID NTAPI VidBitBlt(PUCHAR Buffer, ULONG X, ULONG Y) { (VOID)Buffer; (VOID)X; (VOID)Y; }
VOID NTAPI VidBufferToScreenBlt(PUCHAR Buffer, ULONG Left, ULONG Top, ULONG Width, ULONG Height, ULONG Stride) { (VOID)Buffer; (VOID)Left; (VOID)Top; (VOID)Width; (VOID)Height; (VOID)Stride; }
VOID NTAPI VidScreenToBufferBlt(PUCHAR Buffer, ULONG Left, ULONG Top, ULONG Width, ULONG Height, ULONG Stride) { (VOID)Buffer; (VOID)Left; (VOID)Top; (VOID)Width; (VOID)Height; (VOID)Stride; }
VOID NTAPI VidDisplayString(PCSTR String) { (VOID)String; }
VOID NTAPI VidDisplayStringXY(PCSTR String, ULONG Left, ULONG Top, BOOLEAN Transparent) { (VOID)String; (VOID)Left; (VOID)Top; (VOID)Transparent; }
VOID NTAPI VidResetDisplay(BOOLEAN SetMode) { (VOID)SetMode; }
VOID NTAPI VidSetScrollRegion(ULONG Left, ULONG Top, ULONG Right, ULONG Bottom) { (VOID)Left; (VOID)Top; (VOID)Right; (VOID)Bottom; }
ULONG NTAPI VidSetTextColor(ULONG Color) { (VOID)Color; return 0; }
