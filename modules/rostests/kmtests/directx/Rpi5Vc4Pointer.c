/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Fixed-scanout cursor lifetime, damage and rotation regressions
 */

#include <kmt_test.h>
#include <windef.h>
#include "../../../../drivers/directx/rpi5vc4/rpi5vc4_pointer.h"

#define TEST_WIDTH 79
#define TEST_HEIGHT 73
#define TEST_STRIDE 84
#define TEST_GUARD 0xa55a5aa5

typedef struct _POINTER_TEST
{
    RPI5VC4_SOFTWARE_POINTER Pointer;
    ULONG Scanout[TEST_STRIDE * TEST_WIDTH + 2];
    ULONG Background[TEST_WIDTH * TEST_HEIGHT];
    ULONG Stage[TEST_WIDTH];
} POINTER_TEST;

static VOID
PointerTestCheck(POINTER_TEST *Test, BOOLEAN Visible)
{
    PRPI5VC4_SOFTWARE_POINTER Pointer = &Test->Pointer;
    ULONG Errors = 0;
    ULONG X, Y;

    for (Y = 0; Y < TEST_HEIGHT; ++Y)
    {
        for (X = 0; X < TEST_WIDTH; ++X)
        {
            LONGLONG Sx = (LONGLONG)X - Pointer->X;
            LONGLONG Sy = (LONGLONG)Y - Pointer->Y;
            ULONG Expected = Test->Background[Y * TEST_WIDTH + X];
            ULONG Offset = Pointer->Rotate90 ? X * TEST_STRIDE + TEST_HEIGHT - 1 - Y :
                                              Y * TEST_STRIDE + X;

            if (Visible && Sx >= 0 && Sy >= 0 && Sx < Pointer->Width && Sy < Pointer->Height)
            {
                switch ((Sx + Sy) % 3)
                {
                    case 0: break; /* Transparent: retain the exact background. */
                    case 1: Expected = 0xffffffff; break;
                    case 2: Expected = Expected == 0xff204060 ? 0xff902030 : 0xffb04050; break;
                }
            }
            if (Test->Scanout[1 + Offset] != Expected)
                ++Errors;
        }
    }
    ok_eq_ulong(Errors, 0);
    for (Y = 0; Y < (Pointer->Rotate90 ? TEST_WIDTH : TEST_HEIGHT); ++Y)
    {
        for (X = Pointer->Rotate90 ? TEST_HEIGHT : TEST_WIDTH; X < TEST_STRIDE; ++X)
            ok_eq_ulong(Test->Scanout[1 + Y * TEST_STRIDE + X], TEST_GUARD);
    }
    ok_eq_ulong(Test->Scanout[0], TEST_GUARD);
    ok_eq_ulong(Test->Scanout[TEST_STRIDE * TEST_WIDTH + 1], TEST_GUARD);
}

static VOID
PointerTestShape(POINTER_TEST *Test, ULONG Width, ULONG Height)
{
    ULONG X, Y;

    Rpi5Vc4PointerRestore(&Test->Pointer);
    Test->Pointer.Width = Width;
    Test->Pointer.Height = Height;
    for (Y = 0; Y < Height; ++Y)
    {
        for (X = 0; X < Width; ++X)
        {
            ULONG Pixel = (X + Y) % 3;
            Test->Pointer.Pixels[Y * RPI5VC4_SOFTWARE_POINTER_SIZE + X] =
                Pixel == 0 ? 0 : Pixel == 1 ? 0xffffffff : 0x80800000;
        }
    }
    Rpi5Vc4PointerDraw(&Test->Pointer);
}

static VOID
PointerTestPresent(POINTER_TEST *Test, RECT Rect, ULONG Color)
{
    LONG X, Y;

    for (Y = Rect.top; Y < Rect.bottom; ++Y)
    {
        for (X = Rect.left; X < Rect.right; ++X)
            Test->Background[Y * TEST_WIDTH + X] = Color;

        /* Both an in-place rotation tile and a separate copy stage are used. */
        if (Test->Pointer.Rotate90)
        {
            RtlCopyMemory(Test->Stage, Test->Background + Y * TEST_WIDTH + Rect.left,
                          (Rect.right - Rect.left) * sizeof(ULONG));
            Rpi5Vc4PointerCopyRow(&Test->Pointer, Test->Stage, Test->Stage,
                                 Y, Rect.left, Rect.right);
        }
        else
        {
            Rpi5Vc4PointerCopyRow(&Test->Pointer, Test->Stage,
                                 Test->Background + Y * TEST_WIDTH + Rect.left,
                                 Y, Rect.left, Rect.right);
        }
        for (X = Rect.left; X < Rect.right; ++X)
        {
            ULONG Offset = Test->Pointer.Rotate90 ? X * TEST_STRIDE + TEST_HEIGHT - 1 - Y :
                                                   Y * TEST_STRIDE + X;
            Test->Scanout[1 + Offset] = Test->Stage[X - Rect.left];
        }
    }
}

static VOID
PointerTestRun(POINTER_TEST *Test, BOOLEAN Rotate90)
{
    static const POINT Positions[] = {
        {17, 19}, {-4, -6}, {71, 64}, {78, 72}, {79, 73},
        {(-2147483647 - 1), 0}, {2147483647, 2147483647}, {5, 7}
    };
    RECT Full = {0, 0, TEST_WIDTH, TEST_HEIGHT};
    RECT Damage = {20, 23, 36, 31};
    ULONG Index;

    RtlZeroMemory(Test, sizeof(*Test));
    for (Index = 0; Index < RTL_NUMBER_OF(Test->Scanout); ++Index)
        Test->Scanout[Index] = TEST_GUARD;
    Test->Pointer.Scanout = (PUCHAR)&Test->Scanout[1];
    Test->Pointer.Pitch = TEST_STRIDE * sizeof(ULONG);
    Test->Pointer.ScreenWidth = TEST_WIDTH;
    Test->Pointer.ScreenHeight = TEST_HEIGHT;
    Test->Pointer.Rotate90 = Rotate90;
    PointerTestPresent(Test, Full, 0xff204060);
    Test->Pointer.X = 17;
    Test->Pointer.Y = 19;
    PointerTestShape(Test, 32, 24);
    PointerTestCheck(Test, TRUE);

    /* No pointer events while 120 new desktop frames replace the background. */
    for (Index = 0; Index < 120; ++Index)
    {
        PointerTestPresent(Test, Full, Index & 1 ? 0xff204060 : 0xff6080a0);
        PointerTestCheck(Test, TRUE);
    }
    PointerTestPresent(Test, Damage, 0xff6080a0);
    PointerTestCheck(Test, TRUE);
    Rpi5Vc4PointerRestore(&Test->Pointer);
    PointerTestCheck(Test, FALSE);

    /* Animated wait cursor shapes must restore the latest, partly damaged frame. */
    for (Index = 0; Index < 18; ++Index)
    {
        PointerTestShape(Test, Index & 1 ? 64 : 24, Index & 1 ? 64 : 32);
        PointerTestCheck(Test, TRUE);
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Positions); ++Index)
    {
        Rpi5Vc4PointerRestore(&Test->Pointer);
        Test->Pointer.X = Positions[Index].x;
        Test->Pointer.Y = Positions[Index].y;
        Rpi5Vc4PointerDraw(&Test->Pointer);
        PointerTestCheck(Test, TRUE);
        PointerTestPresent(Test, Full, 0xff6080a0);
        PointerTestCheck(Test, TRUE);
    }
    Rpi5Vc4PointerRestore(&Test->Pointer);
    PointerTestCheck(Test, FALSE);
    PointerTestPresent(Test, Full, 0xff204060);
    PointerTestCheck(Test, FALSE);
    Rpi5Vc4PointerDraw(&Test->Pointer);
    PointerTestCheck(Test, TRUE);
    Rpi5Vc4PointerRestore(&Test->Pointer);
    Rpi5Vc4PointerRestore(&Test->Pointer);
    PointerTestCheck(Test, FALSE);
}

START_TEST(Rpi5Vc4Pointer)
{
    POINTER_TEST *Test = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Test), 'tPcV');

    if (Test == NULL)
    {
        skip(FALSE, "Cannot allocate cursor test surfaces\n");
        return;
    }
    PointerTestRun(Test, FALSE);
    PointerTestRun(Test, TRUE);
    ExFreePoolWithTag(Test, 'tPcV');
}
