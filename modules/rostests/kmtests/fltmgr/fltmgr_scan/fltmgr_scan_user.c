/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filter Manager data scan section test, user-mode part
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#include "fltmgr_scan.h"

START_TEST(FltMgrScan)
{
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"FltMgrScan", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
    {
        return;
    }

    Error = KmtSendToDriver(IOCTL_FLTSCAN_REGISTER);
    ok_eq_int(Error, ERROR_SUCCESS);
    Error = KmtSendToDriver(IOCTL_FLTSCAN_RUN);
    ok_eq_int(Error, ERROR_SUCCESS);
    Error = KmtSendToDriver(IOCTL_FLTSCAN_UNREGISTER);
    ok_eq_int(Error, ERROR_SUCCESS);

    KmtCloseDriver();
    KmtUnloadDriver();
}
