/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     HID-I2C transfer test launcher
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "Hidi2cTest.h"

START_TEST(Hidi2cTransfer)
{
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"Hidi2cTransfer", FALSE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
        return;

    Error = KmtSendToDriver(IOCTL_TEST_HIDI2C_TRANSFER);
    ok_eq_int(Error, ERROR_SUCCESS);

    KmtCloseDriver();
    KmtUnloadDriver();
}
