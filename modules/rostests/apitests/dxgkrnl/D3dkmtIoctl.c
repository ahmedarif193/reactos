/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Validate D3DKMT IOCTL code constants
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * These tests verify that the CTL_CODE macros produce the expected IOCTL
 * values.  This catches accidental changes to device type, function codes,
 * or method/access flags that would break the user-kernel IOCTL contract.
 */

#include "precomp.h"

/*
 * CTL_CODE(DeviceType, Function, Method, Access):
 *   bits [31:16] = DeviceType
 *   bits [15:14] = Access
 *   bits [13:2]  = Function
 *   bits [1:0]   = Method
 *
 * DXGKRNL_DEVICE_TYPE = 0x23
 * METHOD_BUFFERED     = 0
 * FILE_ANY_ACCESS     = 0
 */

static void Test_IoctlCodes(void)
{
    /* Verify device type field (bits 31:16) for all IOCTLs */
    ok_eq_hex((IOCTL_D3DKMT_ENUMADAPTERS >> 16) & 0xFFFF, 0x0023);
    ok_eq_hex((IOCTL_D3DKMT_CREATEDEVICE >> 16) & 0xFFFF, 0x0023);
    ok_eq_hex((IOCTL_D3DKMT_PRESENT >> 16) & 0xFFFF, 0x0023);

    /* Verify method field (bits 1:0) = METHOD_BUFFERED = 0 */
    ok_eq_uint(IOCTL_D3DKMT_ENUMADAPTERS & 0x3, 0);
    ok_eq_uint(IOCTL_D3DKMT_CREATEDEVICE & 0x3, 0);
    ok_eq_uint(IOCTL_D3DKMT_PRESENT & 0x3, 0);
    ok_eq_uint(IOCTL_D3DKMT_LOCK & 0x3, 0);
    ok_eq_uint(IOCTL_D3DKMT_RENDER & 0x3, 0);

    /* Verify access field (bits 15:14) = FILE_ANY_ACCESS = 0 */
    ok_eq_uint((IOCTL_D3DKMT_ENUMADAPTERS >> 14) & 0x3, 0);
    ok_eq_uint((IOCTL_D3DKMT_CREATEDEVICE >> 14) & 0x3, 0);

    /* Verify specific function codes (bits 13:2) */
    ok_eq_uint((IOCTL_D3DKMT_ENUMADAPTERS >> 2) & 0xFFF, 0x100);
    ok_eq_uint((IOCTL_D3DKMT_ENUMADAPTERS2 >> 2) & 0xFFF, 0x101);
    ok_eq_uint((IOCTL_D3DKMT_OPENADAPTERFROMLUID >> 2) & 0xFFF, 0x102);
    ok_eq_uint((IOCTL_D3DKMT_CLOSEADAPTER >> 2) & 0xFFF, 0x103);
    ok_eq_uint((IOCTL_D3DKMT_QUERYADAPTERINFO >> 2) & 0xFFF, 0x104);
    ok_eq_uint((IOCTL_D3DKMT_GETDISPLAYMODELIST >> 2) & 0xFFF, 0x105);

    ok_eq_uint((IOCTL_D3DKMT_OPENADAPTERFROMHDC >> 2) & 0xFFF, 0x110);
    ok_eq_uint((IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME >> 2) & 0xFFF, 0x111);
    ok_eq_uint((IOCTL_D3DKMT_OPENADAPTERFROMDEVICENAME >> 2) & 0xFFF, 0x112);

    ok_eq_uint((IOCTL_D3DKMT_CREATEDEVICE >> 2) & 0xFFF, 0x120);
    ok_eq_uint((IOCTL_D3DKMT_DESTROYDEVICE >> 2) & 0xFFF, 0x121);

    ok_eq_uint((IOCTL_D3DKMT_CREATEALLOCATION >> 2) & 0xFFF, 0x130);
    ok_eq_uint((IOCTL_D3DKMT_DESTROYALLOCATION >> 2) & 0xFFF, 0x131);
    ok_eq_uint((IOCTL_D3DKMT_LOCK >> 2) & 0xFFF, 0x132);
    ok_eq_uint((IOCTL_D3DKMT_UNLOCK >> 2) & 0xFFF, 0x133);

    ok_eq_uint((IOCTL_D3DKMT_RENDER >> 2) & 0xFFF, 0x140);
    ok_eq_uint((IOCTL_D3DKMT_PRESENT >> 2) & 0xFFF, 0x141);

    ok_eq_uint((IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT >> 2) & 0xFFF, 0x150);
    ok_eq_uint((IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT >> 2) & 0xFFF, 0x151);

    ok_eq_uint((IOCTL_D3DKMT_SETDISPLAYMODE >> 2) & 0xFFF, 0x160);

    /* New IOCTLs added for callback exchange protocol */
    ok_eq_uint((IOCTL_D3DKMT_CREATECONTEXT >> 2) & 0xFFF, 0x122);
    ok_eq_uint((IOCTL_D3DKMT_DESTROYCONTEXT >> 2) & 0xFFF, 0x123);
    ok_eq_uint((IOCTL_D3DKMT_GETDEVICESTATE >> 2) & 0xFFF, 0x124);
    ok_eq_uint((IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT >> 2) & 0xFFF, 0x152);
    ok_eq_uint((IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT >> 2) & 0xFFF, 0x153);
    ok_eq_uint((IOCTL_D3DKMT_SETVIDPNSOURCEOWNER >> 2) & 0xFFF, 0x161);
    ok_eq_uint((IOCTL_D3DKMT_ESCAPE >> 2) & 0xFFF, 0x170);
    ok_eq_uint((IOCTL_DXGKRNL_EXCHANGE_INTERFACE >> 2) & 0xFFF, 0x200);
}

static void Test_IoctlUniqueness(void)
{
    /* All IOCTL codes must be unique -- no two can collide */
    DWORD Codes[] = {
        IOCTL_D3DKMT_ENUMADAPTERS,
        IOCTL_D3DKMT_ENUMADAPTERS2,
        IOCTL_D3DKMT_OPENADAPTERFROMLUID,
        IOCTL_D3DKMT_CLOSEADAPTER,
        IOCTL_D3DKMT_QUERYADAPTERINFO,
        IOCTL_D3DKMT_GETDISPLAYMODELIST,
        IOCTL_D3DKMT_OPENADAPTERFROMHDC,
        IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME,
        IOCTL_D3DKMT_OPENADAPTERFROMDEVICENAME,
        IOCTL_D3DKMT_CREATEDEVICE,
        IOCTL_D3DKMT_DESTROYDEVICE,
        IOCTL_D3DKMT_CREATEALLOCATION,
        IOCTL_D3DKMT_DESTROYALLOCATION,
        IOCTL_D3DKMT_LOCK,
        IOCTL_D3DKMT_UNLOCK,
        IOCTL_D3DKMT_RENDER,
        IOCTL_D3DKMT_PRESENT,
        IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT,
        IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT,
        IOCTL_D3DKMT_SETDISPLAYMODE,
        IOCTL_D3DKMT_CREATECONTEXT,
        IOCTL_D3DKMT_DESTROYCONTEXT,
        IOCTL_D3DKMT_GETDEVICESTATE,
        IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT,
        IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT,
        IOCTL_D3DKMT_SETVIDPNSOURCEOWNER,
        IOCTL_D3DKMT_ESCAPE,
        IOCTL_DXGKRNL_EXCHANGE_INTERFACE,
    };
    ULONG Count = sizeof(Codes) / sizeof(Codes[0]);
    ULONG i, j;

    for (i = 0; i < Count; ++i)
    {
        for (j = i + 1; j < Count; ++j)
        {
            ok(Codes[i] != Codes[j],
               "IOCTL codes at index %lu and %lu collide: 0x%08lX\n",
               i, j, Codes[i]);
        }
    }
}

static void Test_IoctlFunctionCodeRanges(void)
{
    /*
     * Verify that function codes start at 0x100 (to avoid collision with
     * the IOCTL_VIDEO_* range which uses 0x00-0xFF).
     */
    ok(((IOCTL_D3DKMT_ENUMADAPTERS >> 2) & 0xFFF) >= 0x100,
       "D3DKMT IOCTL function codes should start at 0x100 or above\n");

    /* Adapter ops: 0x100-0x112 range */
    ok(((IOCTL_D3DKMT_ENUMADAPTERS >> 2) & 0xFFF) >= 0x100 &&
       ((IOCTL_D3DKMT_ENUMADAPTERS >> 2) & 0xFFF) <= 0x112,
       "EnumAdapters function code out of expected range\n");

    /* Device ops: 0x120-0x121 range */
    ok(((IOCTL_D3DKMT_CREATEDEVICE >> 2) & 0xFFF) >= 0x120 &&
       ((IOCTL_D3DKMT_CREATEDEVICE >> 2) & 0xFFF) <= 0x121,
       "CreateDevice function code out of expected range\n");

    /* Allocation ops: 0x130-0x133 range */
    ok(((IOCTL_D3DKMT_CREATEALLOCATION >> 2) & 0xFFF) >= 0x130 &&
       ((IOCTL_D3DKMT_CREATEALLOCATION >> 2) & 0xFFF) <= 0x133,
       "CreateAllocation function code out of expected range\n");

    /* DMA/Present ops: 0x140-0x141 range */
    ok(((IOCTL_D3DKMT_RENDER >> 2) & 0xFFF) >= 0x140 &&
       ((IOCTL_D3DKMT_RENDER >> 2) & 0xFFF) <= 0x141,
       "Render function code out of expected range\n");

    /* Sync ops: 0x150-0x151 range */
    ok(((IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT >> 2) & 0xFFF) >= 0x150 &&
       ((IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT >> 2) & 0xFFF) <= 0x151,
       "WaitForSyncObject function code out of expected range\n");

    /* Display ops: 0x160 */
    ok_eq_uint((IOCTL_D3DKMT_SETDISPLAYMODE >> 2) & 0xFFF, 0x160);
}

START_TEST(D3dkmtIoctl)
{
    Test_IoctlCodes();
    Test_IoctlUniqueness();
    Test_IoctlFunctionCodeRanges();
}
