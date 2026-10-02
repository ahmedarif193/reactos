/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform user-mode management test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winsock2.h>
#include <rpc.h>
#include <initguid.h>
#include <fwpmu.h>

#include "wfp_user.h"

static WFPUSER_STATE State;

static
VOID
Query(
    _In_ DWORD ControlCode)
{
    DWORD Length = sizeof(State);
    DWORD Error;

    Error = KmtSendBufferToDriver(ControlCode, &State, sizeof(State), &Length);
    ok_eq_int(Error, ERROR_SUCCESS);
    ok_eq_int(Length, sizeof(State));
}

static
VOID
QueryUntil(
    _In_ PLONG Value,
    _In_ LONG Expected)
{
    ULONG Tries;

    for (Tries = 0; Tries < 30; Tries++)
    {
        Query(IOCTL_WFPUSER_QUERY);
        if (*Value == Expected)
        {
            break;
        }
        Sleep(100);
    }
}

static
BOOL
Readable(
    _In_ SOCKET Socket)
{
    struct timeval Timeout;
    fd_set Set;

    FD_ZERO(&Set);
    FD_SET(Socket, &Set);
    Timeout.tv_sec = 3;
    Timeout.tv_usec = 0;
    return select(0, &Set, NULL, NULL, &Timeout) == 1;
}

static
VOID
Transfer(
    _In_ SOCKET From,
    _In_ SOCKET To,
    _In_ PCSTR Text)
{
    int Length = (int)strlen(Text), Total = 0, Received;
    char Buffer[64];

    ok(send(From, Text, Length, 0) == Length, "send failed: %d\n", WSAGetLastError());
    while (Total < Length && Readable(To))
    {
        Received = recv(To, Buffer + Total, Length - Total, 0);
        if (Received <= 0)
        {
            break;
        }
        Total += Received;
    }
    ok(Total == Length && !memcmp(Buffer, Text, Length), "Received %d bytes, expected \"%s\"\n", Total, Text);
}

static
VOID
Exchange(VOID)
{
    SOCKET Listener, Client, Server = INVALID_SOCKET;
    struct sockaddr_in Address;

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Address.sin_port = htons(WFPUSER_SERVER_PORT);

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET && Client != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 1) == 0, "listen failed: %d\n", WSAGetLastError());
    ok(connect(Client, (struct sockaddr *)&Address, sizeof(Address)) == 0, "connect failed: %d\n", WSAGetLastError());
    if (Readable(Listener))
    {
        Server = accept(Listener, NULL, NULL);
    }
    ok(Server != INVALID_SOCKET, "accept failed: %d\n", WSAGetLastError());

    if (Server != INVALID_SOCKET)
    {
        Transfer(Client, Server, "HELLO001");
        Transfer(Server, Client, "REPLY001-and-more");
    }

    QueryUntil(&State.StreamClassify, 4);

    closesocket(Client);
    if (Server != INVALID_SOCKET)
    {
        closesocket(Server);
    }
    closesocket(Listener);
}

static
BOOL
ExpectedAppId(
    _In_ PCWSTR Path,
    _Out_writes_(Count) PWCHAR Buffer,
    _In_ ULONG Count)
{
    WCHAR Drive[3];
    DWORD Length;

    Drive[0] = Path[0];
    Drive[1] = L':';
    Drive[2] = UNICODE_NULL;
    if (Path[1] != L':' || !QueryDosDeviceW(Drive, Buffer, Count))
    {
        return FALSE;
    }

    Length = lstrlenW(Buffer);
    if (Length + lstrlenW(Path + 2) + 1 > Count)
    {
        return FALSE;
    }
    lstrcpyW(Buffer + Length, Path + 2);
    CharLowerW(Buffer);
    return TRUE;
}

static
DWORD
AddCallout(
    _In_ HANDLE Engine,
    _In_ const GUID *Key,
    _In_ const GUID *Layer,
    _Out_ UINT32 *Id)
{
    FWPM_CALLOUT0 Callout;

    *Id = 0;
    ZeroMemory(&Callout, sizeof(Callout));
    Callout.calloutKey = *Key;
    Callout.displayData.name = L"LiberNT WFP user test callout";
    Callout.displayData.description = L"LiberNT WFP user test callout";
    Callout.applicableLayer = *Layer;
    return FwpmCalloutAdd0(Engine, &Callout, NULL, Id);
}

static
DWORD
AddFilter(
    _In_ HANDLE Engine,
    _In_ const GUID *Layer,
    _In_ const GUID *CalloutKey,
    _In_opt_ FWP_BYTE_BLOB *AppId,
    _In_ UINT64 Context,
    _Out_opt_ UINT64 *Id)
{
    FWPM_FILTER_CONDITION0 Conditions[2];
    FWPM_FILTER0 Filter;

    ZeroMemory(&Filter, sizeof(Filter));
    ZeroMemory(Conditions, sizeof(Conditions));
    Filter.layerKey = *Layer;
    Filter.displayData.name = L"LiberNT WFP user test filter";
    Filter.displayData.description = L"LiberNT WFP user test filter";
    Filter.action.type = FWP_ACTION_CALLOUT_INSPECTION;
    Filter.action.calloutKey = *CalloutKey;
    Filter.subLayerKey = WFPUSER_SUBLAYER;
    Filter.weight.type = FWP_EMPTY;
    Filter.rawContext = Context;
    Filter.filterCondition = Conditions;
    if (AppId != NULL)
    {
        Conditions[0].fieldKey = FWPM_CONDITION_ALE_APP_ID;
        Conditions[0].matchType = FWP_MATCH_EQUAL;
        Conditions[0].conditionValue.type = FWP_BYTE_BLOB_TYPE;
        Conditions[0].conditionValue.byteBlob = AppId;
        Conditions[1].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
        Conditions[1].matchType = FWP_MATCH_EQUAL;
        Conditions[1].conditionValue.type = FWP_UINT8;
        Conditions[1].conditionValue.uint8 = IPPROTO_TCP;
        Filter.numFilterConditions = 2;
    }
    return FwpmFilterAdd0(Engine, &Filter, NULL, Id);
}

static
VOID
TestCallouts(
    _In_ HANDLE Engine)
{
    UINT32 Id;
    DWORD Error;

    Error = FwpmTransactionCommit0(Engine);
    ok_eq_hex(Error, (DWORD)FWP_E_NO_TXN_IN_PROGRESS);
    Error = FwpmTransactionAbort0(Engine);
    ok_eq_hex(Error, (DWORD)FWP_E_NO_TXN_IN_PROGRESS);
    Error = FwpmTransactionBegin0(Engine, 0);
    ok_eq_hex(Error, ERROR_SUCCESS);
    Error = FwpmTransactionBegin0(Engine, 0);
    ok_eq_hex(Error, (DWORD)FWP_E_TXN_IN_PROGRESS);

    Error = AddCallout(Engine, &WFPUSER_FLOW_CALLOUT, &FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4, &Id);
    ok_eq_hex(Error, ERROR_SUCCESS);
    ok_eq_uint(Id, State.CalloutId[WFPUSER_FLOW]);
    Error = AddCallout(Engine, &WFPUSER_FLOW_CALLOUT, &FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4, &Id);
    ok_eq_hex(Error, (DWORD)FWP_E_ALREADY_EXISTS);
    Error = AddCallout(Engine, &WFPUSER_STREAM_CALLOUT, &FWPM_LAYER_STREAM_V4, &Id);
    ok_eq_hex(Error, ERROR_SUCCESS);
    ok_eq_uint(Id, State.CalloutId[WFPUSER_STREAM]);

    Error = FwpmTransactionCommit0(Engine);
    ok_eq_hex(Error, ERROR_SUCCESS);
}

static
VOID
TestFilters(
    _In_ HANDLE Engine)
{
    WCHAR Own[MAX_PATH], Other[MAX_PATH], Missing[MAX_PATH], Expected[MAX_PATH + 64];
    FWP_BYTE_BLOB *OwnId = NULL, *OtherId = NULL, *MissingId = NULL;
    FWPM_SUBLAYER0 SubLayer;
    FWPM_SESSION0 Session;
    UINT64 FilterId[2] = { 0, 0 };
    HANDLE Dynamic = NULL;
    DWORD Error;

    ZeroMemory(&Session, sizeof(Session));
    Session.displayData.name = L"LiberNT WFP user test session";
    Session.flags = FWPM_SESSION_FLAG_DYNAMIC;
    Error = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &Session, &Dynamic);
    ok_eq_hex(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
    {
        return;
    }

    GetModuleFileNameW(NULL, Own, ARRAYSIZE(Own));
    GetSystemDirectoryW(Other, ARRAYSIZE(Other));
    lstrcatW(Other, L"\\cmd.exe");

    Error = FwpmGetAppIdFromFileName0(Own, &OwnId);
    ok_eq_hex(Error, ERROR_SUCCESS);
    Error = FwpmGetAppIdFromFileName0(Other, &OtherId);
    ok_eq_hex(Error, ERROR_SUCCESS);
    Error = FwpmGetAppIdFromFileName0(L"C:\\wfpuser-missing\\none.exe", &MissingId);
    ok_eq_hex(Error, ERROR_PATH_NOT_FOUND);
    ok(MissingId == NULL, "Missing directory returned %p\n", MissingId);
    GetSystemDirectoryW(Missing, ARRAYSIZE(Missing));
    lstrcatW(Missing, L"\\wfpuser-none.exe");
    Error = FwpmGetAppIdFromFileName0(Missing, &MissingId);
    ok_eq_hex(Error, ERROR_FILE_NOT_FOUND);
    ok(MissingId == NULL, "Missing file returned %p\n", MissingId);
    if (OwnId == NULL || OtherId == NULL)
    {
        FwpmEngineClose0(Dynamic);
        return;
    }

    if (ExpectedAppId(Own, Expected, ARRAYSIZE(Expected)))
    {
        ok(OwnId->size == (lstrlenW(Expected) + 1) * sizeof(WCHAR) && !memcmp(OwnId->data, Expected, OwnId->size),
           "App id %lu bytes \"%.*ls\", expected \"%ls\"\n",
           (ULONG)OwnId->size, (int)(OwnId->size / sizeof(WCHAR)), (PCWSTR)OwnId->data, Expected);
    }
    else
    {
        skip(FALSE, "No device name for %ls\n", Own);
    }

    Error = FwpmTransactionBegin0(Dynamic, 0);
    ok_eq_hex(Error, ERROR_SUCCESS);

    ZeroMemory(&SubLayer, sizeof(SubLayer));
    SubLayer.subLayerKey = WFPUSER_SUBLAYER;
    SubLayer.displayData.name = L"LiberNT WFP user test sublayer";
    SubLayer.displayData.description = L"LiberNT WFP user test sublayer";
    Error = FwpmSubLayerAdd0(Dynamic, &SubLayer, NULL);
    ok_eq_hex(Error, ERROR_SUCCESS);
    Error = FwpmSubLayerAdd0(Dynamic, &SubLayer, NULL);
    ok_eq_hex(Error, (DWORD)FWP_E_ALREADY_EXISTS);

    Error = AddFilter(Dynamic, &FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4, &WFPUSER_UNKNOWN_CALLOUT, OwnId, 1, NULL);
    ok_eq_hex(Error, (DWORD)FWP_E_CALLOUT_NOT_FOUND);
    Error = AddFilter(Dynamic, &FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4, &WFPUSER_FLOW_CALLOUT, OwnId, WFPUSER_CONTEXT_OWN, &FilterId[0]);
    ok_eq_hex(Error, ERROR_SUCCESS);
    Error = AddFilter(Dynamic, &FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4, &WFPUSER_FLOW_CALLOUT, OtherId, WFPUSER_CONTEXT_OTHER, &FilterId[1]);
    ok_eq_hex(Error, ERROR_SUCCESS);
    Error = AddFilter(Dynamic, &FWPM_LAYER_STREAM_V4, &WFPUSER_STREAM_CALLOUT, NULL, WFPUSER_CONTEXT_STREAM, NULL);
    ok_eq_hex(Error, ERROR_SUCCESS);
    ok(FilterId[0] != 0 && FilterId[1] != 0 && FilterId[0] != FilterId[1],
       "Filter ids %I64u %I64u\n", FilterId[0], FilterId[1]);

    Query(IOCTL_WFPUSER_QUERY);
    ok_eq_long(State.NotifyAdd[WFPUSER_FLOW], 0L);
    ok_eq_long(State.NotifyAdd[WFPUSER_STREAM], 0L);

    Error = FwpmTransactionCommit0(Dynamic);
    ok_eq_hex(Error, ERROR_SUCCESS);

    QueryUntil(&State.NotifyAdd[WFPUSER_FLOW], 2);
    ok_eq_long(State.NotifyAdd[WFPUSER_FLOW], 2L);
    ok_eq_long(State.NotifyAdd[WFPUSER_STREAM], 1L);
    ok_eq_long(State.NotifyContext[WFPUSER_FLOW], (LONG)(WFPUSER_CONTEXT_OWN + WFPUSER_CONTEXT_OTHER));
    ok_eq_long(State.NotifyContext[WFPUSER_STREAM], (LONG)WFPUSER_CONTEXT_STREAM);
    ok_eq_hex(State.NotifyAction[WFPUSER_FLOW], (ULONG)FWP_ACTION_CALLOUT_INSPECTION);
    ok_eq_hex(State.NotifyAction[WFPUSER_STREAM], (ULONG)FWP_ACTION_CALLOUT_INSPECTION);

    Exchange();

    ok_eq_long(State.FlowClassify[0], 2L);
    ok_eq_long(State.FlowClassify[1], 0L);
    ok_eq_long(State.FlowClassify[2], 0L);
    ok_eq_long(State.Associated, 2L);
    ok_eq_long(State.StreamClassify, 4L);
    ok_eq_long(State.StreamBytes[0], 25L);
    ok_eq_long(State.StreamBytes[1], 25L);
    ok(State.PathSize == OwnId->size && State.PathSize <= sizeof(State.Path) &&
       !memcmp(State.Path, OwnId->data, State.PathSize),
       "Process path %lu bytes \"%.*ls\"\n",
       State.PathSize, (int)(min(State.PathSize, sizeof(State.Path)) / sizeof(WCHAR)), State.Path);

    Error = FwpmFilterDeleteById0(Dynamic, FilterId[1]);
    ok_eq_hex(Error, ERROR_SUCCESS);
    QueryUntil(&State.NotifyDelete[WFPUSER_FLOW], 1);
    ok_eq_long(State.NotifyDelete[WFPUSER_FLOW], 1L);
    Error = FwpmFilterDeleteById0(Dynamic, FilterId[1]);
    ok_eq_hex(Error, (DWORD)FWP_E_FILTER_NOT_FOUND);

    FwpmFreeMemory0((void **)&OwnId);
    ok(OwnId == NULL, "FwpmFreeMemory0 left %p\n", OwnId);
    FwpmFreeMemory0((void **)&OtherId);

    Error = FwpmEngineClose0(Dynamic);
    ok_eq_hex(Error, ERROR_SUCCESS);
    QueryUntil(&State.NotifyDelete[WFPUSER_FLOW], 2);
    ok_eq_long(State.NotifyDelete[WFPUSER_FLOW], 2L);
    ok_eq_long(State.NotifyDelete[WFPUSER_STREAM], 1L);

    Error = FwpmSubLayerDeleteByKey0(Engine, &WFPUSER_SUBLAYER);
    ok_eq_hex(Error, (DWORD)FWP_E_SUBLAYER_NOT_FOUND);
}

static
VOID
TestCleanup(
    _In_ HANDLE Engine)
{
    FWPM_PROVIDER0 Provider;
    DWORD Error;

    ZeroMemory(&Provider, sizeof(Provider));
    Provider.providerKey = WFPUSER_PROVIDER;
    Provider.displayData.name = L"LiberNT WFP user test provider";
    Provider.displayData.description = L"LiberNT WFP user test provider";
    Error = FwpmProviderAdd0(Engine, &Provider, NULL);
    ok_eq_hex(Error, ERROR_SUCCESS);
    Error = FwpmProviderAdd0(Engine, &Provider, NULL);
    ok_eq_hex(Error, (DWORD)FWP_E_ALREADY_EXISTS);
    Error = FwpmProviderDeleteByKey0(Engine, &WFPUSER_PROVIDER);
    ok_eq_hex(Error, ERROR_SUCCESS);
    Error = FwpmProviderDeleteByKey0(Engine, &WFPUSER_PROVIDER);
    ok_eq_hex(Error, (DWORD)FWP_E_PROVIDER_NOT_FOUND);

    Error = FwpmCalloutDeleteByKey0(Engine, &WFPUSER_FLOW_CALLOUT);
    ok_eq_hex(Error, ERROR_SUCCESS);
    Error = FwpmCalloutDeleteByKey0(Engine, &WFPUSER_FLOW_CALLOUT);
    ok_eq_hex(Error, (DWORD)FWP_E_CALLOUT_NOT_FOUND);
    Error = FwpmCalloutDeleteById0(Engine, State.CalloutId[WFPUSER_STREAM]);
    ok_eq_hex(Error, ERROR_SUCCESS);
}

START_TEST(WfpUser)
{
    HANDLE Engine = NULL;
    WSADATA WsaData;
    DWORD Error;

    Error = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok_eq_int(Error, 0);
    if (Error)
    {
        return;
    }

    Error = KmtLoadAndOpenDriver(L"WfpUser", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
    {
        WSACleanup();
        return;
    }

    Query(IOCTL_WFPUSER_REGISTER);
    ok_eq_hex(State.Status[WFPUSER_FLOW], STATUS_SUCCESS);
    ok_eq_hex(State.Status[WFPUSER_STREAM], STATUS_SUCCESS);

    Error = FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, NULL, &Engine);
    ok_eq_hex(Error, ERROR_SUCCESS);
    if (Error == ERROR_SUCCESS)
    {
        TestCallouts(Engine);
        TestFilters(Engine);
        TestCleanup(Engine);
        Error = FwpmEngineClose0(Engine);
        ok_eq_hex(Error, ERROR_SUCCESS);
    }

    Query(IOCTL_WFPUSER_UNREGISTER);
    ok_eq_hex(State.Status[WFPUSER_FLOW], STATUS_SUCCESS);
    ok_eq_hex(State.Status[WFPUSER_STREAM], STATUS_SUCCESS);
    ok_eq_long(State.FlowDelete, 2L);

    KmtCloseDriver();
    KmtUnloadDriver();
    WSACleanup();
}
