/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     User-mode entry point for ramdisk IOCTL smoke test
 */

#include <kmt_test.h>

START_TEST(RamdiskIoctl)
{
    KmtRunKernelTest("RamdiskIoctl");
}
