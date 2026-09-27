/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     Test for NtUserProcessConnect
 * COPYRIGHT:   Copyright 2008-2020 Timo Kreuzer
 *              Copyright 2021 Hermes Belusca-Maito
 */

#include "../win32nt.h"

START_TEST(NtUserProcessConnect)
{
    NTSTATUS Status;
    USERCONNECT UserConnect = {0};

    UserConnect.ulVersion = MAKELONG(0, 5);
    Status = NtUserProcessConnect(GetCurrentProcess(), &UserConnect, sizeof(UserConnect));
    ok_ntstatus(Status, STATUS_UNSUCCESSFUL);
}
