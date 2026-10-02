/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filter Manager transaction test, user-mode part
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#include "fltmgr_tx.h"

START_TEST(FltMgrTx)
{
    DWORD Error;

    Error = KmtLoadAndOpenDriver(L"FltMgrTx", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
    {
        return;
    }

    Error = KmtSendToDriver(IOCTL_FLTTX_REGISTER);
    ok_eq_int(Error, ERROR_SUCCESS);
    Error = KmtSendToDriver(IOCTL_FLTTX_RUN);
    ok_eq_int(Error, ERROR_SUCCESS);
    Error = KmtSendToDriver(IOCTL_FLTTX_UNREGISTER);
    ok_eq_int(Error, ERROR_SUCCESS);

    KmtCloseDriver();
    KmtUnloadDriver();
}
