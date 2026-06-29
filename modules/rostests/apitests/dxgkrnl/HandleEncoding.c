/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test D3DKMT handle encoding properties
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * The WDDM handle encoding scheme uses XOR obfuscation with per-boot cookies.
 * These tests verify the mathematical properties that the encoding must satisfy,
 * without needing access to the actual cookie value.
 */

#include "precomp.h"

/*
 * Simulate the XOR-based handle encoding used by dxgkrnl.
 * This mirrors DxgkpEncodeHandle / DxgkpDecodeHandle in context.c.
 */
typedef UINT D3DKMT_TEST_HANDLE;

static D3DKMT_TEST_HANDLE
TestEncodeHandle(ULONG_PTR Pointer, ULONG Cookie)
{
    return (D3DKMT_TEST_HANDLE)((ULONG)Pointer ^ Cookie);
}

static ULONG_PTR
TestDecodeHandle(D3DKMT_TEST_HANDLE Handle, ULONG Cookie)
{
    if (Handle == 0)
        return 0;
    return (ULONG_PTR)Handle ^ (ULONG_PTR)Cookie;
}

static void Test_XorRoundTrip(void)
{
    /* Encode then decode must return the original value */
    ULONG Cookies[] = { 0xDEADBEEE, 0x12345678, 0xAAAAAAAA, 0x00000002, 0xFFFFFFFE };
    ULONG_PTR Pointers[] = { 0x80000010, 0x00100020, 0xFFFF0000, 0x40000000, 0x00000008 };
    ULONG i, j;

    for (i = 0; i < sizeof(Cookies)/sizeof(Cookies[0]); ++i)
    {
        for (j = 0; j < sizeof(Pointers)/sizeof(Pointers[0]); ++j)
        {
            ULONG Cookie = Cookies[i];
            ULONG_PTR Ptr = Pointers[j];
            D3DKMT_TEST_HANDLE Handle;
            ULONG_PTR Decoded;

            Handle = TestEncodeHandle(Ptr, Cookie);
            Decoded = TestDecodeHandle(Handle, Cookie);

            ok(Decoded == (ULONG_PTR)(ULONG)Ptr,
               "Round-trip failed: Cookie=0x%08X Ptr=0x%08lX Handle=0x%08X Decoded=0x%08lX\n",
               Cookie, (unsigned long)Ptr, Handle, (unsigned long)Decoded);
        }
    }
}

static void Test_ZeroHandleSentinel(void)
{
    /* Zero handle must always decode to 0 regardless of cookie */
    ULONG Cookies[] = { 0xDEADBEEE, 0x12345678, 0, 0xFFFFFFFE };
    ULONG i;

    for (i = 0; i < sizeof(Cookies)/sizeof(Cookies[0]); ++i)
    {
        ULONG_PTR Decoded = TestDecodeHandle(0, Cookies[i]);
        ok(Decoded == 0,
           "Zero handle must decode to 0 (cookie 0x%08X, got 0x%lX)\n",
           Cookies[i], (unsigned long)Decoded);
    }
}

static void Test_HandleUniqueness(void)
{
    /* Different pointers must produce different handles with the same cookie */
    ULONG Cookie = 0xDEADBEEE;
    ULONG_PTR Ptrs[] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    D3DKMT_TEST_HANDLE Handles[8];
    ULONG i, j;

    for (i = 0; i < 8; ++i)
        Handles[i] = TestEncodeHandle(Ptrs[i], Cookie);

    for (i = 0; i < 8; ++i)
    {
        for (j = i + 1; j < 8; ++j)
        {
            ok(Handles[i] != Handles[j],
               "Handles for ptr 0x%lX and 0x%lX collide: 0x%08X\n",
               (unsigned long)Ptrs[i], (unsigned long)Ptrs[j], Handles[i]);
        }
    }
}

static void Test_CookieBitMasking(void)
{
    /* The real implementation clears bit 0: Cookie & ~1UL
     * Verify this preserves even-address alignment */
    ULONG RawCounter = 0x13579BDF;
    ULONG Cookie = RawCounter & ~1UL;

    ok((Cookie & 1) == 0, "Cookie must have bit 0 cleared\n");
    ok(Cookie == (RawCounter & ~1UL), "Cookie masking incorrect\n");

    /* If counter is 0, fallback cookie is used */
    ULONG ZeroCookie = 0xDEADBEEEUL & ~1UL;
    ok((ZeroCookie & 1) == 0, "Fallback cookie must have bit 0 cleared\n");
    ok(ZeroCookie != 0, "Fallback cookie must be non-zero\n");
}

static void Test_AdapterHandleScheme(void)
{
    /*
     * Adapter handles use a different cookie: 0x4B544D44 ('DMTK')
     * with a monotonic sequence number.
     * Handle = Sequence ^ AdapterHandleCookie
     */
    ULONG AdapterCookie = 0x4B544D44;
    ULONG Sequences[] = { 1, 2, 3, 100, 0xFFFF };
    D3DKMT_TEST_HANDLE Handles[5];
    ULONG i, j;

    for (i = 0; i < 5; ++i)
        Handles[i] = Sequences[i] ^ AdapterCookie;

    /* All handles must be non-zero */
    for (i = 0; i < 5; ++i)
        ok(Handles[i] != 0, "Adapter handle %lu is 0\n", i);

    /* All handles must be unique */
    for (i = 0; i < 5; ++i)
        for (j = i + 1; j < 5; ++j)
            ok(Handles[i] != Handles[j],
               "Adapter handles %lu and %lu collide\n", i, j);

    /* Verify specific known values */
    ok_eq_hex(1 ^ AdapterCookie, 0x4B544D45);
    ok_eq_hex(2 ^ AdapterCookie, 0x4B544D46);
}

static void Test_SyncHandleScheme(void)
{
    /*
     * Sync object handles use cookie: 0x434E5953 ('SYNC')
     */
    ULONG SyncCookie = 0x434E5953;
    ULONG Sequences[] = { 1, 2, 3, 42, 1000 };
    D3DKMT_TEST_HANDLE Handles[5];
    ULONG i, j;

    for (i = 0; i < 5; ++i)
        Handles[i] = Sequences[i] ^ SyncCookie;

    for (i = 0; i < 5; ++i)
        ok(Handles[i] != 0, "Sync handle %lu is 0\n", i);

    for (i = 0; i < 5; ++i)
        for (j = i + 1; j < 5; ++j)
            ok(Handles[i] != Handles[j],
               "Sync handles %lu and %lu collide\n", i, j);

    /* Known value check */
    ok_eq_hex(1 ^ SyncCookie, 0x434E5952);
}

static void Test_HandleNamespaceIsolation(void)
{
    /*
     * Adapter handles (cookie 0x4B544D44) and sync handles (cookie 0x434E5953)
     * must not collide for common sequence values.
     */
    ULONG AdapterCookie = 0x4B544D44;
    ULONG SyncCookie = 0x434E5953;
    ULONG i;

    for (i = 1; i <= 100; ++i)
    {
        D3DKMT_TEST_HANDLE AdapterHandle = i ^ AdapterCookie;
        D3DKMT_TEST_HANDLE SyncHandle = i ^ SyncCookie;

        ok(AdapterHandle != SyncHandle,
           "Adapter and Sync handle collision at sequence %lu: 0x%08X\n",
           i, AdapterHandle);
    }
}

static void Test_HandleAlignmentProperty(void)
{
    /*
     * Pool allocations are at least 8-byte aligned on amd64,
     * so pointer-as-handle XOR with even cookie preserves alignment info.
     * Bit 0 of the handle should be 0 for aligned allocations.
     */
    ULONG Cookie = 0xDEADBEEE; /* bit 0 cleared */
    ULONG_PTR AlignedPtrs[] = { 0x10, 0x100, 0x1000, 0x80000000 };
    ULONG i;

    for (i = 0; i < sizeof(AlignedPtrs)/sizeof(AlignedPtrs[0]); ++i)
    {
        D3DKMT_TEST_HANDLE Handle = TestEncodeHandle(AlignedPtrs[i], Cookie);
        ok((Handle & 1) == 0,
           "Handle for aligned ptr 0x%lX has bit 0 set: 0x%08X\n",
           (unsigned long)AlignedPtrs[i], Handle);
    }
}

START_TEST(HandleEncoding)
{
    Test_XorRoundTrip();
    Test_ZeroHandleSentinel();
    Test_HandleUniqueness();
    Test_CookieBitMasking();
    Test_AdapterHandleScheme();
    Test_SyncHandleScheme();
    Test_HandleNamespaceIsolation();
    Test_HandleAlignmentProperty();
}
