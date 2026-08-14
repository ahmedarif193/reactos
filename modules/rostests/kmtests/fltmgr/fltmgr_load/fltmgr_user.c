/*
 * PROJECT:         ReactOS kernel-mode tests - Filter Manager
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Tests for checking filters load and connect correctly
 * PROGRAMMER:      Ged Murphy <gedmurphy@reactos.org>
 */

#include <kmt_test.h>


START_TEST(FltMgrLoad)
{
    static WCHAR FilterName[] = L"FltMgrLoad";
    SC_HANDLE hService;
    HANDLE hPort;
    DWORD Error;

    trace("Message from user-mode\n");

    ok(KmtFltCreateService(FilterName, L"FltMgrLoad test driver", &hService) == ERROR_SUCCESS, "\n");
    Error = KmtFltLoadDriver(FALSE, FALSE, FALSE, &hPort);
    ok(Error == ERROR_PRIVILEGE_NOT_HELD, "KmtFltLoadDriver without privilege returned %lu\n", Error);
    Error = KmtFltLoadDriver(TRUE, FALSE, FALSE, &hPort);
    ok(Error == ERROR_SUCCESS, "KmtFltLoadDriver with privilege returned %lu\n", Error);

    Error = KmtFltConnectComms(&hPort);
    ok(Error == ERROR_SUCCESS, "KmtFltConnectComms returned %lu\n", Error);

    Error = KmtFltDisconnectComms(hPort);
    ok(Error == ERROR_SUCCESS, "KmtFltDisconnectComms returned %lu\n", Error);
    Error = KmtFltUnloadDriver(hPort, FALSE);
    ok(Error == ERROR_SUCCESS, "KmtFltUnloadDriver returned %lu\n", Error);
    KmtFltDeleteService(NULL, &hService);
}
